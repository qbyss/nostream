#!/usr/bin/env python3
"""
tools/testenv.py up | down | status - put the console's test configs in place
and always get the real ones back.

`up` saves the console's real configs to output/testenv/backup/ (git-ignored:
they hold saved server logins), then writes the test setup:

                                           "could not connect" path, timed
  /data/nostream/evo_network_shares.json  the real servers, plus a dead FTP
                                           server ("TEST dead FTP")

`down` puts the saved files back byte for byte and removes the backup. `up`
refuses while a backup exists (the test setup is already up), so the real
configs can never be overwritten by a second `up`.

NoStream reads these files at start: run it with NoStream closed (nostream-remote.sh quit,
close), then launch. It refuses while NoStream's heartbeat is advancing.

Env: PS5_HOST (or .env), FTP_PORT (2121), TESTENV_DEAD_HOST (192.168.0.253 - an
address on the LAN that nothing answers).
"""
import io
import json
import os
import pathlib
import subprocess
import sys
from ftplib import FTP, error_perm

ROOT = pathlib.Path(__file__).resolve().parent.parent
BACKUP = ROOT / "output" / "testenv" / "backup"
SHARES_FILE = "/data/nostream/evo_network_shares.json"
LEGACY_PLAYLIST_FILE = "/data/nostream/iptv.conf"
MISSING = ".missing"   # marker: the file did not exist before `up`


def host():
    h = os.environ.get("PS5_HOST")
    if not h and (ROOT / ".env").exists():
        for line in (ROOT / ".env").read_text().splitlines():
            if line.startswith("PS5_HOST="):
                h = line.split("=", 1)[1].strip()
    if not h:
        sys.exit("testenv: PS5_HOST not set (.env)")
    return h


def ftp():
    f = FTP()
    f.connect(host(), int(os.environ.get("FTP_PORT", "2121")), timeout=15)
    f.login()
    f.set_pasv(True)
    return f


def get(f, path):
    buf = []
    try:
        f.retrbinary("RETR " + path, buf.append)
    except error_perm:
        return None
    return b"".join(buf)


def put(f, path, data):
    f.storbinary("STOR " + path, io.BytesIO(data))


def evo_running():
    env = dict(os.environ, PS5_HOST=host())
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "nostream_lifecycle.py"), "heartbeat"],
                       capture_output=True, text=True, env=env, timeout=60)
    return r.stdout.strip() == "RUNNING"


def local_name(path):
    return path.strip("/").replace("/", "__")


def up():
    if BACKUP.exists():
        sys.exit("testenv: already up (output/testenv/backup exists) - run `down` first")
    if evo_running():
        sys.exit("testenv: NoStream is running - quit and close it first (it reads these files at start)")
    dead = os.environ.get("TESTENV_DEAD_HOST", "192.168.0.253")
    with ftp() as f:
        saved = get(f, SHARES_FILE)
        BACKUP.mkdir(parents=True)
        backup = BACKUP / local_name(SHARES_FILE)
        if saved is None:
            backup.with_name(backup.name + MISSING).write_bytes(b"")
        else:
            backup.write_bytes(saved)

        shares = json.loads(saved or b"[]")
        shares = [s for s in shares if s.get("id") != "testenv_dead_ftp"]
        shares.append({"id": "testenv_dead_ftp", "name": "TEST dead FTP", "protocol": "ftp",
                       "host": dead, "port": 21, "user": "anonymous", "password": "anonymous",
                       "startPath": "/"})
        put(f, SHARES_FILE, json.dumps(shares, indent=2).encode())
    print(f"  ok test setup up (dead host {dead}); real configs saved in output/testenv/backup")
    print("     launch NoStream; put them back with: tools/testenv.py down (NoStream closed)")


def down():
    if not BACKUP.exists():
        print("  ok nothing to restore (test setup is not up)")
        return
    if evo_running():
        sys.exit("testenv: NoStream is running - quit and close it first, or it may save over the restore")
    with ftp() as f:
        for p in (SHARES_FILE, LEGACY_PLAYLIST_FILE):
            saved = BACKUP / local_name(p)
            if saved.exists():
                put(f, p, saved.read_bytes())
                if get(f, p) != saved.read_bytes():
                    sys.exit(f"testenv: {p} did not read back the same - backup kept in {BACKUP}")
            elif (BACKUP / (local_name(p) + MISSING)).exists():
                try:
                    f.delete(p)
                except error_perm:
                    pass
    for child in BACKUP.iterdir():
        child.unlink()
    BACKUP.rmdir()
    print("  ok real configs restored")


def status():
    print("UP" if BACKUP.exists() else "DOWN")


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    {"up": up, "down": down, "status": status}.get(cmd, lambda: sys.exit(__doc__))()
