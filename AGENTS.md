# NoStream development workflow

For app changes, follow this order:

1. Implement the requested change and run the relevant local checks.
2. Sync changed source files into the existing `evoplayer_fork_workspace` Docker volume. Use `tar --touch -xf -` in the container when extracting the sync archive, or touch the changed sources after copying them, so Make rebuilds their objects. Reuse `evoplayer_fork_sdk`, `evoplayer_fork_ccache`, and `evoplayer_fork_ffmpeg`. Run `./scripts/package-app.sh` there and export the full `output/app/PPSA99273/` folder.
3. On macOS, run `PS5_HOST=192.168.1.23 FTP_PORT=2121 ./scripts/deploy-app.sh` to upload the folder with `/usr/bin/python3`. It verifies each file by SHA-256 before renaming it into `/data/homebrew/PPSA99273/` and moves any previous PFS image aside after the folder is verified. Stop after the upload. The user handles closing and launching the app, tests the build, and confirms it.
4. Commit only when the user explicitly asks after testing. Keep the changes uncommitted until then.

This is standing authorization for routine deployment to this PS5. If build or deployment is blocked, report the specific blocker and keep the changes uncommitted. This workflow takes precedence over the general hardware workflow in [CLAUDE.md](CLAUDE.md) and [the console docs](docs/console/hardware-cycle.md).
