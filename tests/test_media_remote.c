#include "evo_media_remote.h"

#include <stdio.h>
#include <string.h>

static uint8_t samples[16][120];
static int sample_count;
static int open_attempts;
static int closed;
static int fail_read;

int scePadOpen(int user, int type, int index, void *parameters)
{
    (void)index;
    (void)parameters;
    ++open_attempts;
    return user == 7 && type == 2 ? 42 : -1;
}

int scePadRead(int handle, void *out, int count)
{
    if (handle != 42 || count != 16 || fail_read) return -1;
    const int got = sample_count;
    memcpy(out, samples, (size_t)got * sizeof(samples[0]));
    sample_count = 0;
    return got;
}

int scePadClose(int handle)
{
    if (handle == 42) ++closed;
    return 0;
}

static void sample(uint32_t buttons, uint8_t key)
{
    uint8_t *bytes = samples[sample_count++];
    memset(bytes, 0, 120);
    memcpy(bytes, &buttons, sizeof buttons);
    bytes[0x6c] = key;
}

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "Media Remote: %s (line %d)\n", #expr, __LINE__); return 0; } } while (0)

int test_media_remote(void)
{
    const uint32_t cross = 0x4000u, circle = 0x2000u;
    const uint32_t options = 0x0008u, up = 0x0010u;
    evo_media_remote remote = {-1, 7, 0, 0, 0};
    uint32_t pressed, released, held;

    const struct { uint8_t key; uint32_t action; } keys[] = {
        {0x0d, cross}, {0x0f, circle}, {0x2d, EVO_MEDIA_PLAY_PAUSE},
        {0x17, EVO_MEDIA_REWIND}, {0x18, EVO_MEDIA_FORWARD}
    };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        sample(0, keys[i].key);
        sample(0, 0);
        held = evo_media_remote_poll(&remote, i, 0, 0, &pressed, &released);
        CHECK(held == 0 && pressed == keys[i].action);
        CHECK(released == (keys[i].action & ~EVO_MEDIA_TRANSPORT));
    }
    CHECK(open_attempts == 3 && remote.handle == 42);

    sample(options, 0);
    sample(0, 0);
    held = evo_media_remote_poll(&remote, 9, 0, 0, &pressed, &released);
    CHECK(held == 0 && pressed == options && released == options);

    sample(options | up, 0);
    held = evo_media_remote_poll(&remote, 10, options, options, &pressed, &released);
    CHECK(held == (options | up) && pressed == up);
    sample(0, 0);
    held = evo_media_remote_poll(&remote, 11, options, held, &pressed, &released);
    CHECK(held == options && pressed == 0 && released == up);

    sample(0, 0x2d);
    sample(0, 0x2d);
    evo_media_remote_poll(&remote, 12, 0, 0, &pressed, &released);
    CHECK(pressed == EVO_MEDIA_PLAY_PAUSE);
    sample(0, 0x2d);
    evo_media_remote_poll(&remote, 13, 0, 0, &pressed, &released);
    CHECK(pressed == 0);

    sample(0x80000000u, 0x18);
    sample(0, 0x18);
    evo_media_remote_poll(&remote, 14, 0, 0, &pressed, &released);
    CHECK(pressed == 0);
    sample(0, 0);
    sample(up, 0);
    held = evo_media_remote_poll(&remote, 15, 0, 0, &pressed, &released);
    CHECK(held == up && pressed == up);
    fail_read = 1;
    held = evo_media_remote_poll(&remote, 16, 0, held, &pressed, &released);
    CHECK(held == 0 && released == up && closed == 1);
    fail_read = 0;
    evo_media_remote_poll(&remote, 17, 0, 0, &pressed, &released);
    CHECK(open_attempts == 3);
    evo_media_remote_poll(&remote, 5016, 0, 0, &pressed, &released);
    CHECK(open_attempts == 6);
    evo_media_remote_close(&remote);
    CHECK(closed == 2);

    CHECK(evo_media_seek_target(50, 100, -10) == 40);
    CHECK(evo_media_seek_target(50, 100, 10) == 60);
    CHECK(evo_media_seek_target(4, 100, -10) == 0);
    CHECK(evo_media_seek_target(95, 100, 10) == 99);
    puts("  Media Remote mappings, buffered edges and seek bounds [ PASS ]");
    return 1;
}
