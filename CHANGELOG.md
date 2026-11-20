# Changelog

Notable changes per release. The release workflow lifts the matching section
into the GitHub release notes, so keep the headings in the form `## 0.1.0`.

---

## 0.2.0

- **Internal Storage** opens NoStream’s videos folder at `/data/homebrew/PPSA99273/videos/`.
- **Copy to internal** copies USB videos and folders, including subtitles and artwork inside folders. USB originals stay in place, and a same-name item in Internal Storage stops the copy.
- The transfer dialog shows progress and a Circle button icon for **Cancel**. Copied videos can be deleted from Internal Storage.

## 0.1.0

**The first NoStream release.** NoStream is a local media player for jailbroken PS5: your USB drives and folders, played with hardware decode, driven from the pad or the PS5 Media Remote. It started as a fork of [EVO Player](https://github.com/sainsaji/EVO-PLAYER-PS5) 0.13.7 and keeps its video player, and leaves the online services out.

Download **`NoStream-0.1.0-PPSA99273.zip`** and copy the `PPSA99273` folder to `/data/homebrew/`, then launch NoStream from the Games row. Runs on any jailbroken PS5 on firmware 13.60 and below.

### Local first

- **No media providers.** Emby, Jellyfin, IPTV, Xtream and Nuvio are gone, with their menus.
- **Storage shows only folders and videos**, three rows at a time.
- **Favorite folders** sit in the Storage sidebar, survive restarts and keep their place when a USB drive comes back. Search is in the same sidebar.
- **USB drives are recognised** at startup, and Home shows a spinner while it checks them.
- **Thumbnails are kept** across restarts.
- **Hide a USB drive or the Network Storage section.** Bring them back from Settings; saved media and progress are kept.

### Home

- **Simpler Home** with no rotating tips, and menus that move at once.
- **Resume** shows up as soon as recent media is known, and gets focus after the USB scan.
- **Square opens options** from Home.
- **Movie backdrops crossfade.**

### Playback

- **PS5 Media Remote support**, with double presses to jump chapters.
- **Controls stay hidden** when a video starts or resumes. Circle closes them before it stops the video.
- **Video settings** sit inside the playback controls.
- **Resume points are saved every five seconds**, so a crash or a power cut loses almost nothing.
- **Software-decoded video keeps its timestamps.**
- **Touchpad click plays and pauses.** L2 and R2 are ignored.

### Comfort

- **Night Shift** warms the menus on a schedule, and SDR video too.
- **OLED screen saver** fades to black after an idle timeout.
- **Navigation sounds are off by default.**
- **Softer shadows** on cards and dialogs, a folder-tree icon for Browse and larger playback toasts.
