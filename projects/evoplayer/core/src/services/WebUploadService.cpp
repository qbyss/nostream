#include "evo/services/WebUploadService.hpp"
#include "evo/services/FileSystemBrowser.hpp"
#include "evo/services/InternalStorage.hpp"
#include "cJSON.h"
#include "evo_boot_log.h"
#include "evo_jailbreak.h"
#include "evo_keyboard.h"
#include "evo_ntfs.h"
#include "evo_readdir.h"
#include "evo_theme.h"
#include "evo_rmlui_bundle.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string_view>
#include <vector>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#ifdef EVO_APP_MODULE
#include <cpuid.h>
#include <immintrin.h>
#else
#include <sys/statvfs.h>
#endif

extern "C" const unsigned char* evo_web_asset(const char* name, unsigned* size);

namespace evo {
namespace {

constexpr size_t kHeaderLimit = 8192;
constexpr size_t kManifestLimit = 256 * 1024;
constexpr int kMaxFiles = 1000;
constexpr const char* kStagingName = "/.nostream-web-upload";
constexpr uint64_t kMaxSafeSize = 9007199254740991ULL;

struct Request {
    std::string method, target, host, origin, buffered;
    uint64_t length = 0;
    bool hasLength = false;
};

struct UploadFile {
    std::string path;
    uint64_t bytes;
    int index;
};

struct Upload {
    std::string id, stage, name;
    std::vector<UploadFile> files;
    size_t next = 0;
    bool folder = false;
    long long activity = 0;
};

std::mutex stateMutex;
WebUploadInfo info;
std::vector<std::string> deletedVideos;
pthread_t worker{};
bool workerStarted = false;
int client = -1;
std::atomic<bool> stopping{false};
std::atomic<bool> enabled{false};
std::atomic<unsigned> generation{0};
unsigned servingGeneration = 0; // Worker thread only.

bool interrupted() {
    return stopping.load() || !enabled.load() || servingGeneration != generation.load();
}
std::atomic<uint64_t> revision{0};

#ifdef EVO_APP_MODULE
// The app's libc does not export arc4random_buf. Keep entropy generation
// independent of the app-module import table.
__attribute__((target("rdrnd")))
bool uploadRandom(uint64_t& value) {
    unsigned eax, ebx, ecx, edx;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx) || !(ecx & bit_RDRND)) return false;
    for (int attempt = 0; attempt < 10; ++attempt) {
        unsigned long long bits;
        if (_rdrand64_step(&bits)) {
            value = static_cast<uint64_t>(bits);
            return true;
        }
    }
    return false;
}
#else
bool uploadRandom(uint64_t& value) {
    arc4random_buf(&value, sizeof(value));
    return true;
}
#endif

using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

bool sendAll(int fd, std::string_view bytes) {
    while (!bytes.empty()) {
#ifdef MSG_NOSIGNAL
        const ssize_t n = send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
#else
        const ssize_t n = send(fd, bytes.data(), bytes.size(), 0);
#endif
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        bytes.remove_prefix(static_cast<size_t>(n));
    }
    return true;
}

void respond(int fd, const char* status, std::string_view body,
             const char* type = "application/json; charset=utf-8") {
    char head[640];
    const int n = std::snprintf(head, sizeof(head),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'none'; script-src 'unsafe-inline'; "
        "style-src 'unsafe-inline'; connect-src 'self'; img-src 'self'; font-src 'self'; frame-ancestors 'none'; base-uri 'none'\r\n\r\n",
        status, type, body.size());
    if (n > 0 && static_cast<size_t>(n) < sizeof(head) && sendAll(fd, {head, static_cast<size_t>(n)}))
        sendAll(fd, body);
}

void error(int fd, const char* status, const char* message) {
    char body[256];
    std::snprintf(body, sizeof(body), "{\"error\":\"%s\"}", message);
    respond(fd, status, body);
}

bool readRequest(int fd, Request& req) {
    std::string head;
    char chunk[4096];
    const auto deadline = evo_log_mono_ms() + 5000;
    size_t end;
    while ((end = head.find("\r\n\r\n")) == std::string::npos) {
        if (head.size() >= kHeaderLimit || evo_log_mono_ms() > deadline) return false;
        const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        head.append(chunk, static_cast<size_t>(n));
    }
    if (end > kHeaderLimit) return false;
    req.buffered = head.substr(end + 4);
    head.resize(end);
    for (const unsigned char c : head)
        if ((c < 32 && c != '\r' && c != '\n' && c != '\t') || c == 127) return false;

    size_t line = head.find("\r\n");
    const auto first = head.substr(0, line);
    const size_t a = first.find(' '), b = first.rfind(' ');
    if (a == std::string::npos || a == b || first.substr(b + 1) != "HTTP/1.1") return false;
    req.method = first.substr(0, a);
    req.target = first.substr(a + 1, b - a - 1);

    std::set<std::string> seen;
    while (line != std::string::npos) {
        const size_t start = line + 2;
        line = head.find("\r\n", start);
        const auto field = head.substr(start, line - start);
        const size_t colon = field.find(':');
        if (colon == 0 || colon == std::string::npos) return false;
        std::string key = field.substr(0, colon);
        for (char& c : key) {
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
        }
        auto value = field.substr(colon + 1);
        const size_t begin = value.find_first_not_of(" \t");
        value = begin == std::string::npos ? "" : value.substr(begin, value.find_last_not_of(" \t") - begin + 1);
        if (key == "transfer-encoding" || key == "expect") return false;
        if (key == "content-length") {
            if (!seen.insert(key).second || value.empty() || value.size() > 16) return false;
            for (char c : value) {
                if (c < '0' || c > '9') return false;
                req.length = req.length * 10 + static_cast<unsigned>(c - '0');
            }
            req.hasLength = true;
        } else if (key == "host" || key == "origin") {
            if (!seen.insert(key).second) return false;
            if (key == "host") req.host = std::move(value);
            else req.origin = std::move(value);
        }
    }
    return !req.host.empty() && req.buffered.size() <= req.length;
}

template<class Consume>
bool readBody(int fd, const Request& req, Consume consume) {
    uint64_t left = req.length;
    std::vector<char> buffer(256 * 1024);
    if (!req.buffered.empty()) {
        if (!consume(req.buffered.data(), req.buffered.size())) return false;
        left -= req.buffered.size();
    }
    while (left && !interrupted()) {
        const ssize_t n = recv(fd, buffer.data(), std::min<uint64_t>(left, buffer.size()), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            if (n == 0) errno = EIO;
            return false;
        }
        if (!consume(buffer.data(), static_cast<size_t>(n))) return false;
        left -= static_cast<uint64_t>(n);
    }
    return left == 0 && !interrupted();
}

bool capacity(const std::string& root, uint64_t& available, uint64_t* totalBytes = nullptr) {
#ifdef EVO_APP_MODULE
    (void)root;
    uint64_t total;
    if (evo_mounted_storage_space("/data", &total, &available) != 0) return false;
    if (totalBytes) *totalBytes = total;
    return true;
#else
    struct statvfs fs{};
    const auto parent = root.substr(0, root.find_last_of('/'));
    if (statvfs(parent.c_str(), &fs) != 0 || !fs.f_frsize ||
        fs.f_blocks > UINT64_MAX / fs.f_frsize) return false;
    available = std::min(fs.f_bavail, fs.f_blocks) * fs.f_frsize;
    if (totalBytes) *totalBytes = fs.f_blocks * fs.f_frsize;
    return true;
#endif
}

bool safePath(std::string_view path) {
    if (path.empty() || path.size() > 768 || path.back() == '/') return false;
    unsigned depth = 0;
    while (!path.empty()) {
        if (++depth > 32) return false;
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (part.empty() || part.size() > 255 || part == "." || part == "..") return false;
        for (unsigned char c : part)
            if (c < 32 || c == 127 || c == '\\' || c == ':') return false;
        if (slash == std::string_view::npos) break;
        path.remove_prefix(slash + 1);
    }
    return true;
}

bool sidecar(const std::string& path, FileCategory category) {
    if (category == FileCategory::Image) return true;
    auto ext = path.substr(path.find_last_of('.') + 1);
    for (char& c : ext) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return ext == "srt" || ext == "ass" || ext == "ssa" || ext == "vtt" || ext == "sub" ||
           ext == "smi" || ext == "sami" || ext == "mpl" || ext == "jss" || ext == "rt" || ext == "stl";
}

bool removeTree(const std::string& path) {
    struct stat st;
    if (statWithoutFollowingLinks(path.c_str(), st) != 0)
        return errno == ENOENT || (errno == ELOOP && unlink(path.c_str()) == 0);
    if (!S_ISDIR(st.st_mode)) return unlink(path.c_str()) == 0;
    evo_dir_t* dir = evo_opendir(path.c_str());
    if (!dir) return false;
    while (const auto* entry = evo_readdir(dir)) {
        if (std::strcmp(entry->d_name, ".") && std::strcmp(entry->d_name, ".."))
            removeTree(path + "/" + entry->d_name);
    }
    evo_closedir(dir);
    return rmdir(path.c_str()) == 0;
}

void discard(Upload& upload) {
    if (!upload.stage.empty() && !removeTree(upload.stage))
        EVO_LOGW("upload", "Could not clean staging directory (errno=%d)", errno);
    upload = {};
}

bool idFormat(std::string_view name) {
    return name.size() == 16 && name.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

void clearStaleUploads(const std::string& staging) {
    struct stat st;
    if (statWithoutFollowingLinks(staging.c_str(), st) != 0 || !S_ISDIR(st.st_mode)) return;
    evo_dir_t* dir = evo_opendir(staging.c_str());
    if (!dir) return;
    while (const auto* entry = evo_readdir(dir))
        if (idFormat(entry->d_name)) removeTree(staging + "/" + entry->d_name);
    evo_closedir(dir);
}

bool nameIsFree(const std::string& path) {
    struct stat st;
    if (statWithoutFollowingLinks(path.c_str(), st) == 0 || errno == ELOOP) {
        errno = EEXIST;
        return false;
    }
    return errno == ENOENT;
}

bool publish(const Upload& upload, const std::string& root) {
    const auto final = root + "/" + upload.name;
    if (upload.folder) {
        if (mkdir(final.c_str(), 0755) != 0) return false;
    } else {
        const int fd = open(final.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0) return false;
        close(fd);
    }
    if (rename((upload.stage + "/" + upload.name).c_str(), final.c_str()) == 0) return true;
    const int saved = errno;
    if (upload.folder) rmdir(final.c_str());
    else unlink(final.c_str());
    errno = saved;
    return false;
}

bool makeParents(const std::string& stage, const std::string& path) {
    for (size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1))
        if (!ensureInternalStorage((stage + "/" + path.substr(0, slash)).c_str())) return false;
    return true;
}

void prepare(int fd, const Request& req, const std::string& root, Upload& active) {
    if (!active.id.empty()) return error(fd, "409 Conflict", "Another upload is in progress.");
    if (!req.hasLength || req.length > kManifestLimit)
        return error(fd, "413 Content Too Large", "This selection has too many files.");
    std::string body;
    body.reserve(static_cast<size_t>(req.length));
    const auto deadline = evo_log_mono_ms() + 15000;
    if (!readBody(fd, req, [&](const char* bytes, size_t len) {
        body.append(bytes, len);
        return evo_log_mono_ms() <= deadline;
    })) return;
    if (body.find('\0') != std::string::npos || body.find("\\u0000") != std::string::npos)
        return error(fd, "400 Bad Request", "Invalid file list.");

    Json manifest(cJSON_ParseWithLength(body.c_str(), body.size() + 1), cJSON_Delete);
    const auto* folder = cJSON_GetObjectItemCaseSensitive(manifest.get(), "folder");
    const auto* files = cJSON_GetObjectItemCaseSensitive(manifest.get(), "files");
    if (!cJSON_IsBool(folder) || !cJSON_IsArray(files))
        return error(fd, "400 Bad Request", "Invalid file list.");
    const int count = cJSON_GetArraySize(files);
    if (count <= 0 || count > kMaxFiles || (cJSON_IsFalse(folder) && count != 1))
        return error(fd, "400 Bad Request", "Choose one video or a folder with up to 1,000 files.");

    Upload pending;
    pending.folder = cJSON_IsTrue(folder);
    bool hasVideo = false;
    uint64_t total = 0;
    int skipped = 0, index = 0;
    std::set<std::string> names, directories;
    for (const auto* item = files->child; item; item = item->next, ++index) {
        const auto* pathItem = cJSON_GetObjectItemCaseSensitive(item, "path");
        const auto* size = cJSON_GetObjectItemCaseSensitive(item, "size");
        if (!cJSON_IsString(pathItem) || !safePath(pathItem->valuestring) || !cJSON_IsNumber(size) ||
            !std::isfinite(size->valuedouble) || size->valuedouble < 0 ||
            size->valuedouble > kMaxSafeSize || std::floor(size->valuedouble) != size->valuedouble)
            return error(fd, "400 Bad Request", "Invalid file path or size.");
        const std::string path = pathItem->valuestring;
        const auto firstSlash = path.find('/');
        const auto top = path.substr(0, firstSlash);
        if ((firstSlash != std::string::npos) != pending.folder ||
            top.front() == '.' || (!pending.name.empty() && pending.name != top))
            return error(fd, "400 Bad Request", "Files must belong to the selected folder.");
        pending.name = top;
        bool hidden = false;
        for (size_t slash = firstSlash; slash != std::string::npos; slash = path.find('/', slash + 1))
            if (path[slash + 1] == '.') hidden = true;
        const auto category = FileSystemBrowser::classifyName(path, DT_REG);
        if (hidden || (category != FileCategory::Video && !(pending.folder && sidecar(path, category)))) {
            ++skipped;
            continue;
        }
        if (!names.insert(path).second || directories.count(path))
            return error(fd, "400 Bad Request", "The file list contains conflicting paths.");
        for (size_t slash = firstSlash; slash != std::string::npos; slash = path.find('/', slash + 1)) {
            auto dir = path.substr(0, slash);
            if (names.count(dir)) return error(fd, "400 Bad Request", "The file list contains conflicting paths.");
            directories.insert(std::move(dir));
        }
        const auto bytes = static_cast<uint64_t>(size->valuedouble);
        if (bytes > kMaxSafeSize - total) return error(fd, "413 Content Too Large", "This selection is too large.");
        total += bytes;
        hasVideo |= category == FileCategory::Video;
        pending.files.push_back({path, bytes, index});
    }
    if (!hasVideo) return error(fd, "415 Unsupported Media Type", "No supported video was selected.");
    if (!ensureInternalStorage(root.c_str()))
        return error(fd, "503 Service Unavailable", "Internal Storage is unavailable.");
    if (!nameIsFree(root + "/" + pending.name))
        return error(fd, "409 Conflict", "That name is already in use or unavailable in Internal Storage.");
    uint64_t available;
    if (capacity(root, available) && total > available)
        return error(fd, "507 Insufficient Storage", "There is not enough free space.");

    const auto staging = root + kStagingName;
    if (!ensureInternalStorage(staging.c_str()))
        return error(fd, "500 Internal Server Error", "Could not prepare Internal Storage.");
    uint64_t random;
    if (!uploadRandom(random))
        return error(fd, "503 Service Unavailable", "Could not generate an upload ID.");
    char id[17];
    std::snprintf(id, sizeof(id), "%016llx", static_cast<unsigned long long>(random));
    pending.id = id;
    pending.stage = staging + "/" + pending.id;
    if (mkdir(pending.stage.c_str(), 0700) != 0)
        return error(fd, "500 Internal Server Error", "Could not prepare the upload.");
    pending.activity = evo_log_mono_ms();
    active = std::move(pending);

    std::string reply = "{\"id\":\"" + active.id + "\",\"files\":[";
    for (const auto& file : active.files) {
        if (reply.back() != '[') reply += ',';
        reply += std::to_string(file.index);
    }
    reply += "],\"skipped\":" + std::to_string(skipped) + '}';
    respond(fd, "200 OK", reply);
}

bool writeFile(int fd, const Request& req, const std::string& path) {
    const int out = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
    if (out < 0) return false;
    const bool written = readBody(fd, req, [&](const char* data, size_t length) {
        while (length && !interrupted()) {
            const auto size = write(out, data, length);
            if (size < 0 && errno == EINTR) continue;
            if (size <= 0) {
                if (size == 0) errno = EIO;
                return false;
            }
            data += size;
            length -= static_cast<size_t>(size);
        }
        return length == 0;
    });
    const int saved = errno;
    if (close(out) != 0 && written) return false;
    errno = saved;
    return written;
}

void receiveFile(int fd, const Request& req, const std::string& root, Upload& upload) {
    const auto& file = upload.files[upload.next];
    if (!req.hasLength || req.length != file.bytes || req.target !=
        "/api/upload/" + upload.id + "/" + std::to_string(file.index))
        return error(fd, "400 Bad Request", "The file does not match the upload.");

    const timeval timeout{30, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (!makeParents(upload.stage, file.path) || !writeFile(fd, req, upload.stage + "/" + file.path)) {
        const int saved = errno;
        discard(upload);
        if (saved == ENOSPC) return error(fd, "507 Insufficient Storage", "Internal Storage ran out of space.");
        return error(fd, "500 Internal Server Error", "The upload was interrupted or could not be saved.");
    }
    upload.activity = evo_log_mono_ms();
    if (++upload.next < upload.files.size()) return respond(fd, "200 OK", "{\"complete\":false}");
    int publishError = 0;
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        if (interrupted()) publishError = ECANCELED;
        else if (!publish(upload, root)) publishError = errno;
    }
    if (publishError) {
        const int saved = publishError;
        discard(upload);
        if (saved == EEXIST) return error(fd, "409 Conflict", "That name already exists in Internal Storage.");
        return error(fd, "500 Internal Server Error", "Could not publish the completed upload.");
    }
    discard(upload);
    ++revision;
    respond(fd, "200 OK", "{\"complete\":true}");
}

std::string decodePath(std::string_view encoded) {
    auto hex = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string path;
    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] != '%') {
            path += encoded[i];
            continue;
        }
        if (i + 2 >= encoded.size()) return {};
        const int hi = hex(encoded[i + 1]), lo = hex(encoded[i + 2]);
        if (hi < 0 || lo < 0) return {};
        path += static_cast<char>((hi << 4) | lo);
        i += 2;
    }
    return path;
}

std::string encodeFilename(std::string_view name) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-') {
            encoded += static_cast<char>(c);
        } else {
            encoded += '%';
            encoded += hex[c >> 4];
            encoded += hex[c & 15];
        }
    }
    return encoded;
}

int openVideo(const std::string& root, const std::string& relative, struct stat& file) {
    if (!safePath(relative) || relative.front() == '.' || relative.find("/.") != std::string::npos ||
        FileSystemBrowser::classifyName(relative, DT_REG) != FileCategory::Video) return -1;
    struct stat dir;
    if (statWithoutFollowingLinks(root.c_str(), dir) != 0 || !S_ISDIR(dir.st_mode)) return -1;
    for (size_t slash = relative.find('/'); slash != std::string::npos; slash = relative.find('/', slash + 1)) {
        if (statWithoutFollowingLinks((root + "/" + relative.substr(0, slash)).c_str(), dir) != 0 ||
            !S_ISDIR(dir.st_mode)) return -1;
    }
    const int input = open((root + "/" + relative).c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (input < 0) return -1;
    if (fstat(input, &file) == 0 && S_ISREG(file.st_mode) && file.st_size >= 0) return input;
    close(input);
    return -1;
}

void downloadVideo(int fd, const std::string& root, const std::string& relative) {
    struct stat file;
    const int input = openVideo(root, relative, file);
    if (input < 0) return error(fd, "404 Not Found", "Video is no longer available.");
    uint64_t remaining = static_cast<uint64_t>(file.st_size);
    const auto filename = encodeFilename(relative.substr(relative.find_last_of('/') + 1));
    const std::string header = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
        "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n"
        "Content-Disposition: attachment; filename*=UTF-8''" + filename +
        "\r\nContent-Length: " + std::to_string(remaining) + "\r\n\r\n";
    if (sendAll(fd, header)) {
        std::vector<char> buffer(256 * 1024);
        while (remaining && !interrupted()) {
            const auto n = read(input, buffer.data(), std::min<uint64_t>(remaining, buffer.size()));
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0 || !sendAll(fd, {buffer.data(), static_cast<size_t>(n)})) break;
            remaining -= static_cast<uint64_t>(n);
        }
    }
    close(input);
}

void deleteVideo(int fd, const std::string& root, const std::string& relative) {
    struct stat file;
    const int input = openVideo(root, relative, file);
    if (input < 0) return error(fd, "404 Not Found", "Video is no longer available.");
    close(input);
    const auto path = root + "/" + relative;
    bool deleted = false;
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        if (!interrupted() && unlink(path.c_str()) == 0) {
            deletedVideos.push_back(path);
            deleted = true;
        }
    }
    if (!deleted) return error(fd, "500 Internal Server Error", "Could not delete the video.");
    ++revision;
    respond(fd, "200 OK", "{}");
}

struct LibraryVideo {
    std::string path;
    uint64_t bytes;
};

void library(int fd, const std::string& root) {
    struct stat rootInfo;
    if (statWithoutFollowingLinks(root.c_str(), rootInfo) != 0) {
        if (errno == ENOENT) return respond(fd, "200 OK", "{\"videos\":[]}");
        return error(fd, "503 Service Unavailable", "Internal Storage is unavailable.");
    }
    if (!S_ISDIR(rootInfo.st_mode))
        return error(fd, "503 Service Unavailable", "Internal Storage is unavailable.");
    std::vector<std::string> folders{""};
    std::vector<LibraryVideo> videos;
    while (!folders.empty() && !interrupted()) {
        auto parent = std::move(folders.back());
        folders.pop_back();
        const auto base = parent.empty() ? root : root + "/" + parent;
        evo_dir_t* directory = evo_opendir(base.c_str());
        if (!directory) {
            if (parent.empty()) return error(fd, "503 Service Unavailable", "Internal Storage is unavailable.");
            continue;
        }
        while (const auto* entry = evo_readdir(directory)) {
            if (entry->d_name[0] == '.' || interrupted()) continue;
            const std::string path = parent.empty() ? entry->d_name : parent + "/" + entry->d_name;
            struct stat st;
            if (statWithoutFollowingLinks((root + "/" + path).c_str(), st) != 0) continue;
            if (S_ISDIR(st.st_mode)) folders.push_back(path);
            else if (S_ISREG(st.st_mode) && FileSystemBrowser::classifyName(path, DT_REG) == FileCategory::Video)
                videos.push_back({path, static_cast<uint64_t>(st.st_size)});
        }
        evo_closedir(directory);
    }
    if (interrupted()) return;
    std::sort(videos.begin(), videos.end(), [](const auto& x, const auto& y) { return x.path < y.path; });
    Json payload(cJSON_CreateObject(), cJSON_Delete);
    auto* items = cJSON_CreateArray();
    cJSON_AddItemToObject(payload.get(), "videos", items);
    for (const auto& video : videos) {
        auto* row = cJSON_CreateObject();
        cJSON_AddStringToObject(row, "path", video.path.c_str());
        cJSON_AddNumberToObject(row, "bytes", static_cast<double>(video.bytes));
        cJSON_AddItemToArray(items, row);
    }
    char* body = cJSON_PrintUnformatted(payload.get());
    if (!body) return error(fd, "500 Internal Server Error", "Could not list videos.");
    respond(fd, "200 OK", body);
    free(body);
}

void theme(int fd) {
    const auto* selected = evo_theme_current();
    Json payload(cJSON_CreateObject(), cJSON_Delete);
    const std::pair<const char*, uint32_t> colors[] = {
        {"bg", selected->bg_top}, {"bg-bottom", selected->bg_bottom},
        {"surface", selected->surface}, {"border", selected->border},
        {"accent", selected->accent}, {"accent-alt", selected->accent_alt},
        {"text", selected->text_primary}, {"text-muted", selected->text_secondary},
        {"danger", selected->danger},
    };
    for (const auto& [key, rgba] : colors) {
        char color[10];
        std::snprintf(color, sizeof(color), "#%02x%02x%02x%02x",
                     rgba & 255, (rgba >> 8) & 255, (rgba >> 16) & 255, rgba >> 24);
        cJSON_AddStringToObject(payload.get(), key, color);
    }
    char* body = cJSON_PrintUnformatted(payload.get());
    if (!body) return error(fd, "500 Internal Server Error", "Theme unavailable.");
    respond(fd, "200 OK", body);
    free(body);
}

void asset(int fd, const char* name, const char* type) {
    unsigned size = 0;
    const auto* data = evo_web_asset(name, &size);
    if (!data) return error(fd, "404 Not Found", "Asset is unavailable.");
    respond(fd, "200 OK", {reinterpret_cast<const char*>(data), size}, type);
}

void handle(int fd, const std::string& authority, const std::string& root, Upload& upload) {
    if (interrupted()) return;
    Request req;
    if (!readRequest(fd, req)) return error(fd, "400 Bad Request", "Invalid HTTP request.");
    if (req.host != authority ||
        ((req.method != "GET" || !req.origin.empty()) && req.origin != "http://" + authority))
        return error(fd, "403 Forbidden", "Open the address shown in NoStream.");
    if (req.method == "GET" && req.length == 0) {
        if (req.target == "/") return asset(fd, "upload.html", "text/html; charset=utf-8");
        if (req.target == "/assets/logo.png") {
            const auto* logo = evo_rmlui_bundle_find("icons/icon_logo.png");
            if (logo) return respond(fd, "200 OK",
                {reinterpret_cast<const char*>(logo->data), logo->size}, "image/png");
            return error(fd, "404 Not Found", "Asset is unavailable.");
        }
        if (req.target == "/assets/inter.woff2") return asset(fd, "inter.woff2", "font/woff2");
        if (req.target == "/assets/inter-semibold.woff2") return asset(fd, "inter-semibold.woff2", "font/woff2");
        if (req.target == "/assets/montserrat-medium.woff2") return asset(fd, "montserrat-medium.woff2", "font/woff2");
        if (req.target.rfind("/api/download/", 0) == 0)
            return downloadVideo(fd, root, decodePath(std::string_view(req.target).substr(sizeof("/api/download/") - 1)));
        if (req.target == "/api/library") return library(fd, root);
        if (req.target == "/api/theme") return theme(fd);
    }
    if (req.method == "GET" && req.target == "/api/space" && req.length == 0) {
        uint64_t available, total;
        if (!capacity(root, available, &total))
            return respond(fd, "200 OK", "{\"availableBytes\":null,\"totalBytes\":null}");
        return respond(fd, "200 OK", "{\"availableBytes\":" + std::to_string(available) +
                        ",\"totalBytes\":" + std::to_string(total) + "}");

    }
    if (req.method == "POST" && req.length == 0 && req.target.rfind("/api/delete/", 0) == 0)
        return deleteVideo(fd, root, decodePath(std::string_view(req.target).substr(sizeof("/api/delete/") - 1)));
    if (req.method == "POST" && req.target == "/api/prepare") return prepare(fd, req, root, upload);
    if (!upload.id.empty()) {
        if (req.method == "PUT" && req.target.rfind("/api/upload/", 0) == 0)
            return receiveFile(fd, req, root, upload);
        if (req.method == "POST" && req.length == 0 && req.target == "/api/cancel/" + upload.id) {
            discard(upload);
            return respond(fd, "200 OK", "{}");
        }
    }
    error(fd, "404 Not Found", "This upload is no longer active.");
}

std::string ipv4Authority(uint32_t address, uint16_t port) {
    const uint32_t ip = ntohl(address);
    const unsigned first = ip >> 24;
    if (first == 0 || first == 127 || first >= 224) return {};
    char authority[24];
    std::snprintf(authority, sizeof(authority), "%u.%u.%u.%u:%u",
                  first, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255,
                  static_cast<unsigned>(port));
    return authority;
}

std::string lanAuthority(uint16_t port) {
    char routed[32]{};
    evo_keyboard_local_ip(routed, sizeof(routed));
    in_addr ip{};
    if (inet_pton(AF_INET, routed, &ip) == 1) {
        auto authority = ipv4Authority(ip.s_addr, port);
        if (!authority.empty()) return authority;
    }

    // A LAN can be reachable without an IPv4 default route.
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) return {};
    std::string authority;
    for (const auto* entry = interfaces; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET ||
            !(entry->ifa_flags & IFF_UP) || (entry->ifa_flags & IFF_LOOPBACK)) continue;
        const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
        authority = ipv4Authority(address->sin_addr.s_addr, port);
        if (!authority.empty()) break;
    }
    freeifaddrs(interfaces);
    return authority;
}

void setListenerStatus(WebUploadStatus status, std::string address = {}) {
    std::lock_guard<std::mutex> guard(stateMutex);
    if (!interrupted()) info = {std::move(address), status};
}

int listenOn(uint16_t& port, const char*& operation) {
    sockaddr_in addr{};
#if defined(__FreeBSD__) || defined(__APPLE__)
    addr.sin_len = sizeof(addr);
#endif
    addr.sin_family = AF_INET;
    // Port zero asks the OS to assign a free port.
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    operation = "socket";
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    const int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    operation = "bind";
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        operation = "listen";
        if (listen(fd, 4) == 0) {
            operation = "getsockname";
            socklen_t size = sizeof(addr);
            if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &size) == 0) {
                port = ntohs(addr.sin_port);
                if (port != 0) return fd;
                errno = EADDRNOTAVAIL;
            }
        }
    }
    const int saved = errno;
    close(fd);
    errno = saved;
    return -1;
}

void finishResponse(int fd) {
    // A rejected request may still be sending its body. Give the response a
    // chance to reach the browser before closing a socket with unread data.
    if (shutdown(fd, SHUT_WR) != 0) return;
    const auto deadline = evo_log_mono_ms() + 100;
    char buffer[4096];
    size_t drained = 0;
    while (drained < kManifestLimit && !interrupted()) {
        const auto remaining = deadline - evo_log_mono_ms();
        if (remaining <= 0) break;
        pollfd ready{fd, POLLIN, 0};
        if (poll(&ready, 1, static_cast<int>(remaining)) <= 0 || !(ready.revents & POLLIN)) break;
        const auto size = recv(fd, buffer, sizeof(buffer), 0);
        if (size <= 0) break;
        drained += static_cast<size_t>(size);
    }
}

void serveListener(int listener, uint16_t port, const std::string& root) {
    Upload upload;
    auto nextAddressCheck = evo_log_mono_ms();
    while (!interrupted()) {
        const auto now = evo_log_mono_ms();
        if (now >= nextAddressCheck) {
            const auto authority = lanAuthority(port);
            if (authority.empty()) setListenerStatus(WebUploadStatus::WaitingForAddress);
            else setListenerStatus(WebUploadStatus::Ready, "http://" + authority);
            nextAddressCheck = now + 2000;
        }
        if (!upload.id.empty() && now - upload.activity > 120000) discard(upload);
        pollfd entry{listener, POLLIN, 0};
        if (poll(&entry, 1, 250) <= 0) continue;
        if (entry.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
        if (!(entry.revents & POLLIN)) continue;
        const int fd = accept(listener, nullptr, nullptr);
        if (fd < 0) continue;
        const timeval timeout{5, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
        {
            std::lock_guard<std::mutex> guard(stateMutex);
            client = fd;
            if (interrupted()) shutdown(fd, SHUT_RDWR);
        }
        // Validate Host and Origin against the address actually reached by the client.
        sockaddr_in local{};
        socklen_t size = sizeof(local);
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&local), &size) == 0 &&
            local.sin_family == AF_INET) {
            const auto authority = ipv4Authority(local.sin_addr.s_addr, port);
            if (!authority.empty()) handle(fd, authority, root, upload);
        }
        finishResponse(fd);
        {
            std::lock_guard<std::mutex> guard(stateMutex);
            close(fd);
            client = -1;
        }
    }
    close(listener);
    discard(upload);
    std::lock_guard<std::mutex> guard(stateMutex);
    info = {};
}

void* serve(void*) {
    const std::string root = kInternalStorageRoot;
    bool cleaned = false;
    int lastListenError = 0;
    while (!stopping.load()) {
        if (enabled.load()) {
            servingGeneration = generation.load();
            if (!evo_jailbreak_is_open()) {
                setListenerStatus(WebUploadStatus::WaitingForStorage);
                usleep(500000);
                continue;
            }
            if (!cleaned) {
                clearStaleUploads(root + kStagingName);
                cleaned = true;
            }
            const char* operation;
            uint16_t port = 0;
            const int listener = listenOn(port, operation);
            if (listener >= 0) {
                lastListenError = 0;
                EVO_LOGI("upload", "Listening on TCP port %u", static_cast<unsigned>(port));
                serveListener(listener, port, root);
                continue;
            }
            const int failure = errno;
            setListenerStatus(WebUploadStatus::ListenerError);
            if (failure != lastListenError) {
                EVO_LOGE("upload", "TCP %s failed with automatic port assignment (errno=%d, %s)",
                         operation, failure, strerror(failure));
                lastListenError = failure;
            }
        }
        usleep(500000);
    }
    return nullptr;
}

} // namespace

[[maybe_unused]] static void startWebUpload() {
    if (workerStarted) return;
    stopping.store(false);
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) == 0) {
        pthread_attr_setstacksize(&attributes, 512 * 1024);
        workerStarted = pthread_create(&worker, &attributes, serve, nullptr) == 0;
        pthread_attr_destroy(&attributes);
    }
    if (!workerStarted) {
        std::lock_guard<std::mutex> guard(stateMutex);
        info.status = WebUploadStatus::ListenerError;
        EVO_LOGE("upload", "Could not start the HTTP worker");
    }
}

void applyWebUploadSetting(bool value) {
    {
        std::lock_guard<std::mutex> guard(stateMutex);
        if (enabled.exchange(value) != value) {
            ++generation;
            info = {};
            if (client >= 0) shutdown(client, SHUT_RDWR);
        }
    }
#ifdef EVO_APP_MODULE
    if (value) startWebUpload();
#endif
}

void requestWebUploadStop() {
    stopping.store(true);
    std::lock_guard<std::mutex> guard(stateMutex);
    info = {};
    if (client >= 0) shutdown(client, SHUT_RDWR);
}

void stopWebUpload() {
    requestWebUploadStop();
    if (workerStarted) {
        pthread_join(worker, nullptr);
        workerStarted = false;
    }
}

WebUploadInfo getWebUploadInfo() {
    std::lock_guard<std::mutex> guard(stateMutex);
    return info;
}

std::vector<std::string> takeWebDeletedVideos() {
    std::lock_guard<std::mutex> guard(stateMutex);
    std::vector<std::string> result;
    result.swap(deletedVideos);
    return result;
}

uint64_t webUploadRevision() { return revision.load(); }

} // namespace evo
