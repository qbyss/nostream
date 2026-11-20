#ifndef EVO_FAVORITES_H
#define EVO_FAVORITES_H

/* Folder favorites persisted in evo_favorites.txt. */

#include "evo_data_path.h"
#include "evo_ntfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FAVORITES_FILE_DB evo_data_path("evo_favorites.txt")
#define MAX_FAVORITES     100

typedef struct {
    char   path[512];
    char   title[128];
    char volume_id[EVO_USB_VOLUME_ID_CAP];
} FavoriteEntry;

extern FavoriteEntry favorite_files[MAX_FAVORITES];
extern int           favorite_count;

int  favorites_find(const char *path);
int favorites_add_folder(const char *path, const char *title);
int favorites_resolve(const FavoriteEntry *entry, char *path, size_t capacity);
void favorites_remove_at(int index);
void favorites_save(void);
void favorites_load(void);

#ifdef __cplusplus
}
#endif

#endif /* EVO_FAVORITES_H */
