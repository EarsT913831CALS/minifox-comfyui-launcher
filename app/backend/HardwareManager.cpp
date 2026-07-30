#include "HardwareManager.h"

#include "ConfigurationManager.h"
#include "LaunchCommandBuilder.h"
#include "ZludaBootstrap.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

#include <utility>

HardwareManager::HardwareManager(ConfigurationManager *configuration, QObject *parent)
    : QObject(parent),
      m_configuration(configuration)
{
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_process, &QProcess::finished,
            this, &HardwareManager::handleProcessFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_mode == DetectionMode::Python) {
            m_mode = DetectionMode::None;
            QTimer::singleShot(0, this, &HardwareManager::continueAfterPythonProbe);
        } else if (error == QProcess::FailedToStart
                   && m_mode == DetectionMode::PythonZluda) {
            m_zludaLastError = m_process.errorString();
            m_mode = DetectionMode::None;
            QTimer::singleShot(0, this, &HardwareManager::startNextZludaPythonDetection);
        } else if (error == QProcess::FailedToStart) {
            finishWithError(tr("未找到可用的 CUDA 检测工具。"));
        }
    });

    m_timeout.setSingleShot(true);
    m_timeout.setInterval(30000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        const DetectionMode timedOutMode = m_mode;
        m_mode = DetectionMode::None;
        m_process.kill();
        if (timedOutMode == DetectionMode::Python) {
            finishWithError(tr("硬件和 PyTorch 检测超时。"));
        } else if (timedOutMode == DetectionMode::PythonZluda) {
            m_zludaLastError = tr("ZLUDA PyTorch 检测超时");
            QTimer::singleShot(0, this, &HardwareManager::startNextZludaPythonDetection);
        } else {
            finishWithError(tr("CUDA 设备检测超时。"));
        }
    });

    connect(m_configuration, &ConfigurationManager::currentProfileChanged, this, [this] {
        const QString pythonPath = m_configuration->pythonPath();
        if (pythonPath != m_lastPythonPath && !m_detecting) {
            QTimer::singleShot(250, this, &HardwareManager::detect);
        }
    });
    // System adapter classification is synchronous and completes before the
    // QML pages are created. RuntimeManager repeats the same check as a launch
    // gate, so an immediate one-click start cannot race this UI probe.
    detect();
}

bool HardwareManager::detecting() const { return m_detecting; }
bool HardwareManager::hasCuda() const { return !m_cudaDevices.isEmpty(); }
QVariantList HardwareManager::cudaDevices() const { return m_cudaDevices; }
QString HardwareManager::detectionSource() const { return m_detectionSource; }
QString HardwareManager::torchVersion() const { return m_torchVersion; }
QString HardwareManager::cudaRuntimeVersion() const { return m_cudaRuntimeVersion; }
QString HardwareManager::driverVersion() const { return m_driverVersion; }
QString HardwareManager::lastError() const { return m_lastError; }

QString HardwareManager::summary() const
{
    if (m_detecting) {
        return tr("正在检测 CUDA 设备…");
    }
    if (m_cudaDevices.isEmpty()) {
        return tr("未检测到 CUDA 设备");
    }
    if (m_cudaDevices.size() == 1) {
        const QVariantMap device = m_cudaDevices.constFirst().toMap();
        return QStringLiteral("%1 · %2")
            .arg(device.value(QStringLiteral("name")).toString(),
                 device.value(QStringLiteral("memoryText")).toString());
    }
    return tr("检测到 %1 个 CUDA 设备").arg(m_cudaDevices.size());
}

void HardwareManager::detect()
{
    if (m_detecting) {
        return;
    }
    m_cudaDevices.clear();
    m_detectionSource.clear();
    m_torchVersion.clear();
    m_cudaRuntimeVersion.clear();
    m_driverVersion.clear();
    m_lastError.clear();
    m_zludaPreparation = {};
    m_zludaRocmCandidates.clear();
    m_zludaLastError.clear();
    m_pythonBackend = ZludaBootstrap::BackendKind::Unknown;
    m_systemAdapterKind =
        ZludaBootstrap::classifySystemAdapters(ZludaBootstrap::systemAdapterNames());
    m_detecting = true;
    emit detectionChanged();

    m_lastPythonPath = m_configuration->pythonPath();
    if (QFileInfo::exists(m_lastPythonPath)) {
        startPythonDetection(m_lastPythonPath);
    } else if (m_systemAdapterKind == ZludaBootstrap::SystemAdapterKind::NvidiaOnly
               || m_systemAdapterKind == ZludaBootstrap::SystemAdapterKind::Mixed) {
        startNvidiaSmiDetection();
    } else {
        finishWithError(tr("The selected Python environment does not exist."));
    }
}

void HardwareManager::applyAllDevices()
{
    QStringList indexes;
    for (const QVariant &entry : std::as_const(m_cudaDevices)) {
        indexes.append(QString::number(entry.toMap().value(QStringLiteral("index")).toInt()));
    }
    if (!indexes.isEmpty()) {
        m_configuration->setParameterValue(QStringLiteral("cudaDevice"), indexes.join(QLatin1Char(',')));
    }
}

void HardwareManager::useDevice(int index)
{
    for (const QVariant &entry : std::as_const(m_cudaDevices)) {
        const QVariantMap device = entry.toMap();
        if (device.value(QStringLiteral("index")).toInt() == index) {
            m_configuration->setParameterValue(QStringLiteral("cudaDevice"), QString::number(index));
            return;
        }
    }
}

void HardwareManager::retranslate()
{
    emit detectionChanged();
}

void HardwareManager::startPythonDetection(const QString &pythonPath)
{
    const QString script = QStringLiteral(
        "import json,torch;"
        "devices=[{'index':i,'name':torch.cuda.get_device_name(i),"
        "'memory':round(torch.cuda.get_device_properties(i).total_memory/1073741824,1),"
        "'capability':'.'.join(map(str,torch.cuda.get_device_capability(i)))}"
        " for i in range(torch.cuda.device_count())];"
        "print(json.dumps({'available':torch.cuda.is_available(),'torch':torch.__version__,"
        "'cuda':torch.version.cuda,'hip':getattr(torch.version,'hip',None),"
        "'active':__import__('os').environ.get('MINIFOX_ZLUDA_ACTIVE')=='1',"
        "'devices':devices},ensure_ascii=False))");
    QProcessEnvironment environment =
        LaunchCommandBuilder::build(m_configuration->currentProfileSnapshot()).environment;
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    startProcess(DetectionMode::Python, pythonPath,
                 {QStringLiteral("-c"), script}, environment);
}

void HardwareManager::startZludaPythonDetection()
{
    QProcessEnvironment environment =
        LaunchCommandBuilder::build(m_configuration->currentProfileSnapshot()).environment;
    m_zludaPreparation = ZludaBootstrap::prepare(
        m_lastPythonPath, m_configuration->comfyRoot(), environment);
    if (!m_zludaPreparation.valid) {
        finishWithError(tr("检测到 AMD 显卡，但无法准备便携 ZLUDA：%1")
                            .arg(m_zludaPreparation.error));
        return;
    }
    m_zludaRocmCandidates = m_zludaPreparation.rocmBinCandidates;
    startNextZludaPythonDetection();
}

void HardwareManager::startNextZludaPythonDetection()
{
    if (m_zludaRocmCandidates.isEmpty()) {
        finishWithError(tr("检测到 AMD 显卡，但 ZLUDA/HIP 检测未通过：%1")
                            .arg(m_zludaLastError.isEmpty()
                                     ? tr("没有可用的 HIP SDK/ROCm 运行时")
                                     : m_zludaLastError));
        return;
    }
    const QString rocmBin = m_zludaRocmCandidates.takeFirst();
    QProcessEnvironment environment =
        LaunchCommandBuilder::build(m_configuration->currentProfileSnapshot()).environment;
    environment.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    ZludaBootstrap::apply(m_zludaPreparation, rocmBin, environment);

    const QString script = QStringLiteral(
        "import json,torch;"
        "devices=[{'index':i,'name':torch.cuda.get_device_name(i),"
        "'memory':round(torch.cuda.get_device_properties(i).total_memory/1073741824,1),"
        "'capability':'.'.join(map(str,torch.cuda.get_device_capability(i)))}"
        " for i in range(torch.cuda.device_count())];"
        "print(json.dumps({'available':torch.cuda.is_available(),'torch':torch.__version__,"
        "'cuda':torch.version.cuda,'hip':getattr(torch.version,'hip',None),"
        "'active':__import__('os').environ.get('MINIFOX_ZLUDA_ACTIVE')=='1',"
        "'devices':devices},ensure_ascii=False))");
    startProcess(DetectionMode::PythonZluda, m_lastPythonPath,
                 {QStringLiteral("-c"), script}, environment);
}

void HardwareManager::startNvidiaSmiDetection()
{
    const QString nvidiaSmi = findNvidiaSmi();
    if (nvidiaSmi.isEmpty()) {
        finishWithError(tr("未检测到 NVIDIA 驱动或支持 CUDA 的 PyTorch 环境。"));
        return;
    }
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    startProcess(DetectionMode::NvidiaSmi, nvidiaSmi, {
        QStringLiteral("--query-gpu=index,name,memory.total,driver_version"),
        QStringLiteral("--format=csv,noheader,nounits")
    }, environment);
}

void HardwareManager::continueAfterPythonProbe()
{
    using Adapter = ZludaBootstrap::SystemAdapterKind;
    using Backend = ZludaBootstrap::BackendKind;

    if (m_systemAdapterKind == Adapter::AmdOnly) {
        if (m_pythonBackend == Backend::Rocm) {
            finishWithError(tr("Native ROCm PyTorch did not report an available AMD device."));
            return;
        }
        if (m_pythonBackend == Backend::Nvidia) {
            finishWithError(tr("PyTorch reported NVIDIA CUDA on an AMD-only system; ZLUDA was not enabled."));
            return;
        }
        // Keep launcher startup responsive: the UI preflight only verifies the
        // selected Python and system adapter class. Portable ZLUDA injection is
        // still validated by RuntimeManager immediately before ComfyUI starts,
        // where failures can be reported in the launch log.
        m_detectionSource = tr("检测到 AMD 显卡；ZLUDA 将在启动 ComfyUI 时检查。");
        m_detecting = false;
        m_lastError.clear();
        emit detectionChanged();
        return;
    }

    // NVIDIA, mixed and unknown adapter configurations never enter ZLUDA.
    QTimer::singleShot(0, this, &HardwareManager::startNvidiaSmiDetection);
}

void HardwareManager::startProcess(DetectionMode mode,
                                   const QString &program,
                                   const QStringList &arguments,
                                   const QProcessEnvironment &environment)
{
    m_timeout.stop();
    m_mode = mode;
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(m_configuration->comfyRoot());
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    m_process.start();
    m_timeout.setInterval(30000);
    m_timeout.start();
}

void HardwareManager::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_mode == DetectionMode::None) {
        return;
    }
    m_timeout.stop();
    const DetectionMode completedMode = m_mode;
    m_mode = DetectionMode::None;
    const QByteArray output = m_process.readAllStandardOutput();

    bool parsed = false;
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        parsed = completedMode == DetectionMode::NvidiaSmi
            ? parseNvidiaSmiResult(output)
            : parsePythonResult(output);
    }
    if (parsed && !m_cudaDevices.isEmpty()) {
        if (completedMode == DetectionMode::PythonZluda) {
            m_detectionSource = QStringLiteral("PyTorch · ZLUDA");
        }
        m_detecting = false;
        m_lastError.clear();
        emit detectionChanged();
        return;
    }
    if (completedMode == DetectionMode::Python) {
        QTimer::singleShot(0, this, &HardwareManager::continueAfterPythonProbe);
        return;
    }
    if (completedMode == DetectionMode::PythonZluda) {
        QString error = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
        m_zludaLastError = error.isEmpty()
            ? tr("注入 ZLUDA 后 PyTorch 仍未报告 CUDA 设备")
            : error;
        QTimer::singleShot(0, this, &HardwareManager::startNextZludaPythonDetection);
        return;
    }

    QString error = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
    if (error.isEmpty()) {
        error = tr("未检测到可用的 CUDA 设备。");
    }
    finishWithError(error);
}

bool HardwareManager::parsePythonResult(const QByteArray &output)
{
    const QList<QByteArray> lines = output.trimmed().split('\n');
    if (lines.isEmpty()) {
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(lines.constLast().trimmed(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    const QJsonObject root = document.object();
    m_torchVersion = root.value(QStringLiteral("torch")).toString();
    m_cudaRuntimeVersion = root.value(QStringLiteral("cuda")).toString();
    const QString hipRuntimeVersion = root.value(QStringLiteral("hip")).toString();
    QStringList deviceNames;
    bool hasAmdDevice = false;
    for (const QJsonValue &value : root.value(QStringLiteral("devices")).toArray()) {
        const QJsonObject device = value.toObject();
        const double memory = device.value(QStringLiteral("memory")).toDouble();
        const QString deviceName = device.value(QStringLiteral("name")).toString();
        deviceNames.append(deviceName);
        hasAmdDevice |= deviceName.contains(QStringLiteral("AMD"), Qt::CaseInsensitive)
            || deviceName.contains(QStringLiteral("Radeon"), Qt::CaseInsensitive);
        m_cudaDevices.append(QVariantMap{
            {QStringLiteral("index"), device.value(QStringLiteral("index")).toInt()},
            {QStringLiteral("name"), deviceName},
            {QStringLiteral("memoryGb"), memory},
            {QStringLiteral("memoryText"), QStringLiteral("%1 GB").arg(memory, 0, 'f', 1)},
            {QStringLiteral("capability"), device.value(QStringLiteral("capability")).toString()}
        });
    }
    m_pythonBackend = ZludaBootstrap::classifyBackend(
        m_cudaRuntimeVersion, hipRuntimeVersion, deviceNames);
    const bool isZluda = root.value(QStringLiteral("active")).toBool()
        || (hasAmdDevice
            && !m_cudaRuntimeVersion.isEmpty()
            && root.value(QStringLiteral("hip")).toString().isEmpty());
    m_detectionSource = isZluda
        ? QStringLiteral("PyTorch · ZLUDA")
        : QStringLiteral("PyTorch");
    return true;
}

bool HardwareManager::parseNvidiaSmiResult(const QByteArray &output)
{
    const QStringList lines = QString::fromUtf8(output).split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                                              Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList parts = line.split(QLatin1Char(','));
        if (parts.size() < 4) {
            continue;
        }
        const int index = parts.at(0).trimmed().toInt();
        const QString name = parts.at(1).trimmed();
        const double memoryGb = parts.at(2).trimmed().toDouble() / 1024.0;
        const QString driver = parts.at(3).trimmed();
        if (m_driverVersion.isEmpty()) {
            m_driverVersion = driver;
        }
        m_cudaDevices.append(QVariantMap{
            {QStringLiteral("index"), index},
            {QStringLiteral("name"), name},
            {QStringLiteral("memoryGb"), memoryGb},
            {QStringLiteral("memoryText"), QStringLiteral("%1 GB").arg(memoryGb, 0, 'f', 1)},
            {QStringLiteral("capability"), QString()}
        });
    }
    m_detectionSource = QStringLiteral("NVIDIA SMI");
    return true;
}

void HardwareManager::finishWithError(const QString &message)
{
    m_timeout.stop();
    m_mode = DetectionMode::None;
    m_detecting = false;
    m_lastError = message;
    emit detectionChanged();
}

QString HardwareManager::findNvidiaSmi() const
{
    QString path = QStandardPaths::findExecutable(QStringLiteral("nvidia-smi.exe"));
    if (!path.isEmpty()) {
        return path;
    }
    const QStringList candidates {
        QStringLiteral("C:/Windows/System32/nvidia-smi.exe"),
        QStringLiteral("C:/Program Files/NVIDIA Corporation/NVSMI/nvidia-smi.exe")
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}
