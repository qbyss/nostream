/*
 * evo_hui_browser.cpp - the storage browser on ps5-homebrew-ui. See the header.
 */
#include "evo_hui_browser.hpp"

#include "ui/glyphs.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace evo::kit
{

using ::hui::gfx::Align;
using ::hui::gfx::Color;
using ::hui::gfx::DrawList;
using ::hui::gfx::Rect;
namespace tween = ::hui::tween;
namespace ui = ::hui::ui;

namespace
{

const Color kWhite = Color::rgb(0xffffff);
const Color kInk = Color::rgb(0x0b0d16);
const Color kClear = Color::rgb(0x000000, 0.0f);
const Color kDanger = Color::rgb(0xff5a5f);
const Color kStar = Color::rgb(0xffd166);

constexpr float kLeft = NavRail::kWidth + 64.0f;
constexpr float kRight = 1824.0f;
constexpr float kSourceW = 260.0f;
constexpr float kSourceTop = 250.0f;
constexpr float kSourceH = 58.0f;
constexpr float kGridX = kLeft + kSourceW + 40.0f;
constexpr float kGridY = 262.0f;
constexpr float kCardW = 300.0f;
constexpr float kCardH = 169.0f;
constexpr float kGapX = 28.0f;
constexpr float kRowPitch = kCardH + 76.0f; /* card + its caption */
constexpr float kChipsY = 162.0f; /* on the path line, right-aligned under the count */
constexpr float kMenuW = 660.0f;
constexpr float kActionH = 58.0f;
constexpr float kBackdropFadeSeconds = 0.24f;

std::string fit(const ui::FontRef &font, const std::string &text, float size, float width)
{
    return font.font->fit(text, size, width);
}

} // namespace

void BrowserScreen::set(const evo_rmlui_browser_params_t &p)
{
    auto str = [](const char *s) { return std::string(s ? s : ""); };
    raw_ = p;
    path_ = str(p.path);
    title_ = str(p.title);
    sources_.clear();
    for (int i = 0; i < p.source_count; ++i)
        sources_.emplace_back(str(p.source_labels[i]), str(p.source_icons[i]));
    chips_.clear();
    for (int i = 0; i < p.filter_count && i < 4; ++i)
        chips_.push_back(str(p.filter_labels[i]));
    cards_.clear();
    for (int i = 0; i < p.row_count && i < EVO_RMLUI_BROWSER_ROWS; ++i)
    {
        const evo_rmlui_browser_row_t &r = p.rows[i];
        Card c;
        c.name = str(r.name);
        c.detail = str(r.detail);
        c.icon = str(r.icon_path);
        c.badge = str(r.badge);
        c.duration = str(r.duration);
        c.status = str(r.status_text);
        c.status_kind = r.status_kind;
        c.progress = r.progress;
        c.favorite = r.is_favorite != 0;
        c.focused = r.is_focused != 0;
        c.art = r.art;
        c.art_w = r.art_w;
        c.art_h = r.art_h;
        cards_.push_back(c);
    }
    empty_title_ = str(p.empty_title);
    empty_hint_ = str(p.empty_hint);
    loading_text_ = str(p.loading_text);
    act_target_ = str(p.action_menu_target);
    act_sub_ = str(p.action_target_sub);
    act_clip_ = str(p.action_clipboard_info);
    tr_title_ = str(p.transfer_op_title);
    tr_item_ = str(p.transfer_item_name);
    tr_speed_ = str(p.transfer_speed_str);
    tr_bytes_ = str(p.transfer_bytes_str);
    tr_eta_ = str(p.transfer_eta_str);
    tr_pct_ = str(p.transfer_percent_str);
    tr_progress_ = p.transfer_progress_pct;
    /* Strings in raw_ are dangling after this call: never read them. */
    raw_.path = raw_.title = nullptr;
}

void BrowserScreen::enter()
{
    age_ = 0.0f;
    backdrop_initialized_ = false;
    backdrop_from_ = backdrop_to_ = 0;
    backdrop_mix_ = 1.0f;
}

Rect BrowserScreen::card_rect(int i) const
{
    return {kGridX + static_cast<float>(i % EVO_RMLUI_BROWSER_COLUMNS) * (kCardW + kGapX),
            kGridY + static_cast<float>(i / EVO_RMLUI_BROWSER_COLUMNS) * kRowPitch, kCardW, kCardH};
}

Rect BrowserScreen::source_rect(int i) const
{
    const float dividerGap = raw_.source_divider_before > 0 && i >= raw_.source_divider_before ? 12.0f : 0.0f;
    return {kLeft, kSourceTop + static_cast<float>(i) * (kSourceH + 6.0f) + dividerGap, kSourceW, kSourceH};
}

float BrowserScreen::chips_x(int i) const
{
    float total = 0.0f;
    for (std::size_t k = 0; k < chip_w_.size(); ++k)
        total += chip_w_[k] + (k ? 10.0f : 0.0f);
    float x = kRight - total;
    for (int k = 0; k < i && k < static_cast<int>(chip_w_.size()); ++k)
        x += chip_w_[static_cast<std::size_t>(k)] + 10.0f;
    return x;
}

Rect BrowserScreen::chip_rect(int i, const Context *) const
{
    const float w = i < static_cast<int>(chip_w_.size()) ? chip_w_[static_cast<std::size_t>(i)] : 100.0f;
    return {chips_x(i), kChipsY, w, 40.0f};
}

Rect BrowserScreen::action_rect(int i) const
{
    const float h = 170.0f + raw_.action_menu_count * (kActionH + 4.0f) + 24.0f;
    const float top = 540.0f - h * 0.5f;
    return {960.0f - kMenuW * 0.5f + 24.0f, top + 170.0f + static_cast<float>(i) * (kActionH + 4.0f),
            kMenuW - 48.0f, kActionH};
}

void BrowserScreen::update(float dt)
{
    age_ += dt;
    backdrop_mix_ = std::min(1.0f, backdrop_mix_ + dt / kBackdropFadeSeconds);

    ring_ = {};
    if (!raw_.sidebar_focused && !raw_.filter_focused && !raw_.rail_focused)
    {
        for (std::size_t i = 0; i < cards_.size(); ++i)
            if (cards_[i].focused)
            {
                const Rect r = card_rect(static_cast<int>(i));
                ring_ = {r.x - 8, r.y - 8, r.w + 16, r.h + 16};
                break;
            }
    }

    menu_.target = raw_.action_menu_open ? 1.0f : 0.0f;
    menu_.update(dt, 18.0f);
    if (raw_.action_menu_open && raw_.action_menu_count > 0)
    {
        action_ring_ = action_rect(raw_.action_menu_focused);
    }
    transfer_.target = raw_.transfer_modal_open ? 1.0f : 0.0f;
    transfer_.update(dt, 16.0f);
}

void BrowserScreen::draw(DrawList &list, const Context &ctx)
{
    const ui::Fonts &fonts = ctx.fonts;
    const Palette &p = ctx.palette;

    /* Washed behind everything: the focused file's own picture. */
    std::uint32_t wash = 0;
    for (const Card &c : cards_)
        if (c.focused && c.art)
        {
            wash = ctx.textures.pixels(c.art, c.art_w, c.art_h);
            break;
        }
    if (!backdrop_initialized_)
    {
        backdrop_to_ = wash;
        backdrop_initialized_ = true;
    }
    else if (backdrop_mix_ >= 1.0f && wash != backdrop_to_)
    {
        /* Finish the current blend, then use the latest selection. The
         * texture cache keeps both handles alive for these short fades. */
        backdrop_from_ = backdrop_to_;
        backdrop_to_ = wash;
        backdrop_mix_ = 0.0f;
    }
    draw_backdrop(list, ctx, backdrop_to_, 0.6f, backdrop_from_, tween::smoothstep(backdrop_mix_));

    /* Chip widths are measured here (fonts arrive with the draw). */
    chip_w_.clear();
    for (const std::string &c : chips_)
        chip_w_.push_back(fonts.semibold.measure(c, 18) + 40.0f);

    /* ---- header ---- */
    {
        const float in = tween::stagger(age_, 0, 0.05f, 0.45f);
        list.push_opacity(in);
        draw_page_title(list, ctx, title_.empty() ? "Files" : title_, path_, kLeft, 0.0f, 900.0f);
        char counter[48];
        if (raw_.total_count > 0 && raw_.cursor_index >= 0)
            std::snprintf(counter, sizeof(counter), "%d OF %d", raw_.cursor_index + 1, raw_.total_count);
        else
            std::snprintf(counter, sizeof(counter), "%d %s", raw_.total_count,
                          raw_.total_count == 1 ? "ITEM" : "ITEMS");
        draw_page_counter(list, ctx, counter);
        list.pop_opacity();
    }

    /* ---- sources ---- */
    {
        if (raw_.source_divider_before > 0) {
            const Rect next = source_rect(raw_.source_divider_before);
            list.rounded_rect({next.x + 12.0f, next.y - 9.0f, next.w - 24.0f, 1.0f},
                              0.0f, kWhite.with_alpha(0.20f));
        }
        if (!raw_.sidebar_focused && raw_.active_source >= 0)
            list.rounded_rect(source_rect(raw_.active_source), page::kRadiusTile, kWhite.with_alpha(0.10f));
        for (int i = 0; i < static_cast<int>(sources_.size()); ++i)
        {
            const float in = tween::stagger(age_, i + 1, 0.04f, 0.4f);
            list.push_opacity(in);
            const Rect r = source_rect(i);
            const bool focused = raw_.sidebar_focused && i == raw_.sidebar_index;
            const bool active = i == raw_.active_source;
            if (focused)
            {
                list.glow(r, page::kRadiusTile, 16, p.accent.with_alpha(0.4f));
                list.rounded_rect(r, page::kRadiusTile, kWhite);
            }
            if (active && !focused)
                list.rounded_rect({r.x + 4, r.y + 16, 4, r.h - 32}, 2, p.accent);
            const Color ink = focused ? kInk : (active ? kWhite : kWhite.with_alpha(0.6f));
            const std::uint32_t icon = ctx.textures.icon(sources_[i].second.c_str());
            if (icon)
                list.image(icon, {r.x + 20, r.cy() - 13, 26, 26}, ::hui::gfx::kFullUv, focused ? kInk : (active ? p.accent : ink));
            const auto& font = focused || active ? fonts.semibold : fonts.regular;
            ui::text(list, font, fit(font, sources_[i].first, 21, r.w - 72), r.x + 62, r.cy() + 8, 21, ink);
            list.pop_opacity();
        }
    }

    /* ---- filter chips ---- */
    if (!chips_.empty())
    {
        list.push_opacity(tween::stagger(age_, 2, 0.05f, 0.4f));
        for (std::size_t i = 0; i < chips_.size(); ++i)
        {
            const Rect r = chip_rect(static_cast<int>(i), &ctx);
            const bool selected = static_cast<int>(i) == raw_.filter_selected;
            const bool focused = selected && raw_.filter_focused;
            if (focused)
                list.rounded_rect(r, r.h * 0.5f, kWhite);
            else if (selected)
                list.rounded_rect(r, r.h * 0.5f, p.accent.with_alpha(0.9f));
            else
                list.bordered_rect(r, r.h * 0.5f, kWhite.with_alpha(0.06f), 1.0f, kWhite.with_alpha(0.16f));
            ui::text(list, fonts.semibold, chips_[i], r.cx(), r.y + 27, 18,
                     focused ? kInk : (selected ? kInk : kWhite.with_alpha(0.85f)), Align::center);
        }
        list.pop_opacity();
    }

    /* ---- grid ---- */
    if (raw_.loading)
    {
        const float cx = kGridX + (kRight - kGridX) * 0.5f;
        const float cy = 460.0f;
        list.push_opacity(tween::stagger(age_, 1, 0.05f, 0.35f));
        for (int i = 0; i < 3; ++i)
        {
            const float pulse = std::max(0.0f, std::sin(age_ * 6.0f - static_cast<float>(i) * 0.9f));
            const float x = cx + (static_cast<float>(i) - 1.0f) * 40.0f;
            list.rounded_rect({x - 9.0f, cy - 30.0f, 18.0f, 18.0f}, 9,
                              p.accent.with_alpha(0.25f + 0.75f * pulse));
        }
        ui::text(list, fonts.display, loading_text_.empty() ? "Connecting..." : loading_text_,
                 cx, cy + 40.0f, 32, kWhite, Align::center);
        ui::text(list, fonts.regular, "Press Circle to cancel",
                 cx, cy + 80.0f, 20, p.text_muted, Align::center);
        list.pop_opacity();
    }
    else if (raw_.is_empty)
    {
        const float cx = kGridX + (kRight - kGridX) * 0.5f;
        list.push_opacity(tween::stagger(age_, 2, 0.08f, 0.5f));
        const std::uint32_t icon = ctx.textures.icon(raw_.failed ? "../icons/icon_about_support.png" : "../icons/icon_folder_tree.png");
        if (icon)
            list.image(icon, {cx - 44, 360, 88, 88}, ::hui::gfx::kFullUv, raw_.failed ? kDanger : kWhite.with_alpha(0.35f));
        ui::text(list, fonts.display, empty_title_.empty() ? "Folder is empty" : empty_title_, cx, 510, 36,
                 kWhite, Align::center);
        ui::text(list, fonts.regular, empty_hint_, cx, 554, 22, p.text_muted, Align::center);

        if (raw_.failed)
        {
            ui::Hint actionHints[2];
            actionHints[0] = {ui::Button::cross, "Retry"};
            actionHints[1] = {ui::Button::circle, "Back"};
            ui::HintLayout actionLayout;
            actionLayout.size = 28;
            actionLayout.text_size = 20;
            actionLayout.cy = 610.0f;
            ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), actionHints, 2, cx + 110.0f, true, actionLayout);
        }
        list.pop_opacity();
    }
    else
    {
        if (ring_.w > 0.0f)
        {
            list.glow(ring_, page::kRadiusTile + 2, 22, p.accent.with_alpha(0.35f + 0.1f * ui::breathe(ctx.time)));
        }
        for (std::size_t i = 0; i < cards_.size(); ++i)
        {
            const Card &c = cards_[i];
            Rect r = card_rect(static_cast<int>(i));
            if (c.focused)
                r = {r.x - 8, r.y - 8, r.w + 16, r.h + 16};
            list.shadow({r.x, r.y + 5, r.w, r.h}, page::kRadiusTile, 11, Color::rgb(0x000000, 0.25f));
            const std::uint32_t tex = c.focused ? wash : ctx.textures.pixels(c.art, c.art_w, c.art_h);
            if (tex)
            {
                list.image(tex, r, ::hui::gfx::kFullUv, kWhite.with_alpha(c.focused ? 1.0f : 0.85f), page::kRadiusTile);
            }
            else
            {
                list.gradient_rect(r, page::kRadiusTile, p.surface_sel, p.surface);
                const std::uint32_t icon = ctx.textures.icon(c.icon);
                if (icon)
                    list.image(icon, {r.cx() - 30, r.cy() - 30, 60, 60}, ::hui::gfx::kFullUv,
                               kWhite.with_alpha(0.55f));
            }
            if (!c.badge.empty())
            {
                const float w = fonts.semibold.measure(c.badge, 14, 1.5f) + 20;
                const Rect pill{r.x + 10, r.y + 10, w, 26};
                list.rounded_rect(pill, 13, Color::rgb(0x000000, 0.6f));
                ui::text(list, fonts.semibold, c.badge, pill.cx(), pill.y + 18, 14, kWhite, Align::center, 1.5f);
            }
            if (c.status_kind > 0 && !c.status.empty())
            {
                /* Server status chip (#226): dot + text, bottom-left of the card. */
                const Color dot = c.status_kind == 2   ? Color::rgb(0x3ddc84)
                                  : c.status_kind == 3 ? kDanger
                                                       : Color::rgb(0xf2b84b);
                const float w = fonts.semibold.measure(c.status, 15, 0.5f) + 44;
                const Rect chip{r.x + 10, r.y + r.h - 38, w, 28};
                list.rounded_rect(chip, 14, Color::rgb(0x000000, 0.7f));
                list.rounded_rect({chip.x + 11, chip.y + 10, 8, 8}, 4,
                                  dot.with_alpha(c.status_kind == 1 ? 0.45f + 0.4f * ui::breathe(ctx.time) : 1.0f));
                ui::text(list, fonts.semibold, c.status, chip.x + 28, chip.y + 19, 15, kWhite, Align::left, 0.5f);
            }
            if (c.favorite)
                list.star(r.x + r.w - 22, r.y + 22, 11, kStar);
            if (!c.duration.empty())
            {
                const float w = fonts.mono.measure(c.duration, 15) + 16;
                const Rect pill{r.x + r.w - w - 10, r.y + r.h - 34, w, 24};
                list.rounded_rect(pill, 8, Color::rgb(0x000000, 0.65f));
                ui::text(list, fonts.mono, c.duration, pill.cx(), pill.y + 17, 15, kWhite, Align::center);
            }
            if (c.progress >= 0)
            {
                const float f = tween::clamp01(static_cast<float>(c.progress) / 1000.0f);
                const Rect bar{r.x + 12, r.y + r.h - 8, r.w - 24, 4};
                list.rounded_rect(bar, 2, kWhite.with_alpha(0.25f));
                list.rounded_rect({bar.x, bar.y, std::max(4.0f, bar.w * f), bar.h}, 2, p.accent);
            }
            ui::text(list, c.focused ? fonts.semibold : fonts.regular, fit(fonts.semibold, c.name, 21, kCardW),
                     r.x + (c.focused ? 8.0f : 0.0f), r.y + r.h + 32, 21, c.focused ? kWhite : kWhite.with_alpha(0.85f));
            ui::text(list, fonts.regular, fit(fonts.regular, c.detail, 16, kCardW), r.x + (c.focused ? 8.0f : 0.0f),
                     r.y + r.h + 56, 16, p.text_faint);
        }
    }
    if (ring_.w > 0.0f && !raw_.action_menu_open)
    {
        const float radius = page::kRadiusTile + 5.0f;
        list.bordered_rect(ring_.inset(-5), radius, kClear, 3.5f, kWhite);
    }

    /* ---- hints ---- */
    {
        ui::Hint hints[4];
        int n = 0;
        if (raw_.loading)
        {
            hints[n++] = {ui::Button::circle, "Cancel"};
        }
        else if (raw_.failed)
        {
            hints[n++] = {ui::Button::cross, "Retry"};
            hints[n++] = {ui::Button::circle, "Back"};
        }
        else
        {
            /* Square is search and OPTIONS the file / server / Recent menu
             * (BrowserScreen::handleInput); the old row showed Square as Options. */
            hints[n++] = {ui::Button::cross, "Open"};
            hints[n++] = {ui::Button::square, "Search"};
            hints[n++] = {ui::Button::options, "Options"};
            hints[n++] = {ui::Button::circle, "Back"};   // at the root it steps to the source list
        }
        list.push_opacity(tween::stagger(age_, 7, 0.05f, 0.4f));
        draw_page_hints(list, fonts, hints, n);
        list.pop_opacity();
    }

    /* ---- file operations menu ---- */
    const float m = menu_.value;
    if (m > 0.01f && raw_.action_menu_count > 0)
    {
        list.rounded_rect({0, 0, 1920, 1080}, 0, Color::rgb(0x000000, 0.45f * m));
        list.push_transform(0.96f + 0.04f * m, 960, 540, 0, 0);
        list.push_opacity(m);
        const int nActions = raw_.action_menu_count;
        const Rect first = action_rect(0);
        const float h = 170.0f + nActions * (kActionH + 4.0f) + 24.0f;
        const Rect card{960.0f - kMenuW * 0.5f, first.y - 170.0f, kMenuW, h};
        list.shadow({card.x, card.y + 10, card.w, card.h}, page::kRadiusCard, 24, Color::rgb(0x000000, 0.33f));
        list.rounded_rect(card, page::kRadiusCard, p.bg_top.with_alpha(0.97f));
        list.bordered_rect(card, page::kRadiusCard, kClear, 1.5f, kWhite.with_alpha(0.14f));
        ui::text(list, fonts.semibold, raw_.action_menu_eyebrow ? raw_.action_menu_eyebrow : "FILE OPERATIONS", card.x + 32, card.y + 54, 15, p.accent, Align::left, 3.0f);
        ui::text(list, fonts.display, fit(fonts.display, act_target_, 30, card.w - 64), card.x + 32, card.y + 94,
                 30, kWhite);
        ui::text(list, fonts.regular, fit(fonts.regular, act_sub_, 18, card.w - 64), card.x + 32, card.y + 124,
                 18, p.text_muted);
        if (!act_clip_.empty())
            ui::text(list, fonts.regular, fit(fonts.regular, act_clip_, 16, card.w - 64), card.x + 32,
                     card.y + 150, 16, p.text_faint);
        list.rounded_rect(action_ring_, page::kRadiusTile, kWhite);
        for (int i = 0; i < nActions; ++i)
        {
            const Rect r = action_rect(i);
            const bool focused = i == raw_.action_menu_focused;
            const auto& item = raw_.action_menu_items[i];
            const bool disabled = item.disabled != 0;
            const bool danger = item.danger != 0;
            const Color ink = focused ? kInk : (danger ? kDanger : kWhite);
            list.push_opacity(disabled ? 0.35f : 1.0f);
            const char* iconName = item.icon_path;
            const char* labelText = item.label;
            const char* descText = item.desc;
            const std::uint32_t icon = ctx.textures.icon(iconName ? iconName : "");
            if (icon)
                list.image(icon, {r.x + 18, r.cy() - 13, 26, 26}, ::hui::gfx::kFullUv, ink);
            const int sw = item.toggle;
            if (sw)
            {
                /* Drawn here, not with the kit toggle: its accent track vanishes on the white focused row. */
                const bool on = sw == 2;
                const Rect tr{r.x + r.w - 18 - 64, r.cy() - 17, 64, 34};
                const Color track = focused ? (on ? kInk : kInk.with_alpha(0.22f))
                                            : (on ? p.accent : kWhite.with_alpha(0.18f));
                const Color knob = focused ? (on ? kWhite : kInk.with_alpha(0.55f)) : kWhite;
                list.rounded_rect(tr, 17, track);
                list.rounded_rect({on ? tr.x + tr.w - 30.0f : tr.x + 4.0f, tr.y + 4.0f, 26, 26}, 13, knob);
            }
            const float textW = r.w - 60.0f - (sw ? 64.0f + 18.0f + 16.0f : 18.0f);
            ui::text(list, fonts.semibold, fit(fonts.semibold, labelText ? labelText : "", 20, textW), r.x + 60,
                     r.y + 27, 20, ink);
            ui::text(list, fonts.regular, fit(fonts.regular, descText ? descText : "", 15, textW), r.x + 60, r.y + 48, 15,
                     focused ? kInk.with_alpha(0.65f) : p.text_faint);
            list.pop_opacity();
        }
        list.pop_opacity();
        list.pop_transform();
    }

    /* ---- transfer progress ---- */
    const float t = transfer_.value;
    if (t > 0.01f)
    {
        list.rounded_rect({0, 0, 1920, 1080}, 0, Color::rgb(0x000000, 0.55f * t));
        list.push_opacity(t);
        const Rect card{960 - 400, 540 - 170, 800, 340};
        list.shadow({card.x, card.y + 10, card.w, card.h}, page::kRadiusCard, 24, Color::rgb(0x000000, 0.33f));
        list.rounded_rect(card, page::kRadiusCard, p.bg_top.with_alpha(0.97f));
        list.bordered_rect(card, page::kRadiusCard, kClear, 1.5f, kWhite.with_alpha(0.14f));
        ui::text(list, fonts.semibold, ui::upper(tr_title_), card.x + 40, card.y + 62, 16, p.accent, Align::left, 3.0f);
        ui::text(list, fonts.display, fit(fonts.display, tr_item_, 30, card.w - 80), card.x + 40, card.y + 108, 30,
                 kWhite);
        const Rect bar{card.x + 40, card.y + 150, card.w - 80, 12};
        list.rounded_rect(bar, 6, kWhite.with_alpha(0.12f));
        const float fillWidth = bar.w * static_cast<float>(std::clamp(tr_progress_, 0.0, 100.0) / 100.0);
        if (fillWidth > 0.0f)
            list.rounded_rect({bar.x, bar.y, fillWidth, bar.h}, std::min(6.0f, fillWidth * 0.5f), p.accent);
        ui::text(list, fonts.display, tr_pct_, card.x + 40, card.y + 222, 40, kWhite);
        ui::text(list, fonts.regular, tr_speed_, card.x + card.w - 40, card.y + 204, 20, p.text_muted, Align::right);
        ui::text(list, fonts.regular, tr_bytes_ + "   " + tr_eta_, card.x + card.w - 40, card.y + 232, 18,
                 p.text_faint, Align::right);
        const ui::Hint cancel{ui::Button::circle, "Cancel"};
        ui::HintLayout layout;
        layout.size = 30;
        layout.text_size = 20;
        layout.cy = card.y + 294;
        ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), &cancel, 1, card.x + card.w - 40, true, layout);
        list.pop_opacity();
    }
}

bool BrowserScreen::focus(FocusInfo *out) const
{
    out->focusable.clear();
    if (raw_.action_menu_open && raw_.action_menu_count > 0)
    {
        const int nActions = raw_.action_menu_count;
        for (int i = 0; i < nActions; ++i) {
            const char* lbl = raw_.action_menu_items[i].label;
            out->focusable.emplace_back("action-" + std::to_string(i), lbl ? lbl : "");
        }
        const int at = std::clamp(raw_.action_menu_focused, 0, nActions - 1);
        const char* atLbl = raw_.action_menu_items[at].label;
        out->id = "action-" + std::to_string(at);
        out->text = atLbl ? atLbl : "";
        out->index = at + 1;
        out->total = nActions;
        out->rect = action_rect(at);
        return true;
    }
    if (raw_.sidebar_focused && !sources_.empty())
    {
        for (int i = 0; i < static_cast<int>(sources_.size()); ++i)
            out->focusable.emplace_back("source-" + std::to_string(i), sources_[i].first);
        const int at = raw_.sidebar_index;
        out->id = "source-" + std::to_string(at);
        out->text = sources_[at].first;
        out->index = at + 1;
        out->total = static_cast<int>(sources_.size());
        out->rect = source_rect(at);
        return true;
    }
    if (raw_.filter_focused && !chips_.empty())
    {
        for (std::size_t i = 0; i < chips_.size(); ++i)
            out->focusable.emplace_back("chip-" + std::to_string(i), chips_[i]);
        const int at = std::clamp(raw_.filter_selected, 0, static_cast<int>(chips_.size()) - 1);
        out->id = "chip-" + std::to_string(at);
        out->text = chips_[static_cast<std::size_t>(at)];
        out->index = at + 1;
        out->total = static_cast<int>(chips_.size());
        out->rect = chip_rect(at, nullptr);
        return true;
    }
    if (raw_.loading)
    {
        out->id = "browser-busy";
        out->text = loading_text_.empty() ? "Connecting..." : loading_text_;
        out->focusable.emplace_back("browser-busy", out->text);
        out->index = 1;
        out->total = 1;
        out->rect = {kGridX, kGridY, kRight - kGridX, kRowPitch * (EVO_RMLUI_BROWSER_ROWS / EVO_RMLUI_BROWSER_COLUMNS)};
        return true;
    }
    if (raw_.failed)
    {
        out->id = "browser-error";
        out->text = empty_title_.empty() ? "COULD NOT CONNECT" : empty_title_;
        out->focusable.emplace_back("browser-retry", "Retry");
        out->index = 1;
        out->total = 1;
        out->rect = {kGridX, kGridY, kRight - kGridX, kRowPitch * (EVO_RMLUI_BROWSER_ROWS / EVO_RMLUI_BROWSER_COLUMNS)};
        return true;
    }
    for (std::size_t i = 0; i < cards_.size(); ++i)
        out->focusable.emplace_back("card-" + std::to_string(i), cards_[i].name + (cards_[i].status.empty() ? "" : "  [" + cards_[i].status + "]"));
    for (std::size_t i = 0; i < cards_.size(); ++i)
    {
        if (!cards_[i].focused)
            continue;
        out->id = "card-" + std::to_string(i);
        out->text = cards_[i].name + (cards_[i].status.empty() ? "" : "  [" + cards_[i].status + "]");
        out->index = raw_.cursor_index >= 0 ? raw_.cursor_index + 1 : static_cast<int>(i) + 1;
        out->total = raw_.total_count;
        out->rect = card_rect(static_cast<int>(i));
        return true;
    }
    out->focusable.clear();
    return false;
}

} // namespace evo::kit
