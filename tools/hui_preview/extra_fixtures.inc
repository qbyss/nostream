#include "evo_rmlui_bridge.h"
#include <vector>
#include <cstdint>

static void fill_reader_fixture(evo_rmlui_reader_params_t& p)
{
    p.title = "README.md";
    p.subtitle = "evoplayer / docs";
    p.badge = "TEXT";
    p.rail_focused = 0;
    
    static const char* const kLines[] = {
        "# NoStream",
        "",
        "Welcome to NoStream, the ultimate media player for your homebrew console.",
        "",
        "## Features",
        "- Hardware decoding for maximum performance",
        "- 4K HDR support where available",
        "- Native user interface built on ps5-homebrew-ui",
        "- Subtitle rendering",
        "- FTP server integration",
        "",
        "## Controls",
        "Use the directional pad to navigate menus. Press Cross to select,",
        "and Circle to go back.",
        "",
        "During playback:",
        "- L2 / R2: Rewind / Fast Forward",
        "- L1 / R1: Previous / Next Chapter",
        "- Square: Audio and Subtitle options",
        "- Triangle: Media Info",
        "",
        "## Installation",
        "Place your media files in the USB drive or internal storage.",
        "The app will automatically discover them.",
        "",
        "## Known Issues",
        "- Some esoteric audio codecs might require software fallback.",
        "- Network streaming performance depends on your Wi-Fi.",
        "",
        "Enjoy your media!"
    };
    
    p.line_count = 30;
    for (int i = 0; i < 30; ++i) {
        p.lines[i] = kLines[i];
    }
    
    p.face = 2;
    p.progress = 0.3;
    p.visible_frac = 0.4;
    p.notice = "";
    p.footnote = "FIRST 10KB OF 45KB";
}

static void fill_image_fixture(evo_rmlui_image_params_t& p, std::vector<uint32_t>& storage)
{
    p.title = "sample_image.jpg";
    p.w = 640;
    p.h = 360;
    p.loaded = 1;
    
    storage.resize(640 * 360);
    for (int y = 0; y < 360; ++y) {
        for (int x = 0; x < 640; ++x) {
            uint8_t r = (x * 255) / 640;
            uint8_t g = (y * 255) / 360;
            uint8_t b = 128;
            uint8_t a = 255;
            storage[y * 640 + x] = (a << 24) | (b << 16) | (g << 8) | r;
        }
    }
    
    p.pixels = storage.data();
}

static void fill_keyboard_fixture(evo_keyboard_params_t& p)
{
    p.visible = 1;
    p.native_only = 0;
    p.title = "SEARCH";
    p.text = "blade";
    p.mode_label = "LOWERCASE";
    
    p.rows[0] = "1234567890";
    p.rows[1] = "qwertyuiop";
    p.rows[2] = "asdfghjkl-";
    p.rows[3] = "zxcvbnm,._";
    
    p.action_labels[0] = "SHIFT";
    p.action_labels[1] = "SPACE";
    p.action_labels[2] = "DELETE";
    p.action_labels[3] = "CANCEL";
    p.action_labels[4] = "DONE";
    p.action_labels[5] = "";
    
    p.focus_row = 1;
    p.focus_col = 3;
    p.show_caret = 1;
    p.len = 5;
    p.max_len = 64;
}

/* The keyboard with the phone link up: a QR (a fixed pattern here) and phone: ip:port under it. */
static void fill_keyboard_phone_fixture(evo_keyboard_params_t& p)
{
    static unsigned char qr[29 * 29];
    for (int y = 0; y < 29; ++y)
        for (int x = 0; x < 29; ++x)
            qr[y * 29 + x] = (unsigned char)(((x * 7 + y * 13 + x * y) % 5) < 2);
    fill_keyboard_fixture(p);
    p.qr_size = 29;
    p.qr_modules = qr;
    p.phone_title = "phone: 192.168.0.12:9780";
}

/* The keyboard opened while every phone-link port is taken: no QR, a notice instead. */
static void fill_keyboard_noport_fixture(evo_keyboard_params_t& p)
{
    fill_keyboard_fixture(p);
    p.phone_title = "PHONE LINK OFF";
    p.phone_notice = "Ports 9780 to 9789 are all taken, so typing from a phone is off. Set a new port to turn it back on.";
    p.phone_kind = 0;
    p.phone_action = "Set port";
}

/* The digits-only pad for a port: three columns, backspace and Done beside the 0. */
static const evo_kb_key_t kPadR0[] = {{EVO_KB_KEY_CHAR, 1, "1", ""}, {EVO_KB_KEY_CHAR, 1, "2", ""}, {EVO_KB_KEY_CHAR, 1, "3", ""}};
static const evo_kb_key_t kPadR1[] = {{EVO_KB_KEY_CHAR, 1, "4", ""}, {EVO_KB_KEY_CHAR, 1, "5", ""}, {EVO_KB_KEY_CHAR, 1, "6", ""}};
static const evo_kb_key_t kPadR2[] = {{EVO_KB_KEY_CHAR, 1, "7", ""}, {EVO_KB_KEY_CHAR, 1, "8", ""}, {EVO_KB_KEY_CHAR, 1, "9", ""}};
static const evo_kb_key_t kPadR3[] = {{EVO_KB_KEY_BACKSPACE, 1, "", ""}, {EVO_KB_KEY_CHAR, 1, "0", ""}, {EVO_KB_KEY_DONE, 1, "", ""}};

/* The same keyboard after the button: typing a port, with the check result beside it. */
static void fill_keyboard_port_fixture(evo_keyboard_params_t& p, int free_port)
{
    fill_keyboard_fixture(p);
    for (int l = 0; l < EVO_KB_LAYERS; ++l)
    {
        p.key_rows[l][0] = kPadR0; p.key_row_len[l][0] = 3;
        p.key_rows[l][1] = kPadR1; p.key_row_len[l][1] = 3;
        p.key_rows[l][2] = kPadR2; p.key_row_len[l][2] = 3;
        p.key_rows[l][3] = kPadR3; p.key_row_len[l][3] = 3;
    }
    p.key_columns = 3;
    p.focus_row = 1;
    p.focus_col = 1;
    p.title = "PHONE & LOG PORT (1024-65535)";
    p.text = free_port ? "9800" : "9781";
    p.mode_label = "NUMBERS";
    p.len = 4;
    p.max_len = 5;
    p.clear_visible = 0;
    p.phone_title = free_port ? "PORT 9800 IS FREE" : "PORT 9781 IS TAKEN";
    p.phone_notice = free_port ? "Save it and the phone link and QR code come back at once."
                               : "Something else is using it. Type another port.";
    p.phone_kind = free_port ? 1 : 0;
    p.phone_action = nullptr;
}

/* Surround Sound Studio: mode 0 stage, 1 spatial orb, 2 calibration. */
static void fill_surround_fixture(evo_rmlui_surround_params_t& p, int mode)
{
    p = evo_rmlui_surround_params_t{};
    static const char* const kNames[8] = {"FRONT LEFT", "FRONT RIGHT", "CENTER", "SUBWOOFER",
                                          "BACK LEFT", "BACK RIGHT", "SIDE LEFT", "SIDE RIGHT"};
    static const char* const kLabels[8] = {"FL", "FR", "C", "LFE", "BL", "BR", "SL", "SR"};
    static const int kDx[8] = {-190, 190, 0, -300, -200, 200, -320, 320};
    static const int kDy[8] = {-260, -260, -290, -180, 220, 220, -10, -10};
    static const int kOrder[8] = {0, 2, 1, 7, 6, 4, 5, 3};
    p.is_51_layout = 0;
    p.speaker_count = 8;
    for (int i = 0; i < 8; ++i)
    {
        p.speakers[i] = {kNames[i], kLabels[i], i == 3 ? 60.0 : 440.0, kDx[i], kDy[i], i, 8 + i, 0};
        p.order[i] = kOrder[i];
    }
    p.order_count = 8;
    p.px_per_m = 100.0f;
    p.anim_time = 1.3f;
    p.active_channel = -1;
    p.nearest_channel = -1;
    p.selected_item = 2;
    p.cal_message = "";
    p.source_dbfs = -18.0f;
    p.distance_m = 1.8f;
    if (mode == 0)
    {
        p.selected_item = 8;
        p.active_channel = 0;
        p.surround_mode = 1;
        p.proximity[0] = 1.0f;
        p.proximity[2] = 0.35f;
        p.nearest_channel = 0;
    }
    else if (mode == 1)
    {
        p.view_mode = EVO_SURROUND_VIEW_ORB;
        p.orb_active = 1;
        p.tone_follow = 1;
        p.field_playing = 1;
        p.orb_x = -120.0f;
        p.orb_y = -150.0f;
        p.orb_z_m = 0.6f;
        p.flight_mode = 1;
        p.nearest_channel = 0;
        p.azimuth_deg = 322.0f;
        p.elevation_deg = 18.0f;
        for (int i = 0; i < 8; ++i)
            p.proximity[i] = i == 0 ? 0.82f : (i == 2 ? 0.55f : 0.12f * i / 8.0f);
        p.trail_count = 5;
        for (int i = 0; i < 5; ++i)
        {
            p.trail_x[i] = p.orb_x + 14.0f * (i + 1);
            p.trail_y[i] = p.orb_y + 9.0f * (i + 1);
        }
    }
    else
    {
        p.view_mode = EVO_SURROUND_VIEW_CALIBRATION;
        p.cal_phase = EVO_SURROUND_CAL_MEASURING;
        p.cal_step = 3;
        p.cal_total = 8;
        p.cal_channel = 2;
        p.cal_mic_level = 0.55f;
        p.cal_noise_db = -62.0f;
        p.cal_message = "Measuring...";
        for (int i = 0; i < 8; ++i)
        {
            p.cal_detected[i] = 1;
            p.cal_trim_db[i] = -1.5f + i * 0.4f;
            p.cal_path_m[i] = 0.1f * i;
            p.cal_delay_ms[i] = 0.3f * i;
        }
    }
}

static void fill_settings_pv_extra_fixture(evo_rmlui_settings_params_t &p, const std::string &kind)
{
    if (kind == "resume")
    {
        build_preview_section(p, 0, "Video & Display", "DECODER, UPSCALING, 120 HZ & HDR", {
            {EVO_RMLUI_ROW_VALUE, "VIDEO DECODER", "WHICH BACKEND DECODES VIDEO", "../icons/icon_cpu.png", "AUTO", 0, 0, "", {"AUTO", "HARDWARE", "SOFTWARE"}, 0, -1, false},
            {EVO_RMLUI_ROW_TOGGLE, "RESUME PLAYBACK", "REMEMBER PLAYBACK POSITION", "../icons/icon_resume.png", "", 1, EVO_RMLUI_PREVIEW_RESUME, "Picks a film up where you stopped instead of from the start.", {}, 0, -1, false},
            {EVO_RMLUI_ROW_VALUE, "DEFAULT ASPECT RATIO", "HOW VIDEO FILLS THE SCREEN", "../icons/icon_aspect.png", "FIT", 0, 0, "", {"FIT", "FILL", "STRETCH"}, 0, -1, false}
        });
    }
    else if (kind == "resume_off")
    {
        build_preview_section(p, 0, "Video & Display", "DECODER, UPSCALING, 120 HZ & HDR", {
            {EVO_RMLUI_ROW_VALUE, "VIDEO DECODER", "WHICH BACKEND DECODES VIDEO", "../icons/icon_cpu.png", "AUTO", 0, 0, "", {"AUTO", "HARDWARE", "SOFTWARE"}, 0, -1, false},
            {EVO_RMLUI_ROW_TOGGLE, "RESUME PLAYBACK", "REMEMBER PLAYBACK POSITION", "../icons/icon_resume.png", "", 0, EVO_RMLUI_PREVIEW_RESUME, "Picks a film up where you stopped instead of from the start.", {}, 0, -1, false},
            {EVO_RMLUI_ROW_VALUE, "DEFAULT ASPECT RATIO", "HOW VIDEO FILLS THE SCREEN", "../icons/icon_aspect.png", "FIT", 0, 0, "", {"FIT", "FILL", "STRETCH"}, 0, -1, false}
        });
    }
    else if (kind == "decoder")
    {
        build_preview_section(p, 0, "Video & Display", "DECODER, UPSCALING, 120 HZ & HDR", {
            {EVO_RMLUI_ROW_VALUE, "VIDEO DECODER", "WHICH BACKEND DECODES VIDEO", "../icons/icon_cpu.png", "HARDWARE", 0, EVO_RMLUI_PREVIEW_DECODER, "Hardware decoding is smoother and cooler. Software is for odd formats.", {"AUTO", "HARDWARE", "SOFTWARE"}, 1, 1, false},
            {EVO_RMLUI_ROW_TOGGLE, "RESUME PLAYBACK", "REMEMBER PLAYBACK POSITION", "../icons/icon_resume.png", "", 1, 0, "", {}, 0, -1, false},
            {EVO_RMLUI_ROW_VALUE, "UPSCALING", "SHARPEN VIDEO SMALLER THAN THE SCREEN", "../icons/icon_sparkles.png", "AI", 0, 0, "", {"OFF", "SHARP", "AI"}, 2, -1, false}
        });
    }
    else if (kind == "decoder_sw")
    {
        build_preview_section(p, 0, "Video & Display", "DECODER, UPSCALING, 120 HZ & HDR", {
            {EVO_RMLUI_ROW_VALUE, "VIDEO DECODER", "WHICH BACKEND DECODES VIDEO", "../icons/icon_cpu.png", "SOFTWARE", 0, EVO_RMLUI_PREVIEW_DECODER, "Hardware decoding is smoother and cooler. Software is for odd formats.", {"AUTO", "HARDWARE", "SOFTWARE"}, 2, 2, false},
            {EVO_RMLUI_ROW_TOGGLE, "RESUME PLAYBACK", "REMEMBER PLAYBACK POSITION", "../icons/icon_resume.png", "", 1, 0, "", {}, 0, -1, false},
            {EVO_RMLUI_ROW_VALUE, "UPSCALING", "SHARPEN VIDEO SMALLER THAN THE SCREEN", "../icons/icon_sparkles.png", "AI", 0, 0, "", {"OFF", "SHARP", "AI"}, 2, -1, false}
        });
    }
    else if (kind == "nav_sounds")
    {
        build_preview_section(p, 1, "Audio", "OUTPUT CHANNELS, SPEAKERS & SOUNDS", {
            {EVO_RMLUI_ROW_VALUE, "OUTPUT CHANNELS", "STEREO DOWNMIXES 5.1 & 7.1 TO 2.0", "../icons/icon_volume.png", "STEREO", 0, 0, "", {"STEREO", "SURROUND 5.1"}, 0, -1, false},
            {EVO_RMLUI_ROW_ACTION, "SURROUND SOUND TEST", "5.1 & 7.1 SPEAKER CHANNEL VERIFICATION", "../icons/icon_speaker.png", "OPEN", 0, 0, "", {}, 0, -1, false},
            {EVO_RMLUI_ROW_TOGGLE, "NAVIGATION SOUNDS", "AUDIO FEEDBACK ON D-PAD & BUTTONS", "../icons/icon_listener.png", "", 1, EVO_RMLUI_PREVIEW_NAV_SOUNDS, "A soft click when you move and press buttons.", {}, 0, -1, false}
        });
    }
    else if (kind == "nav_sounds_off")
    {
        build_preview_section(p, 1, "Audio", "OUTPUT CHANNELS, SPEAKERS & SOUNDS", {
            {EVO_RMLUI_ROW_VALUE, "OUTPUT CHANNELS", "STEREO DOWNMIXES 5.1 & 7.1 TO 2.0", "../icons/icon_volume.png", "STEREO", 0, 0, "", {"STEREO", "SURROUND 5.1"}, 0, -1, false},
            {EVO_RMLUI_ROW_ACTION, "SURROUND SOUND TEST", "5.1 & 7.1 SPEAKER CHANNEL VERIFICATION", "../icons/icon_speaker.png", "OPEN", 0, 0, "", {}, 0, -1, false},
            {EVO_RMLUI_ROW_TOGGLE, "NAVIGATION SOUNDS", "AUDIO FEEDBACK ON D-PAD & BUTTONS", "../icons/icon_listener.png", "", 0, EVO_RMLUI_PREVIEW_NAV_SOUNDS, "A soft click when you move and press buttons.", {}, 0, -1, false}
        });
    }
    else if (kind == "folders_first")
    {
        build_preview_section(p, 3, "Interface & Storage", "THEMES, CONTROLLER & SORTING", {
            {EVO_RMLUI_ROW_VALUE, "THEME", "COLOR PALETTE & ACCENTS", "../icons/icon_palette.png", "MIDNIGHT", 0, 0, "", {"MIDNIGHT", "DARK", "AMBER", "TEAL"}, 0, -1, true},
            {EVO_RMLUI_ROW_TOGGLE, "FOLDERS FIRST", "USB FILE BROWSER SORTING", "../icons/icon_folder.png", "", 1, EVO_RMLUI_PREVIEW_FOLDERS_FIRST, "Lists folders above files in the USB browser.", {}, 0, -1, false},
            {EVO_RMLUI_ROW_VALUE, "KEYBOARD INPUT", "TEXT ENTRY METHOD", "../icons/icon_keyboard.png", "VIRTUAL KEYBOARD", 0, 0, "", {"VIRTUAL KEYBOARD", "NATIVE PS5 IME"}, 0, -1, false}
        });
    }
    else if (kind == "folders_first_off")
    {
        build_preview_section(p, 3, "Interface & Storage", "THEMES, CONTROLLER & SORTING", {
            {EVO_RMLUI_ROW_VALUE, "THEME", "COLOR PALETTE & ACCENTS", "../icons/icon_palette.png", "MIDNIGHT", 0, 0, "", {"MIDNIGHT", "DARK", "AMBER", "TEAL"}, 0, -1, true},
            {EVO_RMLUI_ROW_TOGGLE, "FOLDERS FIRST", "USB FILE BROWSER SORTING", "../icons/icon_folder.png", "", 0, EVO_RMLUI_PREVIEW_FOLDERS_FIRST, "Lists folders above files in the USB browser.", {}, 0, -1, false},
            {EVO_RMLUI_ROW_VALUE, "KEYBOARD INPUT", "TEXT ENTRY METHOD", "../icons/icon_keyboard.png", "VIRTUAL KEYBOARD", 0, 0, "", {"VIRTUAL KEYBOARD", "NATIVE PS5 IME"}, 0, -1, false}
        });
    }
    else if (kind == "keyboard")
    {
        build_preview_section(p, 3, "Interface & Storage", "THEMES, CONTROLLER & SORTING", {
            {EVO_RMLUI_ROW_TOGGLE, "FOLDERS FIRST", "USB FILE BROWSER SORTING", "../icons/icon_folder.png", "", 1, 0, "", {}, 0, -1, false},
            {EVO_RMLUI_ROW_VALUE, "KEYBOARD INPUT", "TEXT ENTRY METHOD", "../icons/icon_keyboard.png", "VIRTUAL KEYBOARD", 0, EVO_RMLUI_PREVIEW_KEYBOARD, "The virtual keyboard is drawn by NoStream. The native one is the PS5 system keyboard.", {"VIRTUAL KEYBOARD", "NATIVE PS5 IME"}, 0, 0, false},
        });
    }
    else if (kind == "keyboard_ps5")
    {
        build_preview_section(p, 3, "Interface & Storage", "THEMES, CONTROLLER & SORTING", {
            {EVO_RMLUI_ROW_TOGGLE, "FOLDERS FIRST", "USB FILE BROWSER SORTING", "../icons/icon_folder.png", "", 1, 0, "", {}, 0, -1, false},
            {EVO_RMLUI_ROW_VALUE, "KEYBOARD INPUT", "TEXT ENTRY METHOD", "../icons/icon_keyboard.png", "NATIVE PS5 IME", 0, EVO_RMLUI_PREVIEW_KEYBOARD, "The virtual keyboard is drawn by NoStream. The native one is the PS5 system keyboard.", {"VIRTUAL KEYBOARD", "NATIVE PS5 IME"}, 1, 1, false},
        });
    }
    else if (kind == "port_free" || kind == "port_busy" || kind == "port_fallback" || kind == "port_none")
    {
        const int state = (kind == "port_free") ? 0 : (kind == "port_busy") ? 1 : (kind == "port_fallback") ? 2 : 3;
        const char *badge = (state == 3) ? "NONE" : (state == 0) ? "9800" : "9780";
        const char *link = (state == 0) ? "http://192.168.0.12:9800/input"
                         : (state == 1) ? "http://192.168.0.12:9780/input"
                         : (state == 2) ? "http://192.168.0.12:9783/input"
                                        : "";
        const char *status = (state == 0) ? "Listening on 9800"
                           : (state == 1) ? "9800 is in use, kept 9780"
                           : (state == 2) ? "9780 is busy, using 9783"
                                          : "Ports 9780 to 9789 are all taken, so the phone keyboard and live log are off. Please set a new port.";
        build_preview_section(p, 3, "Interface & Storage", "THEMES, CONTROLLER & SORTING", {
            {EVO_RMLUI_ROW_VALUE, "KEYBOARD INPUT", "TEXT ENTRY METHOD", "../icons/icon_keyboard.png", "VIRTUAL KEYBOARD", 0, 0, "", {"VIRTUAL KEYBOARD", "NATIVE PS5 IME"}, 0, -1, false},
            {EVO_RMLUI_ROW_VALUE, "PHONE & LOG PORT", "PHONE LINK & LIVE LOG", "../icons/icon_keyboard.png", badge, 0, EVO_RMLUI_PREVIEW_PORT, "NoStream checks the port is free before it saves it. If it is busy at start-up, NoStream tries the next nine and shows the one it got.", {link, status}, state, -1, false},
        });
    }
    else if (kind == "report" || kind == "report_nolink")
    {
        const int state = (kind == "report") ? 0 : 1;
        const char *link = state == 0 ? "http://192.168.0.12:9780/report" : "";
        const char *status = state == 0 ? "The phone must be on the same network as the PS5. NoStream has to stay open."
                                        : "No network: the phone link is off. Save the report to USB instead.";
        build_preview_section(p, 4, "System & Diagnostics", "DIAGNOSTICS & SYSTEM MANAGEMENT", {
            {EVO_RMLUI_ROW_ACTION, "REPORT A PROBLEM", "SEND NOSTREAM'S LOG FROM YOUR PHONE, OR SAVE IT TO USB", "../icons/icon_report.png", "SAVE", 0, EVO_RMLUI_PREVIEW_REPORT, "The report has NoStream's log, version and settings. Passwords and tokens are taken out.", {link, status}, state, -1, false},
            {EVO_RMLUI_ROW_TOGGLE, "DEBUG OVERLAY", "SHOWS LIVE PERFORMANCE NUMBERS ON SCREEN", "../icons/icon_activity.png", "", 0, 0, "", {}, 0, -1, false},
            {EVO_RMLUI_ROW_ACTION, "DEVELOPER TOOLS", "SYSTEM DIAGNOSTICS & PERFORMANCE STATS", "../icons/icon_developer_tools.png", "OPEN", 0, 0, "", {}, 0, -1, false}
        });
    }
    else if (kind == "debug_overlay")
    {
        build_preview_section(p, 4, "System & Diagnostics", "DIAGNOSTICS & SYSTEM MANAGEMENT", {
            {EVO_RMLUI_ROW_ACTION, "DEVELOPER TOOLS", "SYSTEM DIAGNOSTICS & PERFORMANCE STATS", "../icons/icon_developer_tools.png", "OPEN", 0, 0, "", {}, 0, -1, false},
            {EVO_RMLUI_ROW_TOGGLE, "DEBUG OVERLAY", "SHOWS LIVE PERFORMANCE NUMBERS ON SCREEN", "../icons/icon_activity.png", "", 1, EVO_RMLUI_PREVIEW_DEBUG_OVERLAY, "Shows live performance numbers on screen.", {}, 0, -1, false},
            {EVO_RMLUI_ROW_ACTION, "QUIT NOSTREAM", "RELEASE EVERYTHING, THEN CLOSE FROM THE SWITCHER", "../icons/icon_power.png", "QUIT", 0, 0, "", {}, 0, -1, false}
        });
    }
    else if (kind == "debug_overlay_off")
    {
        build_preview_section(p, 4, "System & Diagnostics", "DIAGNOSTICS & SYSTEM MANAGEMENT", {
            {EVO_RMLUI_ROW_ACTION, "DEVELOPER TOOLS", "SYSTEM DIAGNOSTICS & PERFORMANCE STATS", "../icons/icon_developer_tools.png", "OPEN", 0, 0, "", {}, 0, -1, false},
            {EVO_RMLUI_ROW_TOGGLE, "DEBUG OVERLAY", "SHOWS LIVE PERFORMANCE NUMBERS ON SCREEN", "../icons/icon_activity.png", "", 0, EVO_RMLUI_PREVIEW_DEBUG_OVERLAY, "Shows live performance numbers on screen.", {}, 0, -1, false},
            {EVO_RMLUI_ROW_ACTION, "QUIT NOSTREAM", "RELEASE EVERYTHING, THEN CLOSE FROM THE SWITCHER", "../icons/icon_power.png", "QUIT", 0, 0, "", {}, 0, -1, false}
        });
    }
    else if (kind == "motion_off")
    {
        build_preview_section(p, 5, "Experimental", "UNFINISHED WORK - EXPECT ARTEFACTS", {
            {EVO_RMLUI_ROW_TOGGLE, "MATCH VIDEO FRAME RATE", "24P / 50 HZ OUTPUT DURING PLAYBACK - FALLS BACK TO 120 HZ OR 60 HZ", "../icons/icon_gauge.png", "", 0, 0, "", {}, 0, -1, false},
            {EVO_RMLUI_ROW_VALUE, "MOTION SMOOTHING", "24 -> 60 FPS ON THE GPU - SOFT EDGES AND HALOES AROUND FAST MOTION", "../icons/icon_activity.png", "", 0, EVO_RMLUI_PREVIEW_MOTION, "Pans and sports look fluid. Films can look too smooth, and fast motion can show haloes.", {"OFF", "LOW", "HIGH"}, 0, 0, false},
            {EVO_RMLUI_ROW_VALUE, "DEEP BLACKS", "PULLS ELEVATED SHADOWS TO TRUE OLED BLACK", "../icons/icon_sparkles.png", "OFF", 0, 0, "", {"OFF", "LOW", "HIGH"}, 0, -1, false}
        });
    }
    else if (kind == "refresh_60")
    {
        build_preview_section(p, 0, "Video & Display", "DECODER, UPSCALING, 120 HZ & HDR", {
            {EVO_RMLUI_ROW_VALUE, "AI NETWORK", "BIGGER IS SHARPER BUT HEAVIER - MAXIMUM IS FOR PS5 PRO", "../icons/icon_brain.png", "LARGE", 0, 0, "", {"AUTO", "STANDARD", "LARGE", "MAXIMUM (PRO)"}, 2, -1, false},
            {EVO_RMLUI_ROW_VALUE, "120 HZ OUTPUT", "SMOOTHER 24 FPS - THE TV GOES BLACK BRIEFLY ON EACH SWITCH", "../icons/icon_gauge.png", "", 0, EVO_RMLUI_PREVIEW_REFRESH, "24 fps film plays more evenly on a 120 Hz output. The TV goes black briefly on each switch.", {"OFF", "AUTO (ALWAYS 120 HZ)", "PLAYBACK ONLY"}, 0, 0, false},
            {EVO_RMLUI_ROW_VALUE, "HDR OUTPUT", "HDR10 FOR HDR VIDEO - THE TV GOES BLACK BRIEFLY AT START & END", "../icons/icon_sun.png", "AUTO", 0, 0, "", {"OFF (TONE-MAP TO SDR)", "AUTO (MATCH CONTENT)"}, 1, -1, false}
        });
    }
    else if (kind == "match_rate_off")
    {
        build_preview_section(p, 5, "Experimental", "UNFINISHED WORK - EXPECT ARTEFACTS", {
            {EVO_RMLUI_ROW_VALUE, "DEEP BLACKS", "PULLS ELEVATED SHADOWS TO TRUE OLED BLACK", "../icons/icon_sparkles.png", "OFF", 0, 0, "", {"OFF", "LOW", "HIGH"}, 0, -1, false},
            {EVO_RMLUI_ROW_TOGGLE, "MATCH VIDEO FRAME RATE", "24P / 50 HZ OUTPUT DURING PLAYBACK - FALLS BACK TO 120 HZ OR 60 HZ", "../icons/icon_gauge.png", "", 0, EVO_RMLUI_PREVIEW_MATCH_RATE, "Outputs the video's own rate (24p or 50 Hz) so motion has no judder.", {}, 0, -1, false},
            {EVO_RMLUI_ROW_VALUE, "MOTION SMOOTHING", "24 -> 60 FPS ON THE GPU - SOFT EDGES AND HALOES AROUND FAST MOTION", "../icons/icon_activity.png", "OFF", 0, 0, "", {"OFF", "LOW", "HIGH"}, 0, -1, false}
        });
    }
    else if (kind == "channels_stereo")
    {
        build_preview_section(p, 1, "Audio", "OUTPUT CHANNELS, SPEAKERS & SOUNDS", {
            {EVO_RMLUI_ROW_TOGGLE, "AUDIO PASSTHROUGH", "BITSTREAM DOLBY & DTS TO RECEIVER OVER HDMI", "../icons/icon_volume.png", "", 0, 0, "", {}, 0, -1, false},
            {EVO_RMLUI_ROW_VALUE, "OUTPUT CHANNELS", "STEREO DOWNMIXES 5.1 & 7.1 TO 2.0", "../icons/icon_volume.png", "STEREO (2.0)", 0, EVO_RMLUI_PREVIEW_CHANNELS, "Stereo mixes 5.1 and 7.1 down to two speakers or headphones.", {"AUTO", "STEREO (2.0)"}, 1, 1, false},
            {EVO_RMLUI_ROW_ACTION, "SURROUND SOUND TEST", "5.1 & 7.1 SPEAKER CHANNEL VERIFICATION", "../icons/icon_speaker.png", "OPEN", 0, 0, "", {}, 0, -1, false}
        });
    }
    else if (kind == "lightbar_off")
    {
        build_preview_section(p, 3, "Interface & Storage", "THEMES, CONTROLS & BROWSING", {
            {EVO_RMLUI_ROW_VALUE, "THEME", "COLOR PALETTE & ACCENTS", "../icons/icon_palette.png", "MIDNIGHT", 0, 0, "", {"MIDNIGHT", "CARBON", "EMBER", "AURORA"}, 0, -1, true},
            {EVO_RMLUI_ROW_TOGGLE, "CONTROLLER LIGHTBAR", "DUALSENSE LIGHT FOLLOWS THE THEME ACCENT", "../icons/icon_gamepad.png", "", 0, EVO_RMLUI_PREVIEW_LIGHTBAR, "The DualSense light follows the theme accent.", {}, 0, -1, false},
            {EVO_RMLUI_ROW_TOGGLE, "FOLDERS FIRST", "USB FILE BROWSER SORTING", "../icons/icon_folder.png", "", 1, 0, "", {}, 0, -1, false}
        });
    }
    else if (kind == "sub_font_standard")
    {
        build_preview_section(p, 2, "Subtitles", "PREFERENCES & APPEARANCE", {
            {EVO_RMLUI_ROW_TOGGLE, "AUTO SUBTITLES", "AUTOMATICALLY LOAD SUBTITLES ON PLAYBACK", "../icons/icon_subtitles.png", "", 1, 0, "", {}, 0, -1, false},
            {EVO_RMLUI_ROW_VALUE, "DEFAULT FONT STYLE", "ON-SCREEN TEXT TYPEFACE", "../icons/icon_type.png", "STANDARD", 0, EVO_RMLUI_PREVIEW_SUB_FONT, "Picks the typeface for on-screen subtitles.", {"STANDARD", "ROUNDED", "BOLD", "CONDENSED"}, 0, 0, false},
            {EVO_RMLUI_ROW_VALUE, "SECONDARY SUBTITLE POSITION", "WHERE THE SECOND LINE OF DIALOGUE GOES", "../icons/icon_subtitles.png", "STACKED ABOVE", 0, 0, "", {"STACKED ABOVE", "TOP OF SCREEN"}, 0, -1, false}
        });
    }
    else if (kind == "sub_font_bold")
    {
        build_preview_section(p, 2, "Subtitles", "PREFERENCES & APPEARANCE", {
            {EVO_RMLUI_ROW_TOGGLE, "AUTO SUBTITLES", "AUTOMATICALLY LOAD SUBTITLES ON PLAYBACK", "../icons/icon_subtitles.png", "", 1, 0, "", {}, 0, -1, false},
            {EVO_RMLUI_ROW_VALUE, "DEFAULT FONT STYLE", "ON-SCREEN TEXT TYPEFACE", "../icons/icon_type.png", "BOLD", 0, EVO_RMLUI_PREVIEW_SUB_FONT, "Picks the typeface for on-screen subtitles.", {"STANDARD", "ROUNDED", "BOLD", "CONDENSED"}, 2, 2, false},
            {EVO_RMLUI_ROW_VALUE, "SECONDARY SUBTITLE POSITION", "WHERE THE SECOND LINE OF DIALOGUE GOES", "../icons/icon_subtitles.png", "STACKED ABOVE", 0, 0, "", {"STACKED ABOVE", "TOP OF SCREEN"}, 0, -1, false}
        });
    }
    else if (kind == "sub_font_condensed")
    {
        build_preview_section(p, 2, "Subtitles", "PREFERENCES & APPEARANCE", {
            {EVO_RMLUI_ROW_TOGGLE, "AUTO SUBTITLES", "AUTOMATICALLY LOAD SUBTITLES ON PLAYBACK", "../icons/icon_subtitles.png", "", 1, 0, "", {}, 0, -1, false},
            {EVO_RMLUI_ROW_VALUE, "DEFAULT FONT STYLE", "ON-SCREEN TEXT TYPEFACE", "../icons/icon_type.png", "CONDENSED", 0, EVO_RMLUI_PREVIEW_SUB_FONT, "Picks the typeface for on-screen subtitles.", {"STANDARD", "ROUNDED", "BOLD", "CONDENSED"}, 3, 3, false},
            {EVO_RMLUI_ROW_VALUE, "SECONDARY SUBTITLE POSITION", "WHERE THE SECOND LINE OF DIALOGUE GOES", "../icons/icon_subtitles.png", "STACKED ABOVE", 0, 0, "", {"STACKED ABOVE", "TOP OF SCREEN"}, 0, -1, false}
        });
    }
}

/* The movie / show detail page (#221). variant: 0 overview, 1 cast (with a saved position), 2 versions,
 * 3 a show's Episodes tab, 4 cast still loading and no poster yet. */
