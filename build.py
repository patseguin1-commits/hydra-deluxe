"""Rebuild HydraTest.exe without destroying the app's stored records.

PyInstaller deletes the whole dist/HydraTest folder on every build, and the
app keeps its database in dist/HydraTest/_internal (hymisc.ROOTPATH is
sys._MEIPASS when frozen). Building directly with

    pyinstaller --noconfirm HydraTest.spec

therefore wipes every analyzed record. This script sets aside the user's
files first and puts them back afterwards.

    python build.py

"""

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
INTERNAL = ROOT / "dist" / "HydraTest" / "_internal"
BACKUP = ROOT / "dist" / "_userdata_backup"

# Everything the app writes rather than ships.
USER_FILES = ["hyapp.db", "hyapp.ini", "records.json", "records.json.migrated"]


def stash():
    saved = []
    if not INTERNAL.exists():
        return saved

    BACKUP.mkdir(parents=True, exist_ok=True)
    for name in USER_FILES:
        src = INTERNAL / name
        if src.exists():
            shutil.copy2(src, BACKUP / name)
            saved.append(name)
    return saved


def restore(saved):
    for name in saved:
        src = BACKUP / name
        if src.exists():
            shutil.copy2(src, INTERNAL / name)
    if BACKUP.exists():
        shutil.rmtree(BACKUP, ignore_errors=True)


def main():
    saved = stash()
    if saved:
        print(f"Set aside {len(saved)} user file(s): {', '.join(saved)}")
    else:
        print("No existing user data to preserve.")

    result = subprocess.run(
        [sys.executable, "-m", "PyInstaller", "--noconfirm", str(ROOT / "HydraTest.spec")],
        cwd=ROOT,
    )

    if result.returncode != 0:
        print(f"\nBuild failed ({result.returncode}); user data left in {BACKUP}")
        return result.returncode

    restore(saved)
    if saved:
        print(f"\nRestored {len(saved)} user file(s) into {INTERNAL}")

    print("Build complete.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
