# NoStream

**A native media player for PlayStation 5, forked from [EVO Player](https://github.com/sainsaji/EVO-PLAYER-PS5).**

NoStream uses title ID `PPSA99273` and stores its settings in `/data/nostream`. The release highlights below come from EVO Player. The feature reference describes NoStream.

Plays video from USB drives (exFAT, FAT32 and read-only NTFS), internal storage, and network shares (SMB and FTP). Video decodes on the console's own hardware decoder at up to 4K with 10-bit HDR and Dolby Vision, the interface renders directly on the GPU via bare-metal `sceAgc`, and everything is built from the ground up for the DualSense controller and the big screen.

![Playback on EVO Player: 4K HDR10 over hardware decode, chapter marks, captions and the Skip intro button](docs/images/player.png)

*A real capture from the console: 4K HDR10 HEVC on the hardware decoder, chapter ticks on the seek bar, and the new **Skip intro** button.*

![EVO Player home screen: Continue Watching, Jump back in and the library](docs/images/launch.png)

**[NoStream releases](https://github.com/qbyss/nostream/releases)**

---

## What NoStream Changes

- **PS5 Media Remote support:** the official remote drives the menus and playback. Play/Pause, Rewind and Forward work in the player. See [Controls](#controls-reference).
- **Local media only:** the media providers (Jellyfin, Emby, Plex, Xtream, IPTV, Nuvio) and their menus are removed. NoStream plays files from USB, internal storage, SMB and FTP. YouTube casting and links still work.
- **USB drives by name:** each mounted USB drive shows in the storage sidebar under its volume label.
- **Favorite folders in the sidebar:** Options on a USB folder → **Add to Favorites** adds a shortcut to the sidebar. USB favorites follow the drive by its volume serial, even when its mount number changes, and come back at startup.
- **Thumbnails survive restarts:** local thumbnails are cached in `/data/nostream/thumbnails` (up to 256 MB).
- **Three rows in the storage browser**, and no rotating tips on Home.

---

## What's New in v0.13.7 (EVO Player)

- **Cast from the YouTube app:** EVO shows up as a cast target on Android and iOS, no sign-in. The whole video plays in EVO's own player, up to 1080p60, with a loading toast while it starts. Settings → Network → **Cast to NoStream**.
- **Play a YouTube link, whole video:** paste a link in Settings → Interface & Storage → **Play a YouTube link**, or on the phone page.
- **Your phone as a remote (#238):** open `<console>:9780/remote` for play/pause, seeking, chapters, subtitles, audio tracks and a D-pad for EVO's menus.
- **Smoother playback and browsing:** no more regular stutter in local and USB playback, and opening a file list no longer freezes for a few seconds when a USB drive is plugged in.
- **Album art (#232):** FLAC and MP3 files show their cover on the player card, and FLAC files with art no longer freeze at 0:00.
- **Clearer Settings:** rows are never cut off, every setting has a picture preview and its own icon.
- **Upmix stereo to 4.0**, **controller phone audio**, **subtitle style** and **library filters** (thanks @Andrea17982).
- **Experimental web browser:** Settings → Experimental → **Web browser** (off by default).

Full notes: [CHANGELOG.md](CHANGELOG.md).

### Highlights from v0.13.5

- **Skip intros & credits:** a **Skip intro** / **Skip credits** / **Next episode** button appears while the playhead is inside one, and **TRIANGLE** presses it. **Settings → Video & Display → Skip intros & credits** is Off, Ask (default) or Auto. It works for every file whose chapters are named Intro, Opening, OP, Recap, Credits, Ending, ED or Outro.
- **NTFS USB drives:** open read-only. Browse, play, view pictures and load subtitles from them.
- **Load subtitles from file:** the subtitle picker browses the video's folder, any USB or NTFS drive, or internal storage.
- **Add Server flow for SMB and FTP:** address, Guest or Account, a login test with a plain error, a share picker, and Online / Offline status on every server card.
- **True HDR colour:** HDR10, HLG and Dolby Vision are no longer washed out, plus an **HDR PEAK** setting that fits Dolby Vision to your TV shot by shot.
- **Safer network use:** TLS certificate verification, a dead server fails in 3 seconds instead of 75, and no passwords or keys in logs.
- **Diagnostics:** a phone and PC log viewer with live playback health, a kept log of the last run, and **Report a problem**, which opens a GitHub issue already filled in.


Full notes: [CHANGELOG.md](CHANGELOG.md).

### Highlights from v0.13.0

- **In-app updater:** a side card offers the new version, downloads and verifies it, installs it with one rename, and closes EVO.
- **Chapters:** L1 / R1 jump between chapters, the seek bar shows chapter ticks, R3 opens a chapter picker. The OSD shows when the video ends and the time remaining.
- **Volume boost, audio delay and network buffer:** new settings, plus a playback diagnostics panel with HDR, audio output and dropped frames.
- **Better pickers and keyboard:** stream and audio pickers with pills and a details card, a native-style virtual keyboard, PGS picture subtitles, and a preview card for every setting.
- **Fixes:** VP9 Profile 2 on hardware, AAC without silence, low frame rate video at the right speed, and a pad scrub that always commits.
- **Scrolling changelog:** the right stick scrolls a long release in the in-app changelog.

Full notes: [CHANGELOG.md](CHANGELOG.md).

### Highlights from v0.12.5

- **Multiple USB & M.2 Drives:** Detects `/mnt/usb0`–`/mnt/usb7` and `/mnt/ext0`–`/mnt/ext1`. Displays a drive picker when multiple drives are connected and provides cross-drive search.
- **Keyboard Caret Navigation:** L1 and R1 move a UTF-8 aware cursor to edit text in place without clearing the entire line.
- **Thai Subtitles & CJK Fallback:** Embedded Noto Sans Thai fonts for subtitle rendering, plus runtime CJK font loading from `/data/nostream/fonts/`.

Full notes: [CHANGELOG.md](CHANGELOG.md).

### Highlights from v0.12.0

- **Audio Passthrough:** Settings → Audio → `AUDIO PASSTHROUGH` sends Dolby Digital, Dolby Digital Plus (including Atmos), DTS and AAC over HDMI untouched, so your soundbar or receiver does the decoding. Dolby TrueHD is included as an experimental option.
- **Type from your phone:** server addresses and other text can be sent from a phone browser on the same network. The keyboard shows the link (user-settable port, default 9780) and QR code to open; nothing to install.
- **Match video frame rate (experimental):** asks the display for the video's own rate, with a fall-back to 119.88 Hz for film on firmware 12.70.

### Highlights from v0.11.0

- **Real-Time AI Upscaling:** Sub-4K media is upscaled in real time using AMD FSR 1 spatial sharpening (`Sharp`) or Anime4K convolutional neural networks (`AI` — Standard, Large, and Maximum for PS5 Pro).
- **Dolby Vision Profile 5 & AV1:** Reshaped on the GPU from the RPU for accurate color tonemapping. AV1 hardware/software decode support up to 4K 10-bit.
- **Surround Sound Studio & DualSense Auto-Calibration:** Interactive 2.5D soundstage with sound orb navigation, discrete 5.1 & 7.1 Linear PCM output, and automatic room acoustic calibration using the DualSense controller microphone.
- **Built-in File Manager & Full FTP Support:** Press Options on any file or folder to copy, move, rename (with the on-screen virtual keyboard), or delete, with live transfer speed (MB/s) and ETA. Features **automatic detection of the PS5 internal FTP server** (`127.0.0.1:2121`), high-speed loopback file delegation, and easy mounting of remote FTP/NAS shares right from the sidebar.
- **Subtitles & Auto-Sync:** Acoustic speech auto-sync, concurrent dual subtitle tracks (bottom primary + top secondary), and on-the-fly timing adjustment via DualSense trackpad swipe.
- **120 Hz Output & Motion Smoothing:** HDMI 2.1 120 Hz output for judder-free 5:5 pulldown on 24 fps cinema content, plus experimental 24 → 60 fps GPU motion smoothing.

---

## Installation

NoStream is a **Media app** (`PPSA99273`). It installs as a single `.ffpfsc` image and launches from the **Media** tab, like any other title.

### Prerequisites

- A jailbroken PS5 on firmware **13.60 or below**. Upstream EVO Player was tested on a PS5 Pro on 13.60 (earlier releases were measured on 12.70).
- **ShadowMountPlus** on the console to mount and manage the app image.
- An FTP server on the console (the usual jailbreak payloads provide one on port `2121`).
- *(For USB playback)* a USB drive formatted **exFAT** or **FAT32**, or **NTFS** (read-only).

### Install over FTP

1. Build the package below or download the matching `NoStream-*-PPSA99273.ffpfsc` asset from [NoStream releases](https://github.com/qbyss/nostream/releases).
2. Copy it to `/data/homebrew/PPSA99273.ffpfsc` on the console.
3. After ShadowMountPlus registers the title, launch **NoStream** from the **Media** tab.

Close NoStream from the PS5 switcher before replacing an installed image.

---

## Features

### Hardware Video Decode, HDR & Dolby Vision

Video decodes on the console's own `sceVideodec2` hardware decoder — **H.264, HEVC and VP9 at up to 4K** — with resident per-codec decoders created once at boot. Clips outside hardware limits fall back to FFmpeg automatically.

- **10-bit HDR (HDR10 / PQ and HLG):** Decodes on the hardware decoder. **Settings → Video & Display → HDR OUTPUT** chooses how it is presented: **Auto** matches the display, **Off** tone-maps to SDR using a BT.2390 curve (BT.2020 → BT.709).
- **Dolby Vision Profile 5:** Correct IPT color tonemapping reshaped on the GPU from the RPU data, eliminating magenta/green discoloration.
- **AV1 Support:** Decodes AV1 up to 4K 10-bit via optimized libdav1d.

![Media Info for a 4K HDR10 HEVC clip: hardware decoder, BT.2020 / ST 2084, 60 fps](docs/images/mediainfo.png)

Press **OPTIONS** during playback for the diagnostics panel: codec, HDR, audio output, render and decode rate, dropped frames, queue depth, A/V clock delta and GPU / RAM / CPU graphs.

![Playback diagnostics panel over 4K HDR10 video](docs/images/stats.png)

#### Video Codecs

| Codec | Decoder | Up to | Verified |
|---|---|---|---|
| **HEVC / H.265 Main (8-bit)** | hardware (`sceVideodec2`) | 4K 60fps, real time | yes |
| **HEVC Main 10 (HDR10 & HLG)** | hardware (`sceVideodec2`) | 4K 60fps, real time | yes |
| **Dolby Vision Profile 5** | hardware + GPU tonemap | 4K 60fps, real time | yes |
| **H.264 / AVC High** | hardware (`sceVideodec2`) | 4K 30fps / 1080p 60fps | yes |
| **VP9 Profile 0 & Profile 2** | hardware (`sceVideodec2`) | 4K (Profile 0) / 1080p (Profile 2) | yes |
| **AV1 (10-bit)** | software (libdav1d) | 4K, real time | yes |
| **MPEG-2, MPEG-4 Part 2, VC-1** | software | 1080p | yes |

#### Audio Codecs & Surround Output

Multichannel sources decode to discrete **Linear PCM 5.1 & 7.1** over HDMI, or fold down cleanly to stereo for headphones and TV speakers.

| Codec | Decoder | Channels | Verified |
|---|---|---|---|
| **Linear PCM** | hardware / passthrough | 2.0, 5.1, 7.1 discrete | yes |
| **Dolby TrueHD** | software decode | 5.1, 7.1 | yes |
| **Dolby Digital (AC-3) & E-AC-3** | software decode | 5.1, 7.1 (Atmos bed) | yes |
| **DTS-HD Master Audio & DTS core** | software decode | 5.1, 7.1 | yes |
| **DTS:X (bed)** | software decode | 7.1.4 | yes |
| **FLAC & ALAC** | software decode | Up to 192 kHz / 24-bit (5.1, 7.1) | yes |
| **AAC / AAC-LATM / MP3 / Opus** | software decode | Stereo & multichannel | yes |

---

### Real-Time AI Upscaling

Anything below 4K can be upscaled in real time using console compute shaders:

- **Sharp (AMD FSR 1):** Edge-adaptive directional spatial reconstruction filter. Extremely lightweight (~1.2 ms frame overhead), ideal for broadcast TV, sports, and live-action films.
- **AI (Anime4K Neural Networks):** Deep convolutional neural network (CNN) inference running live on the GPU. Reconstructs sharp linework and fine textures.
  - **Standard:** Balanced neural pass with minimal overhead.
  - **Large:** Multi-pass refinement for animation and clean digital content.
  - **Maximum:** Extreme multi-stage network inference (optimized for PS5 Pro).

Select your preferred engine under **Settings → Video & Display → Upscaling**. The on-screen display (OSD) shows real-time badge confirmation during playback.

---

### Chapters, Skip Intro & Credits

![The chapter list on a clip with Intro, Fractal Dive and Credits chapters](docs/images/chapters.png)

- **Chapter navigation:** **L1 / R1** jump between chapters, the seek bar shows a tick at each one, the OSD names the current chapter, and the **right stick** opens the chapter list.
- **Skip button:** inside an intro or credits, a **Skip intro** or **Skip credits** button shows for 8 seconds and **TRIANGLE** presses it, with the controls hidden or not.
- **Three modes:** **Settings → Video & Display → Skip intros & credits** is **Off**, **Ask** (the default, the button) or **Auto** (skips once and says so; TRIANGLE goes back). Auto never skips a stretch you seeked or resumed into.
- **Any source:** chapter titles with the whole word Intro, Opening, OP, Recap, Credits, Ending, ED or Outro count on every file, including files on USB, NTFS, SMB and FTP.

---

### Surround Sound Studio & DualSense Auto-Calibration

A 2.5D visual soundstage (**Settings → Audio → Surround Sound Test**) to configure and test multichannel speaker setups:

- **Interactive Sound Stage:** Free-roam 3D sound orb controllable via the analog stick, trackpad, or D-pad.
- **Real-Time VU Meters & Test Chirps:** Individual channel tones for `FL`, `FC`, `FR`, `LFE`, `SL`, `SR`, `BL`, `BR`.
- **DualSense Mic Auto-Calibration:** Place the DualSense controller at your primary listening position and let EVO emit swept-sine acoustic chirps. The controller's microphone array captures room impulses to calculate:
  - Precise speaker distance trims (cm / ms).
  - Sub-millisecond phase arrival alignment.
  - Channel gain loudness balance.
- **Clean Stereo Downmix:** Phase-accurate folding of 5.1 and 7.1 content to 2.0 stereo under **Settings → Audio → Output Channels**.

![Surround Sound Studio](docs/images/surround.png)

---

### File Operations & Network Storage (SMB / FTP)

Manage your storage directly on the PS5 with native file operations, integrated Samba/Windows sharing, and FTP:

- **Samba / Windows Shares (SMB):** Connect directly to Windows shared folders, Samba servers, and NAS appliances (`smb://host/Share` or `user:pass@host/Share`) via **+ Add Server**: type the address, pick Guest or Account, and EVO tests the login before saving. Server cards show Online (with ms), Offline or Checking.
- **Direct Network Streaming:** Media streams directly over SMB via `libsmb2` and custom AVIO pipelines (`evo_smb_io`) with zero local caching or download delay.
- **Server Options & Management:** Press **OPTIONS** on any configured network share to Open, **Edit Server** (pre-fills connection details on the on-screen keyboard), or **Remove Server** (with confirmation dialog). Shortcut **TRIANGLE** opens the remove confirmation directly.
- **File Manager:** Press **OPTIONS** on any file or folder to Copy, Cut, Paste, Rename (using the on-screen virtual keyboard), Delete, or create folders.
- **Asynchronous Transfers:** File operations run on a dedicated background thread with real-time transfer speed (MB/s), bytes copied, and remaining ETA.
- **Instant Filter Chips:** Press **Up** from the top row to filter the current folder by video, audio, or images without rescanning the drive.
- **Automatic PS5 Internal FTP Detection:** EVO Player automatically detects and connects to the console's resident jailbreak FTP payload on loopback (`127.0.0.1:2121`). Pre-configured as **PS5 Local FTP** in the browser sidebar, giving you immediate access to explore and manage the entire console filesystem (`/`) without typing IP addresses or credentials.
- **High-Speed Loopback Delegation:** Large internal file transfers (>64 MB) automatically stream through the loopback FTP daemon to bypass per-process application write throttling, sustaining full drive speeds (~80+ MB/s).
- **Remote FTP / NAS Mounting:** Add remote FTP servers from your home network in the sidebar to stream media directly or transfer files between your PC/NAS and console without leaving the sofa.
- **USB labels and Favorites:** Mounted USB drives appear individually in the storage sidebar, using their volume labels when available. On a USB folder, open Options and choose Add to Favorites. New USB favorites use the filesystem’s volume serial, so they follow the drive when its mount number changes. Re-add older USB favorites to give them a volume identity.
- **NTFS USB drives:** an NTFS drive is not mounted by the PS5, so EVO reads the raw disk itself, read-only: browse, play, view pictures and load subtitles from it ([docs/hardware/ntfs.md](docs/hardware/ntfs.md)).
- **Cover art:** FTP and SMB folders show posters from `poster.jpg`, `folder.jpg` and `<name>.jpg` files beside the video.

![Browsing a folder](docs/images/browse.png)

![Network Storage: Add Server, the console's own FTP (Online, 1 ms) and an SMB server that is Offline](docs/images/network.png)

---

### Subtitles & Audio Auto-Sync

- **Experimental Audio Auto-Sync:** Analyzes spoken audio in real time to calculate acoustic timing offsets and synchronize subtitle tracks automatically.
- **Concurrent Dual Subtitles:** Display two subtitle languages simultaneously — primary at the bottom, secondary at the top of the screen.
- **Live Sync Nudge:** in the subtitle picker, press **Left / Right** or swipe or click left/right on the DualSense trackpad to move the timing by ±100 ms without pausing. **L1 / R1** do the same for the secondary line.
- **Broad Format Support:** Styled ASS / SSA with full styling and positioning, SRT, SubRip, WebVTT and PGS picture subtitles.
- **Load from file:** the picker's **Load from file** row browses the video's folder, any USB or NTFS drive, or internal storage for an `.srt`. Online subtitles can be fetched too.
- **Preferred languages:** **Settings → Audio** and **Subtitles** pick the audio and subtitle language to start on.

![Subtitle track picker with Auto-Sync](docs/images/picker.png)

---

### 120 Hz Output & Motion Smoothing

- **120 Hz High Frame Rate (HDMI 2.1):** Enables clean 5:5 pulldown for 24.000 / 23.976 fps cinema film content, eliminating 3:2 pulldown judder. Options: Off, Auto, or Playback Only under **Settings → Video & Display**.
- **Experimental Motion Smoothing:** Real-time 24 → 60 fps frame interpolation synthesised on the GPU under **Settings → Experimental**.

---

### GPU-Rendered Interface (`sceAgc`)

The entire user interface is rendered on the GPU through bare-metal `sceAgc` at the panel's native resolution:
- Locked 60 fps navigation with minimal frame times.
- Renders only when state changes, maintaining near-zero idle GPU load.
- Four built-in color themes (Midnight is the default, then Carbon, Ember and Aurora) plus your own `.theme` files, under **Settings → Interface & Storage → Theme** ([docs/ui/theming.md](docs/ui/theming.md)).
- A preview card beside every setting shows what it does before you change it.

![Settings: Video & Display, with the preview card for each row](docs/images/settings.png)

<table>
  <tr>
    <td><img src="docs/images/settings_audio.png" alt="Settings: Audio, with volume boost, delay, channels and passthrough"></td>
    <td><img src="docs/images/settings_subtitles.png" alt="Settings: Subtitles, with a live caption preview"></td>
  </tr>
  <tr>
    <td><img src="docs/images/settings_interface.png" alt="Settings: Interface and Storage, with the four built-in themes"></td>
    <td><img src="docs/images/settings_experimental.png" alt="Settings: Experimental, with motion smoothing, match frame rate and deep blacks"></td>
  </tr>
</table>

<table>
  <tr>
    <td><img src="docs/images/about.png" alt="About: what EVO decodes, renders and targets"></td>
    <td><img src="docs/images/changelog.png" alt="The in-app changelog, one release at a time"></td>
  </tr>
</table>

---

## Controls Reference

### DualSense

| Button | In Menus & File Browser | During Playback | On-Screen Keyboard |
|---|---|---|---|
| **CROSS ✕** | Select / Open | Play / Pause | Type character |
| **CIRCLE ○** | Back | Stop playback | Cancel |
| **TRIANGLE △** | Add a file to Favorites / Remove network server | **Skip intro / credits** while the button shows (or undo an automatic skip) | Space |
| **SQUARE □** | Search | — | Backspace |
| **OPTIONS** | File operations (Copy, Move, Rename, Delete), Add to Favorites / Server options (Edit, Remove) | Player menu: Audio, Subtitles, View mode (Fit / Fill / Stretch), Media Info, Stats | Done |
| **D-Pad / Left Stick** | Move focus, hold to scroll | **Left / Right:** seek 10 s (hold to scrub faster) | Move between keys (up from the top row: the clear X) |
| **LEFT** | Open navigation rail | — | — |
| **UP** | Instant folder filter chips | — | — |
| **L1 / R1** | Page up / Page down | Previous / next chapter (±30 s without chapters) | Move the text cursor left / right |
| **L2 / R2** | Jump alphabetically (A–Z) | — | **L2:** Shift (press again: Caps Lock)<br>**R2:** Done |
| **L3 / R3** | Take screenshot (to `/mnt/usb0`, or `/data/nostream` with no stick) | **L3:** screenshot<br>**R3:** chapter list (a file with chapters) | — |

### PS5 Media Remote

Pair the remote in the PS5's own settings. NoStream finds it without any setting, and it works together with the DualSense.

| Remote button | Does |
|---|---|
| **D-pad** | Moves focus in menus; Left / Right seek 10 s in the player |
| **Enter** | Same as Cross |
| **Back** | Same as Circle |
| **Options** | Same as Options |
| **Play / Pause** | Plays or pauses; restarts a file that has finished |
| **Rewind / Forward** | Seek 10 s back or forward (hold to keep seeking); a paused video stays paused |

Play/Pause, Rewind and Forward only work in the player.

### Phone

Open `http://<console-ip>:9780/remote` on a phone on the same network for play/pause, seeking, chapters, subtitles, audio tracks and a D-pad for the menus.

---

## Building from Source

Builds inside a pinned Docker toolchain. On Apple Silicon, run `export DOCKER_DEFAULT_PLATFORM=linux/amd64` first.

```bash
git clone --recursive https://github.com/qbyss/nostream.git
cd nostream

docker compose build
```

Build FFmpeg and package the app module:

```bash
docker compose run --rm ps5-dev bash -lc '
  ./scripts/build-ffmpeg.sh --profile minimal --install &&
  ./scripts/package-app.sh --ffpfsc'
```

With NoStream closed on the console, upload the packaged title over FTP:

```bash
PS5_HOST=192.168.1.50 ./scripts/deploy-app.sh --ffpfsc
```

Render UI screens on PC without hardware:

```bash
./tools/uiview.sh --all                  # Outputs to output/uiview/
./tools/uiplay.sh                        # Contact sheet of all screens
```

Run test suite:

```bash
docker compose run --rm ps5-dev ./tests/run_tests.sh
```

---

## Documentation

| Document | Purpose |
|---|---|
| [docs/README.md](docs/README.md) | The index of everything below |
| [docs/build/building.md](docs/build/building.md) | Build environment, SDK, FFmpeg, shaders |
| [docs/build/packaging.md](docs/build/packaging.md) | Packaging the app module and deploying it |
| [docs/console/hardware-cycle.md](docs/console/hardware-cycle.md) | Running a build on the console safely |
| [docs/architecture/architecture.md](docs/architecture/architecture.md) | Core architecture and module boundaries |
| [docs/hardware/codec-support.md](docs/hardware/codec-support.md) | Codec, container and hardware decoding matrix |
| [docs/hardware/video-decode.md](docs/hardware/video-decode.md) | Hardware decode via `sceVideodec2` |
| [docs/hardware/agc-runtime.md](docs/hardware/agc-runtime.md) | Bare-metal `sceAgc` GPU rendering |
| [docs/ui/hui-kit.md](docs/ui/hui-kit.md) | The interface: EVO's screens on the ps5-homebrew-ui kit |
| [docs/ui/settings.md](docs/ui/settings.md) | Every setting |
| [docs/providers/youtube.md](docs/providers/youtube.md) | YouTube casting and playback |
| [docs/ui/theming.md](docs/ui/theming.md) | Theme tokens and the `.theme` format |
| [CHANGELOG.md](CHANGELOG.md) | Complete version history |

---

## Community

Report NoStream bugs on [GitHub issues](https://github.com/qbyss/nostream/issues). Before you send a pull request, read **[CONTRIBUTING.md](CONTRIBUTING.md)**.

For upstream EVO Player questions, use the **[EVO Player Discord](https://discord.gg/47Nc8WtXh)**.

---

## Credits & License

NoStream is a fork of EVO Player and, like it, is licensed under **GPL-3.0-or-later** (see [COPYRIGHT.md](COPYRIGHT.md)). EVO Player's initial idea came from [ProsperoPlayer](https://github.com/KINGDKAK/ProsperoPlayer) by KINGDKAK.

- [EVO Player](https://github.com/sainsaji/EVO-PLAYER-PS5) — the project NoStream is forked from
- TubeMini — the PS5 Media Remote input code
- [ps5-payload-dev](https://github.com/ps5-payload-dev) (John Törnblom) — SDK and toolchain
- [KINGDKAK](https://github.com/KINGDKAK) — ProsperoPlayer
- [zecoxao/sce_symbols](https://github.com/zecoxao/sce_symbols) — NID symbol database
- [BlackBearReloaded](https://github.com/blackbearreloaded) — [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui), the UI kit EVO's interface is built on
- [mihawk-99](https://github.com/mihawk-99) — [PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate), whose [1 GiB flexible-memory change](https://github.com/mihawk-99/PS5_VulkanTemplate/commit/04aa411deb9d9d12b40cc2afb479a328394da4ca) EVO's app module uses
- PS5 developers **Philow**, **sword** and **0xManuel** — for their help and research

*NoStream is independent homebrew software and is not affiliated with, endorsed by, or associated with Sony Interactive Entertainment. All PlayStation trademarks belong to their respective owners.*
