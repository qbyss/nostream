---
name: ship
description: Build, deploy, launch, drive or screenshot NoStream on the PS5, or render its UI on the host. Use whenever asked to run / launch / deploy / install / screenshot / hardware-test the app, verify a change on real hardware, or do a console session. Deploy the PPSA99273 app folder - plus the host UI renderer. Has the exact command loop, so do not re-derive it.
---

# Shipping NoStream

**Read `AGENTS.md` first. It overrides everything in this skill.** Build in the
`evoplayer_fork_workspace` Docker volume, upload the folder from macOS, then
STOP: the owner closes, launches and tests. A plain `docker compose run --rm
ps5-dev` build fails (FFmpeg 7.0.1; NoStream needs 7.1.1 from
`evoplayer_fork_ffmpeg`). **Never call `make`.** Never stack launches, never
deploy over a running NoStream. Reference: `docs/build/tooling.md`.

## 1. Pick the route

| Change is about | Route |
|---|---|
| UI layout / look / text rendering | Host renderer `./tools/hui_preview.sh` (kit screens -> `output/uiview/hui_*.png`) or `./tools/uiview.sh --all` (RmlUi). No console. |
| Decode, GPU, audio, `/data`, input, anything needing hardware | App module loop below |
| Compile check only | `./scripts/build-nostream.sh` inside the fork workspace volume (AGENTS.md); a plain `docker compose run --rm` fails on FFmpeg 7.0.1 |

The host has no compiler. Anything that compiles C/C++ (even a one-off test) runs in the container:
`MSYS_NO_PATHCONV=1 docker compose run --rm ps5-dev bash -c 'cd output/x && clang-18 ...'`.

## 2. Build and deploy (AGENTS.md)

1. Sync the changed sources into the `evoplayer_fork_workspace` volume, extracting
   with `tar --touch -xf -` (or touch them) so Make rebuilds their objects.
2. Run `./scripts/package-app.sh` there, reusing `evoplayer_fork_sdk`,
   `evoplayer_fork_ccache` and `evoplayer_fork_ffmpeg`. Export the full
   `output/app/PPSA99273/` folder.
3. On macOS: `PS5_HOST=192.168.1.23 FTP_PORT=2121 ./scripts/deploy-app.sh`.
4. Stop. The owner closes, launches and tests. Commit only when asked.

Only when the owner asks you to drive the console, the cycle is (one step at a
time, stop at the first anomaly, never auto-retry):

```bash
timeout 100 ./tools/nostream-remote.sh quit     # expect "ok parked"
timeout 100 ./tools/nostream-remote.sh close    # expect "ok closed"
timeout 200 ./tools/nostream-remote.sh launch   # waits for ShadowMount idle
```

- Status line: `nostream-remote.sh status` (`build=... fatal=0 parked=0`). Log: `nostream-remote.sh log | grep -i hui`.
- A tool call blocked by the user = they want to drive that part; wait for their word.
- Generated `projects/evoplayer/ui_rml/src/evo_rmlui_bundle_data.cpp` regenerates on every build and is git-ignored.
- Adding a source file: list it in `projects/evoplayer/Makefile` (media/*.c and ui_kit/*.cpp are listed by hand, not globbed).
- Assets under `projects/evoplayer/assets/{rml,fonts,icons,wallpaper,hui}` are embedded automatically at package time.

## 2b. Issue to done (checklist)

1. State "agy: yes/no, because ..." in the first reply on the issue (CLAUDE.md).
2. Mechanical parts go to agy (`tools/agy-run.sh`); review its report and `git diff`, re-run its checks.
3. Build in the fork workspace volume (section 2, AGENTS.md).
4. Host tests: `./tests/run_tests.sh` in the same fork container. A new setting also extends the settings round-trip test.
5. Deploy the folder and stop; the owner tests on the console. Use `nostream-remote.sh verify --check <name>` only when the owner asks.
6. One commit, only after 3-5 pass. Bump `contentVersion` on a release.
7. Close the issue with the evidence: the PASS line, the commit hash, and what was not verified.

## 3. Drive and look (only when the owner asks)

```bash
# several keys in ONE container start (each start costs seconds)
docker compose run --rm ps5-dev bash -c 'for k in down down right cross; do ./tools/nostream-remote.sh key $k >/dev/null 2>&1; sleep 2; done; ./tools/nostream-remote.sh ui --line 2>&1 | tail -1'
```
- `ui --line` = focused element + screen as text; use it to navigate. Keys: up down left right cross circle square triangle l1 r1 l3 ...
- **Jump to any screen by name:** `nostream-remote.sh go <home|browse|internal|favorites|recent|providers|settings[-audio|-playback|...]|about|changelog|themes|devtools|<provider id>|<ScreenId>>`; works from any screen, no key walk. A provider (about 10 s to load its shelves) can also be opened with `nostream-remote.sh provider <id>`. Fill the search keyboard with `key square` then `type --submit <text>` instead of walking the grid.
- **Change a setting without the D-pad walk (#201, `--usb-remote` builds):** `nostream-remote.sh set <name> <value>` writes it, applies it live and prints it back (`set audio-delay +100`); `set --list` shows every name and its values; `focus <row>` opens the section and puts the cursor on the row.
- **Whole check in one command:** `nostream-remote.sh verify [--check <name>]` = soft close, close, deploy, launch, run `tools/checks/<name>.sh`, quit, close, then PASS/FAIL with the evidence line. No `--check` runs every script in `tools/checks/`. Use `--no-build` to reuse the last packaged app folder.
- Home -> PROVIDERS by keys (only if you need the chooser itself): `down down right right right cross`. `r1`/`l1` flip pages.
- **Screenshot** (`key l3` writes `/mnt/usb0/evo_shot_NNN.bmp`, 3840x2160, numbers wrap at 32). `shot.sh grab` often fails (needs the web server); use FTP:
  `tools/grab_shot.sh <name>` (presses l3, finds the shot NoStream just wrote - `/mnt/usb0`, or `/data/nostream` with no stick - from its log line or the FTP listing, writes `output/screenshots/<name>.png` at 1920x1080; host or container), then Read the PNG. Only for pixel questions; use `ui` for focus/text.

## 4. Test data on the console

- FTP is `ftp://192.168.0.12:2121` (PS5_HOST in `.env`). Config files: `/data/nostream/*.conf`. Fonts: `/data/nostream/fonts/` (CJK Noto OTFs are there). Read-only web `/fs`; delete over FTP.
- IPTV test playlist: serve any `.m3u` from the HOST (not the container) with `python -m http.server <port> --bind <host LAN ip>`; write `playlist=http://<ip>:<port>/x.m3u` to `/data/nostream/iptv.conf` by `curl -T`. **Port 8099 is ignored by the provider on purpose**; use another. Restart NoStream (quit/close/launch) to reload the conf. Back up the original conf first (`curl ftp://.../iptv.conf`) and restore it when done.
- Provider setup is in `/data/nostream/*.conf`; Xtream/Jellyfin/Nuvio are already configured on the dev console.
- **A video that always plays:** `nostream-remote.sh play --sample` (20 s 1080p H.264 + AAC + subtitle in `/data/nostream/test/`; if missing: `tools/make_sample.sh` on the host, then `nostream-remote.sh sample-install`). NoStream cannot read `/data/Media` (sandbox). A file on a saved server: `nostream-remote.sh play --share ps5_local /data/Media/<file>` (the Browse URL; a hand-made `ftp://` URL is not the same).
- **Error-path setup (dead IPTV + dead FTP):** with NoStream closed, `python3 tools/testenv.py up`, launch, test, quit/close, `python3 tools/testenv.py down`. Never hand-copy the confs: `down` restores the real ones byte for byte.
- **Reading the log:** `nostream-remote.sh logq <text> [n]` (log server, instant) instead of pulling the 3 MB `nostream.log` over FTP.

## 5. Rules that bite (details in CLAUDE.md)

- Quit -> close -> deploy -> launch. `close` only after `parked=1`. Never `--force` past the deploy interlock.
- A static `nostream.log` proves nothing (idle heartbeat ~6 min).
- Bash heredocs with backslashes or feeding python are blocked by a hook: use the Write tool for scripts, or the Edit tool; run with `python -X utf8 file.py`. `sed -i` with `\n` replacements in Git Bash is fragile: use Edit.
- Kit (`third_party/ps5-homebrew-ui`) is a git submodule: its changes are separate commits; NoStream's `ui_kit/` and `tools/hui_preview/` are in the main repo.
- No commits until the hardware check passes; one clean commit (memory: no-commits-until-verified).
- Reply to the user short: result first, then next step.

## 6. Font / text pipeline (what exists, #128)

- Kit text: baked Inter/Montserrat/DejaVu atlases (ASCII) -> `noto-fallback.huifont` (Latin ext, Greek, Cyrillic, Hebrew, Arabic, symbols; built by `third_party/ps5-homebrew-ui/tools/font-baker/bake_fallback.cpp`) -> `ComplexText` (`ui_kit/src/evo_hui_complex.cpp`: HarfBuzz + stb SDF, Thai/Indic embedded, CJK from `/data/nostream/fonts`).
- Arabic joining and bidi order live in `third_party/ps5-homebrew-ui/src/gfx/font.cpp` (`visual_codepoints`).
- Subtitle sidecars: `media/src/evo_charset.c` (UTF-16 + legacy code pages; tables from `tools/gen_charset_tables.py`). Native language names: `media/src/evo_langname.c`.
- Host preview has no HarfBuzz: Thai/Indic/CJK only show on the console.

## 7. If text comes out as OTHER strings

After a build, strings that show as OpenSSL names / OIDs, missing recents: check `readelf -lW output/app/.build/eboot.elf` - the R LOAD file offset must be a multiple of 64 KB (package-app.sh now dies if not; see tools/native-app/ps5-pie.ld). Do not blame the fonts or the image first.
