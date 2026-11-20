# NoStream

**A local media player for PlayStation 5.**

NoStream plays your own files: movies, shows, music and pictures from USB drives, the console's internal storage, or shares on your home network (SMB and FTP). You don't need an account, a subscription or a media server.

Video plays on the console's hardware decoder at up to 4K, with HDR10, HLG and Dolby Vision. You get surround sound up to 7.1, subtitles, chapters and intro skipping. You can control it with the DualSense, the PS5 Media Remote or your phone.

NoStream is a fork of [EVO Player](https://github.com/sainsaji/EVO-PLAYER-PS5), focused on local playback.

**[Download NoStream](https://github.com/qbyss/nostream/releases)**

---

## What NoStream Changes

- **PS5 Media Remote support:** the official remote drives the menus and playback. Play/Pause, Rewind and Forward work in the player. See [Controls](#controls-reference).
- **Local media only:** the media providers (Jellyfin, Emby, Plex, Xtream, IPTV, Nuvio) and their menus are removed. NoStream plays files from USB, internal storage, SMB and FTP. YouTube casting and links still work.
- **USB drives by name:** each mounted USB drive shows in the storage sidebar under its volume label.
- **Favorite folders in the sidebar:** Options on a USB folder → **Add to Favorites** adds a shortcut to the sidebar. USB favorites follow the drive by its volume serial, even when its mount number changes, and come back at startup.
- **Thumbnails survive restarts:** local thumbnails are cached in `/data/nostream/thumbnails` (up to 256 MB).
- **Three rows in the storage browser**, and no rotating tips on Home.
- **Scheduled Night Shift:** in **Settings → Video & Display**, enable Night Shift and pick a start and stop hour using the PS5’s local clock. NoStream warms its menus and SDR video during the first 30 minutes and returns to normal over the last 30 minutes. True HDR output keeps its original colors.

---

## Installation

NoStream is a **Media app** (`PPSA99273`). It installs as a single `.ffpfsc` image and launches from the **Media** tab, like any other title. Its settings are stored in `/data/nostream`.

### Prerequisites

- A jailbroken PS5 on firmware **13.60 or below**.
- **ShadowMountPlus** on the console to mount and manage the app image.
- An FTP server on the console (the usual jailbreak payloads provide one on port `2121`).
- *(For USB playback)* a USB drive formatted **exFAT** or **FAT32**, or **NTFS** (read-only).

### Install over FTP

1. Build the package below or download the matching `NoStream-*-PPSA99273.ffpfsc` asset from [NoStream releases](https://github.com/qbyss/nostream/releases).
2. Copy it to `/data/homebrew/PPSA99273.ffpfsc` on the console.
3. After ShadowMountPlus registers the title, launch **NoStream** from the **Media** tab.

Close NoStream from the PS5 switcher before replacing an installed image.

---

## Controls Reference

### DualSense

| Button | In Menus & File Browser | During Playback | On-Screen Keyboard |
|---|---|---|---|
| **CROSS ✕** | Select / Open | Play / Pause | Type character |
| **CIRCLE ○** | Back | Hide the video menu or controls. Stops playback when they’re hidden. | Cancel |
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
| [docs/ui/hui-kit.md](docs/ui/hui-kit.md) | The interface: NoStream's screens on the ps5-homebrew-ui kit |
| [docs/ui/settings.md](docs/ui/settings.md) | Every setting |
| [docs/providers/youtube.md](docs/providers/youtube.md) | YouTube casting and playback |
| [docs/ui/theming.md](docs/ui/theming.md) | Theme tokens and the `.theme` format |
| [CHANGELOG.md](CHANGELOG.md) | Complete version history |

---

## Community

Report NoStream bugs on [GitHub issues](https://github.com/qbyss/nostream/issues). Before you send a pull request, read **[CONTRIBUTING.md](CONTRIBUTING.md)**.

---

## Credits & License

NoStream is a fork of EVO Player and, like it, is licensed under **GPL-3.0-or-later** (see [COPYRIGHT.md](COPYRIGHT.md)). EVO Player's initial idea came from [ProsperoPlayer](https://github.com/KINGDKAK/ProsperoPlayer) by KINGDKAK.

- [EVO Player](https://github.com/sainsaji/EVO-PLAYER-PS5) — the project NoStream is forked from
- TubeMini — the PS5 Media Remote input code
- [ps5-payload-dev](https://github.com/ps5-payload-dev) (John Törnblom) — SDK and toolchain
- [KINGDKAK](https://github.com/KINGDKAK) — ProsperoPlayer
- [zecoxao/sce_symbols](https://github.com/zecoxao/sce_symbols) — NID symbol database
- [BlackBearReloaded](https://github.com/blackbearreloaded) — [ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui), the UI kit NoStream's interface is built on
- [mihawk-99](https://github.com/mihawk-99) — [PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate), whose [1 GiB flexible-memory change](https://github.com/mihawk-99/PS5_VulkanTemplate/commit/04aa411deb9d9d12b40cc2afb479a328394da4ca) NoStream's app module uses
- PS5 developers **Philow**, **sword** and **0xManuel** — for their help and research

*NoStream is independent homebrew software and is not affiliated with, endorsed by, or associated with Sony Interactive Entertainment. All PlayStation trademarks belong to their respective owners.*
