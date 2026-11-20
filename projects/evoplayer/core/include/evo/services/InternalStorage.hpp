#ifndef EVO_INTERNAL_STORAGE_HPP
#define EVO_INTERNAL_STORAGE_HPP

#include "evo/services/FileMetadata.hpp"

#include <cerrno>
#include <string_view>
#include <sys/stat.h>

namespace evo {

inline constexpr char kInternalStorageRoot[] = "/data/homebrew/PPSA99273/videos";

inline bool isInternalStoragePath(std::string_view path) {
    constexpr std::string_view root = kInternalStorageRoot;
    return path == root || (path.compare(0, root.size(), root) == 0 &&
                           path.size() > root.size() && path[root.size()] == '/');
}

inline bool ensureInternalStorage(const char* path = kInternalStorageRoot) {
    struct stat st;
    if (mkdir(path, 0755) != 0 && errno != EEXIST) return false;
    if (statWithoutFollowingLinks(path, st) != 0) return false;
    if (S_ISDIR(st.st_mode)) return true;
    errno = ENOTDIR;
    return false;
}

} // namespace evo

#endif
