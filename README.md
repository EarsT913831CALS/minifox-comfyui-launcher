<div align="center">
  <img src="app/frontend/Minifox/App/resources/app-icon-light.png" width="160" alt="Minifox ComfyUI Launcher">
  <h1>Minifox ComfyUI Launcher</h1>
  <p><strong>A portable, single-file ComfyUI launcher for Windows</strong></p>
  <p>English · <a href="README.zh-CN.md">简体中文</a></p>
  <p>
    <img src="https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-0078D4?logo=windows11&logoColor=white" alt="Windows 10/11 x64">
    <img src="https://img.shields.io/badge/Qt-6.8%2B-41CD52?logo=qt&logoColor=white" alt="Qt 6.8+">
    <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white" alt="C++20">
    <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--only-blue" alt="GPL-3.0-only"></a>
  </p>
</div>

Minifox is a portable ComfyUI launcher for Windows 10/11 x64, built specifically to configure, launch, and manage ComfyUI. It is developed with Qt 6, Qt Quick, C++20, QML, and CMake, and can be built as a single executable that does not require separate Qt DLLs.

Minifox does not use the Windows registry or modify the ComfyUI source tree. It does not embed or host the ComfyUI web interface. The launcher displays the service address and readiness state, while process control, logs, version management, and runtime setup remain focused on ComfyUI itself.

## Features

- Built-in defaults and multiple ComfyUI launch profiles, arguments, and runtime environments
- ComfyUI process control, status monitoring, and console output
- ComfyUI core and extension version management, updates, and rollback
- Detection of NVIDIA, AMD, hybrid GPU, ROCm, and eligible ZLUDA environments
- Themes, languages, fonts, application icons, custom skins, and home layouts

## Quick Start

Place `Minifox ComfyUI Launcher.exe` in the root of a portable ComfyUI package:

```text
ComfyUI-Package/
├─ Minifox ComfyUI Launcher.exe
├─ ComfyUI/
│  └─ main.py
└─ python/
   └─ python.exe
```

The launcher detects common portable directory layouts automatically. Python and ComfyUI paths can also be selected manually in a launch profile.

> Minifox launches and manages ComfyUI only. It does not host the ComfyUI web interface or open a browser automatically.

## GPU and ZLUDA

The launcher detects the installed GPUs first, then inspects the PyTorch backend in the portable package:

| Environment | Behavior |
|---|---|
| NVIDIA GPU | Uses the original CUDA/PyTorch environment |
| Hybrid NVIDIA and AMD GPUs | Keeps the NVIDIA CUDA path |
| AMD GPU with native ROCm PyTorch | Uses ROCm directly |
| AMD-only GPU with CUDA PyTorch | Prepares ZLUDA automatically |

ZLUDA is not extracted or injected on NVIDIA, hybrid-GPU, or native ROCm environments.

> **Compatibility:** HIP SDK 5.7 has been tested. Anything that uses CK (Composable Kernel) or MIOpen has not been tested and should not be considered supported or stable.

### AMD ZLUDA prerequisites

1. Install the HIP SDK with AMD's official installer and keep the `HIP_PATH` created by the installer.
2. HIP must contain rocBLAS/Tensile files for the GPU's `gfx` architecture:

   ```text
   <HIP_PATH>\bin\rocblas\library\
   ```

3. Use a ComfyUI portable package originally built for NVIDIA/CUDA.
4. If a workflow requires a custom Triton wheel, install it into the portable package's own Python environment.

Minifox bundles general-purpose ZLUDA runtime components. It does not include rocBLAS/Tensile patches for specific `gfx` architectures and does not maintain a limited GPU architecture allowlist. Actual architecture support depends on the contents of the installed HIP SDK.

When the ZLUDA path is selected, the launcher:

- Reads the actual `gcnArchName` returned by HIP
- Extracts ZLUDA under `.minifox` in the portable package
- Creates ZLUDA, Triton, and TorchInductor cache directories
- Injects ZLUDA, HIP, and cache variables into the ComfyUI child process only
- Applies the Windows HIP extension compatibility layer in memory
- Avoids unsupported cuDNN and SDP paths while retaining math SDP, Triton, and experimental, unverified HIP/CK extension paths

The launcher does not modify ComfyUI, PyTorch, HIP installation files, or system environment variables. Runtime packages are not extracted again when their version has not changed, and existing caches are reused.

## Version Management

The core view lists stable releases, development releases, remote branches, and historical commits. The extension view shows each installed extension's current branch, version, date, and remote repository.

- Refreshing lists reads remote information without modifying the working tree.
- Core version switching, branch switching, and one-click updates run `git reset --hard` and `git clean -ffd` first.
- Extension version switching presents commit descriptions, dates, and the current version without requiring a commit ID.
- Hold `Ctrl` and left-click a remote repository URL to open it in the default browser.

> **Warning:** Forced switching or updating discards modified files and removes untracked files and directories inside the affected repository. Back up anything that must be preserved.

## Portable Data

The following directories are created beside the executable when needed:

```text
.minifox/
├─ application-settings.json
├─ launch-profiles.json
├─ icons/
│  └─ custom.png
├─ skins/
├─ packages/
└─ runtime/
.cache/
├─ zluda/
├─ triton/
└─ torchinductor/
```

`.minifox` and `.cache` are marked as hidden directories. Contents under `.cache` are read and written only when ZLUDA is used; NVIDIA, native ROCm, and other non-ZLUDA launch paths do not use this directory.

## Build Requirements

- Windows 10/11 x64
- MSYS2 UCRT64 GCC with C++20 support
- CMake 3.25+ and Ninja
- Qt 6.8+: Core, Gui, Network, Qml, Quick, QuickControls2, LinguistTools, plus Test for test builds only

The project has been verified with Qt 6.11.1, GCC 16.1.0, CMake 4.4.0, and Ninja 1.13.2.

### Install dependencies

Open an **MSYS2 UCRT64** terminal and update the system:

```bash
pacman -Syu
```

If the terminal asks to restart, close it, open a new MSYS2 UCRT64 terminal, and install the toolchain and shared Qt dependencies:

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-qt6-base \
  mingw-w64-ucrt-x86_64-qt6-declarative \
  mingw-w64-ucrt-x86_64-qt6-tools
```

To build a single executable without separate Qt DLLs, also install static Qt and the static image-format dependencies:

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-qt6-static \
  mingw-w64-ucrt-x86_64-libwebp \
  mingw-w64-ucrt-x86_64-libtiff
```

Static Qt is large. Keep at least 4 GB of free disk space available.

## Build

Open Windows PowerShell in the repository root.

Debug build, QML lint, and unit tests:

```powershell
.\scripts\build.ps1 -Preset ucrt64-debug -RunTests
```

Shared-library Release:

```powershell
.\scripts\build.ps1 -Preset ucrt64-release
```

> The shared-library Release is a development build. It does not copy Qt DLLs, QML modules, or plugins automatically and cannot be distributed as a standalone executable. Use `windeployqt` separately when a shared-library deployment is required.

Single-file static Release:

```powershell
.\scripts\build.ps1 -Preset ucrt64-static-release
```

Static output:

```text
build/Release/Minifox ComfyUI Launcher.exe
```

If MSYS2 is not installed at `C:\msys64`, pass its location explicitly:

```powershell
.\scripts\build.ps1 -Preset ucrt64-static-release -Msys2Root "D:\msys64"
```

Alternatively, set an environment variable:

```powershell
$env:MINIFOX_MSYS2_ROOT = "D:\msys64"
.\scripts\build.ps1 -Preset ucrt64-debug -RunTests
```

If PowerShell blocks local scripts, bypass the execution policy for the current process only:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
```

### Manual build

Use the target Qt installation's `qt-cmake.bat` when configuring a build directory for the first time. The static preset must use the static Qt installation's `qt-cmake.bat`.

Shared-library Debug:

```powershell
$env:Path = "<MSYS2>\ucrt64\bin;$env:Path"
& "<MSYS2>\ucrt64\bin\qt-cmake.bat" --preset ucrt64-debug
cmake --build --preset ucrt64-debug
cmake --build --preset ucrt64-debug --target all_qmllint
ctest --preset ucrt64-debug
```

Single-file static Release:

```powershell
$env:Path = "<MSYS2>\ucrt64\bin;$env:Path"
& "<MSYS2>\ucrt64\qt6-static\bin\qt-cmake.bat" --preset ucrt64-static-release
cmake --build --preset ucrt64-static-release
```

| Preset | Purpose | Output |
|---|---|---|
| `ucrt64-debug` | Debug, QML lint, and unit tests | `build/debug/` |
| `ucrt64-release` | Shared-library Release | `build/release/` |
| `ucrt64-static-release` | Single-file static Release | `build/Release/` |

## Source Layout

```text
app/                         Application entry point, page composition, and app backend
core/configuration/          Launch profiles, argument catalog, and advanced options
core/application-settings/   Theme, language, proxy, and application settings
core/runtime/                Commands, dependency checks, processes, logs, and console
core/skin/                   Skins, asset import/export, and home layout
shared/                      Shared C++ facilities, theme, and QML controls
platform/windows/            Windows platform implementation
extension-api/               Extension API boundary
extensions/                  Extension directory documentation
assets/zluda/                Bundled general-purpose ZLUDA runtime package
scripts/                     Reproducible build entry points
.github/workflows/           GitHub Actions builds and tests
```

Dependency direction:

```text
Minifox.App UI -> Configuration / Runtime / ApplicationSettings UI -> Minifox.Shared UI
app backend    -> core backends                                  -> shared / platform
```

QML does not read or write configuration files or control processes directly. Pages access the C++ backend through `appContext`.

## Continuous Integration

GitHub Actions runs the following in a Windows UCRT64 environment:

1. Debug configuration and build
2. QML lint
3. C++ unit tests
4. Single-file static Release build verification

CI validates source code only. It does not upload or publish executables, DLLs, installers, or other build artifacts.

## Acknowledgements

- Thanks to the [ZLUDA project and community](https://github.com/vosen/ZLUDA) for their long-term work on running CUDA applications on non-NVIDIA GPUs.
- Thanks to Bilibili creator [秋葉aaaki](https://space.bilibili.com/12566101) and the Aki Launcher for their integration work, launcher experience, and contributions to the Chinese ComfyUI community.

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE) (`GPL-3.0-only`).

You may use, copy, modify, and distribute this project under the terms of GPLv3. Distributions of modified versions or derivative works must comply with GPLv3, including providing the corresponding source code to recipients and preserving the GPLv3 license notice. See [LICENSE](LICENSE) for the full terms.
