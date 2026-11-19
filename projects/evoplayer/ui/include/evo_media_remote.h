#ifndef EVO_MEDIA_REMOTE_H
#define EVO_MEDIA_REMOTE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* App-only input bits, separate from the standard pad mask. */
#define EVO_MEDIA_PLAY_PAUSE UINT32_C(0x01000000)
#define EVO_MEDIA_REWIND     UINT32_C(0x02000000)
#define EVO_MEDIA_FORWARD    UINT32_C(0x04000000)
#define EVO_MEDIA_PREV_CHAPTER UINT32_C(0x08000000)
#define EVO_MEDIA_NEXT_CHAPTER UINT32_C(0x10000000)
#define EVO_MEDIA_CHAPTER (EVO_MEDIA_PREV_CHAPTER | EVO_MEDIA_NEXT_CHAPTER)
#define EVO_MEDIA_TRANSPORT (EVO_MEDIA_PLAY_PAUSE | EVO_MEDIA_REWIND | EVO_MEDIA_FORWARD | EVO_MEDIA_CHAPTER)

typedef struct {
    int handle;
    int user;
    uint64_t retry_at_ms;
    uint32_t buttons;
    int wait_for_release;
    uint64_t seek_pressed_at_ms;
    uint64_t seek_tap_at_ms;
    uint32_t seek_tap;
    uint32_t chapter_key;
    int seek_press_started;
} evo_media_remote;

/* Returns the merged navigation state, accumulating press/release edges
 * from every buffered sample. Play/Pause and chapter jumps appear only in pressed.
 * When chapter gestures are enabled, a second tap replaces seek with a chapter jump. */
uint32_t evo_media_remote_poll(evo_media_remote *remote, uint64_t now_ms,
                              uint32_t standard, uint32_t previous, int chapter_gestures,
                              uint32_t *pressed, uint32_t *released);
void evo_media_remote_close(evo_media_remote *remote);
double evo_media_seek_target(double position, double duration, double delta);

#ifdef __cplusplus
}
#endif
#endif
