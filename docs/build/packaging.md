# Packaging and deploying

Build the NoStream app folder with `scripts/package-app.sh` and upload it to
`/data/homebrew/PPSA99273/` with `scripts/deploy-app.sh`. The older routes (homebrew bundles, a fake-SELF ELF
installed with `sceAppInstUtil`, hbldr launches) were dropped: they reach none of
the features that need an app module ([../architecture/app-module.md](../architecture/app-module.md)).
`scripts/package-pkg.sh` is a leftover from that era; it is not part of any
workflow, and a signed fPKG needs Sony's licensed `prospero-pub-cmd`, which this
repo does not have ([../proprietary.md](../proprietary.md)).

```bash
docker compose run --rm ps5-dev ./scripts/package-app.sh --usb-remote
docker compose run --rm ps5-dev ./scripts/deploy-app.sh    # upload the app folder
./tools/nostream-remote.sh launch      # launch the registered app folder
```

Never build or deploy by hand over a running NoStream. The close sequence and the
bounded hardware cycle are in [../console/hardware-cycle.md](../console/hardware-cycle.md).

## `package-app.sh`

| Flag | Effect |
|---|---|
| `--ffpfsc` | also PFS-pack the app folder into `output/app/PPSA99273.ffpfsc` with MkPFS |
| `--zip` | also write `PPSA99273.zip`, the folder-title archive (the layout Kodi PS5 uses) |
| `--usb-remote` | add the scriptable FTP dev remote (`nostream_cmd`, `nostream_status`, `nostream_ui.json`, verbose decoder log). **Always use it for testing; off in releases** |
| `--pro-memory` | set `attribute3` bit 22 in the packaged `param.json` only, so a PS5 Pro gives the app about 1.2 GiB more direct memory (#143) |
| `--virtual-keyboard` | debug: always use NoStream's own keyboard, never the system IME that the dev remote can neither press nor see |
| `--breadcrumbs` | pop each boot-trace line as an on-screen notification (off by default; klog carries the same lines) |
| `--probe` | build the sandbox probe instead of the player |
| `--rebuild-libc` | force-regenerate the clean-room `libc.prx` |
| `--no-native-secondary`, `--no-native-secondary-4k`, `--no-native-10bit` | rollback switches for the resident decoder set; the matching positive flags are no-ops kept for old scripts |
| `--agc` | accepted and redundant: bare-metal `sceAgc` is the only render path. `--gl`, `--no-gl`, `--gl-smoke` and `--gl-hdr-probe` were removed with the OpenGL path and fail with that explanation |

What the script does, in order: validate `param.json` (`titleId` and the version
format); build the native-app converter against static zlib; compile NoStream; patch a
build-local copy of `librmlui.a`; build the PRX import stubs from
`tools/native-app/stubs/prx/*.syms` (refusing dead entries); link; convert the
PIE into a PS5 module; sign it to `eboot.bin` (FSELF); assemble the app folder; and,
with `--ffpfsc`, pack it. The embedded UI assets are regenerated from `assets/`
on every build by `tools/bundle_rml_assets.py`.

Output:

```
output/app/PPSA99273/
├── eboot.bin                 FSELF-signed. Reads as opaque "data" to file/readelf;
│                             inspect it with `ps5-native-tool self --inspect`
├── sce_sys/{param.json, icon0.png, pic0.png, pic1.png}
├── sce_module/libc.prx
└── assets/                   loose copy of the embedded bundle (CI checks it is not empty)
output/app/PPSA99273.ffpfsc   the same files in a PFS image
output/app/.build/eboot.elf   the pre-sign ELF, the one to check with readelf
```

The folder and the image carry byte-identical `eboot.bin`, `param.json` and
`libc.prx`; the image is only a different container. MkPFS packs with
`--compression-backend zlib`: its default ISA-L streams made the console read some
64 KB blocks as the block before them (strings turned into OpenSSL names, Recent
came up empty, depending on the build's content). If that shows again, repack the
same folder before suspecting code.

## `deploy-app.sh`

```bash
PS5_HOST=<ip> ./scripts/deploy-app.sh    # upload the app folder
./scripts/deploy-app.sh --undeploy       # remove the staged title and active PFS image
```

- The folder uploader sends each file to a temporary name and renames it after
  FTP reports success. It sends `eboot.bin` and `sce_sys/param.json` last and
  moves an existing PFS image to a timestamped backup after the upload.
- On macOS, folder upload uses `/usr/bin/python3` directly. The owner closes
  NoStream before updating it and launches it after the upload.
- The legacy `--ffpfsc` uploader clears the console logs before transfer.
- **Legacy image deployment checks whether NoStream is resident.** It asks ShadowMount's
  `/data/shadowmount/debug.log` (via `tools/nostream_lifecycle.py`): started and not
  released means resident, a parked NoStream included. A heartbeat that is still moving
  is refused outright and `--force` does not override it. `--force` only applies
  when there is no heartbeat to read.
- Launch the registered folder title from the Media tab after the upload.
- `--fresh` goes through `tools/wipe-nostream.py`: the app, `/data/nostream` (settings,
  logins, playlists, calibration) and `/mnt/usb0/nostream*`. A dry run
  (`python3 tools/wipe-nostream.py <host>`) lists what would go; `--do` deletes. It never
  touches other homebrew, games or non-NoStream USB files.

## CI and releases

`.github/workflows/build.yml` and `release.yml` run the same container and scripts.
Release and versioning rules are in [release.md](release.md).
