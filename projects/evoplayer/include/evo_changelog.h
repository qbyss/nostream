/*
 * evo_changelog — what changed in each release, for the About section.
 */
#ifndef EVO_CHANGELOG_H
#define EVO_CHANGELOG_H

typedef enum {
    EVO_CL_NEW = 0,
    EVO_CL_FIXED,
    EVO_CL_IMPROVED,
    EVO_CL_REMOVED,
    EVO_CL_VERSION
} evo_changelog_kind;

typedef struct evo_changelog_item {
    evo_changelog_kind kind;
    const char        *text;
} evo_changelog_item;

typedef struct evo_changelog_release {
    const char               *version;      /* "0.7.0" */
    const char               *tagline;      /* "EMBY PASSWORD AUTH & REFINED UI" */
    const char               *date;         /* "AUGUST 2026" */
    const evo_changelog_item *items;
    int                       item_count;
} evo_changelog_release;

/*
 * Release 0.1.0 - the first NoStream release (forked from EVO Player 0.13.7)
 *
 * The full notes are in CHANGELOG.md. The screen scrolls this list (right stick).
 */
static const evo_changelog_item EVO_CL_010[] = {
    { EVO_CL_REMOVED,  "MEDIA PROVIDERS - NOSTREAM PLAYS YOUR OWN FILES" },
    { EVO_CL_IMPROVED, "STORAGE SHOWS ONLY FOLDERS AND VIDEOS, THREE ROWS AT A TIME" },
    { EVO_CL_NEW,      "FAVORITE FOLDERS AND SEARCH IN THE STORAGE SIDEBAR" },
    { EVO_CL_NEW,      "USB DRIVES RECOGNISED AT STARTUP, THUMBNAILS KEPT ACROSS RESTARTS" },
    { EVO_CL_NEW,      "HIDE A USB DRIVE OR NETWORK STORAGE - RESTORE IT IN SETTINGS" },
    { EVO_CL_IMPROVED, "A SIMPLER HOME - RESUME APPEARS AS SOON AS RECENT MEDIA IS KNOWN" },
    { EVO_CL_NEW,      "SQUARE OPENS OPTIONS FROM HOME" },
    { EVO_CL_NEW,      "PS5 MEDIA REMOTE - DOUBLE PRESS TO JUMP CHAPTERS" },
    { EVO_CL_IMPROVED, "CONTROLS STAY HIDDEN ON START - CIRCLE CLOSES THEM FIRST" },
    { EVO_CL_NEW,      "RESUME POINTS SAVED EVERY FIVE SECONDS" },
    { EVO_CL_FIXED,    "SOFTWARE-DECODED VIDEO KEEPS ITS TIMESTAMPS" },
    { EVO_CL_NEW,      "TOUCHPAD CLICK PLAYS AND PAUSES" },
    { EVO_CL_NEW,      "NIGHT SHIFT FOR MENUS AND SDR VIDEO, ON A SCHEDULE" },
    { EVO_CL_NEW,      "OLED SCREEN SAVER THAT FADES TO BLACK" },
    { EVO_CL_IMPROVED, "NAVIGATION SOUNDS OFF BY DEFAULT, SOFTER SHADOWS" }
};

static const evo_changelog_item EVO_CL_020[] = {
    { EVO_CL_NEW,      "INTERNAL STORAGE OPENS NOSTREAM'S VIDEOS FOLDER" },
    { EVO_CL_NEW,      "COPY USB VIDEOS AND FOLDERS TO INTERNAL STORAGE" },
    { EVO_CL_IMPROVED, "TRANSFER PROGRESS AND CIRCLE TO CANCEL" }
};

static const evo_changelog_item EVO_CL_030[] = {
    { EVO_CL_IMPROVED, "CONFIRMATION BUTTONS HAVE A THIN OUTLINE AT THE BUTTON EDGE" }
};

static const evo_changelog_item EVO_CL_040[] = {
    { EVO_CL_NEW, "BROWSE SHOWS USED AND FREE SPACE ON EACH DRIVE" }
};

static const evo_changelog_item EVO_CL_050[] = {
    { EVO_CL_NEW, "UPLOAD VIDEOS AND FOLDERS TO INTERNAL STORAGE FROM YOUR WEB BROWSER" },
    { EVO_CL_NEW, "SEE TRANSFER SPEED AND FREE SPACE, AND DOWNLOAD OR DELETE VIDEOS" },
    { EVO_CL_NEW, "THE STARTUP TOAST AND BROWSE SHOW THE WEB UPLOAD ADDRESS" }
};

static const evo_changelog_release EVO_CHANGELOG_RELEASES[] = {
    { "0.5.0", "WEB UPLOAD TO INTERNAL STORAGE", "NOVEMBER 2026", EVO_CL_050, sizeof(EVO_CL_050)/sizeof(EVO_CL_050[0]) },
    { "0.4.0", "DISK SPACE IN BROWSE", "NOVEMBER 2026", EVO_CL_040, sizeof(EVO_CL_040)/sizeof(EVO_CL_040[0]) },
    { "0.3.0", "REFINED CONFIRMATION BUTTONS", "NOVEMBER 2026", EVO_CL_030, sizeof(EVO_CL_030)/sizeof(EVO_CL_030[0]) },
    { "0.2.0", "COPY VIDEOS TO INTERNAL STORAGE", "NOVEMBER 2026", EVO_CL_020, sizeof(EVO_CL_020)/sizeof(EVO_CL_020[0]) },
    { "0.1.0", "THE FIRST NOSTREAM RELEASE", "NOVEMBER 2026", EVO_CL_010, sizeof(EVO_CL_010)/sizeof(EVO_CL_010[0]) },
};

#define EVO_CHANGELOG_RELEASE_COUNT \
    ((int)(sizeof(EVO_CHANGELOG_RELEASES) / sizeof(EVO_CHANGELOG_RELEASES[0])))

#endif /* EVO_CHANGELOG_H */
