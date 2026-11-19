#include "evo/services/HiddenStorage.hpp"
#include "evo/screens/FavoritesScreen.hpp"
#include "evo/screens/BrowserScreen.hpp"
#include "evo/Application.hpp"
#include "evo_favorites.h"
#include "evo_error.h"
#include "evo_rmlui_bridge.h"
#include "evo_feedback.h"
#include "evo_toast.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace evo {

FavoritesScreen::FavoritesScreen()
    : StatefulScreen("FavoritesScreen") {
}

void FavoritesScreen::onEnter() {
    StatefulScreen::onEnter();
    if (auto sm = Application::getInstance().getScreenManager()) {
        if (auto bs = dynamic_cast<BrowserScreen*>(sm->getScreen(ScreenId::UsbBrowser))) {
            bs->setSource(3);
        }
        sm->navigateTo(ScreenId::UsbBrowser);
    }
}

void FavoritesScreen::onExit() {
    StatefulScreen::onExit();
}

bool FavoritesScreen::handleInput(uint32_t pressed, uint32_t held, uint32_t released) {
    auto visible = hidden_storage::visibleFavorites();
    (void)held;
    (void)released;

    if (pressed & PadButtons::Up) {
        if (m_selectedIndex > 0) {
            m_selectedIndex--;
            if (m_selectedIndex < m_scrollOffset) {
                m_scrollOffset = m_selectedIndex;
            }
            evo_feedback(EVO_FB_MOVE);
        } else {
            evo_feedback(EVO_FB_BOUNDARY);
        }
        return true;
    }

    if (pressed & PadButtons::Down) {
        if (m_selectedIndex < static_cast<int>(visible.size()) - 1) {
            m_selectedIndex++;
            if (m_selectedIndex >= m_scrollOffset + EVO_RMLUI_LIST_ROWS) {
                m_scrollOffset = m_selectedIndex - EVO_RMLUI_LIST_ROWS + 1;
            }
            evo_feedback(EVO_FB_MOVE);
        } else {
            evo_feedback(EVO_FB_BOUNDARY);
        }
        return true;
    }

    if (pressed & PadButtons::Left) {
        if (auto sm = Application::getInstance().getScreenManager()) {
            sm->setRailFocused(true);
            return true;
        }
    }

    if (pressed & PadButtons::Cross) {
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(visible.size())) {
            const auto& f = favorite_files[visible[m_selectedIndex]];
            evo_feedback(EVO_FB_CONFIRM);
            if (auto pb = Application::getInstance().getPlaybackController()) {
                char path[sizeof f.path];
                if (f.is_folder || !favorites_resolve(&f, path, sizeof path)) return true;
                if (!pb->startPlayback(path, 0.0)) return true;
            }
            if (auto sm = Application::getInstance().getScreenManager()) {
                sm->navigateTo(ScreenId::Player);
            }
        }
        return true;
    }

    if (pressed & (PadButtons::Square | PadButtons::Triangle)) {
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(visible.size())) {
            favorites_remove_at(visible[m_selectedIndex]);
            visible = hidden_storage::visibleFavorites();
            favorites_save();
            toast("FAVORITES", "Removed");
            evo_feedback(EVO_FB_CANCEL);
            if (m_selectedIndex >= static_cast<int>(visible.size()) && m_selectedIndex > 0) {
                m_selectedIndex--;
            }
        }
        return true;
    }

    if (pressed & PadButtons::Circle) {
        evo_feedback(EVO_FB_CANCEL);
        if (auto sm = Application::getInstance().getScreenManager()) {
            sm->navigateTo(ScreenId::MainMenu);
        }
        return true;
    }

    return false;
}

void FavoritesScreen::update(double deltaMs) {
    StatefulScreen::update(deltaMs);
}

void FavoritesScreen::render(uint32_t* framebuffer, int width, int height) {
    const auto visible = hidden_storage::visibleFavorites();
    evo_rmlui_list_params_t params;
    std::memset(&params, 0, sizeof(params));

    bool railFocused = false;
    if (auto sm = Application::getInstance().getScreenManager()) {
        railFocused = sm->isRailFocused();
    }

    params.title = "FAVORITES";
    params.subtitle = "MEDIA YOU SAVED FOR LATER";
    params.total_count = static_cast<int>(visible.size());
    params.cursor_index = m_selectedIndex;
    params.is_empty = (static_cast<int>(visible.size()) == 0);
    params.empty_title = "NO FAVORITES YET";
    params.empty_hint = "Press Triangle in the browser to add favorites";
    params.rail_focused = railFocused ? 1 : 0;

    int rowsToDisplay = std::min(EVO_RMLUI_LIST_ROWS, std::max(0, static_cast<int>(visible.size()) - m_scrollOffset));
    params.row_count = rowsToDisplay;

    char safeTitles[EVO_RMLUI_LIST_ROWS][128];
    char safeDetails[EVO_RMLUI_LIST_ROWS][512];

    for (int i = 0; i < rowsToDisplay; ++i) {
        int idx = m_scrollOffset + i;
        const auto& f = favorite_files[visible[idx]];
        evo_url_sanitize(f.path, safeDetails[i], sizeof(safeDetails[i]));
        evo_url_sanitize(f.title[0] ? f.title : safeDetails[i], safeTitles[i], sizeof(safeTitles[i]));
        params.rows[i].title = safeTitles[i];
        params.rows[i].detail = safeDetails[i];
        params.rows[i].icon_path = "../icons/icon_favorites.png";
        params.rows[i].badge = "FAVORITE";
        params.rows[i].progress = -1;
        params.rows[i].is_focused = (!railFocused && idx == m_selectedIndex);
    }

    evo_rmlui_update_list(&params);
    evo_rmlui_render_list(framebuffer, width, height);
}

} // namespace evo
