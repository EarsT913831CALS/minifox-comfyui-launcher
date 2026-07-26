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
- 自动识别 tqdm 风格动态输出，在控制台底部显示独立的 Windows 11 风格进度条
- 控制台支持滚轮、拖动滚动条和一键回到底部，滚动状态与悬浮按钮保持同步
- 管理 ComfyUI 内核分支和历史版本，支持稳定版、开发版、分支切换与一键更新
- 管理已安装扩展的启用状态、更新、历史版本切换和卸载，并可打开远程仓库
- 自动识别 NVIDIA、AMD 和混合显卡环境；仅在 AMD-only CUDA PyTorch 环境中引导 ZLUDA
- 内置通用 ZLUDA 运行包，自动创建便携运行目录和 ZLUDA、Triton、TorchInductor 缓存
- Fluent/Windows 11 风格界面，支持系统、浅色、深色主题及系统/自定义强调色
- 支持界面字体、字号、控制台字体、代理、语言和减少动态效果设置
- 启动器配置、便携运行数据和缓存仅保存在 EXE 所在整合包内，不写入注册表

## 支持范围

- 操作系统：Windows 10/11 x64
- 编译器：MSYS2 UCRT64 GCC
- 构建系统：CMake 3.22+ 与 Ninja
- Qt：6.8 或更高版本，包含 Core、Gui、Network、Qml、Quick、QuickControls2、LinguistTools 和 Test

本项目已使用 Qt 6.11.1、GCC 16.1.0、CMake 4.4 和 Ninja 完成验证。

## 直接使用

将单文件 `Minifox ComfyUI Launcher.exe` 放到 ComfyUI 整合包根目录。启动器可自动识别常见的 `ComfyUI/` 和 `python/` 便携目录，也可以在配置中手动指定路径。

```text
ComfyUI-Package/
├─ Minifox ComfyUI Launcher.exe
├─ ComfyUI/
│  └─ main.py
└─ python/
   └─ python.exe
```

### GPU 后端选择

启动器在进入首页前完成显卡和 PyTorch 后端判断，一键启动与手动启动使用相同结果：

| 系统环境 | 启动方式 |
|---|---|
| NVIDIA 显卡 | 保持原始 CUDA/PyTorch，不释放或注入 ZLUDA |
| NVIDIA + AMD 混合显卡 | 保持 NVIDIA CUDA 路径 |
| AMD 显卡 + 原生 ROCm PyTorch | 直接使用 ROCm，不进入 ZLUDA |
| 仅 AMD 显卡 + CUDA PyTorch | 自动准备并注入便携 ZLUDA 运行时 |

AMD ZLUDA 用户只需：

1. 使用 AMD 官方安装器安装 HIP SDK；安装器会设置 `HIP_PATH`。
2. 确保 `<HIP_PATH>/bin/rocblas/library` 中已有适用于本机 `gfx` 架构的 rocBLAS/Tensile 文件。
3. 使用原本面向 NVIDIA/CUDA 的 ComfyUI 整合包，并把 Minifox EXE 放到整合包根目录。

启动器会自动释放内置 ZLUDA、创建 `.minifox` 与 `.cache` 下的运行目录和缓存，并只向 ComfyUI 子进程注入环境。它不会修改 ComfyUI、PyTorch、系统 HIP 文件或系统环境变量。不同 `gfx` 架构的 rocBLAS/Tensile 补丁不随启动器分发，由 HIP 环境提供。详细打包与隔离规则见 [ZLUDA_PACKAGING.zh-CN.md](ZLUDA_PACKAGING.zh-CN.md)。

### 版本管理

- “刷新列表”只读取远程分支和提交，不改动工作目录。
- 切换内核版本、切换分支和一键更新会执行强制重置与清理，再切换到目标提交。
- 扩展版本切换会显示提交说明、日期和当前版本，不需要手工输入 commit ID。
- 远程仓库地址支持 `Ctrl + 左键` 在默认浏览器中打开。

> **注意：** 强制版本操作会丢弃仓库内已修改文件，并删除未跟踪文件和目录。需要保留的自定义内容请先备份。

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
| `ucrt64-debug` | 开发、调试和检查 | 开启 | `build/debug/` |
| `ucrt64-release` | 动态链接正式构建 | 关闭 | `build/release/` |
| `ucrt64-static-release` | 可发布的单文件构建 | 关闭 | `build/Release/` |

所有构建目录、运行数据和测试结果均被 Git 忽略，可以安全删除并重新生成。

## 项目结构

```text
app/
├── backend/                         程序入口、依赖注入和模块装配
└── frontend/
    ├── Minifox/App/                 主窗口、导航和应用级页面
    └── translations/                界面翻译
core/
├── application-settings/
│   ├── backend/                     启动器设置、外观和代理
│   └── frontend/Minifox/ApplicationSettings/
├── configuration/
│   ├── backend/                     启动配置和参数目录
│   └── frontend/Minifox/Configuration/
└── runtime/
    ├── backend/                     命令、进程、就绪监控和日志
    ├── frontend/Minifox/Runtime/
    └── tests/                       Runtime 单元测试
shared/
├── backend/                         无业务语义的通用 C++ 能力
└── frontend/Minifox/Shared/         主题和通用 QML 控件
platform/windows/                    Windows 平台实现
extension-api/                       扩展 API 边界说明
extensions/                          扩展目录说明
assets/zluda/                        内置通用 ZLUDA 运行包
scripts/                             可复现构建入口
.github/workflows/                   GitHub Actions 编译验证
```

项目保持垂直业务模块：每个模块在内部拆分 `backend` 与 `frontend`，测试也随模块放置。后端目标不依赖 QML；前端通过独立的 Qt QML 模块公开页面，通用控件统一来自 `Minifox.Shared`。`Minifox.App` 只负责组合页面和注入后端对象，不承载具体业务逻辑。

依赖方向固定为：

```text
Minifox.App UI -> Configuration / Runtime / ApplicationSettings UI -> Minifox.Shared UI
app backend    -> core backends                                  -> shared / platform
```

QML 不直接读写配置文件或操作进程，所有业务行为都通过 `appContext` 注入的 C++ 后端完成。

## 用户数据

程序首次运行时在 EXE 旁创建 `.minifox`。AMD ZLUDA 路径还会按需创建 `.cache`：

```text
.minifox/
├── application-settings.json
├── launch-profiles.json
├── packages/
└── runtime/
.cache/
├── zluda/
├── triton/
└── torchinductor/
```

这些目录在 Windows 中自动隐藏。程序不使用注册表或系统配置目录；请将 EXE 放在用户拥有写权限的位置。NVIDIA 和原生 ROCm 环境不会创建或注入 ZLUDA 运行时。

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
