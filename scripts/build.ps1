[CmdletBinding()]
param(
    [ValidateSet("ucrt64-debug", "ucrt64-release", "ucrt64-static-release")]
    [string]$Preset = "ucrt64-static-release",

    [string]$Msys2Root = "C:\msys64",

    [switch]$RunTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($env:MINIFOX_MSYS2_ROOT -and -not $PSBoundParameters.ContainsKey("Msys2Root")) {
    $Msys2Root = $env:MINIFOX_MSYS2_ROOT
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$ucrt64Bin = Join-Path $Msys2Root "ucrt64\bin"
$cmake = Join-Path $ucrt64Bin "cmake.exe"
$ctest = Join-Path $ucrt64Bin "ctest.exe"
$ninja = Join-Path $ucrt64Bin "ninja.exe"
$compiler = Join-Path $ucrt64Bin "c++.exe"
$qtCmake = if ($Preset -eq "ucrt64-static-release") {
    Join-Path $Msys2Root "ucrt64\qt6-static\bin\qt-cmake.bat"
} else {
    Join-Path $ucrt64Bin "qt-cmake.bat"
}
$buildDirectory = switch ($Preset) {
    "ucrt64-debug" { Join-Path $projectRoot "build\debug" }
    "ucrt64-release" { Join-Path $projectRoot "build\release" }
    "ucrt64-static-release" { Join-Path $projectRoot "build\static" }
}

$requirements = [ordered]@{
    "CMake" = $cmake
    "Ninja" = $ninja
    "G++" = $compiler
    "Qt CMake" = $qtCmake
}

foreach ($requirement in $requirements.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $requirement.Value -PathType Leaf)) {
        throw "$($requirement.Key) was not found at '$($requirement.Value)'. See README.md for the required MSYS2 packages."
    }
}

if ($RunTests -and $Preset -ne "ucrt64-debug") {
    throw "-RunTests is supported by the ucrt64-debug preset only."
}

$originalPath = $env:Path
$env:Path = "$ucrt64Bin;$env:Path"

Push-Location $projectRoot
try {
    & $qtCmake --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed with exit code $LASTEXITCODE."
    }

    & $cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE."
    }

    if ($RunTests) {
        & $cmake --build --preset $Preset --target all_qmllint
        if ($LASTEXITCODE -ne 0) {
            throw "QML lint failed with exit code $LASTEXITCODE."
        }

        & $ctest --preset $Preset
        if ($LASTEXITCODE -ne 0) {
            throw "Tests failed with exit code $LASTEXITCODE."
        }
    }
} finally {
    Pop-Location
    $env:Path = $originalPath
}

if ($Preset -eq "ucrt64-static-release") {
    Write-Host "Static executable: $projectRoot\build\Release\Minifox ComfyUI Launcher.exe"
} else {
    Write-Host "Build directory: $buildDirectory"
}
