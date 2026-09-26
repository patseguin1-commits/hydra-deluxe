# Build the Hydra Windows installer (Inno Setup).
#
#   .\installer\build_installer.ps1              # build Release, stage, compile setup.exe
#   .\installer\build_installer.ps1 -SkipBuild   # reuse the existing ship build
#
# Output: build-cpp\installer\Hydra-<version>-setup.exe
#
# It builds the "ship" preset in build-ship\, where Hydra.exe has no attached
# GUI tests and so no repo paths. The staging step uses `cmake --install`,
# never a glob of a Release folder: dev folders accumulate hydra*.db files
# (hundreds of MB) that must never ship. The install() rules in
# CMakeLists.txt define the exact ship list.
#
# Prerequisite: Inno Setup 6 (winget install -e --id JRSoftware.InnoSetup).

param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot                  # <repo>\installer
$repo = Split-Path $root               # <repo>

. (Join-Path $repo "tools\find_cmake.ps1")

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

# 1. Build Release with the ship preset.
if (-not $SkipBuild) {
    & (Join-Path $repo "build_cpp.ps1") -Preset ship
}
$build = Join-Path $repo "build-ship"

# 2. Version from the single source of truth in CMakeLists.txt.
$cmakeLists = Get-Content (Join-Path $repo "CMakeLists.txt") -Raw
$m = [regex]::Match($cmakeLists, 'project\(Hydra VERSION (\d+\.\d+\.\d+)')
if (-not $m.Success) { throw "could not parse the project version from CMakeLists.txt" }
$version = $m.Groups[1].Value
Write-Host "Hydra version: $version"

# 3. Stage the ship list into a clean dir via the install() rules.
$cmake = Find-CMake
$stage = Join-Path $build "stage"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
& $cmake --install $build --config Release --prefix $stage
if ($LASTEXITCODE -ne 0) { throw "cmake --install failed" }

# Guard the invariant: no user data may ever ship.
$leaked = Get-ChildItem $stage -Recurse -Include *.db, *_settings.ini, *_ui.ini
if ($leaked) { throw "user data leaked into the staging dir: $($leaked.FullName -join ', ')" }

# Guard the other invariant: the shipped exe holds no repo path at all, in
# either slash style. The attached GUI tests (left out by the ship preset)
# and __FILE__ (trimmed by /d1trimfile in CMakeLists.txt) were the sources.
$exeText = [Text.Encoding]::GetEncoding(28591).GetString(
    [IO.File]::ReadAllBytes((Join-Path $stage "Hydra.exe")))
foreach ($p in ($repo -replace '/', '\'), ($repo -replace '\\', '/')) {
    if ($exeText.IndexOf($p, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "Hydra.exe holds the repo path $p; build it with -Preset ship"
    }
}

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
