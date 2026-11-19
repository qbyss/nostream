/* Media Remote port and sample layout adapted from TubeMini's PS5 pad.
 * Copyright (C) 2026 BlackBearReloaded and TubeMini contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "evo_media_remote.h"

#include <stddef.h>

typedef struct {
    uint32_t buttons;
    uint8_t reserved0[0x50 - sizeof(uint32_t)];
    uint64_t timestamp_us;
    uint8_t reserved1[0x6c - 0x58];
    uint8_t key;
    uint8_t reserved2[120 - 0x6d];
} remote_sample;

_Static_assert(sizeof(remote_sample) == 120, "scePadRead sample size");
_Static_assert(offsetof(remote_sample, key) == 0x6c, "remote key offset");

extern int scePadOpen(int user, int type, int index, void *parameters);
extern int scePadRead(int handle, remote_sample *samples, int count);
extern int scePadClose(int handle);

#define INTERCEPTED UINT32_C(0x80000000)
#define SEEK_KEYS (EVO_MEDIA_REWIND | EVO_MEDIA_FORWARD)
#define SEEK_HOLD_MS 220
#define CHAPTER_DOUBLE_TAP_MS 300

static uint32_t map_buttons(const remote_sample *sample)
{
    /* D-pad, Cross, Circle, Square and the remote's Context/Options bit. */
    const uint32_t buttons = sample->buttons & 0xe0f8u;
    switch (sample->key) {
    case 0x0d: return buttons | 0x4000u;
    case 0x0f: return buttons | 0x2000u;
    case 0x2d: return buttons | EVO_MEDIA_PLAY_PAUSE;
    case 0x17: return buttons | EVO_MEDIA_REWIND;
    case 0x18: return buttons | EVO_MEDIA_FORWARD;
    default:   return buttons;
    }
}

static void open_remote(evo_media_remote *remote, uint64_t now_ms)
{
    remote->retry_at_ms = now_ms + 5000;
    if (remote->user >= 0 && remote->user != 0xff)
        remote->handle = scePadOpen(remote->user, 16, 0, NULL);
    if (remote->handle < 0)
        remote->handle = scePadOpen(0xff, 16, 0, NULL);
    if (remote->handle < 0 && remote->user >= 0 && remote->user != 0xff)
        remote->handle = scePadOpen(remote->user, 2, 0, NULL);
}

static void reset_chapter_gesture(evo_media_remote *remote)
{
    remote->seek_tap = 0;
    remote->chapter_key = 0;
    remote->seek_press_started = 0;
}

static void update_chapter_gesture(evo_media_remote *remote, uint32_t buttons,
                                   uint64_t now_ms, uint32_t *pressed)
{
    const uint32_t seek = buttons & SEEK_KEYS;
    const uint32_t old_seek = remote->buttons & SEEK_KEYS;
    if (seek == old_seek) return;

    if (old_seek && remote->seek_press_started && buttons == 0 &&
        now_ms >= remote->seek_pressed_at_ms &&
        now_ms - remote->seek_pressed_at_ms < SEEK_HOLD_MS) {
        remote->seek_tap = old_seek;
        remote->seek_tap_at_ms = now_ms;
    }
    remote->seek_press_started = 0;
    remote->chapter_key = 0;
    if (!seek) return;

    if (!old_seek && seek == remote->seek_tap &&
        now_ms >= remote->seek_tap_at_ms &&
        now_ms - remote->seek_tap_at_ms <= CHAPTER_DOUBLE_TAP_MS) {
        *pressed &= ~seek;
        *pressed |= seek == EVO_MEDIA_REWIND ? EVO_MEDIA_PREV_CHAPTER : EVO_MEDIA_NEXT_CHAPTER;
        remote->chapter_key = seek;
    } else {
        remote->seek_pressed_at_ms = now_ms;
        remote->seek_press_started = 1;
    }
    remote->seek_tap = 0;
}

void evo_media_remote_close(evo_media_remote *remote)
{
    if (remote->handle >= 0) scePadClose(remote->handle);
    remote->handle = -1;
    remote->buttons = 0;
    remote->wait_for_release = 0;
    reset_chapter_gesture(remote);
}

uint32_t evo_media_remote_poll(evo_media_remote *remote, uint64_t now_ms,
                              uint32_t standard, uint32_t previous, int chapter_gestures,
                              uint32_t *pressed, uint32_t *released)
{
    standard &= ~EVO_MEDIA_TRANSPORT;
    if (!chapter_gestures) reset_chapter_gesture(remote);
    uint32_t merged = standard | (remote->buttons & ~(EVO_MEDIA_PLAY_PAUSE | remote->chapter_key));
    *pressed = merged & ~previous;
    *released = previous & ~merged;

    if (remote->handle < 0 && now_ms >= remote->retry_at_ms)
        open_remote(remote, now_ms);
    if (remote->handle < 0) return merged;

    remote_sample samples[16];
    const int count = scePadRead(remote->handle, samples, 16);
    if (count < 0) {
        evo_media_remote_close(remote);
        remote->retry_at_ms = now_ms + 5000;
        *released |= merged & ~standard;
        return standard;
    }

    for (int i = 0; i < count && i < 16; ++i) {
        uint32_t buttons = map_buttons(&samples[i]);
        if (samples[i].buttons & INTERCEPTED) {
            reset_chapter_gesture(remote);
            remote->wait_for_release = 1;
            buttons = 0;
        } else if (remote->wait_for_release) {
            if (samples[i].buttons == 0 && samples[i].key == 0)
                remote->wait_for_release = 0;
            buttons = 0;
        }

        if (chapter_gestures && !remote->wait_for_release) {
            if (buttons & ~SEEK_KEYS) remote->seek_tap = 0;
            const uint64_t sample_ms = samples[i].timestamp_us ? samples[i].timestamp_us / 1000 : now_ms;
            update_chapter_gesture(remote, buttons, sample_ms, pressed);
        }
        const uint32_t next = standard | (buttons & ~(EVO_MEDIA_PLAY_PAUSE | remote->chapter_key));
        *pressed |= (next & ~merged) | (buttons & ~remote->buttons & EVO_MEDIA_PLAY_PAUSE);
        *released |= merged & ~next;
        remote->buttons = buttons;
        merged = next;
    }
    return merged;
}

double evo_media_seek_target(double position, double duration, double delta)
{
    const double maximum = duration > 1.0 ? duration - 1.0 : duration;
    const double target = position + delta;
    if (target < 0.0) return 0.0;
    return target > maximum ? maximum : target;
}
