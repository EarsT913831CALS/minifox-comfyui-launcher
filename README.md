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

Minifox is a portable ComfyUI launcher for Windows 10/11 x64, built specifically to configure, launch, and manage ComfyUI. It is developed with Qt 6, Qt Quick, C++20, QML, and CMake, and can be built as a single executable that does not require separate Qt DLLs and can be placed directly into an existing ComfyUI portable package.

Minifox does not use the Windows Registry. Normal configuration and launch operations do not modify ComfyUI files; the launcher creates portable data and cache directories alongside the executable. ComfyUI files are changed only when the user explicitly runs a version update, version switch, extension install or removal, or cleanup operation.

## Features

- Switch seamlessly between multiple configurations with minimal preset launch arguments (fully managed by ComfyUI)
- Launch, stop, and monitor ComfyUI processes, status, and live console output
- Manage ComfyUI core and extension/custom node versions, featuring “Refresh List” and “Update All” buttons, while keeping local edits
- Automatically detect CUDA, ROCm, and eligible ZLUDA environments
- Personalize the home page with modular and customizable widgets

## Quick Start

Place `Minifox ComfyUI Launcher.exe` in a ComfyUI portable package directory and run it:

```text
ComfyUI-Package/
├── Minifox ComfyUI Launcher.exe
├── ComfyUI/
│   ├── main.py
│   └── ...
└── python/
    ├── python.exe
    └── ...
```

The launcher detects common portable directory layouts automatically. Python and ComfyUI paths can also be selected manually in a launch profile.

## GPU and ZLUDA

The launcher does not display environment information immediately after opening. When ComfyUI starts, it captures the logs and displays the relevant information on the home page card. You can see the detection logic in the console during startup:

| Environment | Behavior |
|---|---|
| CUDA or ROCm | Uses native PyTorch |
| AMD-only GPU with CUDA PyTorch | Prepares ZLUDA automatically |

ZLUDA injection has the lowest priority.

> **Compatibility:** HIP SDK 5.7 + ZLUDA has been tested. Anything that uses CK (Composable Kernel) or MIOpen has not been tested and should not be considered supported or stable.

> **Additional recommendation:** HIP SDK 7.1 + ZLUDA is supported, but its memory usage is less stable than the HIP SDK 5.7 combination. Native PyTorch (either a stable release or ROCm Preview 7.14 and later) is strongly recommended for AMD GPUs. If you use ZLUDA, pair it with a compatible Triton wheel for better operator compatibility and performance.

### AMD ZLUDA prerequisites

1. Install the HIP SDK using AMD's official installer (which will automatically set the HIP_PATH environment variable).
2. HIP must contain rocBLAS/Tensile files for the GPU's `gfx` architecture:

   ```text
   <HIP_PATH>\bin\rocblas\library\
   ```

   > [!NOTE]
   > If your GPU architecture is not officially supported, you will need to manually add the corresponding files to this directory.

3. Use a ComfyUI portable package originally intended for NVIDIA GPUs (PyTorch needs to be reinstalled to a compatible version).

Minifox bundles HIP SDK 5.7 and HIP SDK 7.1 ZLUDA runtime components. It does not include rocBLAS/Tensile patches for specific `gfx` architectures.

Outside explicit version-management operations and temporary ZLUDA staging, the launcher does not modify ComfyUI source files, HIP installation files, or system environment variables. ZLUDA acts as a runtime patch for PyTorch: it temporarily replaces certain required files and restores the originals when the launcher closes. Runtime packages are not re-extracted when their versions are unchanged, and existing caches are reused.

### UI rendering

The launcher UI renders with Direct3D 11 and turns off the flip-model swap chain (`QT_D3D_NO_FLIP=1`) by default. Some integrated AMD GPUs stutter badly while the window is resized in flip mode; without it, resizing is as smooth as with OpenGL, and the launcher keeps about 120 MB less memory resident than with OpenGL or Vulkan. If a graphics driver misbehaves, set `QSG_RHI_BACKEND=opengl` (or `vulkan`) before starting the launcher; values you set are never overridden.

## Version Management

The core view lists stable releases, development releases, remote branches, and historical commits. The extension view shows each installed extension's current branch, version, date, remote repository, and update status. Extensions can also be installed from the catalog or a Git URL, enabled or disabled, removed to the Recycle Bin, and switched to another version.

- Opening the page or switching its tabs reads only local information and the cached catalog. Nothing is fetched until you click "Refresh List"; updating something that has not been checked yet fetches first as part of that click. Refreshing never modifies the working tree.
- Updates and version switches keep local edits: they are captured into a private commit and merged with the target. Where a local change and the remote update touch the same lines, or the same file for deletions and binary files, the remote version is used and the replaced edits are backed up (see below). Requires Git 2.40 or later when there are local edits.
- Untracked files are left alone. An untracked file or folder that collides with a file in the target version (including case-only differences) is moved into the backup instead of failing the operation.
- "Discard edits…" on the local-changes row resets that repository to its current version after backing up the edited files.
- Full Cleanup (in Settings) discards local edits and moves every untracked file that Git does not ignore into the backup, leaving a clean working tree at the current revision.
- Each operation is journaled. If the launcher is interrupted, the next start rolls the repository back to where it was before the operation; ComfyUI cannot be launched until that succeeds, and the Version page offers a retry.
- After a change, dependencies are checked offline; pip runs in a visible console only when something is missing.
- Extension version switching presents commit descriptions, dates, and the current version without requiring a commit ID.
- Hold `Ctrl` and left-click a remote repository URL to open it in the default browser.

### Backup location

Whenever an operation replaces or discards local edits or moves untracked files, they are saved **in the directory above the ComfyUI folder**, alongside `ComfyUI/`:

```text
ComfyUI-Package/
├── ComfyUI/
└── backup/
    └── YYYY-MM-DD/
        └── <repository>-HHmmss/
            ├── files/            Local versions of replaced or discarded files
            ├── untracked/        Untracked files that were moved aside
            ├── conflicts.log     What was replaced and how to restore it
            └── conflicts.patch   Patch that re-applies the replaced edits
```

All local edits are also kept as a Git commit under `refs/minifox/backups/` in the affected repository. The 30 most recent date folders and the 20 most recent backup refs per repository are kept; older ones are removed automatically. Operation journals live in `.minifox/versioning/` beside the executable. These backups do not replace backups on a separate disk.

## Portable Data

The following directories are created beside the executable when needed:

```text
.minifox/
├─ application-settings.json
├─ launch-profiles.json
├─ custom-node-list.json
├─ icons/
│  └─ custom.png
├─ skins/
├─ packages/
├─ runtime/
└─ versioning/
.cache/
├─ zluda/
├─ triton/
└─ torchinductor/
```

`.minifox` and `.cache` are marked as hidden directories. Contents under `.cache` are read and written only when ZLUDA is used; NVIDIA, ROCm, and other non-ZLUDA launch paths do not use this directory.

## Build Requirements

- Windows 10/11 x64
- MSYS2 UCRT64 GCC with C++20 support
- CMake 3.25+ and Ninja
- Qt 6.8+: Core, Concurrent, Gui, Network, Qml, Quick, QuickControls2, LinguistTools, plus Test for test builds only

The project has been verified with Qt 6.12.0, GCC 16.2.0, CMake 4.4.4, and Ninja 1.13.2. For local development, MSVC 2022 with the official Qt 6.12.0 build can also build the project and run the tests.

### Install dependencies

Run the following in an **MSYS2 UCRT64** terminal:

```bash
pacman -Syu
```

If the terminal asks to restart, close it, open a new MSYS2 UCRT64 terminal, and install the dynamic Qt build dependencies:

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-qt6-base \
  mingw-w64-ucrt-x86_64-qt6-declarative \
  mingw-w64-ucrt-x86_64-qt6-tools
```

To build a single executable without separate Qt DLLs, also install static Qt:

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

Dynamic Release:

```powershell
.\scripts\build.ps1 -Preset ucrt64-release
```

> The dynamic Release is a development build. It does not copy Qt DLLs, QML modules, or plugins automatically and cannot be distributed as a standalone executable. Use `windeployqt` separately when a dynamic deployment is required.

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

Dynamic Debug:

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
| `ucrt64-release` | Dynamically linked Release | `build/release/` |
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

Generated build outputs, executables, portable runtime data, and local caches are excluded from version control. The repository keeps only source code, embedded assets, and reproducible build configuration.

## Continuous Integration

GitHub Actions runs the following in a Windows UCRT64 environment:

1. Debug configuration and build
2. QML lint
3. C++ unit tests
4. Single-file static Release build verification

Push and pull-request runs validate source code only and upload nothing. A manual run (`workflow_dispatch`) additionally uploads the single-file executable as a test artifact that expires after one day.

## Configuration and runtime package validation

The local launch command preview shows the full configured environment values and actual arguments. Exporting a profile offers two separate modes: Share Launch Options includes only preset options and numeric parameters, while Full Backup preserves the original profile, including environment variables and free-text arguments. Shared exports omit UI state and preserve the recipient's existing UI state when imported.

Configuration packages exchange only application settings, skin JSON, raster skin assets and the custom icon. Runtime code, package caches, version transactions and recovery backups are outside that data set. Old packages containing protected files, duplicate paths or Windows path aliases are rejected; re-export trusted configurations with the updated launcher. Nested skin assets and uppercase image extensions remain supported.

Profile switching saves `.minifox/configuration-state.pending.zip` before publishing data and the selected profile. After an interruption, startup restores the original configuration before loading settings. Failed recovery preserves the journal and stops startup. Keep the journal and the entire `.minifox` directory. Writes are flushed and read back, but physical power-loss testing has not been performed.

Dependency installation launches Python with an argument list and displays logs/errors in the launcher; no installation batch script is generated or executed. Version management runs Git with `-c safe.directory=*` so portable packages copied between drives or user accounts are not refused as "dubious ownership"; this applies only to the launcher's own Git processes and does not change your Git configuration.

Embedded ZLUDA archives and DLLs are checked against `assets/zluda/manifest.json`, including cached, extracted and loaded contents. Identical verified DLLs can be reused while open; unexpected files block startup. `MINIFOX_ZLUDA_DIR` accepts an explicitly selected absolute directory of user-trusted executable code, not an authenticated bundled package. Integrity checks do not prove upstream binary safety.

## Acknowledgements

- Thanks to the [ZLUDA project and community](https://github.com/vosen/ZLUDA) for their long-term work on running CUDA applications on non-NVIDIA GPUs.
- Thanks to Bilibili creator [秋葉aaaki](https://space.bilibili.com/12566101) and the Aki Launcher for their integration work, launcher experience, and contributions to the Chinese ComfyUI community.

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE) (`GPL-3.0-only`).

You may use, copy, modify, and distribute this project under the terms of GPLv3. Distributions of modified versions or derivative works must comply with GPLv3, including providing the corresponding source code to recipients and preserving the GPLv3 license notice. See [LICENSE](LICENSE) for the full terms.
