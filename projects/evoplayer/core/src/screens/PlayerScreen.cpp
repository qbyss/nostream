#include "evo_media_remote.h"
#include "evo/screens/PlayerScreen.hpp"
#include "evo/Application.hpp"
#include "evo/services/NetworkStorageService.hpp"
#include "evo_rmlui_bridge.h"
#include "evo_playback.h"
#include "evo_demux.h"
#include "evo_audio_out.h"
#include "evo_subtitle.h"
#include "evo_vdec.h"
#include "evo_perf_monitor.h"
#include "evo_toast.h"
#include "evo_feedback.h"
#include "evo_boot_log.h"
#include "evo_dovi.h"
#include "evo_pt.h"
#include "evo_data_path.h"
#include "evo_agc_runtime.h"
#include "evo_hw.h"
#include "pp_playback.h"   /* g_pp_pb.seek_discarding - the LOADING badge */
#include "evo_parallel_io.h"
#include "prospero_thumbnail.h"
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <sys/time.h>

extern evo_vdec *g_vdec;
extern AVFormatContext *play_fmt;
extern AVCodecContext *audio_ctx;
extern int perf_render_fps;
/* Bridge.cpp - reads the DEBUG OVERLAY setting for both FPS pills. */
int evo_fps_counter_enabled(void);
extern int perf_decode_fps;
extern int detected_audio_rate;
extern int evo_audio_channels;
extern char current_media_path[768];
extern double resume_base_offset_seconds;
void draw_video_frame_to_fb(uint32_t *fb, int x, int y, int max_w, int max_h);
extern pp_playback g_pp_pb;
/* Bridge.cpp - raised at open and again whenever the demux queue runs dry on a
 * network source. Both decode threads are parked for as long as it is set. */
extern volatile int pb_prebuffer_hold;
}

#include <cstdio>
#include <cctype>
#include <cstring>
#include <cmath>
#include <ctime>
#include <algorithm>

namespace evo {

static uint64_t NowMs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000ULL + static_cast<uint64_t>(tv.tv_usec / 1000ULL);
}

/*
 * The network reader is stuck waiting on the server and the video queue has
 * run dry. The demuxer is blocked inside that read, so the rebuffer hold never
 * arms and nothing else would say why the picture stopped: on hardware
 * 2026-10-07 a server outage showed a frozen frame, silently, for a minute.
 */
static bool readerStalled() {
    evo_pio_stats_t st;
    evo_pio_get_stats(&st);
    /* A seek empties the queue and the new range takes seconds to start on a
     * debrid/torrent host. That is a slow seek, not a lost connection (hardware
     * 2026-10-22: 5 s and 12 s seeks, no failed chunk), so stay quiet until it
     * settles. */
    if (g_pp_pb.seek_discarding)
        return false;
    return st.active && st.stall_ms > 3000 && packet_queue_count(&video_packet_queue) < 3;
}

PlayerScreen::PlayerScreen()
    : StatefulScreen("PlayerScreen")
    , m_playerFsm(PlayerScreenState::NormalPlayback, "PlayerPresenterFSM")
{
    initPlayerStateMachine();
    /* #167: the diagnostics panel stays the way the user left it. */
    if (FILE* f = std::fopen(evo_data_path("evo_stats_hud.cfg"), "r")) {
        int on = 0;
        if (std::fscanf(f, "%d", &on) == 1) m_showStatsForNerds = (on != 0);
        std::fclose(f);
    }
}

void PlayerScreen::initPlayerStateMachine() {
    m_playerFsm
        .addState(PlayerScreenState::NormalPlayback, "NormalPlayback")
        .addState(PlayerScreenState::OsdVisible, "OsdVisible")
        .addState(PlayerScreenState::Scrubbing, "Scrubbing")
        .addState(PlayerScreenState::StatsOverlay, "StatsOverlay");

    m_playerFsm
        .addTransition(PlayerScreenState::NormalPlayback, PlayerScreenEvent::ShowOsd, PlayerScreenState::OsdVisible)
        .addTransition(PlayerScreenState::NormalPlayback, PlayerScreenEvent::StartScrub, PlayerScreenState::Scrubbing)
        .addTransition(PlayerScreenState::NormalPlayback, PlayerScreenEvent::ToggleStats, PlayerScreenState::StatsOverlay)
        .addTransition(PlayerScreenState::OsdVisible, PlayerScreenEvent::HideOsd, PlayerScreenState::NormalPlayback)
        .addTransition(PlayerScreenState::OsdVisible, PlayerScreenEvent::StartScrub, PlayerScreenState::Scrubbing)
        .addTransition(PlayerScreenState::OsdVisible, PlayerScreenEvent::ToggleStats, PlayerScreenState::StatsOverlay)
        .addTransition(PlayerScreenState::Scrubbing, PlayerScreenEvent::EndScrub, PlayerScreenState::OsdVisible)
        .addTransition(PlayerScreenState::StatsOverlay, PlayerScreenEvent::ToggleStats, PlayerScreenState::NormalPlayback)
        .addTransition(PlayerScreenState::StatsOverlay, PlayerScreenEvent::ShowOsd, PlayerScreenState::OsdVisible);
}

void PlayerScreen::toggleStatsForNerds() {
    m_showStatsForNerds = !m_showStatsForNerds;
    m_playerFsm.postEvent(PlayerScreenEvent::ToggleStats);
    if (FILE* f = std::fopen(evo_data_path("evo_stats_hud.cfg"), "w")) {
        std::fprintf(f, "%d\n", m_showStatsForNerds ? 1 : 0);
        std::fclose(f);
    }
}

/*
 * The clock subtitles are timed against: the raw audio clock when a real audio
 * track is open, otherwise the video clock, plus the resume offset - never the
 * OSD position, which reports the scrub target while the user is scrubbing and
 * would drag every cue to the scrub head instead of the picture on screen.
 */
double PlayerScreen::subtitleClockSeconds() const {
    double clock = (audio_ctx && audio_handle >= 1) ? (double)audio_clock_seconds
                                                    : evo_pb_video_clock_s();
    double pos = resume_base_offset_seconds + clock;
    return (pos < 0.0) ? 0.0 : pos;
}

namespace {

constexpr uint64_t kSkipShowMs = 8000;   /* how long the button stays up */
constexpr uint64_t kSkipBackMs = 8000;   /* how long Triangle still undoes an automatic skip */

enum { kIntroSegment = 1, kOutroSegment };

struct ChapterSegment {
    int kind;
    double start_sec;
    double end_sec;
};

struct SegFlags {
    bool seen = false, noAuto = false, handled = false;
    uint64_t shownAt = 0;
};

struct SkipState {
    std::string key;
    std::vector<ChapterSegment> chap;   /* from chapter titles */
    bool chapBuilt = false;
    std::vector<SegFlags> flags;
    size_t flagsFor = 0;                        /* size of the list the flags belong to */
    bool havePrev = false;
    double prevPos = 0.0;
    int shown = -1;                             /* stretch whose button is up */
    std::string label;
    double backTo = 0.0;                        /* where an automatic skip came from */
    uint64_t backUntil = 0;
};
SkipState g_skip;

/* A chapter title as an intro (1), credits (2) or neither (0): whole words, any case. */
int chapter_kind(const std::string& title)
{
    static const char* const kIntro[] = {"intro", "opening", "op", "recap"};
    static const char* const kOutro[] = {"credits", "ending", "ed", "outro"};
    std::string word;
    for (size_t i = 0; i <= title.size(); ++i) {
        const unsigned char c = i < title.size() ? static_cast<unsigned char>(title[i]) : 0;
        if (std::isalpha(c)) {
            word += static_cast<char>(std::tolower(c));
            continue;
        }
        if (!word.empty()) {
            for (const char* w : kIntro) if (word == w) return kIntroSegment;
            for (const char* w : kOutro) if (word == w) return kOutroSegment;
            word.clear();
        }
    }
    return 0;
}

void build_chapter_segments(IMediaMetadataService* meta, double duration)
{
    g_skip.chap.clear();
    if (!meta || duration <= 1.0) return;
    const size_t n = meta->getChapterCount();
    if (n == 0) return;
    for (size_t i = 0; i < n; ++i) {
        const ChapterInfo* ch = meta->getChapter(i);
        if (!ch) continue;
        const int kind = chapter_kind(ch->title);
        if (!kind) continue;
        const ChapterInfo* nx = i + 1 < n ? meta->getChapter(i + 1) : nullptr;
        const double end = nx ? nx->startTimeSeconds : duration;
        if (end - ch->startTimeSeconds < 3.0) continue;
        /* A chapter called "Opening" that runs for half an hour is a scene, not a title sequence. */
        if (kind == kIntroSegment && end - ch->startTimeSeconds > 600.0) continue;
        g_skip.chap.push_back({kind, ch->startTimeSeconds, end});
    }
    g_skip.chapBuilt = true;
}

/* The stretch the playhead is inside (-1 for none). */
int active_segment(double pos)
{
    const auto& segs = g_skip.chap;
    for (size_t i = 0; i < segs.size(); ++i)
        if (pos >= segs[i].start_sec - 0.5 && pos < segs[i].end_sec - 1.0)
            return static_cast<int>(i);
    return -1;
}

/* What the button says for a stretch. */
const char* skip_text(const ChapterSegment& sg)
{
    if (sg.kind == kIntroSegment) return "Skip intro";
    return "Skip credits";
}

/* Move past a stretch. True when the playhead moved within this item (so it can be undone). */
template <class P>
bool skip_past(P& playback, const ChapterSegment& sg)
{
    const double dur = playback->getDurationSeconds();
    playback->seekTo(sg.end_sec < dur - 2.0 ? sg.end_sec : dur - 1.0);
    return true;
}

/* Once per frame: note where the playhead is, raise or drop the button, run Auto. */
template <class P>
void skip_tick(P& playback, uint64_t now)
{
    g_skip.shown = -1;
    if (playback->isLiveSource() || playback->isMusicMode()) return;

    const std::string key = playback->getCurrentFilePath();
    if (key != g_skip.key) {
        g_skip = SkipState();
        g_skip.key = key;
    }
    if (!g_skip.chapBuilt)
        build_chapter_segments(Application::getInstance().getMediaMetadataService(),
                               playback->getDurationSeconds());

    const auto& segs = g_skip.chap;
    if (g_skip.flagsFor != segs.size() || g_skip.flags.size() != segs.size()) {
        g_skip.flags.assign(segs.size(), SegFlags());
        g_skip.flagsFor = segs.size();
    }

    ISettingsService* st = Application::getInstance().getSettingsService();
    const SkipSegments mode = st ? st->getSkipSegments() : SkipSegments::Ask;

    /* A scrub in progress moves the playhead on purpose: judge it when it lands. */
    if (playback->isScrubbing()) return;
    const double pos = playback->getPositionSeconds();
    const bool jumped = g_skip.havePrev && (pos < g_skip.prevPos - 0.5 || pos > g_skip.prevPos + 2.5);
    g_skip.havePrev = true;
    g_skip.prevPos = pos;

    if (mode == SkipSegments::Off || segs.empty()) return;
    const int i = active_segment(pos);
    if (i < 0) return;
    SegFlags& f = g_skip.flags[i];
    const ChapterSegment sg = segs[i];
    if (!f.seen) {
        f.seen = true;
        /* Playback walked into it from before: auto is fine. Seeked or resumed into it: not. */
        f.noAuto = jumped || pos > sg.start_sec + 3.0;
        f.shownAt = now;
        evo_boot_log("skip: enter %s %.1f-%.1f at %.1f via %s, mode %d, auto %s",
                     sg.kind == kIntroSegment ? "intro" : "credits", sg.start_sec, sg.end_sec, pos,
                     "chapters", static_cast<int>(mode),
                     f.noAuto ? "no (seeked in)" : "yes");
    }
    if (f.handled) return;

    if (mode == SkipSegments::Auto) {
        if (f.noAuto) return;
        f.handled = true;
        evo_boot_log("skip: auto skip past %.1f", sg.end_sec);
        if (skip_past(playback, sg)) {
            g_skip.backTo = std::max(0.0, sg.start_sec);
            g_skip.backUntil = now + kSkipBackMs;
            toast("PLAYBACK", sg.kind == kIntroSegment ? "Skipped intro - press Triangle to go back"
                                                          : "Skipped credits - press Triangle to go back");
        }
        return;
    }
    /* Ask */
    if (now - f.shownAt >= kSkipShowMs) return;
    g_skip.shown = i;
    g_skip.label = skip_text(sg);
}

/* Triangle: press the button, or undo an automatic skip. False = not ours. */
template <class P>
bool skip_press(P& playback, uint64_t now)
{
    if (g_skip.backUntil > now) {
        g_skip.backUntil = 0;
        evo_boot_log("skip: Triangle undoes the auto skip, back to %.1f", g_skip.backTo);
        playback->seekTo(g_skip.backTo);
        toast("PLAYBACK", "Back to the start");
        return true;
    }
    const auto& segs = g_skip.chap;
    if (g_skip.shown < 0 || g_skip.shown >= static_cast<int>(segs.size())) return false;
    const ChapterSegment sg = segs[g_skip.shown];
    g_skip.flags[g_skip.shown].handled = true;
    g_skip.shown = -1;
    evo_boot_log("skip: Triangle pressed, skipping past %.1f", sg.end_sec);
    if (skip_past(playback, sg))
        toast("PLAYBACK", sg.kind == kIntroSegment ? "Skipped intro" : "Skipped credits");
    return true;
}

} // namespace

bool PlayerScreen::hasActiveOverlay() const {
    if (isVideoSettingsOpen() || m_osdVisibilityAlpha > 0 || m_showStatsForNerds || g_skip.shown >= 0) {
        return true;
    }
    auto playback = Application::getInstance().getPlaybackController();
    if (playback && playback->isMusicMode()) return true;
    if (prospero_subtitle_enabled) {
        if (playback) {
            double subPos = prospero_subtitle_position(subtitleClockSeconds());
            if (prospero_subtitle_use_external) {
                const ProsperoSubtitleCue* cue = prospero_subtitle_active_cue(subPos);
                if (cue && cue->text[0]) return true;
            } else {
                char activeSubText[PROSPERO_EMBEDDED_SUBTITLE_TEXT_SIZE] = {0};
                prospero_embedded_subtitle_text_at(subPos, activeSubText, sizeof(activeSubText));
                if (activeSubText[0]) return true;
                ProsperoSubtitleImage img;
                if (prospero_embedded_subtitle_image_at(subPos, &img)) return true;
            }

            // #110: a caption in the secondary line is an overlay too.
            char secondarySubText[PROSPERO_EMBEDDED_SUBTITLE_TEXT_SIZE] = {0};
            prospero_secondary_subtitle_text_at(subtitleClockSeconds(), secondarySubText,
                                                sizeof(secondarySubText));
            if (secondarySubText[0]) return true;
        }
    }
    return false;
}

void PlayerScreen::onEnter() {
    StatefulScreen::onEnter();
    const bool returningToSettings = m_returnToVideoSettings;
    if (!returningToSettings) {
        closeVideoSettings();
        g_skip = SkipState();   /* #219: a file played twice offers its skip twice */
    }
    m_returnToVideoSettings = false;
    m_controlsLastUsedMs = NowMs();

    auto playback = Application::getInstance().getPlaybackController();
    if (!returningToSettings && playback && !playback->isMusicMode() && !playback->isPaused()) {
        hideControlsOnPlay();
        m_playerFsm.postEvent(PlayerScreenEvent::HideOsd);
    } else {
        m_hideControlsAfterPlay = false;
        m_osdVisibilityAlpha = 255;
        m_playerFsm.postEvent(PlayerScreenEvent::ShowOsd);
    }
}

void PlayerScreen::onExit() {
    StatefulScreen::onExit();
}

void PlayerScreen::closeVideoSettings() {
    m_videoSettingsRow = 0;
    m_returnToVideoSettings = false;
}

void PlayerScreen::handleVideoSettingsInput(uint32_t pressed) {
    constexpr int rowCount = 5;
    if (pressed & (PadButtons::Options | PadButtons::Circle)) {
        closeVideoSettings();
        evo_feedback(EVO_FB_CANCEL);
        return;
    }

    if (pressed & (PadButtons::Up | PadButtons::Down)) {
        const int step = (pressed & PadButtons::Up) ? -1 : 1;
        m_videoSettingsRow = (m_videoSettingsRow - 1 + step + rowCount) % rowCount + 1;
        evo_feedback(EVO_FB_MOVE);
        return;
    }

    auto playback = Application::getInstance().getPlaybackController();
    if (m_videoSettingsRow == 3 && (pressed & (PadButtons::Left | PadButtons::Right | PadButtons::Cross))) {
        if (playback && !playback->isMusicMode()) {
            const int step = (pressed & PadButtons::Left) ? 2 : 1;
            playback->setViewMode(static_cast<ViewMode>((static_cast<int>(playback->getViewMode()) + step) % 3));
            evo_feedback(EVO_FB_MOVE);
        } else {
            evo_feedback(EVO_FB_BOUNDARY);
        }
        return;
    }
    if (!(pressed & PadButtons::Cross)) return;

    if (m_videoSettingsRow == 5) {
        toggleStatsForNerds();
        evo_feedback(EVO_FB_CONFIRM);
        return;
    }

    auto sm = Application::getInstance().getScreenManager();
    if (!sm) return;
    const ScreenId target = m_videoSettingsRow == 1 ? ScreenId::AudioTrackPicker :
                            m_videoSettingsRow == 2 ? ScreenId::SubtitlePicker : ScreenId::MediaInfo;
    m_returnToVideoSettings = true;
    sm->navigateTo(target);
    evo_feedback(EVO_FB_OPEN);
}

void PlayerScreen::hideControlsOnPlay() {
    m_hideControlsAfterPlay = true;
    m_osdVisibilityAlpha = 0;
}

bool PlayerScreen::handleInput(uint32_t pressed, uint32_t held, uint32_t released) {
    (void)released;
    auto playback = Application::getInstance().getPlaybackController();
    auto screenMgr = Application::getInstance().getScreenManager();
    if (!playback) return false;

    if (pressed) {
        m_hideControlsAfterPlay = false;
        m_controlsLastUsedMs = NowMs();
    }

    if (isVideoSettingsOpen()) {
        handleVideoSettingsInput(pressed);
        return true;
    }

    if (pressed & PadButtons::Options) {
        if (playback->isScrubbing()) playback->confirmScrub();
        if (playback->isActive()) {
            m_videoSettingsRow = 1;
            evo_feedback(EVO_FB_OPEN);
        }
        return true;
    }

    if (pressed & PadButtons::Circle) {
        evo_feedback(EVO_FB_CONFIRM);
        playback->stopPlayback();
        if (screenMgr) screenMgr->returnFromPlayback();
        return true;
    }

    if (pressed & EVO_MEDIA_CHAPTER) {
        if (playback->isScrubbing()) playback->cancelScrub();
        playback->jumpChapter((pressed & EVO_MEDIA_NEXT_CHAPTER) ? 1 : -1);
        return true;
    }

    playback->updateScrubHold(held);

    if (pressed & EVO_MEDIA_PLAY_PAUSE) {
        if (!playback->isActive() || playback->isScrubbing()) return true;
        if (evo_pb_is_eof() && !playback->isLiveSource()) {
            playback->seekTo(0.0);
            playback->setPaused(false);
            hideControlsOnPlay();
            return true;
        }
    }

    if (pressed & (PadButtons::Cross | EVO_MEDIA_PLAY_PAUSE)) {
        uint64_t now = NowMs();
        if (now - m_lastPlayPauseToggleMs >= 150) {
            m_lastPlayPauseToggleMs = now;
            playback->togglePause();
            if (!playback->isPaused() && playback->isActive())
                hideControlsOnPlay();
        }
        return true;
    }
    if (pressed & PadButtons::Triangle) {
        return skip_press(playback, NowMs());
    }
    /* Live TV has no timeline: seek and chapter keys are swallowed, not sent
     * to a demuxer that has nothing behind the live edge. */
    if (playback->isLiveSource() &&
        (pressed & (PadButtons::L1 | PadButtons::R1 | PadButtons::Left | PadButtons::Right)))
        return true;
    if (pressed & PadButtons::L1) {
        playback->jumpChapter(-1);
        return true;
    }
    if (pressed & PadButtons::R1) {
        playback->jumpChapter(1);
        return true;
    }
    /* #150: R3 opens the chapter list - only for a file that has chapters. */
    if (pressed & PadButtons::R3) {
        auto meta = Application::getInstance().getMediaMetadataService();
        if (meta && meta->getChapterCount() > 0 && !playback->isScrubbing() && screenMgr) {
            screenMgr->navigateTo(ScreenId::ChapterPicker);
            return true;
        }
    }
    if (pressed & PadButtons::Left) {
        playback->moveScrub(-10.0);
        return true;
    }
    if (pressed & PadButtons::Right) {
        playback->moveScrub(10.0);
        return true;
    }
    return false;
}

void PlayerScreen::update(double deltaMs) {
    StatefulScreen::update(deltaMs);
    m_playerFsm.update(deltaMs);

    auto playback = Application::getInstance().getPlaybackController();
    if (!playback) return;

    playback->tickScrubAutoCommit();

    /*
     * The decoder gave up (repeated failures, or a hardware call that stopped
     * returning - #39). The engine has already raised its error toast; without
     * this the player sat on a frozen picture with the controls up and nothing
     * to say why. Leave through the same path as STOP so the toast lands on the
     * screen the user returns to.
     */
    if (evo_pb_decode_fatal()) {
        playback->stopPlayback();
        if (auto sm = Application::getInstance().getScreenManager())
            sm->returnFromPlayback();
        return;
    }

    uint64_t now = NowMs();

    /* #219: the skip button, or the automatic skip, for any file with intro / credits marks. */
    skip_tick(playback, now);

    /*
     * Hold the OSD up once the clip has finished.
     *
     * Nothing in the list below is true at EOF - the clock simply stops, it is
     * not "paused", and the 3.5s input timer has long expired - so the OSD
     * faded out and the screen was left showing a held last frame with no
     * indication of what had happened or what the controls were. evo_pb_is_eof()
     * stays set for as long as the finished file is open, so the OSD stays up
     * until something actually changes (a seek back clears it).
     */
    const bool ended = (evo_pb_is_eof() != 0);

    /*
     * Ask the playback engine whether it is paused, not the state machine.
     *
     * Those two disagree. A D-pad seek sets `player_paused` directly
     * (evo_demux.c) and so does restoring a subtitle track, neither of which
     * goes through the FSM - so isPaused() reported false while the picture
     * was genuinely stopped, the 3.5s input timer ran out, and the OSD faded
     * away over a paused frame with nothing on screen to say why.
     *
     * The PAUSED badge below deliberately keeps using isPaused(): the engine
     * flag is also raised for the moment a seek is in flight, and a badge that
     * blinked on every seek would be worse than one that is occasionally late.
     */
    const bool reallyPaused = (evo_pb_is_paused() != 0) || playback->isPaused();

    /* Hold the OSD active while video frames are still buffering into the
     * decoder - and while a seek is: on a network stream that is seconds of a
     * still picture that would otherwise read as frozen. */
    /* Say so once when the server stops the picture, and once when it is back.
     * Raised here rather than in the reader, which stalls long before anyone
     * can see it: the read-ahead keeps playing for half a minute. */
    const bool stalled = readerStalled();
    if (stalled && !m_stallToastShown) {
        m_stallToastShown = true;
        toast("CONNECTION LOST", "Can't get data from the server - retrying...");
    } else if (!stalled && m_stallToastShown && !pb_prebuffer_hold) {
        m_stallToastShown = false;
        toast("CONNECTION", "Reconnected - the stream is back");
    }

    const bool isBuffering = !playback->isMusicMode() &&
                             (!video_frame_loaded || g_pp_pb.seek_discarding ||
                              pb_prebuffer_hold || stalled);
    if (isBuffering) {
        m_controlsLastUsedMs = now;
    }

    // The demuxer can reach EOF while the decoded picture is still catching up.
    const bool showEndControls = ended && !isBuffering &&
        (!m_hideControlsAfterPlay ||
         (playback->getDurationSeconds() > 0.0 &&
          playback->getPositionSeconds() >= playback->getDurationSeconds() - 0.5));
    const bool wantsControls = isVideoSettingsOpen() || showEndControls ||
        (!m_hideControlsAfterPlay &&
         (playback->isMusicMode() || now - m_controlsLastUsedMs < 3500 || reallyPaused ||
          m_showStatsForNerds || playback->isScrubbing() || isBuffering));

    if (playback->isScrubbing()) {
        m_playerFsm.postEvent(PlayerScreenEvent::StartScrub);
    } else if (wantsControls) {
        m_playerFsm.postEvent(PlayerScreenEvent::ShowOsd);
    } else {
        m_playerFsm.postEvent(PlayerScreenEvent::HideOsd);
    }

    // Smooth OSD fade in/out
    if (wantsControls) {
        if (m_osdVisibilityAlpha < 255) {
            m_osdVisibilityAlpha = std::min(255, m_osdVisibilityAlpha + 42);
        }
    } else {
        if (m_osdVisibilityAlpha > 0) {
            m_osdVisibilityAlpha = std::max(0, m_osdVisibilityAlpha - 28);
        }
    }
}

void PlayerScreen::feedPerformanceHud() {
    static char l_video[192], l_audio[192], l_subs[128];
    static char l_perf[160], l_queues[160], l_clocks[160];
    static char l_source[160], l_pipeline[160];

    char vcodec[48] = "NONE", acodec[48] = "NONE", alang[24] = "und";
    const char *hdrName = "SDR";
    int vw = 0, vh = 0, ach = 0, arate = detected_audio_rate;

    if (play_fmt && video_stream_index >= 0 && video_stream_index < (int)play_fmt->nb_streams) {
        AVCodecParameters *cp = play_fmt->streams[video_stream_index]->codecpar;
        if (cp) {
            std::snprintf(vcodec, sizeof(vcodec), "%s", avcodec_get_name(cp->codec_id));
            vw = cp->width;
            vh = cp->height;
            if (evo_dovi_stream_profile(cp) > 0)                hdrName = "DOLBY VISION";
            else if (cp->color_trc == AVCOL_TRC_SMPTE2084)      hdrName = "HDR10";
            else if (cp->color_trc == AVCOL_TRC_ARIB_STD_B67)   hdrName = "HLG";
        }
    }
    if (play_fmt && audio_stream_index >= 0 && audio_stream_index < (int)play_fmt->nb_streams) {
        AVStream *st = play_fmt->streams[audio_stream_index];
        if (st && st->codecpar) {
            std::snprintf(acodec, sizeof(acodec), "%s", avcodec_get_name(st->codecpar->codec_id));
            ach = st->codecpar->ch_layout.nb_channels;
            arate = st->codecpar->sample_rate;
            const AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", nullptr, 0);
            if (lang && lang->value && lang->value[0]) {
                std::snprintf(alang, sizeof(alang), "%s", lang->value);
            }
        }
    }

    const char *backend = (evo_pb_active_backend() == EVO_VDEC_BACKEND_NATIVE)
                              ? "HARDWARE" : "SOFTWARE";

    /* HDR peak mapping in effect on the last frame (Settings -> HDR PEAK) */
    char hdrBuf[64];
    {
        const int mode = evo_agc_runtime_hdr_map_mode();
        const int peak = evo_agc_runtime_get_hdr_peak();
        if (mode == 2)
            std::snprintf(hdrBuf, sizeof hdrBuf, "%s > %d NITS PER SHOT", hdrName, peak);
        else if (mode == 1)
            std::snprintf(hdrBuf, sizeof hdrBuf, "%s > %d NITS", hdrName, peak);
        else
            std::snprintf(hdrBuf, sizeof hdrBuf, "%s", hdrName);
    }
    std::snprintf(l_video, sizeof(l_video), "VIDEO  %s  %dx%d  %.3f fps  %s  %s",
                  vcodec, vw, vh, evo_pb_video_fps(), hdrBuf, backend);
    std::snprintf(l_audio, sizeof(l_audio), "AUDIO  %s  %s  %dch  %d Hz  %s",
                  acodec, alang, ach, arate, evo_pt_is_active() ? "BITSTREAM" : "PCM");

    /* Container and where it plays from: the demuxer's first name ("mov,mp4,..." reads
     * as MP4), then the source location. */
    char container[24] = "?";
    if (play_fmt && play_fmt->iformat && play_fmt->iformat->name) {
        const char *n = play_fmt->iformat->name;
        if (std::strstr(n, "mp4")) {
            std::snprintf(container, sizeof(container), "MP4");
        } else {
            std::snprintf(container, sizeof(container), "%.*s",
                          static_cast<int>(std::strcspn(n, ",")), n);
            for (char *c = container; *c; ++c)
                if (*c >= 'a' && *c <= 'z') *c = static_cast<char>(*c - 'a' + 'A');
        }
    }
    const char* where = std::strstr(current_media_path, "://") ? "NETWORK" : "LOCAL FILE";
    std::snprintf(l_source, sizeof(l_source), "SOURCE  %s  /  %s", container, where);
    std::snprintf(l_subs, sizeof(l_subs), "SUBS  %s  /  %s%s",
                  prospero_subtitle_enabled ? "ON" : "OFF",
                  prospero_subtitle_use_external ? "EXTERNAL"
                      : (prospero_embedded_subtitle_stream_index >= 0 ? "EMBEDDED" : "NONE"),
                  (prospero_subtitle_enabled && prospero_secondary_subtitle_active())
                      ? "  +  2ND TRACK" : "");
    std::snprintf(l_perf, sizeof(l_perf), "RENDER %d fps  /  DECODE %d fps  /  DROPPED %llu",
                  perf_render_fps, perf_decode_fps,
                  static_cast<unsigned long long>(g_pp_pb.stats.frames_late_dropped));
    std::snprintf(l_pipeline, sizeof(l_pipeline), "UPSCALER %s  /  SMOOTH %s  /  %s",
                  evo_agc_upscale_label(), evo_agc_motion_smoothing_label(),
                  evo_hw_model_name());
    std::snprintf(l_queues, sizeof(l_queues), "QUEUES  video %d/96  audio %d/96  pcm %d",
                  packet_queue_count(&video_packet_queue),
                  packet_queue_count(&audio_packet_queue),
                  audio_queue_count);
    std::snprintf(l_clocks, sizeof(l_clocks), "CLOCKS  video %.2f  audio %.2f  delta %.3f",
                  evo_pb_video_clock_s(), audio_clock_seconds,
                  evo_pb_video_clock_s() - audio_clock_seconds);

    evo_perf_snapshot_t s;
    evo_perf_monitor_get(&s);

    evo_perf_hud_t hud;
    std::memset(&hud, 0, sizeof(hud));
    hud.line_video = l_video;
    hud.line_audio = l_audio;
    hud.line_subs = l_subs;
    hud.line_perf = l_perf;
    hud.line_queues = l_queues;
    hud.line_clocks = l_clocks;
    hud.line_source = l_source;
    hud.line_pipeline = l_pipeline;
    hud.hist_len = EVO_PERF_HIST;
    hud.gpu_hist = s.gpu_hist;
    hud.ram_hist = s.ram_hist;
    hud.cpu_hist = s.cpu_hist;
    hud.gpu_pct = s.gpu_pct;
    hud.gpu_peak_pct = s.gpu_peak_pct;
    hud.ram_mb = s.ram_mb;
    hud.ram_peak_mb = s.ram_peak_mb;
    hud.ram_total_mb = s.ram_total_mb;
    hud.cpu_pct = s.cpu_pct;
    hud.cpu_peak_pct = s.cpu_peak_pct;

    evo_rmlui_update_perf_hud(&hud);
}

void PlayerScreen::render(uint32_t* framebuffer, int width, int height) {
    auto playback = Application::getInstance().getPlaybackController();
    auto metaService = Application::getInstance().getMediaMetadataService();
    if (!playback) return;

    static int s_player_screen_log = 5;
    if (s_player_screen_log > 0) {
        s_player_screen_log--;
        evo_boot_log("PlayerScreen::render fb=%p %dx%d alpha=%d rml_init=%d",
                     (void*)framebuffer, width, height, m_osdVisibilityAlpha,
                     evo_rmlui_is_initialized());
        evo_boot_log_flush();
    }

    /*
     * Both branches only matter to the CPU rasteriser. In AGC mode RmlUi draws
     * through the GPU and never reads this buffer back, so the music-mode fill
     * was 8.3 MB of stores a frame that nothing looked at -
     * draw_video_frame_to_fb has already been an empty function since the GL
     * quad took over.
     */
    if (evo_rmlui_blit_mode()) {
        if (!playback->isMusicMode()) {
            draw_video_frame_to_fb(framebuffer, 0, 0, width, height);
        } else {
            std::fill_n(framebuffer, width * height, 0xFF000000);
        }
    }

    char activeSubText[PROSPERO_EMBEDDED_SUBTITLE_TEXT_SIZE] = {0};
    ProsperoSubtitleImage subImage = {};
    bool hasSubImage = false;
    if (prospero_subtitle_enabled && !playback->isMusicMode()) {
        double subPos = prospero_subtitle_position(subtitleClockSeconds());

        if (!prospero_subtitle_use_external) {
            hasSubImage = prospero_embedded_subtitle_image_at(subPos, &subImage) != 0;
        }

        if (prospero_subtitle_use_external) {
            const ProsperoSubtitleCue* cue = prospero_subtitle_active_cue(subPos);
            if (cue && cue->text[0]) {
                std::snprintf(activeSubText, sizeof(activeSubText), "%s", cue->text);
            }
        } else {
            prospero_embedded_subtitle_text_at(subPos, activeSubText, sizeof(activeSubText));
        }
    }

    // #110: the secondary line of dialogue, with its own delay applied.
    char secondarySubText[PROSPERO_EMBEDDED_SUBTITLE_TEXT_SIZE] = {0};
    if (prospero_subtitle_enabled && !playback->isMusicMode()) {
        prospero_secondary_subtitle_text_at(subtitleClockSeconds(), secondarySubText,
                                            sizeof(secondarySubText));
    }

    if ((m_osdVisibilityAlpha > 0 || isVideoSettingsOpen() || playback->isMusicMode() ||
         m_showStatsForNerds) && evo_rmlui_is_initialized()) {
        std::string titleStr, metaStr;
        /*
         * #90 / #9: a provider supplies its own title, and deriving one from
         * the source string is what put a URL with a query string on the OSD.
         * cleanMediaTitle takes the last path component and tidies it, which is
         * exactly right for /mnt/usb0/Movie.2019.mkv and exactly wrong for
         * http://host/emby/Videos/abc/stream?api_key=...
         */
        titleStr = playback->getDisplayTitle();
        if (titleStr.empty() && metaService) {
            std::string fileFormat;
            metaService->cleanMediaTitle(playback->getCurrentFilePath(), titleStr, fileFormat);
        }
        if (playback->isLiveSource())
            metaStr = !video_frame_loaded ? "BUFFERING..." : "LIVE";

        /*
         * A rebuffer parks both decode threads mid-playback, so the picture
         * freezes on its last frame with the title still on the OSD - which
         * reads as a hung player rather than a short link. Say which it is.
         * This overrides whatever the branches above chose: while the hold is
         * up, nothing else about the stream is the useful thing to show.
         */
        if (readerStalled())
            metaStr = "WAITING FOR THE SERVER...";
        else if (pb_prebuffer_hold)
            metaStr = "BUFFERING...";

        evo_playback_osd_params_t p;
        std::memset(&p, 0, sizeof(p));
        p.title = titleStr.empty() ? "Video Playback" : titleStr.c_str();
        p.metadata = metaStr.c_str();
        p.is_live = playback->isLiveSource() ? 1 : 0;
        p.position_sec = playback->getPositionSeconds();
        p.duration_sec = playback->getDurationSeconds();
        p.percentage = playback->getPercentage();
        p.paused = playback->isPaused() ? 1 : 0;
        p.scrub_active = playback->isScrubbing() ? 1 : 0;
        p.loading = (!playback->isMusicMode() &&
                     (!video_frame_loaded || g_pp_pb.seek_discarding)) ? 1 : 0;
        p.scrub_target = playback->getScrubTargetSeconds();
        /* Scrub preview: ask the worker for the frame under the cursor and hand
         * on whatever it has published since this scrub began (the baseline
         * keeps the last scrub's frame from flashing up first). */
        {
            static std::vector<uint32_t> thumbPixels;
            static unsigned long long thumbBaseline = 0, thumbSerial = 0;
            const char *thumbPath = current_media_path[0] ? current_media_path : playback->getCurrentFilePath().c_str();
            if (p.scrub_active && thumbPath[0] && !playback->isMusicMode()) {
                if (!m_scrubThumbActive) {
                    m_scrubThumbActive = true;
                    thumbBaseline = prospero_thumbnail_serial();
                    thumbSerial = 0;
                }
                std::string credThumb = NetworkStorageService::attachCredentials(thumbPath ? thumbPath : "");
                prospero_thumbnail_request(credThumb.c_str(), p.scrub_target, 1);
                const int words = PROSPERO_THUMB_W * PROSPERO_THUMB_H;
                if (static_cast<int>(thumbPixels.size()) < words)
                    thumbPixels.resize(static_cast<size_t>(words));
                const unsigned long long now = prospero_thumbnail_serial();
                if (now && now != thumbSerial && now != thumbBaseline) {
                    const unsigned long long got = prospero_thumbnail_snapshot(thumbPixels.data(), words);
                    if (got) {
                        if (!thumbSerial)
                            evo_boot_log("thumb: first preview frame of this scrub at %.0fs", p.scrub_target);
                        thumbSerial = got;
                    }
                }
                if (thumbSerial) {
                    p.scrub_thumb = thumbPixels.data();
                    p.scrub_thumb_w = PROSPERO_THUMB_W;
                    p.scrub_thumb_h = PROSPERO_THUMB_H;
                    p.scrub_thumb_serial = thumbSerial;
                }
            } else {
                m_scrubThumbActive = false;
            }
        }
        p.view_mode = static_cast<int>(playback->getViewMode());
        p.show_stats = m_showStatsForNerds ? 1 : 0;
        p.alpha = m_osdVisibilityAlpha;
        p.subtitle_text = activeSubText[0] ? activeSubText : nullptr;
        if (hasSubImage) {
            p.subtitle_image = subImage.pixels;
            p.subtitle_image_x = subImage.x;
            p.subtitle_image_y = subImage.y;
            p.subtitle_image_w = subImage.w;
            p.subtitle_image_h = subImage.h;
            p.subtitle_image_canvas_w = subImage.canvas_w;
            p.subtitle_image_canvas_h = subImage.canvas_h;
        }
        p.subtitle_face = prospero_subtitle_face;
        p.subtitle_raised = m_osdVisibilityAlpha > 0 && !isVideoSettingsOpen();
        p.subtitle_text2 = secondarySubText[0] ? secondarySubText : nullptr;
        p.subtitle2_position = prospero_secondary_position;
        p.subtitle2_color = prospero_secondary_color;
        p.subtitle2_face = prospero_secondary_face;
        p.subtitle2_background = prospero_secondary_background;
        p.subtitle_style = prospero_subtitle_style;
        p.subtitle_color = prospero_subtitle_color;            /* #231 */
        p.subtitle_lift = prospero_subtitle_lift;
        p.subtitle_background = prospero_subtitle_background;
        p.chrome_hidden = isVideoSettingsOpen() || m_osdVisibilityAlpha == 0;
        p.video_settings_row = m_videoSettingsRow;
        p.skip_label = !isVideoSettingsOpen() && g_skip.shown >= 0 ? g_skip.label.c_str() : nullptr;   /* #219 */
        p.fps = perf_render_fps;
        p.debug_overlay = evo_fps_counter_enabled();
        p.music_mode = playback->isMusicMode() ? 1 : 0;
        p.music_codec = "AUDIO";
        p.music_cover = playback->musicCover(&p.music_cover_w, &p.music_cover_h,
                                             &p.music_cover_gen);

        /* #150: chapters. */
        static double chapterStarts[128];
        static char chapterTitle[96], scrubChapterTitle[96];
        chapterTitle[0] = scrubChapterTitle[0] = '\0';
        if (metaService && !playback->isLiveSource() && !playback->isMusicMode()) {
            const int cn = static_cast<int>(std::min<size_t>(metaService->getChapterCount(), 128));
            for (int i = 0; i < cn; ++i)
                chapterStarts[i] = metaService->getChapter(i)->startTimeSeconds;
            p.chapter_count = cn;
            p.chapter_starts = chapterStarts;
            if (cn > 0) {
                int ci = metaService->getChapterIndexAtPosition(playback->getPositionSeconds());
                if (ci >= 0)
                    std::snprintf(chapterTitle, sizeof(chapterTitle), "%s", metaService->getChapter(ci)->title.c_str());
                if (playback->isScrubbing()) {
                    int si = metaService->getChapterIndexAtPosition(playback->getScrubTargetSeconds());
                    if (si >= 0)
                        std::snprintf(scrubChapterTitle, sizeof(scrubChapterTitle), "%s",
                                      metaService->getChapter(si)->title.c_str());
                }
                p.chapter_title = chapterTitle;
                p.scrub_chapter_title = scrubChapterTitle;
            }
        }

        /* #151: time left, and the clock time the video ends at. The end time
         * moves while paused, as the remaining time is fixed then. */
        static char remainingBuf[24], endsAtBuf[16];
        remainingBuf[0] = endsAtBuf[0] = '\0';
        if (!playback->isLiveSource() && !playback->isMusicMode() && p.duration_sec > 1.0) {
            const long left = static_cast<long>(std::max(0.0, p.duration_sec - p.position_sec));
            if (left >= 3600)
                std::snprintf(remainingBuf, sizeof(remainingBuf), "-%ld:%02ld:%02ld", left / 3600, (left / 60) % 60, left % 60);
            else
                std::snprintf(remainingBuf, sizeof(remainingBuf), "-%ld:%02ld", left / 60, left % 60);
            time_t endT = time(nullptr) + left;
            struct tm tmv;
            if (localtime_r(&endT, &tmv))
                std::snprintf(endsAtBuf, sizeof(endsAtBuf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
            p.remaining = remainingBuf;
            p.ends_at = endsAtBuf;
        }

        evo_rmlui_update_playback_params(&p);
        if (m_showStatsForNerds) {
            feedPerformanceHud();
        }
        evo_rmlui_render_playback_osd(framebuffer, width, height);
    } else if ((activeSubText[0] || secondarySubText[0] || hasSubImage || g_skip.shown >= 0) && !playback->isMusicMode() &&
               evo_rmlui_is_initialized()) {
        evo_playback_osd_params_t p;
        std::memset(&p, 0, sizeof(p));
        p.title = "Video Playback";
        p.subtitle_text = activeSubText;
        if (hasSubImage) {
            p.subtitle_image = subImage.pixels;
            p.subtitle_image_x = subImage.x;
            p.subtitle_image_y = subImage.y;
            p.subtitle_image_w = subImage.w;
            p.subtitle_image_h = subImage.h;
            p.subtitle_image_canvas_w = subImage.canvas_w;
            p.subtitle_image_canvas_h = subImage.canvas_h;
        }
        p.subtitle_face = prospero_subtitle_face;
        p.subtitle_raised = 0;
        p.subtitle_text2 = secondarySubText[0] ? secondarySubText : nullptr;
        p.subtitle2_position = prospero_secondary_position;
        p.subtitle2_color = prospero_secondary_color;
        p.subtitle2_face = prospero_secondary_face;
        p.subtitle2_background = prospero_secondary_background;
        p.subtitle_style = prospero_subtitle_style;
        p.subtitle_color = prospero_subtitle_color;            /* #231 */
        p.subtitle_lift = prospero_subtitle_lift;
        p.subtitle_background = prospero_subtitle_background;
        p.chrome_hidden = 1;
        p.skip_label = !isVideoSettingsOpen() && g_skip.shown >= 0 ? g_skip.label.c_str() : nullptr;   /* #219 */
        p.alpha = 255;
        p.fps = perf_render_fps;
        p.debug_overlay = evo_fps_counter_enabled();

        evo_rmlui_update_playback_params(&p);
        evo_rmlui_render_playback_osd(framebuffer, width, height);
    }
}

} // namespace evo
