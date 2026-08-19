# Configure and build the Hydra C++ port.
#
#   .\build_cpp.ps1              # configure (if needed) + build Release
#   .\build_cpp.ps1 -Configure   # force a reconfigure first
#   .\build_cpp.ps1 -Target hydra_tests
#   .\build_cpp.ps1 -Package     # build, then zip a release (CPack)
#
# CMake and MSVC ship with Visual Studio, so this finds the VS-bundled cmake.exe
# via vswhere rather than requiring cmake on PATH.

param(
    [switch]$Configure,
    [switch]$Package,
    [string]$Target = "",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

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

$cmake = Find-CMake
Write-Host "Using cmake: $cmake"

$buildDir = Join-Path $root "build-cpp"
if ($Configure -or -not (Test-Path (Join-Path $buildDir "CMakeCache.txt"))) {
    & $cmake --preset default
    if ($LASTEXITCODE -ne 0) { throw "configure failed" }
}

$buildArgs = @("--build", "--preset", "default", "--config", $Config)
if ($Target -ne "") { $buildArgs += @("--target", $Target) }
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
