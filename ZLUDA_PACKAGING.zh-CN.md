# Minifox ZLUDA 自包含打包

## 用户需要做什么

1. 用 AMD 官方安装器正确安装 HIP SDK。安装器会设置系统 `HIP_PATH`。
2. 下载一个可正常使用的 NVIDIA/CUDA ComfyUI 整合包。
3. 把单文件 `Minifox ComfyUI Launcher.exe` 放到整合包根目录并启动。

用户不需要下载 ZLUDA、不需要选择 `gfx` 补丁、不需要编辑环境变量，也不需要
手工创建目录。

```text
ComfyUI-Package/
├─ Minifox ComfyUI Launcher.exe
├─ ComfyUI/
│  └─ main.py
└─ python/
   └─ python.exe
```

## 构建时内置内容

以下压缩包通过 Qt Resource 编入 Minifox EXE：

```text
zluda.extpack
tensile-gfx101x.extpack
tensile-gfx1031.extpack
tensile-gfx1032.extpack
tensile-gfx103x.extpack
tensile-gfx1103.extpack
tensile-gfx8xx-9xx.extpack
tensile-gfx906.extpack
tensile-gfx94x.extpack
```

原始资源位于 `assets/zluda`。构建产物不是依赖这些外部文件的散装目录；发布时只需
分发 EXE。

## AMD 首次启动

仅 AMD 机器在原始 PyTorch 无法识别 CUDA 设备后才进入 ZLUDA 引导：

1. 从系统 `HIP_PATH` 找到 HIP。
2. 运行 HIP 自带的 `hipInfo.exe`，读取实际 `gcnArchName`。
3. 只把内置 ZLUDA 和对应架构的 Tensile 包释放到整合包根目录的 `.minifox`。
4. 在 `.minifox` 内生成“HIP 同名文件优先、extpack 补充缺失文件”的 rocBLAS
   目录，不修改系统 HIP。
5. 创建 `.cache`、`.cache/zluda`、`.cache/triton` 和
   `.cache/torchinductor`。
6. 仅对 ComfyUI 子进程注入所需 DLL 路径、NVRTC 路径和 rocBLAS
   Tensile 路径，然后执行 ZLUDA 预检。
7. 在内存中为 `torch.utils.cpp_extension` 应用与绘世启动器
   `torch_zluda_timer` 相同的 Windows HIP 扩展补丁：
   `HIP_HOME = ROCM_HOME`，并允许 Windows 调用 HIP 扩展编译路径。
8. 仅在 ZLUDA 子进程中关闭不受支持的 cuDNN、Flash SDP 和
   memory-efficient SDP，保留 math SDP、Triton SageAttention 和 HIP/CK
   扩展。该兼容层不修改 ComfyUI、PyTorch 或系统 HIP 文件。

最终目录由启动器自动生成：

```text
ComfyUI-Package/
├─ .minifox/
│  ├─ packages/
│  │  ├─ zluda.extpack
│  │  ├─ zluda/
│  │  ├─ tensile-gfxXXXX.extpack
│  │  └─ tensile/gfxXXXX/
│  └─ runtime/
│     ├─ zluda/
│     ├─ python-bootstrap/
│     └─ rocblas/library/
└─ .cache/
   ├─ zluda/
   ├─ triton/
   └─ torchinductor/
```

已经释放且版本未变化的包不会重复解压。已有缓存会继续复用。

## NVIDIA 隔离

- NVIDIA 机器继续使用原始 CUDA/PyTorch，完全不释放或注入 ZLUDA。
- AMD 与 NVIDIA 混合机器也保持 NVIDIA 路径，不自动注入 ZLUDA。
- 只有“检测到 AMD 且没有 NVIDIA”的机器才允许自动回退到 ZLUDA。

## AMD 架构兼容性

启动器不会把单一补丁宣称为所有 AMD 通用。它会读取实际 `gfx` 并选择内置包：

- `gfx1010`—`gfx1012`
- `gfx1031`、`gfx1032`、`gfx1034`、`gfx1035`
- `gfx1103`
- `gfx803`、`gfx900`、`gfx906`
- `gfx940`—`gfx942`

未列出的架构如果系统 HIP 已带对应 Tensile 文件，则直接使用 HIP 提供的文件；
否则阻止启动并显示不支持的具体 `gfx`，不会套用错误补丁。

定制 Triton wheel 仍属于整合包的 Python 环境，应预装在
`python/Lib/site-packages`。Minifox 只负责把它的缓存指向包内 `.cache/triton`。
