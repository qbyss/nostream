#include "evo/services/FileTransferService.hpp"
#include "evo/services/FtpClient.hpp"
#include "evo/services/InternalStorage.hpp"
#include "TransferSource.hpp"
#include "evo_readdir.h"
#include "evo_jailbreak.h"
#include "evo_boot_trace.h"
#include "evo_boot_log.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <limits.h>
#include <utime.h>
#include <errno.h>
#include <queue>
#include <condition_variable>

namespace evo {

// 1MB aligned buffer chunk size for smooth progress updates & high NVMe / USB throughput
static constexpr size_t TRANSFER_BUFFER_SIZE = 1 * 1024 * 1024;

/*
 * Why big copies do not use write() at all.
 *
 * Measured on FW 12.70 with tools/evo-remote.sh iobench: the app process writes
 * at ~160 MB/s for exactly 1152 MB and is then throttled to single-digit MB/s
 * for the rest of its life. The cliff lands at the same byte count whatever we
 * do - 64 KB / 1 MB / 8 MB chunks, O_DIRECT, O_SYNC, ftruncate preallocation,
 * fsync every 64 MB (a no-op here, it returns in 1 ms), closing and reopening
 * the fd, even mmap + msync instead of write(). It is a per-process budget: the
 * console's own FTP daemon, writing the same 3 GB into the same folder from
 * another process, holds a flat 84 MB/s while EVO sits at 2 MB/s.
 *
 * So anything large is streamed to that daemon over loopback instead, and EVO
 * only reads. kProcessWriteBudget keeps our own writes well clear of the cliff;
 * once we are past it every file goes over FTP, however small.
 */
static constexpr uint64_t kProcessWriteBudget = 512ULL * 1024 * 1024;
static constexpr uint64_t kFtpDelegateMinBytes = 64ULL * 1024 * 1024;
static constexpr const char* kLoopbackFtpHost = "127.0.0.1";
static constexpr int         kLoopbackFtpPort = 2121;

static uint64_t GetCurrentTimeMs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000ULL + static_cast<uint64_t>(tv.tv_usec / 1000ULL);
}

FileTransferService::FileTransferService() {
    EVO_LOGI("transfer", "FileTransferService initialized");
}

FileTransferService::~FileTransferService() {
    cancelTransfer();
    if (m_workerThread != 0) {
        pthread_join(m_workerThread, nullptr);
        m_workerThread = 0;
    }
}

void FileTransferService::copyToClipboard(const std::string& path, const std::string& name, bool isDir) {
    m_clipboard.path = path;
    m_clipboard.name = name;
    m_clipboard.isDirectory = isDir;
    m_clipboard.action = ClipboardAction::Copy;
    EVO_LOGI("transfer", "Clipboard COPY: '%s'", path.c_str());
}

void FileTransferService::cutToClipboard(const std::string& path, const std::string& name, bool isDir) {
    m_clipboard.path = path;
    m_clipboard.name = name;
    m_clipboard.isDirectory = isDir;
    m_clipboard.action = ClipboardAction::Cut;
    EVO_LOGI("transfer", "Clipboard CUT: '%s'", path.c_str());
}

void FileTransferService::clearClipboard() {
    m_clipboard.path.clear();
    m_clipboard.name.clear();
    m_clipboard.isDirectory = false;
    m_clipboard.action = ClipboardAction::None;
}

bool FileTransferService::isBusy() const {
    return m_isBusy.load();
}

TransferProgress FileTransferService::getProgress() const {
    std::lock_guard<std::mutex> lock(m_progressMutex);
    return m_progress;
}

void FileTransferService::cancelTransfer() {
    if (m_isBusy.load()) {
        EVO_LOGI("transfer", "Cancellation requested");
        m_cancelRequested.store(true);
    }
}

void FileTransferService::resetStatus() {
    if (!m_isBusy.load()) {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        m_progress = TransferProgress();
        m_activeOp = FileOpType::None;
        m_activeSrc.clear();
        m_activeDst.clear();
        m_createdDirs.clear();
    }
}

bool FileTransferService::startCopy(const std::string& srcPath, const std::string& dstDir) {
    return startCopyOperation(FileOpType::Copy, srcPath, dstDir);
}

bool FileTransferService::startImport(const std::string& srcPath, const std::string& dstDir) {
    return startCopyOperation(FileOpType::Import, srcPath, dstDir);
}

bool FileTransferService::startCopyOperation(FileOpType op, const std::string& srcPath,
                                            const std::string& dstDir) {
    if (m_isBusy.load() || srcPath.empty() || dstDir.empty()) return false;

    struct stat st{};
    if (op == FileOpType::Copy && stat(srcPath.c_str(), &st) != 0) return false;

    m_activeOp = op;
    m_activeSrc = srcPath;
    m_activeDst = dstDir;
    m_activeIsDir = S_ISDIR(st.st_mode);

    m_cancelRequested.store(false);
    m_isBusy.store(true);

    {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        m_progress = TransferProgress();
        m_progress.opType = op;
        m_progress.status = TransferStatus::Scanning;
        m_progress.currentSourcePath = srcPath;
        m_progress.currentDestPath = dstDir;
    }

    if (m_workerThread != 0) {
        pthread_join(m_workerThread, nullptr);
        m_workerThread = 0;
    }

    if (pthread_create(&m_workerThread, nullptr, WorkerThreadEntry, this) != 0) {
        m_isBusy.store(false);
        return false;
    }

    return true;
}

bool FileTransferService::startMove(const std::string& srcPath, const std::string& dstDir) {
    if (m_isBusy.load() || srcPath.empty() || dstDir.empty()) return false;

    struct stat st;
    if (stat(srcPath.c_str(), &st) != 0) return false;

    m_activeOp = FileOpType::Move;
    m_activeSrc = srcPath;
    m_activeDst = dstDir;
    m_activeIsDir = S_ISDIR(st.st_mode);

    m_cancelRequested.store(false);
    m_isBusy.store(true);

    {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        m_progress = TransferProgress();
        m_progress.opType = FileOpType::Move;
        m_progress.status = TransferStatus::Scanning;
        m_progress.currentSourcePath = srcPath;
        m_progress.currentDestPath = dstDir;
    }

    if (m_workerThread != 0) {
        pthread_join(m_workerThread, nullptr);
        m_workerThread = 0;
    }

    if (pthread_create(&m_workerThread, nullptr, WorkerThreadEntry, this) != 0) {
        m_isBusy.store(false);
        return false;
    }

    return true;
}

bool FileTransferService::startDelete(const std::string& path) {
    if (m_isBusy.load() || path.empty()) return false;

    m_activeOp = FileOpType::Delete;
    m_activeSrc = path;
    m_activeDst.clear();

    m_cancelRequested.store(false);
    m_isBusy.store(true);

    {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        m_progress = TransferProgress();
        m_progress.opType = FileOpType::Delete;
        m_progress.status = TransferStatus::Transferring;
        m_progress.currentSourcePath = path;
    }

    if (m_workerThread != 0) {
        pthread_join(m_workerThread, nullptr);
        m_workerThread = 0;
    }

    if (pthread_create(&m_workerThread, nullptr, WorkerThreadEntry, this) != 0) {
        m_isBusy.store(false);
        return false;
    }

    return true;
}

bool FileTransferService::startPaste(const std::string& targetDir) {
    if (!hasClipboardItem() || targetDir.empty() || m_isBusy.load()) return false;

    if (m_clipboard.isDirectory) {
        char sourcePath[PATH_MAX];
        char destinationPath[PATH_MAX];
        if (!realpath(m_clipboard.path.c_str(), sourcePath) || !realpath(targetDir.c_str(), destinationPath))
            return false;
        const std::string source(sourcePath);
        const std::string destination(destinationPath);
        if (destination == source || destination.rfind(source + "/", 0) == 0)
            return false;
    }

    if (m_clipboard.action == ClipboardAction::Copy) {
        return startCopy(m_clipboard.path, targetDir);
    } else if (m_clipboard.action == ClipboardAction::Cut) {
        bool res = startMove(m_clipboard.path, targetDir);
        if (res) {
            clearClipboard();
        }
        return res;
    }
    return false;
}

bool FileTransferService::renameItem(const std::string& oldPath, const std::string& newName) {
    if (oldPath.empty() || newName.empty()) return false;

    evo_jailbreak_ensure();

    size_t lastSlash = oldPath.find_last_of('/');
    std::string parentDir = (lastSlash != std::string::npos && lastSlash > 0) ? oldPath.substr(0, lastSlash) : "";
    std::string newPath = parentDir.empty() ? newName : (parentDir + "/" + newName);

    if (rename(oldPath.c_str(), newPath.c_str()) == 0) {
        EVO_LOGI("transfer", "renameItem '%s' -> '%s' succeeded", oldPath.c_str(), newPath.c_str());
        return true;
    }
    EVO_LOGE("transfer", "renameItem failed (errno=%d)", errno);
    return false;
}

bool FileTransferService::createDirectory(const std::string& parentDir, const std::string& folderName) {
    if (parentDir.empty() || folderName.empty()) return false;

    evo_jailbreak_ensure();

    std::string fullPath = parentDir + "/" + folderName;
    if (mkdir(fullPath.c_str(), 0755) == 0) {
        EVO_LOGI("transfer", "createDirectory '%s' created", fullPath.c_str());
        return true;
    }
    if (errno == EEXIST) return true;
    EVO_LOGE("transfer", "createDirectory failed (errno=%d)", errno);
    return false;
}

void* FileTransferService::WorkerThreadEntry(void* arg) {
    auto* self = static_cast<FileTransferService*>(arg);
    if (self) {
        self->runTransfer();
    }
    return nullptr;
}

void FileTransferService::runTransfer() {
    evo_jailbreak_ensure();

    bool success = false;
    m_lastSampleTimeMs = GetCurrentTimeMs();
    m_lastSampleBytes = 0;
    m_smoothedSpeed = 0.0;
    m_createdDirs.clear();

    if (m_activeOp == FileOpType::Copy) {
        success = executeCopy(m_activeSrc, m_activeDst);
    } else if (m_activeOp == FileOpType::Import) {
        success = executeImport(m_activeSrc, m_activeDst);
    } else if (m_activeOp == FileOpType::Move) {
        success = executeMove(m_activeSrc, m_activeDst);
    } else if (m_activeOp == FileOpType::Delete) {
        success = executeDelete(m_activeSrc);
    }

    TransferStatus status;
    {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        if (m_cancelRequested.load() && !(success && m_activeOp == FileOpType::Import)) {
            m_progress.status = TransferStatus::Cancelled;
        } else if (success) {
            m_progress.status = TransferStatus::Completed;
            m_progress.progressPercent = 100.0;
        } else {
            m_progress.status = TransferStatus::Failed;
            if (m_progress.errorMessage.empty()) {
                m_progress.errorMessage = "Operation failed (error " + std::to_string(errno) + ")";
            }
        }
        status = m_progress.status;
    }

    m_isBusy.store(false);
    EVO_LOGI("transfer", "Operation complete (status=%d)", static_cast<int>(status));
}

bool FileTransferService::ensureDirectory(const std::string& dirPath) {
    if (dirPath.empty()) return true;
    if (m_createdDirs.find(dirPath) != m_createdDirs.end()) return true;

    struct stat st;
    if (stat(dirPath.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            m_createdDirs.insert(dirPath);
            return true;
        }
        return false;
    }

    size_t lastSlash = dirPath.find_last_of('/');
    if (lastSlash != std::string::npos && lastSlash > 0) {
        std::string parent = dirPath.substr(0, lastSlash);
        if (!ensureDirectory(parent)) return false;
    }

    if (mkdir(dirPath.c_str(), 0755) == 0 || errno == EEXIST) {
        m_createdDirs.insert(dirPath);
        return true;
    }

    return false;
}

std::string FileTransferService::resolveDestinationCollision(const std::string& dstPath, bool isDir) {
    struct stat st;
    if (stat(dstPath.c_str(), &st) != 0) {
        return dstPath; // Does not exist, safe to use
    }

    if (m_collisionAction == CollisionAction::Overwrite) {
        return dstPath;
    }

    if (m_collisionAction == CollisionAction::Skip) {
        return ""; // Skip
    }

    // AutoRename: "name (1).ext"
    size_t slash = dstPath.find_last_of('/');
    std::string dir = (slash != std::string::npos) ? dstPath.substr(0, slash) : ".";
    std::string base = (slash != std::string::npos) ? dstPath.substr(slash + 1) : dstPath;

    std::string stem = base;
    std::string ext;
    if (!isDir) {
        size_t dot = base.find_last_of('.');
        if (dot != std::string::npos && dot > 0) {
            stem = base.substr(0, dot);
            ext = base.substr(dot);
        }
    }

    for (int counter = 1; counter < 1000; ++counter) {
        std::string candidate = dir + "/" + stem + " (" + std::to_string(counter) + ")" + ext;
        if (stat(candidate.c_str(), &st) != 0) {
            return candidate;
        }
    }

    return dstPath;
}

bool FileTransferService::readCopyEntry(const std::string& path, FileEntry& entry) const {
    const bool importing = m_activeOp == FileOpType::Import;
    if (importing && evo_ntfs_is_url(path.c_str())) {
        int isDir = 0;
        if (evo_ntfs_stat(path.c_str(), &isDir, &entry.size) != 0) {
            errno = EIO;
            return false;
        }
        entry.isDir = isDir != 0;
        entry.mode = entry.isDir ? 0755 : 0644;
        return true;
    }

    struct stat st;
    if ((importing ? statWithoutFollowingLinks(path.c_str(), st) : stat(path.c_str(), &st)) != 0) return false;
    if (importing && !S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) {
        errno = EINVAL;
        return false;
    }

    entry.size = static_cast<uint64_t>(st.st_size);
    entry.isDir = S_ISDIR(st.st_mode);
    entry.mode = st.st_mode;
    entry.mtime = st.st_mtime;
    entry.atime = st.st_atime;
    return true;
}

bool FileTransferService::scanTree(const std::string& rootPath, std::vector<FileEntry>& outEntries, uint64_t& outTotalBytes) {
    outEntries.clear();
    outTotalBytes = 0;
    const bool importing = m_activeOp == FileOpType::Import;

    FileEntry root;
    if (!readCopyEntry(rootPath, root)) return false;
    if (!root.isDir) {
        root.relPath = rootPath.substr(rootPath.find_last_of('/') + 1);
        outTotalBytes = root.size;
        outEntries.push_back(std::move(root));
        return true;
    }

    std::vector<std::string> dirQueue;
    dirQueue.push_back("");

    while (!dirQueue.empty()) {
        if (m_cancelRequested.load()) return false;

        std::string sub = dirQueue.back();
        dirQueue.pop_back();

        std::string fullDirPath = sub.empty() ? rootPath : (rootPath + "/" + sub);

        evo_dir_t* dir = evo_opendir(fullDirPath.c_str());
        if (!dir) {
            if (importing) return false;
            continue;
        }

        for (;;) {
            errno = 0;
            struct dirent* de = evo_readdir(dir);
            if (!de || (importing && m_cancelRequested.load())) {
                const int error = errno;
                evo_closedir(dir);
                if (importing && (error || m_cancelRequested.load())) {
                    errno = error;
                    return false;
                }
                break;
            }
            /* Only "." and "..": a dotfile is still the user's file and has to
             * be copied, or a move would delete an original we never wrote. */
            if (std::strcmp(de->d_name, ".") == 0 || std::strcmp(de->d_name, "..") == 0) continue;
            if (std::strcmp(de->d_name, "$RECYCLE.BIN") == 0) continue;
            if (std::strcmp(de->d_name, "System Volume Information") == 0) continue;

            std::string itemRel = sub.empty() ? de->d_name : (sub + "/" + de->d_name);
            std::string itemFull = rootPath + "/" + itemRel;

            FileEntry fe;
            fe.relPath = itemRel;
            if (!readCopyEntry(itemFull, fe)) {
                if (!importing) continue;
                const int error = errno;
                evo_closedir(dir);
                errno = error;
                return false;
            }

            if (fe.isDir) {
                dirQueue.push_back(itemRel);
            } else {
                outTotalBytes += fe.size;
            }
            outEntries.push_back(std::move(fe));
        }
    }

    return true;
}

/*
 * Fold `bytes` into the progress block and refresh the smoothed speed and ETA.
 * Called from both copy paths, on the worker thread only.
 */
void FileTransferService::accountBytes(uint64_t bytes) {
    uint64_t transferred;
    {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        m_progress.bytesTransferred += bytes;
        transferred = m_progress.bytesTransferred;
        if (m_progress.totalBytes > 0) {
            m_progress.progressPercent =
                (static_cast<double>(transferred) / static_cast<double>(m_progress.totalBytes)) * 100.0;
        }
    }

    uint64_t nowMs = GetCurrentTimeMs();
    uint64_t timeDeltaMs = nowMs - m_lastSampleTimeMs;
    if (timeDeltaMs < 150) return;

    uint64_t bytesDelta = (transferred > m_lastSampleBytes) ? (transferred - m_lastSampleBytes) : 0;
    double instantSpeed = (static_cast<double>(bytesDelta) / static_cast<double>(timeDeltaMs)) * 1000.0;

    if (m_smoothedSpeed <= 0.0) {
        m_smoothedSpeed = instantSpeed;
    } else {
        m_smoothedSpeed = 0.70 * m_smoothedSpeed + 0.30 * instantSpeed;
    }

    m_lastSampleTimeMs = nowMs;
    m_lastSampleBytes = transferred;

    std::lock_guard<std::mutex> lock(m_progressMutex);
    m_progress.currentSpeedBps = m_smoothedSpeed;
    if (m_progress.totalBytes > 0) {
        uint64_t remainingBytes = (m_progress.totalBytes > transferred) ? (m_progress.totalBytes - transferred) : 0;
        m_progress.estimatedSecondsRemaining =
            (m_smoothedSpeed > 0.0) ? static_cast<int>(remainingBytes / m_smoothedSpeed) : 0;
    }
}

bool FileTransferService::shouldDelegateToFtp(uint64_t fileSize) const {
    return fileSize >= kFtpDelegateMinBytes || m_processBytesWritten >= kProcessWriteBudget;
}

bool FileTransferService::loopbackFtpUsable() {
    if (m_ftpProbe != 0) return m_ftpProbe > 0;

    FtpClient probe;
    if (probe.connectServer(kLoopbackFtpHost, kLoopbackFtpPort, "anonymous", "anonymous", 3)) {
        probe.disconnect();
        m_ftpProbe = 1;
        EVO_LOGI("transfer", "loopback FTP daemon on %s:%d is up - big copies go through it",
               kLoopbackFtpHost, kLoopbackFtpPort);
    } else {
        m_ftpProbe = -1;
        EVO_LOGI("transfer", "no FTP daemon on %s:%d (%s) - copying in-process, "
               "expect throttling past ~1 GB",
               kLoopbackFtpHost, kLoopbackFtpPort, probe.getLastError().c_str());
    }
    return m_ftpProbe > 0;
}

/* FtpClient::storeFile progress hook: same accounting as the in-process loop,
 * and the return value is how a cancel reaches the middle of a transfer. */
static bool FtpStoreProgress(void* user, uint64_t /*totalSent*/, uint32_t chunkBytes) {
    auto* self = static_cast<FileTransferService*>(user);
    return self->onFtpChunk(chunkBytes);
}

bool FileTransferService::onFtpChunk(uint32_t chunkBytes) {
    if (chunkBytes > 0) accountBytes(chunkBytes);
    return !m_cancelRequested.load();
}

/*
 * The daemon's FTP root is the real filesystem root, so the remote path is just
 * the destination path. We only read here; the write happens in its process.
 */
bool FileTransferService::copyViaLoopbackFtp(const std::string& src, const std::string& finalDst) {
    FtpClient ftp;
    if (!ftp.connectServer(kLoopbackFtpHost, kLoopbackFtpPort, "anonymous", "anonymous", 5)) {
        EVO_LOGE("transfer", "copyViaLoopbackFtp: connect failed (%s)", ftp.getLastError().c_str());
        m_ftpProbe = -1;
        return false;
    }

    uint64_t t0 = GetCurrentTimeMs();
    bool ok = ftp.storeFile(finalDst, src, FtpStoreProgress, this, 30, m_activeOp == FileOpType::Import);
    uint64_t dt = GetCurrentTimeMs() - t0;
    if (!ok) {
        EVO_LOGE("transfer", "copyViaLoopbackFtp: STOR '%s' failed after %llu ms (%s)",
               finalDst.c_str(), (unsigned long long)dt, ftp.getLastError().c_str());
    } else {
        EVO_LOGI("transfer", "copyViaLoopbackFtp: '%s' stored in %llu ms", finalDst.c_str(), (unsigned long long)dt);
    }
    ftp.disconnect();
    return ok;
}

bool FileTransferService::copyInProcess(const std::string& src, const std::string& finalDst,
                                        uint64_t fileSize, uint32_t mode) {
    TransferSource source(src, m_activeOp == FileOpType::Import);
    if (!source.isOpen()) {
        EVO_LOGE("transfer", "copyInProcess: open src '%s' failed (errno=%d)", src.c_str(), errno);
        return false;
    }

    int dstFd = open(finalDst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode & 0777);
    if (dstFd < 0) {
        EVO_LOGE("transfer", "copyInProcess: open dst '%s' failed (errno=%d)", finalDst.c_str(), errno);
        return false;
    }

    if (m_copyBuf.size() < TRANSFER_BUFFER_SIZE) {
        m_copyBuf.resize(TRANSFER_BUFFER_SIZE);
    }
    char* buffer = m_copyBuf.data();

    uint64_t fileBytesCopied = 0;
    bool success = true;

    while (fileBytesCopied < fileSize) {
        if (m_cancelRequested.load()) {
            success = false;
            break;
        }

        size_t toRead = static_cast<size_t>(std::min<uint64_t>(TRANSFER_BUFFER_SIZE, fileSize - fileBytesCopied));
        ssize_t bytesRead = source.readChunk(buffer, toRead);
        if (bytesRead <= 0) {
            if (bytesRead < 0 && errno == EINTR) continue;
            if (bytesRead == 0) errno = EIO;
            success = false;
            break;
        }

        ssize_t bytesWrittenTotal = 0;
        while (bytesWrittenTotal < bytesRead) {
            if (m_cancelRequested.load()) {
                success = false;
                break;
            }
            ssize_t written = write(dstFd, buffer + bytesWrittenTotal, bytesRead - bytesWrittenTotal);
            if (written <= 0) {
                if (written < 0 && errno == EINTR) continue;
                success = false;
                break;
            }
            bytesWrittenTotal += written;
        }

        fileBytesCopied += static_cast<uint64_t>(bytesWrittenTotal);
        m_processBytesWritten += static_cast<uint64_t>(bytesWrittenTotal);
        accountBytes(static_cast<uint64_t>(bytesWrittenTotal));

        if (!success) break;
    }

    const int error = success ? 0 : errno;
    if (close(dstFd) != 0 && success) return false;
    if (!success) errno = error;
    return success;
}

bool FileTransferService::copySingleFile(const std::string& src, const std::string& dst, uint64_t fileSize,
                                        uint32_t mode, time_t mtime, time_t atime) {
    if (m_cancelRequested.load()) return false;

    // Check collision
    const bool importing = m_activeOp == FileOpType::Import;
    std::string finalDst = importing ? dst : resolveDestinationCollision(dst, false);
    if (finalDst.empty()) {
        // Skipped - still count it so the bar and the ETA keep moving.
        accountBytes(fileSize);
        return true;
    }

    // Ensure parent destination directory exists
    size_t lastSlash = finalDst.find_last_of('/');
    if (lastSlash != std::string::npos && lastSlash > 0) {
        std::string parent = finalDst.substr(0, lastSlash);
        if (!ensureDirectory(parent)) return false;
    }

    m_currentDestinationPartialFile = finalDst;

    uint64_t progressBefore;
    {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        progressBefore = m_progress.bytesTransferred;
    }

    bool success = false;
    bool viaFtp = false;

    if (finalDst.find_first_of("\r\n") == std::string::npos &&
        shouldDelegateToFtp(fileSize) && loopbackFtpUsable()) {
        viaFtp = true;
        success = copyViaLoopbackFtp(src, finalDst);
        if (!success && !m_cancelRequested.load()) {
            /* Rewind the accounting the aborted STOR added, then try ourselves. */
            {
                std::lock_guard<std::mutex> lock(m_progressMutex);
                m_progress.bytesTransferred = progressBefore;
            }
            m_lastSampleBytes = progressBefore;
            unlink(finalDst.c_str());
            EVO_LOGW("transfer", "copySingleFile: falling back to an in-process copy of '%s'", src.c_str());
            viaFtp = false;
        }
    }

    if (!viaFtp && !success) {
        success = copyInProcess(src, finalDst, fileSize, mode);
    }

    if (success && importing) {
        struct stat st;
        if (statWithoutFollowingLinks(finalDst.c_str(), st) != 0) {
            success = false;
        } else if (!S_ISREG(st.st_mode) || static_cast<uint64_t>(st.st_size) != fileSize) {
            errno = EIO;
            success = false;
        }
    }

    if (!success || m_cancelRequested.load()) {
        const int error = errno;
        // Clean up partial incomplete file (Ultracopier safety rule)
        unlink(finalDst.c_str());
        m_currentDestinationPartialFile.clear();
        errno = error;
        return false;
    }

    m_currentDestinationPartialFile.clear();

    // Preserve metadata and timestamp (utimes)
    struct timeval tv[2];
    tv[0].tv_sec = atime;
    tv[0].tv_usec = 0;
    tv[1].tv_sec = mtime;
    tv[1].tv_usec = 0;
    if (!evo_ntfs_is_url(src.c_str())) utimes(finalDst.c_str(), tv);
    chmod(finalDst.c_str(), mode & 0777);

    return true;
}

bool FileTransferService::executeCopy(const std::string& src, const std::string& dst) {
    std::vector<FileEntry> entries;
    uint64_t totalBytes = 0;

    {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        m_progress.status = TransferStatus::Scanning;
        size_t slash = src.find_last_of('/');
        m_progress.currentItemName = (slash != std::string::npos) ? src.substr(slash + 1) : src;
    }

    if (!scanTree(src, entries, totalBytes)) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        m_progress.totalBytes = totalBytes;
        m_progress.totalFiles = static_cast<uint32_t>(entries.size());
        m_progress.bytesTransferred = 0;
        m_progress.filesTransferred = 0;
        m_progress.status = TransferStatus::Transferring;
    }

    m_lastSampleTimeMs = GetCurrentTimeMs();
    m_lastSampleBytes = 0;
    m_smoothedSpeed = 0.0;

    size_t srcSlash = src.find_last_of('/');
    std::string srcRootName = (srcSlash != std::string::npos) ? src.substr(srcSlash + 1) : src;

    for (const auto& entry : entries) {
        if (m_cancelRequested.load()) return false;

        std::string targetRel;
        if (m_activeIsDir) {
            targetRel = srcRootName + "/" + entry.relPath;
        } else {
            targetRel = entry.relPath;
        }
        std::string targetDstPath = dst + "/" + targetRel;

        std::string sourceFilePath = m_activeIsDir ? (src + "/" + entry.relPath) : src;
        {
            std::lock_guard<std::mutex> lock(m_progressMutex);
            size_t slash = entry.relPath.find_last_of('/');
            m_progress.currentItemName = (slash != std::string::npos) ? entry.relPath.substr(slash + 1) : entry.relPath;
            m_progress.currentSourcePath = sourceFilePath;
            m_progress.currentDestPath = targetDstPath;
        }

        if (entry.isDir) {
            if (!ensureDirectory(targetDstPath) && m_activeOp == FileOpType::Import) return false;
        } else {
            if (!copySingleFile(sourceFilePath, targetDstPath, entry.size, entry.mode, entry.mtime, entry.atime)) {
                return false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_progressMutex);
            m_progress.filesTransferred++;
            if (m_progress.totalBytes == 0 && m_progress.totalFiles > 0) {
                m_progress.progressPercent = (static_cast<double>(m_progress.filesTransferred) / static_cast<double>(m_progress.totalFiles)) * 100.0;
            }
        }
    }

    return true;
}

bool FileTransferService::failImport(const char* message) {
    EVO_LOGW("transfer", "Import failed: %s", message);
    std::lock_guard<std::mutex> lock(m_progressMutex);
    m_progress.errorMessage = message;
    return false;
}

bool FileTransferService::executeImport(const std::string& src, const std::string& dst) {
    constexpr const char* kAlreadyExists = "An item with that name is already in Internal Storage";
    const std::string name = src.substr(src.find_last_of('/') + 1);
    if (name.empty() || name == "." || name == "..")
        return failImport("Select a video or folder on the USB drive");

    // Reject symlinked ancestors on native USB volumes as well as symlinked entries.
    char volume[EVO_USB_VOLUME_ROOT_CAP];
    if (!evo_ntfs_is_url(src.c_str()) && evo_usb_volume_root(src.c_str(), volume)) {
        for (size_t end = src.find('/', std::strlen(volume) + 1);
             end != std::string::npos; end = src.find('/', end + 1)) {
            const std::string folder = src.substr(0, end);
            struct stat st;
            if (statWithoutFollowingLinks(folder.c_str(), st) != 0) {
                EVO_LOGE("transfer", "Cannot inspect USB folder '%s' (errno=%d)", folder.c_str(), errno);
                return failImport("Could not open the USB folder");
            }
            if (!S_ISDIR(st.st_mode)) return failImport("Could not open the USB folder");
        }
    }

    FileEntry source;
    if (!readCopyEntry(src, source)) {
        EVO_LOGE("transfer", "Cannot inspect USB item '%s' (errno=%d)", src.c_str(), errno);
        return failImport("Could not read the selected USB item");
    }
    m_activeIsDir = source.isDir;

    if (!ensureInternalStorage(dst.c_str())) {
        EVO_LOGE("transfer", "Cannot open or create '%s' (errno=%d)", dst.c_str(), errno);
        return failImport("Could not open or create Internal Storage");
    }

    const std::string finalPath = dst + "/" + name;
    struct stat st;
    if (statWithoutFollowingLinks(finalPath.c_str(), st) == 0 || errno == ELOOP)
        return failImport(kAlreadyExists);
    if (errno != ENOENT) return failImport("Could not check the destination in Internal Storage");

    const std::string prefix = dst + "/.nostream-copy-" + std::to_string(getpid()) +
        "-" + std::to_string(GetCurrentTimeMs());
    std::string stage;
    bool created = false;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        stage = prefix + "-" + std::to_string(attempt);
        if (mkdir(stage.c_str(), 0700) == 0) {
            created = true;
            break;
        }
        if (errno != EEXIST) break;
    }
    if (!created) {
        return failImport(errno == ENOSPC ? "Internal Storage is full"
                                          : "Could not prepare the copy in Internal Storage");
    }

    const std::string stagedItem = stage + "/" + name;
    bool copied = (!source.isDir || mkdir(stagedItem.c_str(), 0755) == 0) &&
        executeCopy(src, stage);
    if (copied && !m_cancelRequested.load()) {
        // Reserve the final name exclusively, then replace only our own placeholder.
        bool reserved;
        if (source.isDir) {
            reserved = mkdir(finalPath.c_str(), 0755) == 0;
        } else {
            const int fd = open(finalPath.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
            reserved = fd >= 0;
            if (reserved) close(fd);
        }
        if (reserved && rename(stagedItem.c_str(), finalPath.c_str()) == 0) {
            if (rmdir(stage.c_str()) != 0)
                EVO_LOGW("transfer", "Could not remove staging directory '%s'", stage.c_str());
            return true;
        }
        const int error = errno;
        if (reserved) {
            if (source.isDir) rmdir(finalPath.c_str());
            else unlink(finalPath.c_str());
        }
        if (error == EEXIST) failImport(kAlreadyExists);
        else if (error == ENOSPC) failImport("Internal Storage is full");
        else failImport("Could not save the completed copy");
    } else if (!copied) {
        const int error = errno;
        EVO_LOGE("transfer", "Copy of '%s' failed (errno=%d)", src.c_str(), error);
        failImport(error == ENOSPC ? "Internal Storage is full" : "Could not copy the USB item");
    }

    if (!recursiveDelete(stage))
        EVO_LOGW("transfer", "Could not clean up staging directory '%s'", stage.c_str());
    return false;
}

bool FileTransferService::executeMove(const std::string& src, const std::string& dst) {
    size_t slash = src.find_last_of('/');
    std::string srcName = (slash != std::string::npos) ? src.substr(slash + 1) : src;
    std::string directDst = dst + "/" + srcName;

    // Ultracopier Fast Path: If moving on the same device/mount, rename(2) is instant!
    // Only when nothing is in the way - rename(2) would silently replace a file
    // the collision policy says to keep or rename.
    struct stat dstSt;
    if (stat(directDst.c_str(), &dstSt) != 0 && rename(src.c_str(), directDst.c_str()) == 0) {
        EVO_LOGI("transfer", "executeMove: Fast atomic rename succeeded '%s' -> '%s'", src.c_str(), directDst.c_str());
        return true;
    }

    // Cross-device fallback: copy then recursive delete source
    EVO_LOGW("transfer", "executeMove: rename unavailable (errno=%d), falling back to copy+delete", errno);
    if (!executeCopy(src, dst)) {
        return false;
    }

    if (m_cancelRequested.load()) {
        return false;
    }

    // Copy was completely verified and succeeded: safely remove source
    return recursiveDelete(src);
}

bool FileTransferService::recursiveDelete(const std::string& path) {
    struct stat st;
    const bool importing = m_activeOp == FileOpType::Import;
    const int result = importing ? statWithoutFollowingLinks(path.c_str(), st) : lstat(path.c_str(), &st);
    if (result != 0) {
        if (importing && errno == ELOOP) return unlink(path.c_str()) == 0;
        return errno == ENOENT;
    }

    if (!S_ISDIR(st.st_mode)) {
        return unlink(path.c_str()) == 0;
    }

    evo_dir_t* dir = evo_opendir(path.c_str());
    if (dir) {
        struct dirent* de;
        while ((de = evo_readdir(dir)) != nullptr) {
            if (std::strcmp(de->d_name, ".") == 0 || std::strcmp(de->d_name, "..") == 0) continue;
            std::string child = path + "/" + de->d_name;
            recursiveDelete(child);
        }
        evo_closedir(dir);
    }

    return rmdir(path.c_str()) == 0;
}

bool FileTransferService::executeDelete(const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        size_t slash = path.find_last_of('/');
        m_progress.currentItemName = (slash != std::string::npos) ? path.substr(slash + 1) : path;
        m_progress.currentSourcePath = path;
        m_progress.currentDestPath.clear();
        m_progress.status = TransferStatus::Transferring;
        m_progress.progressPercent = 50.0;
    }
    struct stat st;
    if (statWithoutFollowingLinks(path.c_str(), st) != 0) {
        const int error = errno;
        EVO_LOGE("transfer", "Cannot inspect file for deletion '%s' (errno=%d)", path.c_str(), error);
        errno = error;
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        errno = EINVAL;
        return false;
    }
    if (unlink(path.c_str()) == 0) return true;

    const int error = errno;
    EVO_LOGE("transfer", "Cannot delete file '%s' (errno=%d)", path.c_str(), error);
    errno = error;
    return false;
}

} // namespace evo
