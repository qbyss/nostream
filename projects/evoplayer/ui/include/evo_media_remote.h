#ifndef EVO_MEDIA_REMOTE_H
#define EVO_MEDIA_REMOTE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* App-only press bits, separate from the standard pad mask. */
#define EVO_MEDIA_PLAY_PAUSE UINT32_C(0x01000000)
#define EVO_MEDIA_REWIND     UINT32_C(0x02000000)
#define EVO_MEDIA_FORWARD    UINT32_C(0x04000000)
#define EVO_MEDIA_TRANSPORT (EVO_MEDIA_PLAY_PAUSE | EVO_MEDIA_REWIND | EVO_MEDIA_FORWARD)

typedef struct {
    int handle;
    int user;
    uint64_t retry_at_ms;
    uint32_t buttons;
    int wait_for_release;
} evo_media_remote;

/* Returns the merged navigation state, accumulating press/release edges
 * from every buffered sample. Transport bits appear only in pressed. */
uint32_t evo_media_remote_poll(evo_media_remote *remote, uint64_t now_ms,
                              uint32_t standard, uint32_t previous,
                              uint32_t *pressed, uint32_t *released);
void evo_media_remote_close(evo_media_remote *remote);
double evo_media_seek_target(double position, double duration, double delta);

#ifdef __cplusplus
}
#endif
#endif
