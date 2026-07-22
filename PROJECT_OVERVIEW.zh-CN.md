# Minifox ComfyUI Launcher 项目与构建说明

## 项目简介

Minifox ComfyUI Launcher 是面向 Windows 10/11 x64 的便携式 ComfyUI 启动器。它使用 C++ 后端管理启动配置、Python/ComfyUI 进程、日志和服务就绪状态，使用 Qt Quick/QML 提供 Windows 11 风格界面。

当前源码包含多配置启动、命令行参数编辑、环境变量与代理设置、CUDA 设备检测、版本管理、扩展管理、控制台日志、依赖预检、Windows Job Object 进程树清理，以及中英文界面。

## 技术栈与编译工具链

- 操作系统：Windows 10/11 x64
- 编译器：MSYS2 UCRT64 MinGW GCC，C++20
- 构建系统：CMake 3.22+、Ninja
- Qt：6.8+；已验证 Qt 6.11.1
- 已验证工具版本：GCC 16.1.0、CMake 4.4、Ninja
- Qt 模块：Core、Gui、Network、Qml、Quick、QuickControls2、LinguistTools、Test
- 发布方式：可选动态 Release，或使用静态 Qt 构建单文件 EXE

推荐使用 MSYS2 UCRT64 环境安装依赖：

```bash
pacman -Syu
pacman -S --needed \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-qt6-base \
  mingw-w64-ucrt-x86_64-qt6-declarative \
  mingw-w64-ucrt-x86_64-qt6-tools
```

生成无需 Qt DLL 的静态单文件版本，还需要：

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-qt6-static
```

## 构建方式

在 PowerShell 中进入仓库根目录后，可使用项目脚本：

```powershell
.\scripts\build.ps1 -Preset ucrt64-debug -RunTests
.\scripts\build.ps1 -Preset ucrt64-release
.\scripts\build.ps1 -Preset ucrt64-static-release
```

三个预设分别对应：

| 预设 | 用途 | 输出 |
| --- | --- | --- |
| `ucrt64-debug` | Debug、QML 检查和单元测试 | `build/debug/` |
| `ucrt64-release` | 动态链接正式版 | `build/release/` |
| `ucrt64-static-release` | 可复制的单文件发布版 | `build/Release/Minifox ComfyUI Launcher.exe` |

如果 MSYS2 不在 `C:\msys64`，可传入：

```powershell
.\scripts\build.ps1 -Preset ucrt64-static-release -Msys2Root "D:\msys2"
```

首次配置应使用目标 Qt 安装附带的 `qt-cmake.bat`。静态预设必须使用静态 Qt 的 `qt-cmake.bat`，不能用共享 Qt 配置。

## 源码结构

```text
app/                         应用入口、页面组合和应用级后端
core/configuration/          启动配置、参数目录和高级选项
core/application-settings/   主题、语言、代理及应用设置
core/runtime/                命令生成、依赖检查、进程、日志和控制台
shared/                      通用 C++ 能力、主题和 QML 控件
platform/windows/            Windows Job Object 等平台实现
extension-api/               扩展 API 边界
extensions/                  扩展目录说明
scripts/                     可复现构建脚本
.github/workflows/           GitHub Actions 编译与测试流程
```

业务依赖方向为：

```text
Minifox.App UI -> Configuration / Runtime / ApplicationSettings UI -> Minifox.Shared UI
app backend    -> core backends                                  -> shared / platform
```

QML 不直接访问配置文件或操作进程，页面通过 `appContext` 调用 C++ 后端。用户数据保存在 EXE 旁的隐藏 `.minifox/` 目录中。

## GitHub 覆盖说明

源码包不包含 `.git/`、`b/`、`build/`、Portable 目录、EXE、DLL、调试对象文件或本机缓存；这些内容属于本地构建产物。解压后可直接作为 GitHub 仓库源码使用，按上面的 MSYS2/Qt 工具链重新配置和编译。

项目许可证为 GPL-3.0-only，详见 `LICENSE`。
