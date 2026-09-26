# Find cmake.exe: PATH first, else the copy bundled with Visual Studio.
# Shared by build_cpp.ps1 and installer\build_installer.ps1. Dot-source it:
#   . (Join-Path <repo> "tools\find_cmake.ps1"); $cmake = Find-CMake

function Find-CMake {
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        # -latest only reports instances in a "complete" state; a VS with a
        # pending update reports nothing there but still shows under -all.
        if (-not $vs) {
            $vs = & $vswhere -all -prerelease -products * -property installationPath
        }
        foreach ($path in @($vs)) {
            $candidate = Join-Path $path "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $candidate) { return $candidate }
        }
    }
    throw "cmake.exe not found (not on PATH and no Visual Studio C++ install located)."
}
