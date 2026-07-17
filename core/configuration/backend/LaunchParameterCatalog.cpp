#include "LaunchParameterCatalog.h"

#include <QCoreApplication>

#include <algorithm>
#include <tuple>

namespace {

QVariantMap option(const QString &value, const QString &label, const QString &argument = {})
{
    return {
        {QStringLiteral("value"), value},
        {QStringLiteral("label"), label},
        {QStringLiteral("argument"), argument}
    };
}

LaunchParameterDefinition parameter(
    const QString &key,
    const QString &category,
    const QString &title,
    const QString &description,
    const QString &control,
    const QString &mode,
    const QString &flag = {},
    const QVariant &defaultValue = {},
    const QVariantList &options = {},
    const QString &disableFlag = {},
    const QVariant &minimum = {},
    const QVariant &maximum = {})
{
    return {key, category, title, description, control, mode, flag, disableFlag,
            defaultValue, options, minimum, maximum};
}

QVariantList flagOptions(std::initializer_list<std::tuple<const char *, const char *, const char *>> values)
{
    QVariantList result;
    for (const auto &[value, label, argument] : values) {
        result.append(option(QString::fromLatin1(value), QString::fromUtf8(label), QString::fromLatin1(argument)));
    }
    return result;
}

} // namespace

QVariantMap LaunchParameterDefinition::toVariant(const QVariant &currentValue) const
{
    QString displayFlag = disableFlag.isEmpty()
        ? flag
        : QStringLiteral("%1 / %2").arg(flag, disableFlag);
    if (displayFlag.isEmpty()) {
        QStringList optionFlags;
        for (const auto &entry : options) {
            const QString argument = entry.toMap().value(QStringLiteral("argument")).toString();
            if (!argument.isEmpty() && !optionFlags.contains(argument)) {
                optionFlags.append(argument);
            }
        }
        displayFlag = optionFlags.join(QStringLiteral(" / "));
    }
    QVariantMap result {
        {QStringLiteral("key"), key},
        {QStringLiteral("category"), category},
        {QStringLiteral("title"), title},
        {QStringLiteral("description"), description},
        {QStringLiteral("control"), control},
        {QStringLiteral("flag"), displayFlag},
        {QStringLiteral("value"), currentValue.isValid() ? currentValue : defaultValue},
        {QStringLiteral("defaultValue"), defaultValue},
        {QStringLiteral("options"), options}
    };
    if (minimum.isValid()) {
        result.insert(QStringLiteral("minimum"), minimum);
    }
    if (maximum.isValid()) {
        result.insert(QStringLiteral("maximum"), maximum);
    }
    return result;
}

const QList<LaunchParameterDefinition> &LaunchParameterCatalog::parameters()
{
    static const QList<LaunchParameterDefinition> catalog {
        parameter("browser", "basic", "浏览器启动策略", "使用 ComfyUI 默认行为，或显式开启、关闭启动后自动打开浏览器。", "choice", "triState", "--auto-launch", "default",
                  flagOptions({{"default", "默认", ""}, {"enable", "自动打开", "--auto-launch"}, {"disable", "不自动打开", "--disable-auto-launch"}}), "--disable-auto-launch"),
        parameter("multiUser", "basic", "多用户模式", "为不同用户启用独立存储。", "switch", "switch", "--multi-user", false),
        parameter("manager", "basic", "启用 ComfyUI Manager", "启用 ComfyUI 内置的 Manager 功能。", "switch", "switch", "--enable-manager", false),
        parameter("managerUi", "basic", "Manager 界面模式", "选择默认界面、禁用界面端点或使用旧版界面。", "choice", "flagChoice", {}, "",
                  flagOptions({{"", "默认", ""}, {"disabled", "禁用界面", "--disable-manager-ui"}, {"legacy", "旧版界面", "--enable-manager-legacy-ui"}})),
        parameter("verbose", "basic", "日志级别", "设置 ComfyUI 输出的最低日志级别。", "choice", "value", "--verbose", "INFO",
                  flagOptions({{"DEBUG", "调试", ""}, {"INFO", "信息（默认）", ""}, {"WARNING", "警告", ""}, {"ERROR", "错误", ""}, {"CRITICAL", "严重错误", ""}})),
        parameter("logStdout", "basic", "常规日志写入 stdout", "将常规进程输出从 stderr 改为 stdout。", "switch", "switch", "--log-stdout", false),
        parameter("windowsStandalone", "basic", "Windows 独立包兼容模式", "启用 ComfyUI Windows 独立包的便利行为。", "switch", "switch", "--windows-standalone-build", false),

        parameter("listen", "network", "监听地址", "可填写单个地址或用逗号分隔多个地址。", "text", "value", "--listen", "127.0.0.1"),
        parameter("port", "network", "监听端口", "ComfyUI Web 服务使用的 TCP 端口。", "integer", "value", "--port", 8188, {}, {}, 1, 65535),
        parameter("tlsKeyfile", "network", "TLS 私钥文件", "与证书文件同时设置后启用 HTTPS。", "file", "value", "--tls-keyfile", ""),
        parameter("tlsCertfile", "network", "TLS 证书文件", "与私钥文件同时设置后启用 HTTPS。", "file", "value", "--tls-certfile", ""),
        parameter("corsOrigin", "network", "CORS 来源", "留空不启用；填写 * 允许所有来源。", "text", "value", "--enable-cors-header", ""),
        parameter("maxUploadSize", "network", "最大上传大小", "允许上传的最大文件大小，单位 MB。", "real", "value", "--max-upload-size", 100.0, {}, {}, 1.0, 1048576.0),
        parameter("compressResponse", "network", "压缩响应正文", "启用 HTTP 响应正文压缩。", "switch", "switch", "--enable-compress-response-body", false),
        parameter("comfyApiBase", "network", "Comfy API 地址", "覆盖 ComfyUI 使用的 API 基础地址。", "text", "value", "--comfy-api-base", "https://api.comfy.org"),
        parameter("databaseUrl", "network", "数据库 URL", "留空使用 ComfyUI 默认 SQLite 数据库。", "text", "value", "--database-url", ""),

        parameter("baseDirectory", "paths", "基础目录", "统一设置模型、节点、输入、输出、临时和用户目录的基础路径。", "folder", "value", "--base-directory", ""),
        parameter("modelsDirectory", "paths", "模型目录", "覆盖基础目录中的 models 目录。", "folder", "value", "--models-directory", ""),
        parameter("outputDirectory", "paths", "输出目录", "覆盖基础目录中的 output 目录。", "folder", "value", "--output-directory", ""),
        parameter("inputDirectory", "paths", "输入目录", "覆盖基础目录中的 input 目录。", "folder", "value", "--input-directory", ""),
        parameter("tempDirectory", "paths", "临时目录", "覆盖基础目录中的 temp 目录。", "folder", "value", "--temp-directory", ""),
        parameter("userDirectory", "paths", "用户目录", "覆盖基础目录中的 user 目录。", "folder", "value", "--user-directory", ""),
        parameter("extraModelPaths", "paths", "额外模型路径配置", "每行填写一个 extra_model_paths.yaml 文件。", "multiline", "repeat", "--extra-model-paths-config", ""),

        parameter("cudaDevice", "device", "CUDA 可见设备", "填写一个或多个用逗号分隔的 CUDA 设备编号。", "text", "value", "--cuda-device", ""),
        parameter("defaultDevice", "device", "默认设备", "指定默认设备编号，其他设备仍然可见。", "integerOptional", "value", "--default-device", "", {}, {}, 0, 128),
        parameter("directml", "device", "DirectML", "留空禁用；填写 auto 自动选择，或填写设备编号。", "text", "special", "--directml", ""),
        parameter("oneapiSelector", "device", "oneAPI 设备选择器", "设置 oneAPI 设备选择字符串。", "text", "value", "--oneapi-device-selector", ""),
        parameter("vramMode", "device", "显存模式", "选择 ComfyUI 的模型驻留和卸载策略。", "choice", "flagChoice", {}, "",
                  flagOptions({{"", "默认（动态显存）", ""}, {"gpu", "仅 GPU", "--gpu-only"}, {"high", "高显存", "--highvram"}, {"low", "低显存", "--lowvram"}, {"none", "极低显存", "--novram"}, {"cpu", "仅 CPU", "--cpu"}})),
        parameter("cudaMalloc", "device", "cudaMallocAsync", "显式开启、关闭，或保留 PyTorch 默认行为。", "choice", "triState", "--cuda-malloc", "default",
                  flagOptions({{"default", "默认", ""}, {"enable", "启用", "--cuda-malloc"}, {"disable", "禁用", "--disable-cuda-malloc"}}), "--disable-cuda-malloc"),
        parameter("tritonBackend", "device", "Triton 后端", "显式开启或强制关闭 comfy-kitchen Triton 后端。", "choice", "triState", "--enable-triton-backend", "default",
                  flagOptions({{"default", "默认", ""}, {"enable", "启用", "--enable-triton-backend"}, {"disable", "禁用", "--disable-triton-backend"}}), "--disable-triton-backend"),
        parameter("supportsFp8", "device", "声明支持 FP8 计算", "让 ComfyUI 按支持 FP8 计算的设备处理。", "switch", "switch", "--supports-fp8-compute", false),

        parameter("globalPrecision", "precision", "全局精度", "显式强制使用 FP32 或 FP16。", "choice", "flagChoice", {}, "",
                  flagOptions({{"", "默认", ""}, {"fp32", "FP32", "--force-fp32"}, {"fp16", "FP16", "--force-fp16"}})),
        parameter("unetPrecision", "precision", "扩散模型精度", "设置 UNet / 扩散模型权重精度。", "choice", "flagChoice", {}, "",
                  flagOptions({{"", "默认", ""}, {"fp64", "FP64", "--fp64-unet"}, {"fp32", "FP32", "--fp32-unet"}, {"bf16", "BF16", "--bf16-unet"}, {"fp16", "FP16", "--fp16-unet"}, {"fp8e4m3", "FP8 E4M3FN", "--fp8_e4m3fn-unet"}, {"fp8e5m2", "FP8 E5M2", "--fp8_e5m2-unet"}, {"fp8e8m0", "FP8 E8M0FNU", "--fp8_e8m0fnu-unet"}})),
        parameter("vaePrecision", "precision", "VAE 精度", "设置 VAE 的计算精度。", "choice", "flagChoice", {}, "",
                  flagOptions({{"", "默认", ""}, {"fp32", "FP32", "--fp32-vae"}, {"bf16", "BF16", "--bf16-vae"}, {"fp16", "FP16", "--fp16-vae"}})),
        parameter("textEncoderPrecision", "precision", "文本编码器精度", "设置文本编码器权重精度。", "choice", "flagChoice", {}, "",
                  flagOptions({{"", "默认", ""}, {"fp32", "FP32", "--fp32-text-enc"}, {"bf16", "BF16", "--bf16-text-enc"}, {"fp16", "FP16", "--fp16-text-enc"}, {"fp8e4m3", "FP8 E4M3FN", "--fp8_e4m3fn-text-enc"}, {"fp8e5m2", "FP8 E5M2", "--fp8_e5m2-text-enc"}})),
        parameter("cpuVae", "precision", "VAE 使用 CPU", "在 CPU 上运行 VAE。", "switch", "switch", "--cpu-vae", false),
        parameter("fp16Intermediates", "precision", "FP16 中间张量", "实验性地使用 FP16 节点间中间张量。", "switch", "switch", "--fp16-intermediates", false),
        parameter("channelsLast", "precision", "Channels Last", "推理时强制使用 channels-last 内存格式。", "switch", "switch", "--force-channels-last", false),

        parameter("cacheMode", "memory", "缓存模式", "选择默认缓存、RAM 压力缓存、经典缓存、LRU 或禁用缓存。", "choice", "special", {}, "",
                  flagOptions({{"", "默认", ""}, {"ram", "RAM 压力缓存", "--cache-ram"}, {"classic", "经典缓存", "--cache-classic"}, {"lru", "LRU 缓存", "--cache-lru"}, {"none", "禁用缓存", "--cache-none"}, {"highram", "高内存模式", "--high-ram"}})),
        parameter("cacheValue", "memory", "缓存参数", "RAM 模式填写一至两个 GB 阈值；LRU 模式填写最大节点结果数。", "text", "auxiliary", "--cache-ram / --cache-lru", ""),
        parameter("reserveVram", "memory", "系统保留显存", "为操作系统或其他软件保留的显存，单位 GB。", "realOptional", "value", "--reserve-vram", "", {}, {}, 0.0, 1024.0),
        parameter("vramHeadroom", "memory", "动态显存余量", "在默认值之上保持完全空闲的额外显存，单位 GB。", "real", "value", "--vram-headroom", 0.0, {}, {}, 0.0, 1024.0),
        parameter("dynamicVram", "memory", "动态显存", "显式启用或禁用动态显存。", "choice", "triState", "--enable-dynamic-vram", "default",
                  flagOptions({{"default", "默认", ""}, {"enable", "启用", "--enable-dynamic-vram"}, {"disable", "禁用", "--disable-dynamic-vram"}}), "--disable-dynamic-vram"),
        parameter("asyncOffload", "memory", "异步权重卸载", "显式启用、禁用或保留平台默认行为。", "choice", "special", {}, "default",
                  flagOptions({{"default", "默认", ""}, {"enable", "启用", "--async-offload"}, {"disable", "禁用", "--disable-async-offload"}})),
        parameter("asyncOffloadStreams", "memory", "异步卸载流数量", "启用异步卸载时使用；留空采用 ComfyUI 默认值 2。", "integerOptional", "auxiliary", "--async-offload", "", {}, {}, 1, 32),
        parameter("fastDisk", "memory", "快速磁盘卸载", "优先使用磁盘支持的动态加载和卸载。", "switch", "switch", "--fast-disk", false),
        parameter("disableSmartMemory", "memory", "禁用智能内存", "更积极地将模型卸载到常规内存。", "switch", "switch", "--disable-smart-memory", false),
        parameter("pinnedMemory", "memory", "固定内存策略", "默认使用固定内存，或显式禁用。", "choice", "flagChoice", {}, "",
                  flagOptions({{"", "默认", ""}, {"disable", "禁用固定内存", "--disable-pinned-memory"}})),
        parameter("mmap", "memory", "内存映射策略", "显式启用 torch 文件 mmap，或禁用 safetensors mmap。", "choice", "flagChoice", {}, "",
                  flagOptions({{"", "默认", ""}, {"enable", "启用 torch mmap", "--mmap-torch-files"}, {"disable", "禁用 mmap", "--disable-mmap"}})),

        parameter("attention", "performance", "注意力实现", "选择交叉注意力优化实现。", "choice", "flagChoice", {}, "",
                  flagOptions({{"", "默认", ""}, {"split", "Split", "--use-split-cross-attention"}, {"quad", "Sub-quadratic", "--use-quad-cross-attention"}, {"pytorch", "PyTorch 2", "--use-pytorch-cross-attention"}, {"sage", "Sage Attention", "--use-sage-attention"}, {"flash", "FlashAttention", "--use-flash-attention"}})),
        parameter("disableXformers", "performance", "禁用 xFormers", "强制不使用 xFormers。", "switch", "switch", "--disable-xformers", false),
        parameter("upcastAttention", "performance", "注意力 Upcast", "显式强制启用或禁用注意力 upcast。", "choice", "triState", "--force-upcast-attention", "default",
                  flagOptions({{"default", "默认", ""}, {"enable", "强制启用", "--force-upcast-attention"}, {"disable", "禁用", "--dont-upcast-attention"}}), "--dont-upcast-attention"),
        parameter("forceNonBlocking", "performance", "非阻塞操作", "对适用张量强制使用非阻塞操作。", "switch", "switch", "--force-non-blocking", false),
        parameter("hashFunction", "performance", "默认哈希算法", "选择重复文件和内容比较使用的哈希算法。", "choice", "value", "--default-hashing-function", "sha256",
                  flagOptions({{"md5", "MD5", ""}, {"sha1", "SHA-1", ""}, {"sha256", "SHA-256（默认）", ""}, {"sha512", "SHA-512", ""}})),
        parameter("deterministic", "performance", "确定性算法", "尽可能使用较慢的确定性 PyTorch 算法。", "switch", "switch", "--deterministic", false),
        parameter("fastFeatures", "performance", "实验性优化", "填写空格分隔的 fp16_accumulation、fp8_matrix_mult、cublas_ops、autotune；填写 all 启用全部。", "text", "special", "--fast", ""),

        parameter("previewMethod", "frontend", "采样预览方法", "设置采样节点的默认预览方式。", "choice", "value", "--preview-method", "none",
                  flagOptions({{"none", "无预览（默认）", ""}, {"auto", "自动", ""}, {"latent2rgb", "Latent2RGB", ""}, {"taesd", "TAESD", ""}})),
        parameter("previewSize", "frontend", "最大预览尺寸", "设置采样预览图的最大边长。", "integer", "value", "--preview-size", 512, {}, {}, 64, 8192),
        parameter("frontEndVersion", "frontend", "前端版本", "留空使用 ComfyUI 默认前端版本；也可填写 owner/repo@version。", "text", "value", "--front-end-version", ""),
        parameter("frontEndRoot", "frontend", "本地前端目录", "使用本地前端目录并覆盖前端版本。", "folder", "value", "--front-end-root", ""),
        parameter("enableAssets", "frontend", "Assets 系统", "启用资源 API、数据库同步和后台扫描。", "switch", "switch", "--enable-assets", false),
        parameter("assetHashing", "frontend", "资源内容哈希", "扫描资源时计算 BLAKE3 内容哈希。", "switch", "switch", "--enable-asset-hashing", false),
        parameter("featureFlags", "frontend", "功能标志", "每行填写一个 KEY 或 KEY=VALUE；参数会逐项传递。", "multiline", "repeat", "--feature-flag", ""),

        parameter("disableMetadata", "security", "禁用元数据", "不在生成文件中保存元数据。", "switch", "switch", "--disable-metadata", false),
        parameter("disableCustomNodes", "security", "禁用全部自定义节点", "启动时不加载任何自定义节点。", "switch", "switch", "--disable-all-custom-nodes", false),
        parameter("whitelistCustomNodes", "security", "自定义节点白名单", "禁用全部自定义节点时仍允许加载的文件夹；每行一项。", "multiline", "list", "--whitelist-custom-nodes", ""),
        parameter("disableApiNodes", "security", "禁用 API 节点", "禁用全部 API 节点，并阻止前端访问互联网。", "switch", "switch", "--disable-api-nodes", false),

        parameter("debugHang", "diagnostics", "挂起诊断", "启用 Ctrl+C 时的堆栈转储。", "switch", "switch", "--debug-hang", false),
        parameter("dontPrintServer", "diagnostics", "隐藏服务器输出", "禁止打印服务器输出。", "switch", "switch", "--dont-print-server", false),
        parameter("quickTest", "diagnostics", "CI 快速测试", "运行 ComfyUI 的 CI 快速测试模式。", "switch", "switch", "--quick-test-for-ci", false),
        parameter("listFeatureFlags", "diagnostics", "列出功能标志后退出", "打印可由命令行设置的功能标志 JSON，然后退出。", "switch", "switch", "--list-feature-flags", false)
    };
    return catalog;
}

QVariantList LaunchParameterCatalog::categories()
{
    return {
        QVariantMap{{"key", "basic"}, {"title", "常规"}, {"icon", "\uE713"}, {"description", "启动行为、Manager 与日志"}},
        QVariantMap{{"key", "network"}, {"title", "网络与服务"}, {"icon", "\uE774"}, {"description", "地址、端口、TLS 与 API"}},
        QVariantMap{{"key", "paths"}, {"title", "目录"}, {"icon", "\uE8B7"}, {"description", "模型、输入输出与用户目录"}},
        QVariantMap{{"key", "device"}, {"title", "设备"}, {"icon", "\uE950"}, {"description", "CUDA、DirectML 与显存模式"}},
        QVariantMap{{"key", "precision"}, {"title", "精度"}, {"icon", "\uE9D9"}, {"description", "模型、VAE 与编码器精度"}},
        QVariantMap{{"key", "memory"}, {"title", "内存与缓存"}, {"icon", "\uE964"}, {"description", "缓存、卸载与内存映射"}},
        QVariantMap{{"key", "performance"}, {"title", "性能"}, {"icon", "\uE9D2"}, {"description", "注意力、哈希与实验优化"}},
        QVariantMap{{"key", "frontend"}, {"title", "前端与预览"}, {"icon", "\uE7F4"}, {"description", "预览、前端版本与功能标志"}},
        QVariantMap{{"key", "security"}, {"title", "安全与节点"}, {"icon", "\uEA18"}, {"description", "元数据、自定义节点与 API"}},
        QVariantMap{{"key", "diagnostics"}, {"title", "诊断"}, {"icon", "\uEBE8"}, {"description", "调试、测试与诊断选项"}},
        QVariantMap{{"key", "environment"}, {"title", "环境变量"}, {"icon", "\uE756"}, {"description", "常用与自定义子进程环境"}},
        QVariantMap{{"key", "custom"}, {"title", "自定义参数"}, {"icon", "\uE943"}, {"description", "追加未覆盖的命令行参数"}}
    };
}

const LaunchParameterDefinition *LaunchParameterCatalog::find(const QString &key)
{
    const auto &catalog = parameters();
    const auto iterator = std::find_if(catalog.cbegin(), catalog.cend(), [&key](const auto &entry) {
        return entry.key == key;
    });
    return iterator == catalog.cend() ? nullptr : &(*iterator);
}
