# Minifox ComfyUI Launcher

Minifox 是面向 Windows 10/11 的便携式 ComfyUI 启动器，使用 Qt 6、Qt Quick、C++20、QML 和 CMake 开发。

## 功能

- 创建、复制、重命名和删除多套 ComfyUI 启动配置
- 自动保存 Python、ComfyUI 路径、命令行参数、环境变量和应用设置
- 分类编辑 ComfyUI 参数，支持布尔、三态、枚举、数值、路径及自定义参数
- 生成完整启动命令，并自动隐藏敏感环境变量
- 使用 `QProcess` 管理 ComfyUI，显示 PID、运行时间、服务地址和就绪状态
- 使用 Windows Job Object 管理子进程树
- 实时显示 stdout/stderr，支持 UTF-8、ANSI 前景色、时间戳、自动换行和日志导出
- Fluent/Windows 11 风格界面，支持系统、浅色、深色主题及系统/自定义强调色
- 支持界面字体、字号、控制台字体、代理、语言和减少动态效果设置
- 所有用户数据仅保存在 EXE 旁的隐藏目录 `.minifox`

## 支持范围

- 操作系统：Windows 10/11 x64
- 编译器：MSYS2 UCRT64 GCC
- 构建系统：CMake 3.22+ 与 Ninja
- Qt：6.8 或更高版本，包含 Core、Gui、Network、Qml、Quick、QuickControls2、LinguistTools 和 Test

本项目已使用 Qt 6.11.1、GCC 16.1.0、CMake 4.4 和 Ninja 完成验证。

## 1. 安装构建依赖

安装 [MSYS2](https://www.msys2.org/)，打开“MSYS2 UCRT64”终端并更新系统：

```bash
pacman -Syu
```

如果终端要求重启，请关闭后重新打开 UCRT64 终端，再安装开发和动态 Qt 依赖：

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-qt6-base \
  mingw-w64-ucrt-x86_64-qt6-declarative \
  mingw-w64-ucrt-x86_64-qt6-tools
```

如需生成无需 DLL 的单文件 EXE，再安装静态 Qt：

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-qt6-static
```

静态 Qt 的安装体积较大，请预留至少 4 GB 空间。

## 2. 一键构建

在 Windows PowerShell 中进入仓库目录。

调试版、QML 检查和单元测试：

```powershell
.\scripts\build.ps1 -Preset ucrt64-debug -RunTests
```

动态 Release：

```powershell
.\scripts\build.ps1 -Preset ucrt64-release
```

单文件静态 Release：

```powershell
.\scripts\build.ps1 -Preset ucrt64-static-release
```

静态版最终生成在：

```text
build/Release/Minifox ComfyUI Launcher.exe
```

如果 MSYS2 不在 `C:\msys64`，可以传入安装目录：

```powershell
.\scripts\build.ps1 -Preset ucrt64-static-release -Msys2Root "D:\msys64"
```

也可以设置环境变量，GitHub Actions 使用的也是这一入口：

```powershell
$env:MINIFOX_MSYS2_ROOT = "D:\msys64"
.\scripts\build.ps1 -Preset ucrt64-debug -RunTests
```

若 PowerShell 阻止本地脚本，可仅为当前进程放行：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
```

## 3. 手动构建

`CMakePresets.json` 不包含用户名、盘符或固定 MSYS2 安装路径。请使用目标 Qt 自带的 `qt-cmake` 完成首次配置。

动态 Debug：

```powershell
$env:Path = "<MSYS2>\ucrt64\bin;$env:Path"
& "<MSYS2>\ucrt64\bin\qt-cmake.bat" --preset ucrt64-debug
cmake --build --preset ucrt64-debug
ctest --preset ucrt64-debug
```

单文件静态 Release：

```powershell
$env:Path = "<MSYS2>\ucrt64\bin;$env:Path"
& "<MSYS2>\ucrt64\qt6-static\bin\qt-cmake.bat" --preset ucrt64-static-release
cmake --build --preset ucrt64-static-release
```

首次配置后可以直接重复执行相应的 `cmake --build --preset ...` 进行增量构建。

## 构建预设

| 预设 | 用途 | 测试 | 输出 |
|---|---|---:|---|
| `ucrt64-debug` | 开发、调试和检查 | 开启 | `build/ucrt64-debug/` |
| `ucrt64-release` | 动态链接正式构建 | 关闭 | `build/ucrt64-release/` |
| `ucrt64-static-release` | 可发布的单文件构建 | 关闭 | `build/Release/` |

所有构建目录、运行数据和测试结果均被 Git 忽略，可以安全删除并重新生成。

## 项目结构

```text
app/                         程序入口、依赖注入、翻译和 QML
core/application-settings/   启动器设置与系统外观
core/configuration/          启动配置与 ComfyUI 参数目录
core/runtime/                命令构建、进程、就绪监控和日志
platform/windows/            Windows Job Object 进程树管理
shared/                      便携路径等通用能力
extension-api/               扩展 API 边界说明
extensions/                  扩展目录说明
tests/                       C++ 单元测试
scripts/                     可复现构建入口
.github/workflows/           GitHub Actions 编译验证
```

QML 不直接读写配置文件或操作进程，业务逻辑由注入的 C++ 后端负责。

## 用户数据

程序首次运行时在 EXE 旁创建：

```text
.minifox/
├── application-settings.json
└── launch-profiles.json
```

该目录在 Windows 中自动隐藏。程序不使用注册表或系统配置目录；请将 EXE 放在用户拥有写权限的位置。

## 持续集成

`.github/workflows/build.yml` 会在 Windows UCRT64 环境中执行：

1. Debug 配置与编译
2. QML 静态检查
3. C++ 单元测试
4. 单文件静态 Release 编译验证

CI 仅用于验证源码能够成功编译，不上传或发布 EXE、DLL、安装包等二进制构建产物。

## 许可证

本项目采用 [GNU General Public License v3.0](LICENSE)（`GPL-3.0-only`）发布。

你可以使用、复制、修改和分发本项目。分发本项目的修改版或基于本项目的衍生作品时，必须遵守 GPLv3，包括向接收者提供相应源码并保留 GPLv3 授权。完整条款见 [LICENSE](LICENSE)。
