# Configure and build the Hydra C++ port.
#
#   .\build_cpp.ps1              # configure (if needed) + build Release, with
#                                # hydra_bench and hydra_replay
#   .\build_cpp.ps1 -Configure   # force a reconfigure first
#   .\build_cpp.ps1 -Target hydra_tests
#   .\build_cpp.ps1 -Preset ship # the installer's build, in build-ship\
#   .\build_cpp.ps1 -Package     # build, then zip a release (CPack)
#
# CMake and MSVC ship with Visual Studio, so this finds the VS-bundled cmake.exe
# via vswhere rather than requiring cmake on PATH.

param(
    [switch]$Configure,
    [switch]$Package,
    [string]$Target = "",
    [string]$Config = "Release",
    # default: dev build in build-cpp. vs2022: the VS 2022 generator, in its
    # own folder. ship: the installer's build (no attached GUI tests).
    [ValidateSet("default", "vs2022", "ship")]
    [string]$Preset = "default"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

. (Join-Path $root "tools\find_cmake.ps1")
$cmake = Find-CMake
Write-Host "Using cmake: $cmake"

# Must match each preset's binaryDir in CMakePresets.json.
$buildDirs = @{ default = "build-cpp"; vs2022 = "build-cpp-vs2022"; ship = "build-ship" }
$buildDir = Join-Path $root $buildDirs[$Preset]
if ($Configure -or -not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
    & $cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "configure failed" }
}

$buildArgs = @("--build", "--preset", $Preset, "--config", $Config)
if ($Target -ne "") {
    $buildArgs += @("--target", $Target)
} else {
    # hydra_bench and hydra_replay are EXCLUDE_FROM_ALL (not shipped), so a
    # plain build names them too; otherwise they could break unnoticed.
    $buildArgs += @("--target", "ALL_BUILD", "hydra_bench", "hydra_replay")
}
& $cmake @buildArgs
if ($LASTEXITCODE -ne 0) { throw "build failed" }

Write-Host "Build succeeded. Artifacts in $buildDir\$Config\"

if ($Package) {
    # cpack.exe sits next to cmake.exe in the VS bundle.
    $cpack = Join-Path (Split-Path $cmake) "cpack.exe"
    Push-Location $buildDir
    try {
        & $cpack -G ZIP -C $Config
        if ($LASTEXITCODE -ne 0) { throw "cpack failed" }
    } finally {
        Pop-Location
    }
    Write-Host "Package written to $buildDir\package\"
}
