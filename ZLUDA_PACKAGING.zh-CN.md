# Minifox ZLUDA 自包含打包

## 用户需要准备什么

1. 使用 AMD 官方安装器正确安装 HIP SDK。安装器会设置系统 `HIP_PATH`。
2. 确保 HIP 中已经安装适用于本机 `gfx` 架构的 rocBLAS/Tensile 文件。
3. 下载一个可正常使用的 NVIDIA/CUDA ComfyUI 整合包。
4. 把单文件 `Minifox ComfyUI Launcher.exe` 放到整合包根目录并启动。

用户不需要另外下载 ZLUDA，也不需要手工设置 ZLUDA 环境变量或创建缓存目录。不同显卡需要的架构补丁不随启动器分发，应由 HIP 环境提供。

```text
ComfyUI-Package/
├─ Minifox ComfyUI Launcher.exe
├─ ComfyUI/
│  └─ main.py
└─ python/
   └─ python.exe
```

## 构建时内置内容

只有通用的 `zluda.extpack` 通过 Qt Resource 编入 Minifox EXE。原始资源位于 `assets/zluda`。

`tensile-gfx*.extpack` 不属于启动器资源，也不会提交到仓库。`gfx1103`、`gfx903` 等不同架构所需的 rocBLAS/Tensile 文件由用户的 HIP SDK 提供，位置为：

```text
<HIP_PATH>/bin/rocblas/library/
```

## AMD 首次启动

仅 AMD 机器在原始 PyTorch 无法识别 CUDA 设备后才进入 ZLUDA 引导：

1. 从系统 `HIP_PATH` 找到 HIP。
2. 运行 HIP 自带的 `hipInfo.exe`，读取实际 `gcnArchName`，用于诊断提示。
3. 检查 `<HIP_PATH>/bin/rocblas/library` 是否存在且包含库文件，不维护有限的 gfx 白名单。
4. 把内置 ZLUDA 释放到整合包根目录的 `.minifox`。
5. 创建 `.cache`、`.cache/zluda`、`.cache/triton` 和 `.cache/torchinductor`。
6. 仅对 ComfyUI 子进程注入所需 DLL 路径、NVRTC 路径和 HIP 的 rocBLAS/Tensile 路径。
7. 在内存中为 `torch.utils.cpp_extension` 应用 Windows HIP 扩展兼容层，不修改 PyTorch 文件。
8. 仅在 ZLUDA 子进程中关闭不受支持的 cuDNN、Flash SDP 和 memory-efficient SDP，保留 math SDP、Triton SageAttention 和 HIP/CK 扩展。

最终目录由启动器自动生成：

```text
ComfyUI-Package/
├─ .minifox/
│  ├─ packages/
│  │  ├─ zluda.extpack
│  │  └─ zluda/
│  └─ runtime/
│     ├─ zluda/
│     └─ python-bootstrap/
└─ .cache/
   ├─ zluda/
   ├─ triton/
   └─ torchinductor/
```

已经释放且版本未变化的 ZLUDA 包不会重复解压，已有缓存会继续复用。

## NVIDIA 隔离

- NVIDIA 机器继续使用原始 CUDA/PyTorch，完全不释放或注入 ZLUDA。
- AMD 与 NVIDIA 混合机器也保持 NVIDIA 路径，不自动注入 ZLUDA。
- 只有“检测到 AMD 且没有 NVIDIA”的机器才允许自动回退到 ZLUDA。
- 原生 ROCm PyTorch 直接使用 ROCm，不进入 ZLUDA。

## AMD 架构兼容性

启动器不会把某个补丁声明为所有 AMD 通用，也不会维护 `gfx` 到补丁包的映射。它读取实际架构用于错误提示，并使用 HIP 已安装的 rocBLAS/Tensile 库。因此能否运行某个 `gfx`，取决于用户的 HIP 目录中是否已经放入该架构所需文件。

定制 Triton wheel 仍属于整合包的 Python 环境，应预装在 `python/Lib/site-packages`。Minifox 只负责把缓存指向包内 `.cache/triton`。
