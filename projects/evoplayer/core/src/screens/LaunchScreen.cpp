#include "evo/services/HiddenStorage.hpp"
#include "evo/screens/LaunchScreen.hpp"
#include "evo/screens/BrowserScreen.hpp"
#include "evo/Application.hpp"
#include "evo/services/NetworkStorageService.hpp"
#include "evo_rmlui_bridge.h"
#include "evo_recent.h"
#include "evo_favorites.h"
#include "evo_error.h"
#include "evo_feedback.h"
#include "evo_nav.h"

#include "evo_boot_trace.h"
#include "evo_boot_log.h"
#include "evo_prev_run.h"
#include "evo_toast.h"
#include "evo/animation/AnimationManager.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

#include <algorithm>


namespace evo {

namespace {

struct LibTile {
    const char* title;
    const char* detail;
    const char* icon;
    int         source;
};

/* Home library shortcuts. */
constexpr LibTile libraryTiles[] = {
    {"BROWSE",    "USB drives",      "../icons/icon_browse_usb.png",   0},
    {"RECENT",    "Resume watching", "../icons/icon_recent_files.png", 4},
    {"FAVORITES", "Saved for later", "../icons/icon_favorites.png",    3},
};
constexpr int libraryTileCount = sizeof(libraryTiles) / sizeof(libraryTiles[0]);

} // namespace


LaunchScreen::LaunchScreen()
    : StatefulScreen("LaunchScreen")
    , m_launchFsm(LaunchScreenState::HeroFocused, "LaunchFSM")
{
    initLaunchStateMachine();
}

void LaunchScreen::initLaunchStateMachine() {
    m_launchFsm
        .addState(LaunchScreenState::HeroFocused, "HeroFocused")
        .addState(LaunchScreenState::RecentShelf, "RecentShelf")
        .addState(LaunchScreenState::LibraryShelf, "LibraryShelf");

    m_launchFsm
        .addTransition(LaunchScreenState::HeroFocused, LaunchScreenEvent::FocusRecent, LaunchScreenState::RecentShelf)
        .addTransition(LaunchScreenState::HeroFocused, LaunchScreenEvent::FocusLibrary, LaunchScreenState::LibraryShelf)
        .addTransition(LaunchScreenState::RecentShelf, LaunchScreenEvent::FocusHero, LaunchScreenState::HeroFocused)
        .addTransition(LaunchScreenState::RecentShelf, LaunchScreenEvent::FocusLibrary, LaunchScreenState::LibraryShelf)
        .addTransition(LaunchScreenState::LibraryShelf, LaunchScreenEvent::FocusRecent, LaunchScreenState::RecentShelf)
        .addTransition(LaunchScreenState::LibraryShelf, LaunchScreenEvent::FocusHero, LaunchScreenState::HeroFocused);
}

void LaunchScreen::onEnter() {
    StatefulScreen::onEnter();
    evo::animation::AnimationManager::getInstance().setContinuousAnimation(true);
    recent_prune_missing();
    const auto visible = hidden_storage::visibleRecent();
    /* #229: once per launch, after a crash: where the report of that run is. */
    static bool s_crashNoticeShown = false;
    if (!s_crashNoticeShown && evo_prev_run_state() == EVO_PREV_RUN_CRASHED) {
        s_crashNoticeShown = true;
        EVO_LOGW("app", "the last run crashed; its log is in %s", evo_prev_run_path());
        toast("NoStream CLOSED UNEXPECTEDLY", "Settings > System > Report the last run");
    }
    m_selectedRow = (!visible.empty()) ? 0 : 2;
    m_selectedCol = 0;
    m_heroSettlePath.clear();
    m_heroSettleMs = 0.0;
    if (m_selectedRow == 0) m_launchFsm.postEvent(LaunchScreenEvent::FocusHero);
    else if (m_selectedRow == 1) m_launchFsm.postEvent(LaunchScreenEvent::FocusRecent);
    else m_launchFsm.postEvent(LaunchScreenEvent::FocusLibrary);
}

void LaunchScreen::onExit() {
    m_recentMenuPath.clear();
    evo::animation::AnimationManager::getInstance().setContinuousAnimation(false);
    StatefulScreen::onExit();
}

std::string LaunchScreen::heroPathForSelection() const {
    const auto visible = hidden_storage::visibleRecent();
    /*
     * The hero is the most recently played file and nothing else. It used to
     * follow the recent-shelf cursor and to be overwritten by whichever library
     * tile was hovered, so moving around Home kept rewriting the one panel that
     * is supposed to sit still and say "here is what you were watching".
     */
    if (visible.empty()) {
        return std::string();
    }
    return std::string(visible[0].path);
}

void LaunchScreen::update(double deltaMs) {
    StatefulScreen::update(deltaMs);
    m_launchFsm.update(deltaMs);
    const int count = static_cast<int>(hidden_storage::visibleRecent().size());
    if (m_selectedRow != 2 && count == 0) {
        m_selectedRow = 2;
        m_selectedCol = 0;
    } else if (m_selectedRow == 1) {
        m_selectedCol = std::clamp(m_selectedCol, 0, count - 1);
    }

    /* Restart the debounce whenever the cursor lands on a different file. */
    std::string heroPath = heroPathForSelection();
    if (heroPath != m_heroSettlePath) {
        m_heroSettlePath = heroPath;
        m_heroSettleMs = 0.0;
    } else if (m_heroSettleMs < HeroSettleMs) {
        m_heroSettleMs += deltaMs;
    }
}

void LaunchScreen::navigate(int dx, int dy) {
    const auto visible = hidden_storage::visibleRecent();
    evo::animation::AnimationManager::getInstance().triggerTransition(350.0);
    if (dy < 0) {
        if (m_selectedRow == 2) {
            m_selectedRow = (!visible.empty()) ? 1 : 0;
            evo_feedback(EVO_FB_MOVE);
        } else if (m_selectedRow == 1) {
            m_selectedRow = 0;
            evo_feedback(EVO_FB_MOVE);
        } else {
            evo_feedback(EVO_FB_BOUNDARY);
        }
    } else if (dy > 0) {
        if (m_selectedRow == 0) {
            m_selectedRow = (!visible.empty()) ? 1 : 2;
            evo_feedback(EVO_FB_MOVE);
        } else if (m_selectedRow == 1) {
            m_selectedRow = 2;
            evo_feedback(EVO_FB_MOVE);
        } else {
            evo_feedback(EVO_FB_BOUNDARY);
        }
    }

    int maxCol = 0;
    if (m_selectedRow == 0) {
        maxCol = 0;
    } else if (m_selectedRow == 1) {
        maxCol = (!visible.empty()) ? std::min(static_cast<int>(visible.size()) - 1, EVO_RMLUI_TILES - 1) : 0;
    } else if (m_selectedRow == 2) {
        maxCol = libraryTileCount - 1;
    }

    if (dx != 0) {
        int nextCol = m_selectedCol + dx;
        if (nextCol < 0) {
            m_selectedCol = 0;
            evo_feedback(EVO_FB_BOUNDARY);
        } else if (nextCol > maxCol) {
            m_selectedCol = maxCol;
            evo_feedback(EVO_FB_BOUNDARY);
        } else {
            m_selectedCol = nextCol;
            evo_feedback(EVO_FB_MOVE);
        }
    } else {
        if (m_selectedCol > maxCol) m_selectedCol = maxCol;
        if (m_selectedCol < 0) m_selectedCol = 0;
    }
}

void LaunchScreen::activateSelection() {
    const auto visible = hidden_storage::visibleRecent();
    auto playback = Application::getInstance().getPlaybackController();
    auto screenMgr = Application::getInstance().getScreenManager();
    if (!playback || !screenMgr) return;

    if (m_selectedRow == 0) {
        // Hero row
        if (!visible.empty()) {
            if (playback->startPlayback(visible[0].path, visible[0].last_pos))
                screenMgr->navigateTo(ScreenId::Player);   // on failure the toast is up and we stay
        } else {
            screenMgr->navigateTo(ScreenId::UsbBrowser);
        }
    } else if (m_selectedRow == 1) {
        // Recent shelf
        if (m_selectedCol >= 0 && m_selectedCol < static_cast<int>(visible.size())) {
            if (playback->startPlayback(visible[m_selectedCol].path, visible[m_selectedCol].last_pos))
                screenMgr->navigateTo(ScreenId::Player);
        }
    } else if (m_selectedRow == 2) {
        if (m_selectedCol < 0 || m_selectedCol >= libraryTileCount)
            return;
        if (auto bs = dynamic_cast<BrowserScreen*>(screenMgr->getScreen(ScreenId::UsbBrowser))) {
            bs->setSource(libraryTiles[m_selectedCol].source);
        }
        screenMgr->navigateTo(ScreenId::UsbBrowser);
    }
}

bool LaunchScreen::handleInput(uint32_t pressed, uint32_t held, uint32_t released) {
    auto visible = hidden_storage::visibleRecent();
    (void)held;
    (void)released;

    if (!m_recentMenuPath.empty()) {
        if (pressed & (PadButtons::Circle | PadButtons::Options)) {
            m_recentMenuPath.clear();
            evo_feedback(EVO_FB_CANCEL);
        } else if (pressed & (PadButtons::Up | PadButtons::Down)) {
            m_recentMenuFocused = 1 - m_recentMenuFocused;
            evo_feedback(EVO_FB_MOVE);
        } else if (pressed & PadButtons::Cross) {
            if (m_recentMenuFocused == 0) {
                const int removed = recent_forget(m_recentMenuPath.c_str());
                visible = hidden_storage::visibleRecent();
                if (visible.empty()) m_selectedRow = 2;
                m_selectedCol = std::clamp(m_selectedCol, 0, std::max(0, static_cast<int>(visible.size()) - 1));
                evo_feedback(removed ? EVO_FB_CONFIRM : EVO_FB_BOUNDARY);
            } else {
                evo_feedback(EVO_FB_CANCEL);
            }
            m_recentMenuPath.clear();
        }
        return true;
    }

    if (pressed & PadButtons::Options) {
        if (m_selectedRow != 2 && m_selectedCol >= 0 && m_selectedCol < static_cast<int>(visible.size())) {
            m_recentMenuPath = visible[m_selectedCol].path;
            m_recentMenuFocused = 0;
            evo_feedback(EVO_FB_OPEN);
            return true;
        }
        return false;
    }

    if (pressed & PadButtons::Up) {
        navigate(0, -1);
        return true;
    }
    if (pressed & PadButtons::Down) {
        navigate(0, 1);
        return true;
    }
    if (pressed & PadButtons::Left) {
        if (m_selectedCol <= 0) {
            if (auto sm = Application::getInstance().getScreenManager()) {
                sm->setRailFocused(true);
                return true;
            }
        }
        navigate(-1, 0);
        return true;
    }
    if (pressed & PadButtons::Right) {
        navigate(1, 0);
        return true;
    }
    if (pressed & PadButtons::Cross) {
        evo_feedback(EVO_FB_CONFIRM);
        activateSelection();
        return true;
    }
    if (pressed & PadButtons::Circle) {
        // Switch to USB browser
        evo_feedback(EVO_FB_CANCEL);
        if (auto sm = Application::getInstance().getScreenManager()) {
            sm->navigateTo(ScreenId::UsbBrowser);
        }
        return true;
    }

    return false;
}

void LaunchScreen::render(uint32_t* framebuffer, int width, int height) {
    const auto visible = hidden_storage::visibleRecent();
    evo_rmlui_launch_params_t params;
    std::memset(&params, 0, sizeof(params));

    params.app_name = "NoStream";
    params.version = (EVO_PLAYER_VERSION[0] == 'v' || EVO_PLAYER_VERSION[0] == 'V')
                     ? EVO_PLAYER_VERSION
                     : ("v" EVO_PLAYER_VERSION);

    // Clock
    char clockBuf[16] = {0};
    time_t now = time(nullptr);
    struct tm tmv;
    if (localtime_r(&now, &tmv)) {
        std::snprintf(clockBuf, sizeof(clockBuf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
        params.clock = clockBuf;
    }

    auto coverService = Application::getInstance().getCoverArtService();

    bool railFocused = false;
    if (auto sm = Application::getInstance().getScreenManager()) {
        railFocused = sm->isRailFocused();
    }
    params.hero_focused = (!railFocused && m_selectedRow == 0);

    char safeHeroTitle[128];
    char safeRecentTitles[EVO_RMLUI_TILES][128];

    /* A file's name tidied again on the way out, so entries saved before the
     * title cleaner learned release tags read well too. */
    auto metaService = Application::getInstance().getMediaMetadataService();
    auto displayTitle = [&](const RecentFileEntry& r) -> std::string {
        if (metaService) {
            std::string t, cat;
            metaService->cleanMediaTitle(r.path, t, cat);
            if (!t.empty()) return t;
        }
        return r.title;
    };

    // Hero - always the most recent file; it does not track the cursor.
    if (!visible.empty()) {
        const auto& r = visible[0];
        const std::string heroName = displayTitle(r);
        const char* filename = std::strrchr(r.path, '/');
        evo_url_sanitize(!heroName.empty() ? heroName.c_str() : (filename ? filename + 1 : r.path),
                         safeHeroTitle, sizeof(safeHeroTitle));
        params.hero_eyebrow = (r.last_pos > 1.0) ? "CONTINUE WATCHING" : "START WATCHING";
        /* Say so before Play is pressed, not after a 3 s connect fails. */
        if (NetworkStorageService::serverStatusForUrl(r.path) == 0)
            params.hero_eyebrow = "SERVER OFFLINE";
        params.hero_title = safeHeroTitle;
        params.hero_action = (r.last_pos > 1.0) ? "RESUME" : "PLAY";
        if (r.duration > 1.0) {
            params.hero_progress = static_cast<int>((r.last_pos / r.duration) * 1000.0);
        }
        if (coverService) {
            /*
             * Only pay for the 960x540 extraction once the cursor has settled
             * (see HeroSettleMs). While it is moving, the shelf's own cached
             * 320x180 poster is the backdrop - it is the same aspect and it is
             * already decoded, so a D-pad press costs nothing.
             */
            if (m_heroSettleMs >= HeroSettleMs) {
                coverService->ensureHeroArt(r.path);
            }

            if (coverService->isHeroArtValid() &&
                coverService->getHeroArtPath() == r.path) {
                params.hero_art = coverService->getHeroArtPixels();
                params.hero_art_w = ICoverArtService::HeroWidth;
                params.hero_art_h = ICoverArtService::HeroHeight;
            } else if (const uint32_t* poster = coverService->peekCoverArt(r.path)) {
                params.hero_art = poster;
                params.hero_art_w = ICoverArtService::PosterWidth;
                params.hero_art_h = ICoverArtService::PosterHeight;
            }
        }
    } else {
        params.hero_eyebrow = "WELCOME";
        params.hero_title = "Welcome to NoStream";
        params.hero_detail = "Browse storage to select and play media files";
        params.hero_action = "BROWSE USB";
        params.hero_progress = -1;
    }

    // Recent shelf
    params.recent_total = static_cast<int>(visible.size());
    params.recent_cursor = (!railFocused && m_selectedRow == 1) ? m_selectedCol : -1;
    params.recent_visible = (static_cast<int>(visible.size()) > EVO_RMLUI_TILES) ? EVO_RMLUI_TILES : static_cast<int>(visible.size());

    int coverBudget = 1;
    for (int i = 0; i < params.recent_visible; ++i) {
        const auto& r = visible[i];
        char safeRecentPath[512];
        evo_url_sanitize(r.path, safeRecentPath, sizeof(safeRecentPath));
        const std::string name = displayTitle(r);
        evo_url_sanitize(!name.empty() ? name.c_str() : safeRecentPath, safeRecentTitles[i], sizeof(safeRecentTitles[i]));
        params.recent[i].title = safeRecentTitles[i];
        params.recent[i].detail = "Recent File";
        params.recent[i].icon_path = "../icons/icon_recent_files.png";
        params.recent[i].progress = r.duration > 1.0
            ? static_cast<int>((r.last_pos / r.duration) * 1000.0) : -1;

        const uint32_t* art = nullptr;
        if (coverService) {
            art = coverService->peekCoverArt(r.path);
            bool tried = coverService->hasTriedCoverArt(r.path);
            if (!art && !tried && coverBudget > 0) {
                art = coverService->getCoverArt(r.path, false);
                coverBudget--;
            }
        }
        if (art) {
            params.recent[i].art = art;
        }
        params.recent[i].art_w = ICoverArtService::PosterWidth;
        params.recent[i].art_h = ICoverArtService::PosterHeight;
        params.recent[i].is_focused = (!railFocused && m_selectedRow == 1 && m_selectedCol == i);
    }

    // Library shelf
    params.library_visible = libraryTileCount;

    for (int i = 0; i < params.library_visible; ++i) {
        params.library[i].title = libraryTiles[i].title;
        params.library[i].detail = libraryTiles[i].detail;
        params.library[i].icon_path = libraryTiles[i].icon;
        params.library[i].progress = -1;
        params.library[i].art = nullptr;
        params.library[i].is_focused = (!railFocused && m_selectedRow == 2 && m_selectedCol == i);
    }

    evo_rmlui_update_launch(&params);
    evo_rmlui_render_launch(framebuffer, width, height);

    if (!m_recentMenuPath.empty()) {
        evo_rmlui_dialog_params_t menu{};
        menu.title = "Continue Watching";
        menu.progress_pct = -1.0;
        menu.action_count = 2;
        menu.focused_action = m_recentMenuFocused;
        menu.actions[0] = {"../icons/icon_recent_files.png", "Remove from Continue Watching", 0};
        menu.actions[1] = {"../icons/btn_circle.png", "Close", 0};
        menu.side_panel = 1;
        evo_rmlui_update_dialog(&menu);
        evo_rmlui_render_dialog(framebuffer, width, height);
    }
}

} // namespace evo
