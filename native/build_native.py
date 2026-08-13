"""Build Hydra's native scoring core into a shared library.

    python native/build_native.py

Produces native/build/hydra_score.dll (or .so/.dylib), which hydra.hynative
loads at import time. If no compiler is found this exits non-zero and says
what to install; the app still runs without the library, just in pure Python.

Deliberately does not require CMake -- the target is one translation unit, so
driving the compiler directly removes a dependency that would otherwise have
to be installed too. CMakeLists.txt is provided for editor/IDE integration.

Toolchains, in the order tried:
  * MSVC  -- located via vswhere, then through its own vcvars environment
  * g++   -- from PATH, including a portable MinGW that needs no admin
  * clang++

"""

import os
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
BUILD = HERE / "build"
SOURCES = [HERE / "hydra_score.cpp", HERE / "hydra_search.cpp"]

VSWHERE = pathlib.Path(
    os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"


def library_name():
    if sys.platform == "win32":
        return "hydra_score.dll"
    if sys.platform == "darwin":
        return "libhydra_score.dylib"
    return "libhydra_score.so"


def find_vcvars():
    """Return the vcvars64.bat of a Visual Studio with the C++ tools, or None."""
    if not VSWHERE.exists():
        return None
    try:
        out = subprocess.run(
            [str(VSWHERE), "-latest", "-products", "*",
             "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-property", "installationPath"],
            capture_output=True, text=True, check=False,
        ).stdout.strip()
    except OSError:
        return None

    if not out:
        return None

    vcvars = pathlib.Path(out) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    return vcvars if vcvars.exists() else None


def build_msvc(vcvars, out_path):
    # cl has to run inside the environment vcvars sets up, so the two are
    # chained in one cmd invocation rather than run separately.
    #
    # The /Fo path ends in a backslash, and doubling it is deliberate: cl reads
    # `\"` as an escaped quote and would otherwise swallow the source filename
    # that follows (D8003). Don't "simplify" it.
    sources = " ".join(f'"{s}"' for s in SOURCES)
    cmd = (
        f'"{vcvars}" >nul && cl /nologo /LD /O2 /EHsc /std:c++17 /W4 '
        f'/Fe:"{out_path}" /Fo:"{BUILD}\\\\" {sources}'
    )
    print("Building with MSVC...")

    # Passed as one command line rather than as an argument list. The list form
    # goes through subprocess.list2cmdline, which escapes the quotes above as
    # \" -- and cmd.exe reads those literally, so it looks for a program whose
    # name begins with a quote and the build dies before vcvars even runs.
    #
    # PATH is patched because vcvars64.bat invokes vswhere.exe bare, expecting
    # it on PATH. Visual Studio 2026 installs its own binaries under
    # Program Files\...\18\Community while the Installer -- and vswhere with it
    # -- stays under Program Files (x86). Without this, vcvars still completes
    # and cl still works, but it reports a spurious error on stderr on every
    # build, and a vcvars that relied on the vswhere result would not.
    env = dict(os.environ)
    if VSWHERE.exists():
        env["PATH"] = str(VSWHERE.parent) + os.pathsep + env.get("PATH", "")

    return subprocess.run(
        f'cmd /c "{cmd}"', cwd=str(BUILD), env=env
    ).returncode


def build_gnu(compiler, out_path):
    cmd = [
        compiler, "-O2", "-shared", "-std=c++17", "-Wall", "-Wextra",
        "-fPIC", "-o", str(out_path),
    ] + [str(s) for s in SOURCES]
    if sys.platform == "win32":
        # Avoid a runtime dependency on the MinGW DLLs, so the built library
        # works on a machine that has no toolchain installed.
        cmd += ["-static-libgcc", "-static-libstdc++"]
    print(f"Building with {compiler}...")
    return subprocess.run(cmd, cwd=str(BUILD)).returncode


def main():
    for source in SOURCES:
        if not source.exists():
            print(f"error: missing source {source}", file=sys.stderr)
            return 1

    BUILD.mkdir(parents=True, exist_ok=True)
    out_path = BUILD / library_name()

    vcvars = find_vcvars()
    if vcvars:
        rc = build_msvc(vcvars, out_path)
    else:
        compiler = shutil.which("g++") or shutil.which("clang++")
        if compiler:
            rc = build_gnu(compiler, out_path)
        else:
            print(
                "error: no C++ compiler found.\n"
                "\n"
                "Install one of:\n"
                "  * Visual Studio Build Tools 2022 (needs admin):\n"
                "      winget install Microsoft.VisualStudio.2022.BuildTools \\\n"
                "        --override \"--quiet --add "
                "Microsoft.VisualStudio.Workload.VCTools "
                "--includeRecommended\"\n"
                "  * WinLibs MinGW-w64 (portable, no admin): unzip it and put\n"
                "    its bin directory on PATH.\n"
                "\n"
                "Hydra runs without this library; analysis is just slower.",
                file=sys.stderr,
            )
            return 2

    if rc != 0:
        print(f"error: compiler exited {rc}", file=sys.stderr)
        return rc

    if not out_path.exists():
        print(f"error: build reported success but {out_path} is missing",
              file=sys.stderr)
        return 1

    print(f"built {out_path}")

    # Prove the fresh library actually loads and agrees with the Python, so a
    # broken build fails here rather than silently falling back at runtime.
    sys.path.insert(0, str(HERE.parent))
    import hydra.hynative as hynative
    if not hynative.AVAILABLE:
        print(f"error: built library did not load: {hynative.STATUS}",
              file=sys.stderr)
        return 1

    print(f"loaded and self-tested OK: {hynative.STATUS}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
