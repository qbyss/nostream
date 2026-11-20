/*
 * Module: evo_favorites
 *
 * Folder favorites database.
 */

#include <stdio.h>
#include <string.h>

#include "evo_favorites.h"
#include "evo_error.h"

FavoriteEntry favorite_files[MAX_FAVORITES];
int favorite_count = 0;

int favorites_resolve(const FavoriteEntry *entry, char *path, size_t capacity)
{
    if (!capacity) return 0;
    path[0] = 0;
    int written;
    if (!entry->volume_id[0]) {
        written = snprintf(path, capacity, "%s", entry->path);
    } else {
        char original[EVO_USB_VOLUME_ROOT_CAP], current[EVO_USB_VOLUME_ROOT_CAP];
        if (!evo_usb_volume_root(entry->path, original) ||
            !evo_usb_volume_resolve(entry->volume_id, current))
            return 0;
        written = snprintf(path, capacity, "%s%s", current, entry->path + strlen(original));
    }
    return written > 0 && (size_t)written < capacity;
}

int favorites_find(const char *path)
{
    if (!path || !path[0]) return -1;
    char clean_path[512];
    evo_url_sanitize(path, clean_path, sizeof(clean_path));
    for (int i = 0; i < favorite_count; i++) {
        char resolved[512];
        if (favorites_resolve(&favorite_files[i], resolved, sizeof resolved) &&
            strcmp(resolved, clean_path) == 0) return i;
    }
    return -1;
}

int favorites_add_folder(const char *path, const char *title)
{
    if (!path || !path[0]) return 0;
    char clean_path[512];
    evo_url_sanitize(path, clean_path, sizeof(clean_path));
    if (favorites_find(clean_path) >= 0) return 0;
    if (favorite_count >= MAX_FAVORITES) return 0;

    char root[EVO_USB_VOLUME_ROOT_CAP], resolved[EVO_USB_VOLUME_ROOT_CAP];
    evo_usb_volume_info info = {0};
    if (!evo_usb_volume_root(clean_path, root) || !evo_usb_volume_get(root, &info) ||
        !evo_usb_volume_resolve(info.id, resolved) || strcmp(root, resolved) != 0)
        return 0;

    FavoriteEntry *e = &favorite_files[favorite_count];
    memset(e, 0, sizeof(*e));
    snprintf(e->path, sizeof(e->path), "%s", clean_path);

    char clean_title[128];
    evo_url_sanitize(title && title[0] ? title : clean_path, clean_title, sizeof(clean_title));
    snprintf(e->title, sizeof(e->title), "%s", clean_title);
    snprintf(e->volume_id, sizeof e->volume_id, "%s", info.id);

    favorite_count++;
    return 1;
}

void favorites_remove_at(int idx)
{
    if (idx < 0 || idx >= favorite_count) return;

    for (int i = idx; i < favorite_count - 1; i++)
        favorite_files[i] = favorite_files[i + 1];

    --favorite_count;
}

void favorites_save(void)
{
    FILE *fp = fopen(FAVORITES_FILE_DB, "w");
    if (!fp) return;

    for (int i = 0; i < favorite_count; i++) {
        /* Keep the legacy columns so earlier builds can read saved folders. */
        fprintf(fp, "%s\t%s\t0.000\t%s\t1\n",
                favorite_files[i].path,
                favorite_files[i].title,
                favorite_files[i].volume_id[0] ? favorite_files[i].volume_id : "-");
    }

    fclose(fp);
}

void favorites_load(void)
{
    favorite_count = 0;

    FILE *fp = fopen(FAVORITES_FILE_DB, "r");
    if (!fp) return;

    char line[1024];
    while (fgets(line, sizeof(line), fp) && favorite_count < MAX_FAVORITES) {
        FavoriteEntry *e = &favorite_files[favorite_count];
        memset(e, 0, sizeof(*e));

        char *path     = strtok(line, "\t");
        char *title    = strtok(NULL, "\t");
        strtok(NULL, "\t\r\n"); /* Legacy media duration. */

        char *volume_id = strtok(NULL, "\t\r\n");
        char *kind = strtok(NULL, "\t\r\n");
        if (!path || !title || !kind || strcmp(kind, "1") != 0) continue;
        if (volume_id && strcmp(volume_id, "-") != 0) {
            if (strlen(volume_id) >= sizeof e->volume_id) continue;
            snprintf(e->volume_id, sizeof e->volume_id, "%s", volume_id);
        }

        /* Older builds also stored provider stream URLs or a bare channel
         * title here. Keep only paths handled by the storage browser. */
        if (path[0] != '/' && strncmp(path, "file://", 7) != 0 &&
            strncmp(path, "ntfs://", 7) != 0 && strncmp(path, "smb://", 6) != 0 &&
            strncmp(path, "ftp://", 6) != 0 && strncmp(path, "ftps://", 7) != 0)
            continue;

        evo_url_sanitize(path, e->path, sizeof(e->path));
        evo_url_sanitize(title, e->title, sizeof(e->title));

        favorite_count++;
    }

    fclose(fp);
}
