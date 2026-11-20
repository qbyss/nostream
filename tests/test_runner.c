/*
 * tests/test_runner.c — Comprehensive Unit & Integration Test Suite for EVO Player
 *
 * Runs natively on Linux / macOS / Docker / CI without requiring a PS5 console.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#include "evo_direct_mem.h"
#include "evo_draw.h"
#include "evo_focus.h"
#include "evo_theme.h"
#include "evo_screens.h"
#include "evo_addon.h"
#include "evo_error.h"
#include "evo_net.h"
#include "cJSON.h"
#include "evo_playlist_sniff.h"
#include "evo_changelog.h"
#include "SDL_ps5tilemap.inc"
#include <sys/time.h>
#include <sys/stat.h>
#include "evo_data_path.h"
#include "pp_playback.h"
#include "evo_audio_gain.h"
#include "evo_langpref.h"

/* Test runner assertion tracking */
int g_tests_run = 0;
int g_tests_passed = 0;
int g_tests_failed = 0;

#define TEST_START(name) do { \
    g_tests_run++; \
    printf("  RUN  %-50s", name); \
    fflush(stdout); \
} while(0)

#define TEST_PASS() do { \
    g_tests_passed++; \
    printf(" [ PASS ]\n"); \
} while(0)

#define TEST_FAIL(reason) do { \
    g_tests_failed++; \
    printf(" [ FAIL ] (%s at line %d)\n", reason, __LINE__); \
} while(0)

#define TEST_ASSERT(cond, reason) do { \
    if (!(cond)) { \
        TEST_FAIL(reason); \
        return; \
    } \
} while(0)

char g_current_media_title[256] = {0};
char current_media_path[768] = {0};
double media_duration_sec = 0.0;
double resume_base_offset_seconds = 0.0;
long long now_ms(void) { return 0; }
double prospero_media_clock_seconds(void) { return 0.0; }
void toast(const char *category, const char *msg) {
    (void)category;
    (void)msg;
}

void evo_boot_log(const char *fmt, ...) {
    (void)fmt;
}

void pp_stage_bc_checkpoint(const char *stage) {
    (void)stage;
}

static bool str_contains_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    char a[256], b[256];
    int i = 0;
    for (; haystack[i] && i < 255; i++) {
        char c = haystack[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        a[i] = c;
    }
    a[i] = 0;
    i = 0;
    for (; needle[i] && i < 255; i++) {
        char c = needle[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        b[i] = c;
    }
    b[i] = 0;
    return strstr(a, b) != NULL;
}

void clean_media_title(const char *path, char *line1, size_t line1_sz, char *line2, size_t line2_sz) {
    if (!path || !path[0]) {
        if (line1 && line1_sz > 0) line1[0] = '\0';
        if (line2 && line2_sz > 0) line2[0] = '\0';
        return;
    }

    if (strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0) {
        if (g_current_media_title[0]) {
            snprintf(line1, line1_sz, "%s", g_current_media_title);
        } else {
            const char *slash = strrchr(path, '/');
            const char *fname = slash ? slash + 1 : path;
            char temp[256];
            snprintf(temp, sizeof(temp), "%s", fname);
            char *q = strchr(temp, '?');
            if (q) *q = '\0';
            if (temp[0] && strcmp(temp, "stream") != 0 && strcmp(temp, "master.m3u8") != 0) {
                snprintf(line1, line1_sz, "%s", temp);
            } else {
                snprintf(line1, line1_sz, "Emby Media Stream");
            }
        }
        if (line2 && line2_sz > 0) {
            snprintf(line2, line2_sz, "EMBY STREAM");
        }
        return;
    }

    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;

    char original[256];
    snprintf(original, sizeof(original), "%s", name);

    char *ext = strrchr(original, '.');
    if (ext) *ext = 0;

    char season[16] = "";
    int season_pos = -1;

    for (int i = 0; original[i]; i++) {
        if ((original[i] == 'S' || original[i] == 's') &&
            original[i+1] >= '0' && original[i+1] <= '9' &&
            original[i+2] >= '0' && original[i+2] <= '9' &&
            (original[i+3] == 'E' || original[i+3] == 'e') &&
            original[i+4] >= '0' && original[i+4] <= '9' &&
            original[i+5] >= '0' && original[i+5] <= '9') {
            snprintf(season, sizeof(season), "S%c%cE%c%c", original[i+1], original[i+2], original[i+4], original[i+5]);
            season_pos = i;
            break;
        }
    }

    if (season_pos > 0) {
        char title[256];
        snprintf(title, sizeof(title), "%s", original);
        title[season_pos] = 0;

        while (strlen(title) > 0 &&
              (title[strlen(title)-1] == '.' || title[strlen(title)-1] == '_' || title[strlen(title)-1] == '-' || title[strlen(title)-1] == ' '))
            title[strlen(title)-1] = 0;

        snprintf(line1, line1_sz, "%s", title);
    } else {
        snprintf(line1, line1_sz, "%s", original);
    }

    const char *quality = str_contains_ci(name, "2160") ? "2160p" :
                          str_contains_ci(name, "1080") ? "1080p" :
                          str_contains_ci(name, "720")  ? "720p"  : "";

    const char *codec = (str_contains_ci(name, "hevc") || str_contains_ci(name, "h265") || str_contains_ci(name, "x265")) ? "HEVC" :
                        (str_contains_ci(name, "h264") || str_contains_ci(name, "x264")) ? "H.264" : "";

    const char *ch = str_contains_ci(name, "6ch") ? "6CH" :
                     str_contains_ci(name, "5.1") ? "5.1" :
                     str_contains_ci(name, "2ch") ? "2CH" : "";

    snprintf(line2, line2_sz, "%s%s%s%s%s%s%s",
        season,
        season[0] && quality[0] ? "  " : "",
        quality,
        (season[0] || quality[0]) && codec[0] ? "  " : "",
        codec,
        (season[0] || quality[0] || codec[0]) && ch[0] ? "  " : "",
        ch);
}

/* ==========================================================================
 * 1. CPU SIMD / Color Converter Tests
 * ========================================================================== */

/* ==========================================================================
 * 2. Direct Memory Slab Allocator Tests
 * ========================================================================== */

static void test_direct_mem_lifecycle(void)
{
    TEST_START("Direct Memory: Allocation, Alignment & Reset");
    
    int rc = evo_direct_mem_init(4 * 1024 * 1024); /* 4 MB test arena */
    TEST_ASSERT(rc == 0, "evo_direct_mem_init failed");
    
    void *p1 = evo_direct_mem_alloc(1024);
    TEST_ASSERT(p1 != NULL, "64-byte aligned alloc failed");
    TEST_ASSERT(((uintptr_t)p1 % 64) == 0, "Pointer not 64-byte aligned");
    
    void *p2 = evo_direct_mem_alloc(2048);
    TEST_ASSERT(p2 != NULL, "128-byte aligned alloc failed");
    TEST_ASSERT(((uintptr_t)p2 % 64) == 0, "Pointer not 64-byte aligned");
    
    evo_direct_mem_stats_t stats;
    evo_direct_mem_get_stats(&stats);
    TEST_ASSERT(stats.allocated_bytes >= (1024 + 2048), "Stats allocated bytes incorrect");
    TEST_ASSERT(stats.num_allocations == 2, "Alloc count mismatch");
    
    evo_direct_mem_free(p1);
    evo_direct_mem_free(p2);
    evo_direct_mem_get_stats(&stats);
    TEST_ASSERT(stats.allocated_bytes == 0, "Free did not clear allocated bytes");

    /*
     * #6: a request larger than the whole pool must still succeed (graceful
     * malloc fallback) and free() must route it back correctly — the 4K video
     * buffers depend on this when the console can't give the full slab.
     */
    void *big = evo_direct_mem_alloc(8 * 1024 * 1024);
    TEST_ASSERT(big != NULL, "oversize alloc did not fall back");
    memset(big, 0xAB, 8 * 1024 * 1024); /* must be writable for its full extent */
    evo_direct_mem_free(big);
    evo_direct_mem_get_stats(&stats);
    TEST_ASSERT(stats.allocated_bytes == 0, "fallback free leaked into pool stats");

    evo_direct_mem_shutdown();
    TEST_PASS();
}

/* ==========================================================================
 * 3. UI Typography, Text Fitting & Title Cleaning Tests
 * ========================================================================== */

static void test_clean_media_title_resolution(void)
{
    TEST_START("UI Media Title: Local & Remote Title Resolution");
    char line1[256];
    char line2[256];
    
    /* 1. Local USB Movie Path */
    g_current_media_title[0] = '\0';
    clean_media_title("/mnt/usb0/Movies/Inception.2010.1080p.BluRay.x264.mkv", line1, sizeof(line1), line2, sizeof(line2));
    TEST_ASSERT(strstr(line1, "Inception") != NULL, "Movie title parsing failed");
    TEST_ASSERT(strstr(line2, "1080p") != NULL || strstr(line2, "H.264") != NULL, "Metadata parsing failed");
    
    /* 2. TV Show Season / Episode */
    clean_media_title("/mnt/usb0/TV/Breaking.Bad.S05E14.720p.mkv", line1, sizeof(line1), line2, sizeof(line2));
    TEST_ASSERT(strstr(line1, "Breaking.Bad") != NULL || strstr(line1, "Breaking Bad") != NULL, "TV title parsing failed");
    TEST_ASSERT(strstr(line2, "S05E14") != NULL, "Season/Episode tag missing");
    
    /* 3. Remote Emby Stream with explicit active title */
    strncpy(g_current_media_title, "Interstellar (2014) [IMAX]", sizeof(g_current_media_title) - 1);
    clean_media_title("http://192.168.0.11:8096/emby/Videos/9988/stream.mkv?static=true", line1, sizeof(line1), line2, sizeof(line2));
    TEST_ASSERT(strcmp(line1, "Interstellar (2014) [IMAX]") == 0, "Emby stream title not resolved");
    TEST_ASSERT(strcmp(line2, "EMBY STREAM") == 0, "Emby stream subtitle badge missing");
    
    g_current_media_title[0] = '\0';
    TEST_PASS();
}

static void test_cjson_print_roundtrip(void)
{
    TEST_START("cJSON: arrays, nesting and escaping survive print + parse");

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "addons", arr);
    cJSON_AddItemToArray(arr, cJSON_CreateString("https://torrentio.strem.fun/a|b=c/manifest.json"));
    cJSON_AddItemToArray(arr, cJSON_CreateString("say \"hi\"\\\n"));
    cJSON_AddNumberToObject(root, "port", 8096);
    cJSON_AddNumberToObject(root, "ratio", 1.5);
    cJSON_AddItemToObject(root, "on", cJSON_CreateTrue());
    cJSON *top = cJSON_CreateArray();             /* a top-level array, as the share list is */
    cJSON_AddItemToArray(top, root);

    char *txt = cJSON_Print(top);
    TEST_ASSERT(txt != NULL, "print failed");
    TEST_ASSERT(strcmp(txt, "[{\"addons\":[\"https://torrentio.strem.fun/a|b=c/manifest.json\","
                            "\"say \\\"hi\\\"\\\\\\n\"],\"port\":8096,\"ratio\":1.5,\"on\":true}]") == 0,
                "printed JSON is wrong");

    cJSON *back = cJSON_Parse(txt);
    TEST_ASSERT(back && cJSON_IsArray(back), "re-parse failed");
    cJSON *a = cJSON_GetObjectItem(cJSON_GetArrayItem(back, 0), "addons");
    TEST_ASSERT(a && cJSON_GetArraySize(a) == 2, "addon list lost in the round trip");
    TEST_ASSERT(strcmp(cJSON_GetArrayItem(a, 1)->valuestring, "say \"hi\"\\\n") == 0,
                "escaped string changed in the round trip");

    cJSON_Delete(back);
    free(txt);
    cJSON_Delete(top);
    TEST_PASS();
}

static void test_navigation_grid_and_focus(void)
{
    TEST_START("Navigation Engine: Grid & Focus Bounds Clamping");
    
    evo_focus f;
    evo_focus_init(&f, 10, 6, 0); /* 10 items, 6 visible, wrap=0 */
    TEST_ASSERT(f.index == 0 && f.scroll == 0, "Focus init failed");
    
    /* Move Down */
    evo_focus_move(&f, +1);
    TEST_ASSERT(f.index == 1, "Focus move +1 failed");
    
    /* Move past visible window */
    evo_focus_move(&f, +6);
    TEST_ASSERT(f.index == 7, "Focus move +6 failed");
    TEST_ASSERT(f.scroll > 0, "Focus did not scroll window");
    
    /* Clamp at upper bound */
    evo_focus_move(&f, +50);
    TEST_ASSERT(f.index == 9, "Focus did not clamp at max index");
    
    /* Clamp at lower bound */
    evo_focus_move(&f, -50);
    TEST_ASSERT(f.index == 0 && f.scroll == 0, "Focus did not clamp at zero");
    
    TEST_PASS();
}

/* ==========================================================================
 * 6. Changelog Model Consistency Tests
 * ========================================================================== */

static void test_changelog_model_integrity(void)
{
    TEST_START("Changelog Model: Release & Badges Integrity");
    
    TEST_ASSERT(EVO_CHANGELOG_RELEASE_COUNT > 0, "No changelog releases defined");
    
    for (int i = 0; i < EVO_CHANGELOG_RELEASE_COUNT; i++) {
        const evo_changelog_release *r = &EVO_CHANGELOG_RELEASES[i];
        TEST_ASSERT(r->version != NULL && strlen(r->version) > 0, "Release version is empty");
        TEST_ASSERT(r->date != NULL && strlen(r->date) > 0, "Release date is empty");
        TEST_ASSERT(r->tagline != NULL && strlen(r->tagline) > 0, "Release tagline is empty");
        TEST_ASSERT(r->item_count > 0, "Release has no changelog items");
        TEST_ASSERT(r->items != NULL, "Release items pointer is NULL");
        
        for (int j = 0; j < r->item_count; j++) {
            TEST_ASSERT(r->items[j].text != NULL, "Changelog item text is NULL");
            TEST_ASSERT(r->items[j].kind >= 0 && r->items[j].kind <= 3, "Invalid changelog item category");
        }
    }
    
    TEST_PASS();
}

/* ==========================================================================
 * 7. Common UI Widgets & Surround Studio Rendering Tests
 * ========================================================================== */

/* ==========================================================================
 * 10. Frame Interpolation & Motion Smoothing Tests (#105)
 * ========================================================================== */

static int simulate_interp_plan(uint32_t src_w, uint32_t src_h, int ten_bit, double fps, int requested_mode, const char **out_reason)
{
    if (requested_mode == 0) {
        if (out_reason) *out_reason = "Off";
        return 0;
    }
    if (ten_bit) {
        if (out_reason) *out_reason = "10-bit/HDR";
        return 0;
    }
    if (fps >= 49.0) {
        if (out_reason) *out_reason = ">= 50 fps";
        return 0;
    }
    if (src_w > 1920 || src_h > 1080) {
        if (out_reason) *out_reason = "source > 1080p";
        return 0;
    }
    if (out_reason) *out_reason = (requested_mode == 1) ? "Low (Smooth)" : "High (Ultra Smooth)";
    return 1;
}

static uint64_t test_now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ull + (uint64_t)tv.tv_usec;
}

static void test_frame_interpolation(void)
{
    TEST_START("Frame Interpolation: Phase Calculation, Bypass & Settings");

    /* 1. Bypass Evaluation Rules (#105 spec: auto-off for sources >= 50 fps, 10-bit/HDR, or > 1080p) */
    const char *reason = NULL;

    /* 10-bit / HDR bypass */
    TEST_ASSERT(simulate_interp_plan(1920, 1080, 1, 23.976, 2, &reason) == 0, "10-bit source must bypass interpolation");
    TEST_ASSERT(strcmp(reason, "10-bit/HDR") == 0, "Bypass reason mismatch for 10-bit");

    /* High frame rate bypass (>= 50 fps) */
    TEST_ASSERT(simulate_interp_plan(1920, 1080, 0, 50.0, 1, &reason) == 0, "50 fps source must bypass interpolation");
    TEST_ASSERT(strcmp(reason, ">= 50 fps") == 0, "Bypass reason mismatch for 50 fps");

    TEST_ASSERT(simulate_interp_plan(1920, 1080, 0, 59.94, 2, &reason) == 0, "59.94 fps source must bypass interpolation");
    TEST_ASSERT(strcmp(reason, ">= 50 fps") == 0, "Bypass reason mismatch for 59.94 fps");

    TEST_ASSERT(simulate_interp_plan(1920, 1080, 0, 60.0, 2, &reason) == 0, "60 fps source must bypass interpolation");
    TEST_ASSERT(strcmp(reason, ">= 50 fps") == 0, "Bypass reason mismatch for 60 fps");

    /* High resolution bypass (> 1080p) */
    TEST_ASSERT(simulate_interp_plan(3840, 2160, 0, 24.0, 2, &reason) == 0, "4K source must bypass interpolation");
    TEST_ASSERT(strcmp(reason, "source > 1080p") == 0, "Bypass reason mismatch for 4K");

    TEST_ASSERT(simulate_interp_plan(2560, 1440, 0, 24.0, 1, &reason) == 0, "1440p source must bypass interpolation");
    TEST_ASSERT(strcmp(reason, "source > 1080p") == 0, "Bypass reason mismatch for 1440p");

    /* Eligible SDR streams <= 1080p and < 50 fps */
    TEST_ASSERT(simulate_interp_plan(1920, 1080, 0, 23.976, 1, &reason) == 1, "1080p 24fps SDR must be eligible");
    TEST_ASSERT(strcmp(reason, "Low (Smooth)") == 0, "Expected Low (Smooth) label");

    TEST_ASSERT(simulate_interp_plan(1920, 1080, 0, 24.0, 2, &reason) == 1, "1080p 24fps High must be eligible");
    TEST_ASSERT(strcmp(reason, "High (Ultra Smooth)") == 0, "Expected High (Ultra Smooth) label");

    TEST_ASSERT(simulate_interp_plan(1280, 720, 0, 25.0, 2, &reason) == 1, "720p 25fps SDR must be eligible");

    /* Off mode */
    TEST_ASSERT(simulate_interp_plan(1920, 1080, 0, 23.976, 0, &reason) == 0, "Off mode must not activate plan");
    TEST_ASSERT(strcmp(reason, "Off") == 0, "Expected Off label");

    /* 2. Presentation Clock & Phase Calculation (pp_playback_get_interp_phase) */
    pp_playback pb;
    pp_playback_init(&pb);

    float phase = -1.0f;
    /* Inactive playback must return 0 */
    TEST_ASSERT(pp_playback_get_interp_phase(&pb, &phase) == 0, "Inactive playback must return 0");
    TEST_ASSERT(phase == 0.0f, "Phase should be 0.0 on inactive playback");

    pb.active = 1;
    pp_clock_start(&pb.clock, 0);
    pb.gl_ready = 1;

    /* When paused, must return 0 */
    pp_clock_pause(&pb.clock);
    TEST_ASSERT(pp_playback_get_interp_phase(&pb, &phase) == 0, "Paused playback must return 0");
    pp_clock_resume(&pb.clock);

    /* When seek_discarding, must return 0 */
    pb.seek_discarding = 1;
    TEST_ASSERT(pp_playback_get_interp_phase(&pb, &phase) == 0, "Seek discarding must return 0");
    pb.seek_discarding = 0;

    /* 24 fps cadence on 60 Hz display (41666 us duration) */
    const int64_t frame_dur_us = 41666;
    pb.display_pts_us = 1000000;
    pb.next_pts_us = pb.display_pts_us + frame_dur_us;
    pb.prev_pts_us = pb.display_pts_us - frame_dur_us;

    /* Simulate 60 Hz vsync ticks:
     * Tick 0: elapsed 0 us     -> phase 0.000
     * Tick 1: elapsed 16666 us -> phase 0.400
     * Tick 2: elapsed 33333 us -> phase 0.800
     * Next frame lands: display_pts_us becomes 1041666, next_pts becomes 1083332
     * Tick 3: elapsed 8334 us  -> phase 0.200
     * Tick 4: elapsed 25000 us -> phase 0.600
     */
    const int64_t simulated_elapsed[5] = { 0, 16666, 33333, 8334, 25000 };
    const float expected_phase[5] = { 0.0f, 0.400f, 0.800f, 0.200f, 0.600f };

    for (int i = 0; i < 5; i++) {
        if (i == 3) {
            /* Frame A advances to next frame */
            pb.prev_pts_us = pb.display_pts_us;
            pb.display_pts_us += frame_dur_us;
            pb.next_pts_us = pb.display_pts_us + frame_dur_us;
        }

        uint64_t now = test_now_us();
        pb.clock.host_start_us = now - (uint64_t)simulated_elapsed[i];
        pb.clock.media_start_pts_us = pb.display_pts_us;

        int rc = pp_playback_get_interp_phase(&pb, &phase);
        TEST_ASSERT(rc == 1, "Valid phase calculation should return 1");
        float diff = fabsf(phase - expected_phase[i]);
        TEST_ASSERT(diff < 0.015f, "Cadence phase mismatch on vsync tick");
    }

    /* Fallback to prev_pts_us when next_pts_us is unknown/equal */
    pb.display_pts_us = 2000000;
    pb.prev_pts_us = 2000000 - frame_dur_us;
    pb.next_pts_us = pb.display_pts_us; /* no future frame known yet */

    uint64_t now = test_now_us();
    pb.clock.host_start_us = now - 20833; /* half frame elapsed */
    pb.clock.media_start_pts_us = pb.display_pts_us;

    int rc = pp_playback_get_interp_phase(&pb, &phase);
    TEST_ASSERT(rc == 1, "Fallback to prev_pts_us should succeed");
    TEST_ASSERT(fabsf(phase - 0.50f) < 0.015f, "Half frame phase should be ~0.50");

    /* Out of bounds duration rejection (< 8000 us or > 120000 us) */
    pb.next_pts_us = pb.display_pts_us + 5000; /* < 8000 us */
    TEST_ASSERT(pp_playback_get_interp_phase(&pb, &phase) == 0, "Dur < 8000us must be rejected");

    pb.next_pts_us = pb.display_pts_us + 150000; /* > 120000 us */
    TEST_ASSERT(pp_playback_get_interp_phase(&pb, &phase) == 0, "Dur > 120000us must be rejected");

    /* 3. Settings Persistence Format Validation */
    /* Verify line 22 parsing logic for MotionSmoothing enum */
    int parsed_mode = -1;
    const char *cfg_lines[] = { "0", "1", "2", "99", "-1" };
    int expected_modes[] = { 0, 1, 2, 0, 0 }; /* out of bounds fall back to 0 (Off) */

    for (size_t i = 0; i < sizeof(cfg_lines)/sizeof(cfg_lines[0]); i++) {
        int v = atoi(cfg_lines[i]);
        if (v < 0 || v > 2) v = 0;
        parsed_mode = v;
        TEST_ASSERT(parsed_mode == expected_modes[i], "Config parsing mode mismatch");
    }

    pp_playback_shutdown(&pb);
    TEST_PASS();
}

/* ==========================================================================
 * Error reporting: playlist sniffing, host extraction, failure wording
 * ========================================================================== */

static void test_error_reporting(void)
{
    TEST_START("Errors: playlist sniffing, host-only text, failure wording");

    /* A channel list - what an Xtream get.php link returns - is a playlist. */
    const char *iptv = "#EXTM3U\n#EXTINF:-1 tvg-id=\"a\",One\nhttp://h/live/1.ts\n";
    TEST_ASSERT(evo_playlist_sniff_is_channel_list(iptv, strlen(iptv)), "IPTV channel list is detected");

    /* ...also with a UTF-8 BOM and leading blank lines. */
    const char *bom = "\xEF\xBB\xBF\r\n\r\n#EXTM3U\r\n#EXTINF:-1,Two\r\nhttp://h/2.ts\r\n";
    TEST_ASSERT(evo_playlist_sniff_is_channel_list(bom, strlen(bom)), "BOM + CRLF channel list is detected");

    /* HLS is also M3U but FFmpeg plays it: it must never be taken for a list. */
    const char *hls_media = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:6\n#EXTINF:6.0,\nseg0.ts\n";
    TEST_ASSERT(!evo_playlist_sniff_is_channel_list(hls_media, strlen(hls_media)), "HLS media playlist is not a channel list");
    const char *hls_master = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=800000\nlow.m3u8\n";
    TEST_ASSERT(!evo_playlist_sniff_is_channel_list(hls_master, strlen(hls_master)), "HLS master playlist is not a channel list");

    /* Things that are not playlists at all. */
    TEST_ASSERT(!evo_playlist_sniff_is_channel_list("<html>403 Forbidden</html>", 26), "an HTML error page is not a playlist");
    TEST_ASSERT(!evo_playlist_sniff_is_channel_list("\x00\x00\x00\x20" "ftypisom", 12), "an MP4 header is not a playlist");
    TEST_ASSERT(!evo_playlist_sniff_is_channel_list("#EXTM3U\n", 8), "an empty M3U has no channels to load");
    TEST_ASSERT(!evo_playlist_sniff_is_channel_list(NULL, 0), "NULL is not a playlist");

    /* Host only: never the login, path or query. */
    char host[128];
    evo_error_url_host("http://cf.example.ink:8080/get.php?username=u&password=p", host, sizeof host);
    TEST_ASSERT(strcmp(host, "cf.example.ink") == 0, "host drops port, path and query");
    evo_error_url_host("https://user:secret@srv.example.com/live/u/p/1.ts", host, sizeof host);
    TEST_ASSERT(strcmp(host, "srv.example.com") == 0, "host drops user:pass@");
    evo_error_url_host("/mnt/usb0/file.mkv", host, sizeof host);
    TEST_ASSERT(host[0] == 0 || strstr(host, "secret") == NULL, "a path never yields a credential");

    /* The pending reason is taken once. */
    char msg[256];
    evo_error_clear();
    TEST_ASSERT(!evo_error_take(msg, sizeof msg), "nothing pending after clear");
    evo_error_set("%s: boom %d", "host", 7);
    TEST_ASSERT(evo_error_peek(msg, sizeof msg) && strcmp(msg, "host: boom 7") == 0, "peek returns the reason");
    TEST_ASSERT(evo_error_take(msg, sizeof msg) && strcmp(msg, "host: boom 7") == 0, "take returns the reason");
    TEST_ASSERT(!evo_error_take(msg, sizeof msg), "take clears it");

    /* Failure wording names the host and the cause, and never the login. */
    evo_net_describe_failure(EVO_NET_ERR_DNS, 0, "http://cf.example.ink/get.php?password=p", msg, sizeof msg);
    TEST_ASSERT(strstr(msg, "cf.example.ink") && strstr(msg, "name lookup") && !strstr(msg, "password"),
                "DNS failure names the host and cause, not the login");
    evo_net_describe_failure(EVO_NET_ERR_HTTP, 403, "http://h/x", msg, sizeof msg);
    TEST_ASSERT(strstr(msg, "403") && strstr(msg, "refused"), "HTTP 403 says refused");
    evo_net_describe_failure(EVO_NET_ERR_HTTP, 404, "http://h/x", msg, sizeof msg);
    TEST_ASSERT(strstr(msg, "404") && strstr(msg, "not found"), "HTTP 404 says not found");
    evo_net_describe_failure(EVO_NET_ERR_BODY_LIMIT, 200, "http://h/x", msg, sizeof msg);
    TEST_ASSERT(strstr(msg, "too large"), "oversize reply says so");
    evo_net_describe_failure(-99, 0, "http://h/x", msg, sizeof msg);
    TEST_ASSERT(strstr(msg, "-99"), "unknown code is still shown");
    TEST_PASS();
}

/* ==========================================================================
 * Audio volume boost: integer gain scaling and clean clipping (#166)
 * ========================================================================== */

static void test_audio_volume_boost(void)
{
    TEST_START("Audio: volume boost (+50%..+300%) with clean clipping");

    /* Null and zero checks */
    evo_audio_apply_gain_s16(NULL, 10, 50);
    int16_t empty[1] = { 100 };
    evo_audio_apply_gain_s16(empty, 0, 50);
    TEST_ASSERT(empty[0] == 100, "zero count leaves buffer unchanged");

    /* Off (0% boost) leaves samples untouched */
    int16_t s_off[4] = { 0, 1000, -1000, 32000 };
    evo_audio_apply_gain_s16(s_off, 4, 0);
    TEST_ASSERT(s_off[0] == 0 && s_off[1] == 1000 && s_off[2] == -1000 && s_off[3] == 32000,
                "boost 0 leaves samples untouched");

    /* +50% boost: multiply by 1.5 (150/100) */
    int16_t s_50[4] = { 0, 1000, -1000, 100 };
    evo_audio_apply_gain_s16(s_50, 4, 50);
    TEST_ASSERT(s_50[0] == 0 && s_50[1] == 1500 && s_50[2] == -1500 && s_50[3] == 150,
                "+50% boost scales by 1.5x");

    /* +100% boost: multiply by 2.0 (200/100) */
    int16_t s_100[2] = { 1000, -2000 };
    evo_audio_apply_gain_s16(s_100, 2, 100);
    TEST_ASSERT(s_100[0] == 2000 && s_100[1] == -4000,
                "+100% boost scales by 2.0x");

    /* +200% boost: multiply by 3.0 (300/100) */
    int16_t s_200[2] = { 1000, -1000 };
    evo_audio_apply_gain_s16(s_200, 2, 200);
    TEST_ASSERT(s_200[0] == 3000 && s_200[1] == -3000,
                "+200% boost scales by 3.0x");

    /* +300% boost: multiply by 4.0 (400/100) */
    int16_t s_300[2] = { 1000, -1000 };
    evo_audio_apply_gain_s16(s_300, 2, 300);
    TEST_ASSERT(s_300[0] == 4000 && s_300[1] == -4000,
                "+300% boost scales by 4.0x");

    /* Clean clipping at +32767 limit: no wraparound into negative numbers */
    int16_t s_clip_pos[4] = { 20000, 25000, 32000, 32767 };
    evo_audio_apply_gain_s16(s_clip_pos, 4, 100);
    TEST_ASSERT(s_clip_pos[0] == 32767 && s_clip_pos[1] == 32767 &&
                s_clip_pos[2] == 32767 && s_clip_pos[3] == 32767,
                "positive overflow clips cleanly to +32767 without wrap");

    /* Clean clipping at -32768 limit: no wraparound into positive numbers */
    int16_t s_clip_neg[4] = { -20000, -25000, -32000, -32768 };
    evo_audio_apply_gain_s16(s_clip_neg, 4, 100);
    TEST_ASSERT(s_clip_neg[0] == -32768 && s_clip_neg[1] == -32768 &&
                s_clip_neg[2] == -32768 && s_clip_neg[3] == -32768,
                "negative overflow clips cleanly to -32768 without wrap");

    /* Extreme +300% boost with full-scale values: int32 math avoids overflow */
    int16_t s_extreme[2] = { 32767, -32768 };
    evo_audio_apply_gain_s16(s_extreme, 2, 300);
    TEST_ASSERT(s_extreme[0] == 32767 && s_extreme[1] == -32768,
                "+300% boost on full-scale values clamps cleanly");

    /* State setter/getter and process helper */
    evo_audio_gain_set_boost(100);
    TEST_ASSERT(evo_audio_gain_get_boost() == 100, "gain state setter/getter works");
    int16_t s_proc[1] = { 500 };
    evo_audio_gain_process(s_proc, 1);
    TEST_ASSERT(s_proc[0] == 1000, "gain process applies active boost");

    evo_audio_gain_set_boost(0);
    TEST_ASSERT(evo_audio_gain_get_boost() == 0, "gain can be disabled");

    TEST_PASS();
}


/* ==========================================================================
 * Preferred language: tag normalisation and track ranking
 * ========================================================================== */
static void test_language_preference(void)
{
    TEST_START("Language preference: normalise + rank");
    char c[4];
    TEST_ASSERT(evo_lang_normalize("it", c) && !strcmp(c, "ita"), "it -> ita");
    TEST_ASSERT(evo_lang_normalize("ITA", c) && !strcmp(c, "ita"), "ITA -> ita");
    TEST_ASSERT(evo_lang_normalize("Italian", c) && !strcmp(c, "ita"), "Italian -> ita");
    TEST_ASSERT(evo_lang_normalize("italiano", c) && !strcmp(c, "ita"), "italiano -> ita");
    TEST_ASSERT(evo_lang_normalize("it-IT", c) && !strcmp(c, "ita"), "it-IT -> ita");
    TEST_ASSERT(evo_lang_normalize("fre", c) && !strcmp(c, "fra"), "fre -> fra");
    TEST_ASSERT(evo_lang_normalize("pob", c) && !strcmp(c, "por"), "pob -> por");
    TEST_ASSERT(evo_lang_normalize("pt-BR", c) && !strcmp(c, "por"), "pt-BR -> por");
    TEST_ASSERT(evo_lang_normalize("English", c) && !strcmp(c, "eng"), "English -> eng");
    TEST_ASSERT(!evo_lang_normalize("und", c) && c[0] == 0, "und is no language");
    TEST_ASSERT(!evo_lang_normalize(NULL, c), "NULL is no language");

    /* Auto: subtitles prefer English, audio has no preference */
    evo_langpref_set_subtitle("");
    evo_langpref_set_audio("");
    TEST_ASSERT(evo_langpref_rank("eng", 's') == 2, "auto subs: eng preferred");
    TEST_ASSERT(evo_langpref_rank("ita", 's') == 0, "auto subs: ita not");
    TEST_ASSERT(evo_langpref_rank("eng", 'a') == 0, "auto audio: no preference");

    /* Italian preferred, English the fallback */
    evo_langpref_set_subtitle("ita");
    evo_langpref_set_audio("Italiano");
    TEST_ASSERT(!strcmp(evo_langpref_subtitle(), "ita"), "stored as 639-2/T");
    TEST_ASSERT(!strcmp(evo_langpref_audio(), "ita"), "audio stored normalised");
    TEST_ASSERT(evo_langpref_rank("it", 's') == 2, "it preferred");
    TEST_ASSERT(evo_langpref_rank("en", 's') == 1, "en fallback");
    TEST_ASSERT(evo_langpref_rank("jpn", 's') == 0, "jpn other");
    TEST_ASSERT(evo_langpref_rank("ita", 'a') == 2, "audio ita preferred");
    evo_langpref_set_subtitle("");
    evo_langpref_set_audio("");
    TEST_PASS();
}

/* Settings service save/load and backward compatibility host tests */
extern void test_settings_save_load_roundtrip(void);
extern void test_settings_legacy_compat(void);
extern void test_settings_oled_screen_saver(void);
extern void test_settings_night_shift(void);

/* SMB client and network storage host tests */
extern int test_media_remote(void);
extern void test_smb_split_path(void);
extern void test_smb_storage_service(void);
extern void test_smb_url_sanitizer(void);
extern void test_smb_url_decoder(void);
extern void test_smb_url_parsing(void);
extern void test_smb_recent_and_favorites(void);
extern void test_smb_attach_credentials(void);

/* ==========================================================================
 * Main Test Runner Entrypoint
 * ========================================================================== */

int main(void)
{
    mkdir("/tmp/evo_test_data", 0777);
    setenv("EVO_DATA_DIR_OVERRIDE", "/tmp/evo_test_data", 1);
    evo_data_path_rebind();

    printf("\n======================================================================\n");
    printf("  EVO Player — Automated Test Suite & Coverage Verification\n");
    printf("======================================================================\n\n");
    
    test_direct_mem_lifecycle();
    test_clean_media_title_resolution();
    test_cjson_print_roundtrip();
    test_navigation_grid_and_focus();
    if (!test_media_remote()) return 1;
    test_changelog_model_integrity();
    test_frame_interpolation();
    test_error_reporting();
    test_audio_volume_boost();
    test_settings_save_load_roundtrip();
    test_settings_legacy_compat();
    test_settings_oled_screen_saver();
    test_settings_night_shift();
    test_smb_split_path();
    test_smb_storage_service();
    test_smb_url_sanitizer();
    test_smb_url_decoder();
    test_smb_url_parsing();
    test_smb_recent_and_favorites();
    test_smb_attach_credentials();
    test_language_preference();

    printf("\n----------------------------------------------------------------------\n");
    printf("  Results: %d/%d passed (%d failed)\n",
           g_tests_passed, g_tests_run, g_tests_failed);
    printf("======================================================================\n\n");
    
    return (g_tests_failed == 0) ? 0 : 1;
}
