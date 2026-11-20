#ifndef EVO_FILE_METADATA_HPP
#define EVO_FILE_METADATA_HPP

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace evo {

// App-module directory listings already use open and fstat on PS5 mounts.
inline int statWithoutFollowingLinks(const char* path, struct stat& info) {
    const int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return -1;
    const int result = fstat(fd, &info);
    const int error = errno;
    close(fd);
    errno = error;
    return result;
}

} // namespace evo

#endif
