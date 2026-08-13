# -*- mode: python ; coding: utf-8 -*-

# Same app as HydraTest.spec, built as the uncapped edition. The entry script
# is hydra_app.py either way; the runtime hook is what sets the edition, since
# it runs before the app imports hydra.hymisc.

import os

# The native scoring core, if it has been built. It is optional: without it
# hydra.hynative falls back to pure Python, so a build on a machine with no
# C++ toolchain still produces a working exe. Bundled as a binary so it lands
# next to the exe's _internal, where hynative looks via sys._MEIPASS.
_native = os.path.join('native', 'build', 'hydra_score.dll')
_binaries = [(_native, '.')] if os.path.exists(_native) else []

a = Analysis(
    ['hydra_app.py'],
    pathex=[],
    binaries=_binaries,
    datas=[('resource', 'resource')],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=['hydra_uncapped_hook.py'],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='HydraUncapped',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=['resource/icon_app.ico'],
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='HydraUncapped',
)
