#include "evo/services/HiddenStorage.hpp"
#include "evo_data_path.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

namespace evo::hidden_storage {
namespace {
std::vector<Entry> rules;

bool save(std::vector<Entry> updated) {
    const std::string path = evo_data_path("hidden_storage.tsv");
    const std::string pending = path + ".tmp";
    FILE* file = std::fopen(pending.c_str(), "w");
    if (!file) return false;
    bool ok = true;
    for (const auto& entry : updated)
        if (std::fprintf(file, "%s\t%s\n", entry.id.c_str(), entry.name.c_str()) < 0) ok = false;
    if (std::fclose(file) != 0) ok = false;
    if (!ok || std::rename(pending.c_str(), path.c_str()) != 0) {
        std::remove(pending.c_str());
        return false;
    }
    rules = std::move(updated);
    return true;
}
}

void load() {
    FILE* file = std::fopen(evo_data_path("hidden_storage.tsv"), "r");
    if (!file) return;
    rules.clear();
    char line[512];
    while (std::fgets(line, sizeof line, file)) {
        char* tab = std::strchr(line, '\t');
        if (!tab) continue;
        *tab++ = '\0';
        tab[std::strcspn(tab, "\r\n")] = '\0';
        if (line[0] && !contains(line)) rules.push_back({line, tab});
    }
    std::fclose(file);
}

const std::vector<Entry>& entries() { return rules; }

bool contains(const char* id) {
    return std::any_of(rules.begin(), rules.end(), [id](const Entry& entry) { return entry.id == id; });
}

bool hide(const std::string& id, const std::string& name) {
    if (id.empty()) return false;
    if (contains(id.c_str())) return true;
    auto updated = rules;
    auto label = name.substr(0, 255);
    for (char& ch : label)
        if (ch == '\t' || ch == '\r' || ch == '\n') ch = ' ';
    updated.push_back({id, label});
    return save(std::move(updated));
}

bool restore(const std::string& id) {
    auto updated = rules;
    updated.erase(std::remove_if(updated.begin(), updated.end(), [&](const Entry& entry) {
        return entry.id == id;
    }), updated.end());
    return save(std::move(updated));
}

std::string usbId(const char* path) {
    char id[EVO_USB_VOLUME_ID_CAP];
    return recent_usb_identity(path, id) ? id : "";
}

bool hidesPath(const char* path, const char* id) {
    if (std::strncmp(path, "smb://", 6) == 0 || std::strncmp(path, "ftp://", 6) == 0)
        return contains(NetworkId);
    if (id[0]) return contains(id);
    if (rules.empty()) return false;
    char root[EVO_USB_VOLUME_ROOT_CAP];
    if (!evo_usb_volume_root(path, root)) return false;
    const auto identity = usbId(root);
    if (!identity.empty()) return contains(identity.c_str());
    return !evo_usb_identity_ready() && std::any_of(rules.begin(), rules.end(), [](const Entry& entry) {
        return entry.id != NetworkId;
    });
}

std::vector<RecentFileEntry> visibleRecent() {
    std::vector<RecentFileEntry> visible;
    for (int i = 0; i < recent_file_count; ++i) {
        auto entry = recent_files[i];
        if (hidesPath(entry.path, entry.volume_id)) continue;
        if (!recent_resolve(&recent_files[i], entry.path, sizeof entry.path)) continue;
        visible.push_back(entry);
    }
    return visible;
}

std::vector<int> visibleFavorites() {
    std::vector<int> visible;
    for (int i = 0; i < favorite_count; ++i)
        if (!hidesPath(favorite_files[i].path, favorite_files[i].volume_id)) visible.push_back(i);
    return visible;
}
}
