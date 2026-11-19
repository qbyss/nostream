# NoStream development workflow

For app changes, follow this order:

1. Implement the requested change and run the relevant local checks.
2. Sync changed source files into the existing `evoplayer_fork_workspace` Docker volume. Reuse `evoplayer_fork_sdk`, `evoplayer_fork_ccache`, and `evoplayer_fork_ffmpeg`. Run `./scripts/package-app.sh --ffpfsc` there and export `output/app/PPSA99273.ffpfsc`.
3. On macOS, use `/usr/bin/python3` and the FTP upload sequence from `scripts/deploy-app.sh` to send the image to `192.168.1.23:2121`. Upload under a temporary name, read it back to verify SHA-256, then rename it to `/data/homebrew/PPSA99273.ffpfsc`. Upload directly and stop after the verified rename. The user handles closing and launching the app, tests the build, and confirms it.
4. Commit only when the user explicitly asks after testing. Keep the changes uncommitted until then.

This is standing authorization for routine deployment to this PS5. If build or deployment is blocked, report the specific blocker and keep the changes uncommitted. This workflow takes precedence over the general hardware workflow in [CLAUDE.md](CLAUDE.md) and [the console docs](docs/console/hardware-cycle.md).
