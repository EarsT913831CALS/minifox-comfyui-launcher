#include "LaunchParameterCatalog.h"

#include <QCoreApplication>

#include <algorithm>
#include <tuple>

namespace {

QString catalogText(const char *source)
{
    return QString::fromUtf8(source);
}

QString translatedCatalogText(const QString &source)
{
    const QByteArray sourceUtf8 = source.toUtf8();
    return QCoreApplication::translate("LaunchParameterCatalog", sourceUtf8.constData());
}

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

QVariantList flagOptions(std::initializer_list<std::tuple<const char *, QString, const char *>> values)
{
    QVariantList result;
    for (const auto &[value, label, argument] : values) {
        result.append(option(QString::fromLatin1(value), label, QString::fromLatin1(argument)));
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
    QVariantList localizedOptions;
    localizedOptions.reserve(options.size());
    for (const auto &entry : options) {
        QVariantMap localizedOption = entry.toMap();
        localizedOption.insert(
            QStringLiteral("label"),
            translatedCatalogText(localizedOption.value(QStringLiteral("label")).toString()));
        localizedOptions.append(localizedOption);
    }

    QVariantMap result {
        {QStringLiteral("key"), key},
        {QStringLiteral("category"), category},
        {QStringLiteral("title"), translatedCatalogText(title)},
        {QStringLiteral("description"), translatedCatalogText(description)},
        {QStringLiteral("control"), control},
        {QStringLiteral("flag"), displayFlag},
        {QStringLiteral("value"), currentValue.isValid() ? currentValue : defaultValue},
        {QStringLiteral("defaultValue"), defaultValue},
        {QStringLiteral("options"), localizedOptions}
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
        parameter("browser", "basic", catalogText("浏览器启动策略"), catalogText("使用 ComfyUI 默认行为，或显式开启、关闭启动后自动打开浏览器。"), "choice", "triState", "--auto-launch", "default",
                  flagOptions({{"default", catalogText("默认"), ""}, {"enable", catalogText("自动打开"), "--auto-launch"}, {"disable", catalogText("不自动打开"), "--disable-auto-launch"}}), "--disable-auto-launch"),
        parameter("multiUser", "basic", catalogText("多用户模式"), catalogText("为不同用户启用独立存储。"), "switch", "switch", "--multi-user", false),
        parameter("manager", "basic", catalogText("启用 ComfyUI Manager"), catalogText("启用 ComfyUI 内置的 Manager 功能。"), "switch", "switch", "--enable-manager", false),
        parameter("managerUi", "basic", catalogText("Manager 界面模式"), catalogText("选择默认界面、禁用界面端点或使用旧版界面。"), "choice", "flagChoice", {}, "",
                  flagOptions({{"", catalogText("默认"), ""}, {"disabled", catalogText("禁用界面"), "--disable-manager-ui"}, {"legacy", catalogText("旧版界面"), "--enable-manager-legacy-ui"}})),
        parameter("verbose", "basic", catalogText("日志级别"), catalogText("设置 ComfyUI 输出的最低日志级别。"), "choice", "value", "--verbose", "INFO",
                  flagOptions({{"DEBUG", catalogText("调试"), ""}, {"INFO", catalogText("信息（默认）"), ""}, {"WARNING", catalogText("警告"), ""}, {"ERROR", catalogText("错误"), ""}, {"CRITICAL", catalogText("严重错误"), ""}})),
        parameter("logStdout", "basic", catalogText("常规日志写入 stdout"), catalogText("将常规进程输出从 stderr 改为 stdout。"), "switch", "switch", "--log-stdout", false),
        parameter("windowsStandalone", "basic", catalogText("Windows 独立包兼容模式"), catalogText("启用 ComfyUI Windows 独立包的便利行为。"), "switch", "switch", "--windows-standalone-build", false),

        parameter("listen", "network", catalogText("监听地址"), catalogText("可填写单个地址或用逗号分隔多个地址。"), "text", "value", "--listen", "127.0.0.1"),
        parameter("port", "network", catalogText("监听端口"), catalogText("ComfyUI Web 服务使用的 TCP 端口。"), "integer", "value", "--port", 8188, {}, {}, 1, 65535),
        parameter("tlsKeyfile", "network", catalogText("TLS 私钥文件"), catalogText("与证书文件同时设置后启用 HTTPS。"), "file", "value", "--tls-keyfile", ""),
        parameter("tlsCertfile", "network", catalogText("TLS 证书文件"), catalogText("与私钥文件同时设置后启用 HTTPS。"), "file", "value", "--tls-certfile", ""),
        parameter("corsOrigin", "network", catalogText("CORS 来源"), catalogText("留空不启用；填写 * 允许所有来源。"), "text", "value", "--enable-cors-header", ""),
        parameter("maxUploadSize", "network", catalogText("最大上传大小"), catalogText("允许上传的最大文件大小，单位 MB。"), "real", "value", "--max-upload-size", 100.0, {}, {}, 1.0, 1048576.0),
        parameter("compressResponse", "network", catalogText("压缩响应正文"), catalogText("启用 HTTP 响应正文压缩。"), "switch", "switch", "--enable-compress-response-body", false),
        parameter("comfyApiBase", "network", catalogText("Comfy API 地址"), catalogText("覆盖 ComfyUI 使用的 API 基础地址。"), "text", "value", "--comfy-api-base", "https://api.comfy.org"),
        parameter("databaseUrl", "network", catalogText("数据库 URL"), catalogText("留空使用 ComfyUI 默认 SQLite 数据库。"), "text", "value", "--database-url", ""),

        parameter("baseDirectory", "paths", catalogText("基础目录"), catalogText("统一设置模型、节点、输入、输出、临时和用户目录的基础路径。"), "folder", "value", "--base-directory", ""),
        parameter("modelsDirectory", "paths", catalogText("模型目录"), catalogText("覆盖基础目录中的 models 目录。"), "folder", "value", "--models-directory", ""),
        parameter("outputDirectory", "paths", catalogText("输出目录"), catalogText("覆盖基础目录中的 output 目录。"), "folder", "value", "--output-directory", ""),
        parameter("inputDirectory", "paths", catalogText("输入目录"), catalogText("覆盖基础目录中的 input 目录。"), "folder", "value", "--input-directory", ""),
        parameter("tempDirectory", "paths", catalogText("临时目录"), catalogText("覆盖基础目录中的 temp 目录。"), "folder", "value", "--temp-directory", ""),
        parameter("userDirectory", "paths", catalogText("用户目录"), catalogText("覆盖基础目录中的 user 目录。"), "folder", "value", "--user-directory", ""),
        parameter("extraModelPaths", "paths", catalogText("额外模型路径配置"), catalogText("每行填写一个 extra_model_paths.yaml 文件。"), "multiline", "repeat", "--extra-model-paths-config", ""),

        parameter("cudaDevice", "device", catalogText("CUDA 可见设备"), catalogText("填写一个或多个用逗号分隔的 CUDA 设备编号。"), "text", "value", "--cuda-device", ""),
        parameter("defaultDevice", "device", catalogText("默认设备"), catalogText("指定默认设备编号，其他设备仍然可见。"), "integerOptional", "value", "--default-device", "", {}, {}, 0, 128),
        parameter("directml", "device", catalogText("DirectML"), catalogText("留空禁用；填写 auto 自动选择，或填写设备编号。"), "text", "special", "--directml", ""),
        parameter("oneapiSelector", "device", catalogText("oneAPI 设备选择器"), catalogText("设置 oneAPI 设备选择字符串。"), "text", "value", "--oneapi-device-selector", ""),
        parameter("vramMode", "device", catalogText("显存模式"), catalogText("选择 ComfyUI 的模型驻留和卸载策略。"), "choice", "flagChoice", {}, "",
                  flagOptions({{"", catalogText("默认（动态显存）"), ""}, {"gpu", catalogText("仅 GPU"), "--gpu-only"}, {"high", catalogText("高显存"), "--highvram"}, {"low", catalogText("低显存"), "--lowvram"}, {"none", catalogText("极低显存"), "--novram"}, {"cpu", catalogText("仅 CPU"), "--cpu"}})),
        parameter("cudaMalloc", "device", catalogText("cudaMallocAsync"), catalogText("显式开启、关闭，或保留 PyTorch 默认行为。"), "choice", "triState", "--cuda-malloc", "default",
                  flagOptions({{"default", catalogText("默认"), ""}, {"enable", catalogText("启用"), "--cuda-malloc"}, {"disable", catalogText("禁用"), "--disable-cuda-malloc"}}), "--disable-cuda-malloc"),
        parameter("tritonBackend", "device", catalogText("Triton 后端"), catalogText("显式开启或强制关闭 comfy-kitchen Triton 后端。"), "choice", "triState", "--enable-triton-backend", "default",
                  flagOptions({{"default", catalogText("默认"), ""}, {"enable", catalogText("启用"), "--enable-triton-backend"}, {"disable", catalogText("禁用"), "--disable-triton-backend"}}), "--disable-triton-backend"),
        parameter("supportsFp8", "device", catalogText("声明支持 FP8 计算"), catalogText("让 ComfyUI 按支持 FP8 计算的设备处理。"), "switch", "switch", "--supports-fp8-compute", false),

        parameter("globalPrecision", "precision", catalogText("全局精度"), catalogText("显式强制使用 FP32 或 FP16。"), "choice", "flagChoice", {}, "",
                  flagOptions({{"", catalogText("默认"), ""}, {"fp32", catalogText("FP32"), "--force-fp32"}, {"fp16", catalogText("FP16"), "--force-fp16"}})),
        parameter("unetPrecision", "precision", catalogText("扩散模型精度"), catalogText("设置 UNet / 扩散模型权重精度。"), "choice", "flagChoice", {}, "",
                  flagOptions({{"", catalogText("默认"), ""}, {"fp64", catalogText("FP64"), "--fp64-unet"}, {"fp32", catalogText("FP32"), "--fp32-unet"}, {"bf16", catalogText("BF16"), "--bf16-unet"}, {"fp16", catalogText("FP16"), "--fp16-unet"}, {"fp8e4m3", catalogText("FP8 E4M3FN"), "--fp8_e4m3fn-unet"}, {"fp8e5m2", catalogText("FP8 E5M2"), "--fp8_e5m2-unet"}, {"fp8e8m0", catalogText("FP8 E8M0FNU"), "--fp8_e8m0fnu-unet"}})),
        parameter("vaePrecision", "precision", catalogText("VAE 精度"), catalogText("设置 VAE 的计算精度。"), "choice", "flagChoice", {}, "",
                  flagOptions({{"", catalogText("默认"), ""}, {"fp32", catalogText("FP32"), "--fp32-vae"}, {"bf16", catalogText("BF16"), "--bf16-vae"}, {"fp16", catalogText("FP16"), "--fp16-vae"}})),
        parameter("textEncoderPrecision", "precision", catalogText("文本编码器精度"), catalogText("设置文本编码器权重精度。"), "choice", "flagChoice", {}, "",
                  flagOptions({{"", catalogText("默认"), ""}, {"fp32", catalogText("FP32"), "--fp32-text-enc"}, {"bf16", catalogText("BF16"), "--bf16-text-enc"}, {"fp16", catalogText("FP16"), "--fp16-text-enc"}, {"fp8e4m3", catalogText("FP8 E4M3FN"), "--fp8_e4m3fn-text-enc"}, {"fp8e5m2", catalogText("FP8 E5M2"), "--fp8_e5m2-text-enc"}})),
        parameter("cpuVae", "precision", catalogText("VAE 使用 CPU"), catalogText("在 CPU 上运行 VAE。"), "switch", "switch", "--cpu-vae", false),
        parameter("fp16Intermediates", "precision", catalogText("FP16 中间张量"), catalogText("实验性地使用 FP16 节点间中间张量。"), "switch", "switch", "--fp16-intermediates", false),
        parameter("channelsLast", "precision", catalogText("Channels Last"), catalogText("推理时强制使用 channels-last 内存格式。"), "switch", "switch", "--force-channels-last", false),

        parameter("cacheMode", "memory", catalogText("缓存模式"), catalogText("选择默认缓存、RAM 压力缓存、经典缓存、LRU 或禁用缓存。"), "choice", "special", {}, "",
                  flagOptions({{"", catalogText("默认"), ""}, {"ram", catalogText("RAM 压力缓存"), "--cache-ram"}, {"classic", catalogText("经典缓存"), "--cache-classic"}, {"lru", catalogText("LRU 缓存"), "--cache-lru"}, {"none", catalogText("禁用缓存"), "--cache-none"}, {"highram", catalogText("高内存模式"), "--high-ram"}})),
        parameter("cacheValue", "memory", catalogText("缓存参数"), catalogText("RAM 模式填写一至两个 GB 阈值；LRU 模式填写最大节点结果数。"), "text", "auxiliary", "--cache-ram / --cache-lru", ""),
        parameter("reserveVram", "memory", catalogText("系统保留显存"), catalogText("为操作系统或其他软件保留的显存，单位 GB。"), "realOptional", "value", "--reserve-vram", "", {}, {}, 0.0, 1024.0),
        parameter("vramHeadroom", "memory", catalogText("动态显存余量"), catalogText("在默认值之上保持完全空闲的额外显存，单位 GB。"), "real", "value", "--vram-headroom", 0.0, {}, {}, 0.0, 1024.0),
        parameter("dynamicVram", "memory", catalogText("动态显存"), catalogText("显式启用或禁用动态显存。"), "choice", "triState", "--enable-dynamic-vram", "default",
                  flagOptions({{"default", catalogText("默认"), ""}, {"enable", catalogText("启用"), "--enable-dynamic-vram"}, {"disable", catalogText("禁用"), "--disable-dynamic-vram"}}), "--disable-dynamic-vram"),
        parameter("asyncOffload", "memory", catalogText("异步权重卸载"), catalogText("显式启用、禁用或保留平台默认行为。"), "choice", "special", {}, "default",
                  flagOptions({{"default", catalogText("默认"), ""}, {"enable", catalogText("启用"), "--async-offload"}, {"disable", catalogText("禁用"), "--disable-async-offload"}})),
        parameter("asyncOffloadStreams", "memory", catalogText("异步卸载流数量"), catalogText("启用异步卸载时使用；留空采用 ComfyUI 默认值 2。"), "integerOptional", "auxiliary", "--async-offload", "", {}, {}, 1, 32),
        parameter("fastDisk", "memory", catalogText("快速磁盘卸载"), catalogText("优先使用磁盘支持的动态加载和卸载。"), "switch", "switch", "--fast-disk", false),
        parameter("disableSmartMemory", "memory", catalogText("禁用智能内存"), catalogText("更积极地将模型卸载到常规内存。"), "switch", "switch", "--disable-smart-memory", false),
        parameter("pinnedMemory", "memory", catalogText("固定内存策略"), catalogText("默认使用固定内存，或显式禁用。"), "choice", "flagChoice", {}, "",
                  flagOptions({{"", catalogText("默认"), ""}, {"disable", catalogText("禁用固定内存"), "--disable-pinned-memory"}})),
        parameter("mmap", "memory", catalogText("内存映射策略"), catalogText("显式启用 torch 文件 mmap，或禁用 safetensors mmap。"), "choice", "flagChoice", {}, "",
                  flagOptions({{"", catalogText("默认"), ""}, {"enable", catalogText("启用 torch mmap"), "--mmap-torch-files"}, {"disable", catalogText("禁用 mmap"), "--disable-mmap"}})),

        parameter("attention", "performance", catalogText("注意力实现"), catalogText("选择交叉注意力优化实现。"), "choice", "flagChoice", {}, "",
                  flagOptions({{"", catalogText("默认"), ""}, {"split", catalogText("Split"), "--use-split-cross-attention"}, {"quad", catalogText("Sub-quadratic"), "--use-quad-cross-attention"}, {"pytorch", catalogText("PyTorch 2"), "--use-pytorch-cross-attention"}, {"sage", catalogText("Sage Attention"), "--use-sage-attention"}, {"flash", catalogText("FlashAttention"), "--use-flash-attention"}})),
        parameter("disableXformers", "performance", catalogText("禁用 xFormers"), catalogText("强制不使用 xFormers。"), "switch", "switch", "--disable-xformers", false),
        parameter("upcastAttention", "performance", catalogText("注意力 Upcast"), catalogText("显式强制启用或禁用注意力 upcast。"), "choice", "triState", "--force-upcast-attention", "default",
                  flagOptions({{"default", catalogText("默认"), ""}, {"enable", catalogText("强制启用"), "--force-upcast-attention"}, {"disable", catalogText("禁用"), "--dont-upcast-attention"}}), "--dont-upcast-attention"),
        parameter("forceNonBlocking", "performance", catalogText("非阻塞操作"), catalogText("对适用张量强制使用非阻塞操作。"), "switch", "switch", "--force-non-blocking", false),
        parameter("hashFunction", "performance", catalogText("默认哈希算法"), catalogText("选择重复文件和内容比较使用的哈希算法。"), "choice", "value", "--default-hashing-function", "sha256",
                  flagOptions({{"md5", catalogText("MD5"), ""}, {"sha1", catalogText("SHA-1"), ""}, {"sha256", catalogText("SHA-256（默认）"), ""}, {"sha512", catalogText("SHA-512"), ""}})),
        parameter("deterministic", "performance", catalogText("确定性算法"), catalogText("尽可能使用较慢的确定性 PyTorch 算法。"), "switch", "switch", "--deterministic", false),
        parameter("fastFeatures", "performance", catalogText("实验性优化"), catalogText("填写空格分隔的 fp16_accumulation、fp8_matrix_mult、cublas_ops、autotune；填写 all 启用全部。"), "text", "special", "--fast", ""),

        parameter("previewMethod", "frontend", catalogText("采样预览方法"), catalogText("设置采样节点的默认预览方式。"), "choice", "value", "--preview-method", "none",
                  flagOptions({{"none", catalogText("无预览（默认）"), ""}, {"auto", catalogText("自动"), ""}, {"latent2rgb", catalogText("Latent2RGB"), ""}, {"taesd", catalogText("TAESD"), ""}})),
        parameter("previewSize", "frontend", catalogText("最大预览尺寸"), catalogText("设置采样预览图的最大边长。"), "integer", "value", "--preview-size", 512, {}, {}, 64, 8192),
        parameter("frontEndVersion", "frontend", catalogText("前端版本"), catalogText("留空使用 ComfyUI 默认前端版本；也可填写 owner/repo@version。"), "text", "value", "--front-end-version", ""),
        parameter("frontEndRoot", "frontend", catalogText("本地前端目录"), catalogText("使用本地前端目录并覆盖前端版本。"), "folder", "value", "--front-end-root", ""),
        parameter("enableAssets", "frontend", catalogText("Assets 系统"), catalogText("启用资源 API、数据库同步和后台扫描。"), "switch", "switch", "--enable-assets", false),
        parameter("assetHashing", "frontend", catalogText("资源内容哈希"), catalogText("扫描资源时计算 BLAKE3 内容哈希。"), "switch", "switch", "--enable-asset-hashing", false),
        parameter("featureFlags", "frontend", catalogText("功能标志"), catalogText("每行填写一个 KEY 或 KEY=VALUE；参数会逐项传递。"), "multiline", "repeat", "--feature-flag", ""),

        parameter("disableMetadata", "security", catalogText("禁用元数据"), catalogText("不在生成文件中保存元数据。"), "switch", "switch", "--disable-metadata", false),
        parameter("disableCustomNodes", "security", catalogText("禁用全部自定义节点"), catalogText("启动时不加载任何自定义节点。"), "switch", "switch", "--disable-all-custom-nodes", false),
        parameter("whitelistCustomNodes", "security", catalogText("自定义节点白名单"), catalogText("禁用全部自定义节点时仍允许加载的文件夹；每行一项。"), "multiline", "list", "--whitelist-custom-nodes", ""),
        parameter("disableApiNodes", "security", catalogText("禁用 API 节点"), catalogText("禁用全部 API 节点，并阻止前端访问互联网。"), "switch", "switch", "--disable-api-nodes", false),

        parameter("debugHang", "diagnostics", catalogText("挂起诊断"), catalogText("启用 Ctrl+C 时的堆栈转储。"), "switch", "switch", "--debug-hang", false),
        parameter("dontPrintServer", "diagnostics", catalogText("隐藏服务器输出"), catalogText("禁止打印服务器输出。"), "switch", "switch", "--dont-print-server", false),
        parameter("quickTest", "diagnostics", catalogText("CI 快速测试"), catalogText("运行 ComfyUI 的 CI 快速测试模式。"), "switch", "switch", "--quick-test-for-ci", false),
        parameter("listFeatureFlags", "diagnostics", catalogText("列出功能标志后退出"), catalogText("打印可由命令行设置的功能标志 JSON，然后退出。"), "switch", "switch", "--list-feature-flags", false)
    };
    return catalog;
}

QVariantList LaunchParameterCatalog::categories()
{
    QVariantList result {
        QVariantMap{{"key", "basic"}, {"title", catalogText("常规")}, {"icon", "\uE713"}, {"description", catalogText("启动行为、Manager 与日志")}},
        QVariantMap{{"key", "network"}, {"title", catalogText("网络与服务")}, {"icon", "\uE774"}, {"description", catalogText("地址、端口、TLS 与 API")}},
        QVariantMap{{"key", "paths"}, {"title", catalogText("目录")}, {"icon", "\uE8B7"}, {"description", catalogText("模型、输入输出与用户目录")}},
        QVariantMap{{"key", "device"}, {"title", catalogText("设备")}, {"icon", "\uE950"}, {"description", catalogText("CUDA、DirectML 与显存模式")}},
        QVariantMap{{"key", "precision"}, {"title", catalogText("精度")}, {"icon", "\uE9D9"}, {"description", catalogText("模型、VAE 与编码器精度")}},
        QVariantMap{{"key", "memory"}, {"title", catalogText("内存与缓存")}, {"icon", "\uE964"}, {"description", catalogText("缓存、卸载与内存映射")}},
        QVariantMap{{"key", "performance"}, {"title", catalogText("性能")}, {"icon", "\uE9D2"}, {"description", catalogText("注意力、哈希与实验优化")}},
        QVariantMap{{"key", "frontend"}, {"title", catalogText("前端与预览")}, {"icon", "\uE7F4"}, {"description", catalogText("预览、前端版本与功能标志")}},
        QVariantMap{{"key", "security"}, {"title", catalogText("安全与节点")}, {"icon", "\uEA18"}, {"description", catalogText("元数据、自定义节点与 API")}},
        QVariantMap{{"key", "diagnostics"}, {"title", catalogText("诊断")}, {"icon", "\uEBE8"}, {"description", catalogText("调试、测试与诊断选项")}},
        QVariantMap{{"key", "environment"}, {"title", catalogText("环境变量")}, {"icon", "\uE756"}, {"description", catalogText("常用与自定义子进程环境")}},
        QVariantMap{{"key", "custom"}, {"title", catalogText("自定义参数")}, {"icon", "\uE943"}, {"description", catalogText("追加未覆盖的命令行参数")}}
    };
    for (QVariant &entry : result) {
        QVariantMap category = entry.toMap();
        category.insert(
            QStringLiteral("title"),
            translatedCatalogText(category.value(QStringLiteral("title")).toString()));
        category.insert(
            QStringLiteral("description"),
            translatedCatalogText(category.value(QStringLiteral("description")).toString()));
        entry = category;
    }
    return result;
}

const LaunchParameterDefinition *LaunchParameterCatalog::find(const QString &key)
{
    const auto &catalog = parameters();
    const auto iterator = std::find_if(catalog.cbegin(), catalog.cend(), [&key](const auto &entry) {
        return entry.key == key;
    });
    return iterator == catalog.cend() ? nullptr : &(*iterator);
}
