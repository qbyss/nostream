#pragma once

#include "evo_favorites.h"
#include "evo_recent.h"
#include <string>
#include <vector>

namespace evo::hidden_storage {

struct Entry {
    std::string id;
    std::string name;
};

inline constexpr char NetworkId[] = "network";

void load();
const std::vector<Entry>& entries();
bool contains(const char* id);
bool hide(const std::string& id, const std::string& name);
bool restore(const std::string& id);
std::string usbId(const char* path);
bool hidesPath(const char* path, const char* usbId = "");
std::vector<RecentFileEntry> visibleRecent();
std::vector<int> visibleFavorites();

}
