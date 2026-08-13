"""Rebuild Hydra's exe without destroying the app's stored records.

PyInstaller deletes the whole dist/<name> folder on every build, and the
app keeps its database in dist/<name>/_internal (hymisc.ROOTPATH is
sys._MEIPASS when frozen). Building directly with

    pyinstaller --noconfirm HydraTest.spec

therefore wipes every analyzed record. This script sets aside the user's
files first and puts them back afterwards.

    python build.py                 # HydraTest.exe, the normal app
    python build.py --uncapped      # HydraUncapped.exe, SP meter uncapped

The two builds are separate folders holding separate libraries, and either
can be rebuilt without touching the other's records.

"""

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BACKUP = ROOT / "dist" / "_userdata_backup"

# Everything the app writes rather than ships. Each edition writes its own
# set (see hymisc._EDITIONFILE), so which names to preserve depends on which
# edition is being rebuilt.
USER_FILES = {
    'standard': ["hyapp.db", "hyapp.ini", "records.json", "records.json.migrated"],
    'uncapped': ["hyapp_uncapped.db", "hyapp_uncapped.ini",
                 "records_uncapped.json", "records_uncapped.json.migrated"],
}

TARGETS = {
    'standard': ("HydraTest", "HydraTest.spec"),
    'uncapped': ("HydraUncapped", "HydraUncapped.spec"),
}


def stash(internal, user_files):
    saved = []
    if not internal.exists():
        return saved

    BACKUP.mkdir(parents=True, exist_ok=True)
    for name in user_files:
        src = internal / name
        if src.exists():
            shutil.copy2(src, BACKUP / name)
            saved.append(name)
    return saved


def restore(saved, internal):
    for name in saved:
        src = BACKUP / name
        if src.exists():
            shutil.copy2(src, internal / name)
    if BACKUP.exists():
        shutil.rmtree(BACKUP, ignore_errors=True)


def main(argv):
    edition = 'uncapped' if '--uncapped' in argv else 'standard'
    name, spec = TARGETS[edition]
    internal = ROOT / "dist" / name / "_internal"

    print(f"Building {name} ({edition} edition)")

    saved = stash(internal, USER_FILES[edition])
    if saved:
        print(f"Set aside {len(saved)} user file(s): {', '.join(saved)}")
    else:
        print("No existing user data to preserve.")

    result = subprocess.run(
        [sys.executable, "-m", "PyInstaller", "--noconfirm", str(ROOT / spec)],
        cwd=ROOT,
    )

    if result.returncode != 0:
        print(f"\nBuild failed ({result.returncode}); user data left in {BACKUP}")
        return result.returncode

    restore(saved, internal)
    if saved:
        print(f"\nRestored {len(saved)} user file(s) into {internal}")

    print("Build complete.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
