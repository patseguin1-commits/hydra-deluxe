# Build the Hydra Windows installer (Inno Setup).
#
#   .\installer\build_installer.ps1              # build Release, stage, compile setup.exe
#   .\installer\build_installer.ps1 -SkipBuild   # reuse the existing Release build
#
# Output: build-cpp\installer\Hydra-<version>-setup.exe
#
# The staging step uses `cmake --install`, never a glob of build-cpp\Release:
# that folder accumulates dev hydra*.db files (hundreds of MB) that must never
# ship. The install() rules in CMakeLists.txt define the exact ship list.
#
# Prerequisite: Inno Setup 6 (winget install -e --id JRSoftware.InnoSetup).

param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot                  # <repo>\installer
$repo = Split-Path $root               # <repo>

# Same lookup as build_cpp.ps1: prefer PATH, else the VS-bundled cmake.
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

function Find-ISCC {
    $onPath = Get-Command iscc -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:ProgramFiles "Inno Setup 6\ISCC.exe"),
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe")
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    throw "ISCC.exe not found. Install Inno Setup: winget install -e --id JRSoftware.InnoSetup"
}

# 1. Build Release.
if (-not $SkipBuild) {
    & (Join-Path $repo "build_cpp.ps1")
}

# 2. Version from the single source of truth in CMakeLists.txt.
$cmakeLists = Get-Content (Join-Path $repo "CMakeLists.txt") -Raw
$m = [regex]::Match($cmakeLists, 'project\(Hydra VERSION (\d+\.\d+\.\d+)')
if (-not $m.Success) { throw "could not parse the project version from CMakeLists.txt" }
$version = $m.Groups[1].Value
Write-Host "Hydra version: $version"

# 3. Stage the ship list into a clean dir via the install() rules.
$cmake = Find-CMake
$stage = Join-Path $repo "build-cpp\stage"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
& $cmake --install (Join-Path $repo "build-cpp") --config Release --prefix $stage
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed" }

# Guard the invariant: no user data may ever ship.
$leaked = Get-ChildItem $stage -Recurse -Include *.db, *_settings.ini, *_ui.ini
if ($leaked) { throw "user data leaked into the staging dir: $($leaked.FullName -join ', ')" }

# 4. VC++ x64 redistributable (chained by the installer). Cached out of git.
$redistDir = Join-Path $root "redist"
$redist = Join-Path $redistDir "VC_redist.x64.exe"
if (-not (Test-Path $redist)) {
    New-Item -ItemType Directory -Force $redistDir | Out-Null
    Write-Host "Downloading VC_redist.x64.exe..."
    Invoke-WebRequest "https://aka.ms/vs/17/release/vc_redist.x64.exe" -OutFile $redist
}

# 5. Compile the installer.
$iscc = Find-ISCC
Write-Host "Using ISCC: $iscc"
& $iscc "/DHYDRA_VERSION=$version" "/DHYDRA_STAGE=$stage" "/DHYDRA_REDIST=$redistDir" `
    (Join-Path $root "hydra.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed" }

Write-Host "Installer written to build-cpp\installer\Hydra-$version-setup.exe"
