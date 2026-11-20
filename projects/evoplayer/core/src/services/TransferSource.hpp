#ifndef EVO_TRANSFER_SOURCE_HPP
#define EVO_TRANSFER_SOURCE_HPP

#include "evo_ntfs.h"

#include <cerrno>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace evo {

class TransferSource {
public:
    explicit TransferSource(const std::string& path, bool noFollow = false) {
        if (evo_ntfs_is_url(path.c_str())) {
            m_ntfs = evo_ntfs_open(path.c_str());
            if (!m_ntfs) errno = EIO;
        } else {
            m_fd = open(path.c_str(), O_RDONLY | (noFollow ? O_NOFOLLOW : 0));
        }
    }

    ~TransferSource() {
        const int savedError = errno;
        if (m_ntfs) evo_ntfs_close(m_ntfs);
        if (m_fd >= 0) close(m_fd);
        errno = savedError;
    }

    TransferSource(const TransferSource&) = delete;
    TransferSource& operator=(const TransferSource&) = delete;

    bool isOpen() const { return m_fd >= 0 || m_ntfs; }

    ssize_t readChunk(void* buffer, size_t size) {
        if (!m_ntfs) return read(m_fd, buffer, size);
        const long got = evo_ntfs_read(m_ntfs, buffer, size, m_offset);
        if (got < 0) {
            errno = EIO;
        } else {
            m_offset += static_cast<uint64_t>(got);
        }
        return got;
    }

private:
    int m_fd = -1;
    evo_ntfs_file_t* m_ntfs = nullptr;
    uint64_t m_offset = 0;
};

} // namespace evo

#endif
