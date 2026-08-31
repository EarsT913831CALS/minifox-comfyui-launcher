#include "RuntimeManager.h"

#include "ApplicationSettings.h"
#include "CommandPromptBuilder.h"
#include "ConfigurationManager.h"
#include "LaunchCommandBuilder.h"
#include "LogModel.h"
#include "PortablePaths.h"
#include "ProcessTextDecoder.h"
#include "ZludaBootstrap.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QResource>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrlQuery>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

static void initializeProgressBridgeResources()
{
    static const bool initialized = [] {
        Q_INIT_RESOURCE(minifox_python_bridge);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace {

QString materializeProgressBridge(QString *error)
{
    initializeProgressBridgeResources();
    QFile resource(QStringLiteral(":/minifox/python/minifox_progress_bridge.py"));
    if (!resource.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("The embedded ComfyUI progress bridge is unavailable.");
        }
        return {};
    }
    const QByteArray contents = resource.readAll();
    const QString runtimeDirectory = QDir(PortablePaths::dataDirectory())
                                         .filePath(QStringLiteral("runtime"));
    if (!QDir().mkpath(runtimeDirectory)) {
        if (error) {
            *error = QStringLiteral("Unable to create the Minifox runtime directory.");
        }
        return {};
    }

    const QString path = QDir(runtimeDirectory).filePath(
        QStringLiteral("minifox_progress_bridge.py"));
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.readAll() == contents) {
        return path;
    }

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(contents) != contents.size()
        || !output.commit()) {
        if (error) {
            *error = QStringLiteral("Unable to prepare the ComfyUI progress bridge: %1")
                         .arg(output.errorString());
        }
        return {};
    }
    return path;
}

void forceKillProcessTree(qint64 processId)
{
#ifdef Q_OS_WIN
    if (processId <= 0) {
        return;
    }

    const QString systemRoot = qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"));
    QString taskkill = QDir(systemRoot).filePath(QStringLiteral("System32/taskkill.exe"));
    if (!QFileInfo::exists(taskkill)) {
        taskkill = QStandardPaths::findExecutable(QStringLiteral("taskkill.exe"));
    }
    if (taskkill.isEmpty()) {
        return;
    }

    QProcess killer;
    killer.setProgram(taskkill);
    killer.setArguments({QStringLiteral("/PID"), QString::number(processId),
                         QStringLiteral("/T"), QStringLiteral("/F")});
    killer.start();
    if (!killer.waitForFinished(5000)) {
        killer.kill();
        killer.waitForFinished(1000);
    }
#else
    Q_UNUSED(processId)
#endif
}

#ifdef Q_OS_WIN
void configureInteractiveConsole(QProcess::CreateProcessArguments *arguments)
{
    arguments->flags &= ~CREATE_NO_WINDOW;
    arguments->flags |= CREATE_NEW_CONSOLE;
    if (arguments->startupInfo) {
        // A GUI launcher has no interactive standard input. Let Windows create
        // normal CONIN$/CONOUT$ handles for the newly-created console.
        arguments->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
        arguments->startupInfo->hStdInput = nullptr;
        arguments->startupInfo->hStdOutput = nullptr;
        arguments->startupInfo->hStdError = nullptr;
    }
}
#endif

// Offline requirements checker: inspects only the selected interpreter's installed
// metadata.  It never contacts an index and never installs or changes a package.
// Every positional argument is one requirements.txt; each gets its own result entry
// so the caller can install and re-check exactly the files that failed.
QString dependencyCheckerScript()
{
    static const QString checker = QStringLiteral(R"PY(
import importlib.metadata as metadata
import json
import os
import re
import sys

try:
    from packaging.requirements import Requirement
except Exception:
    try:
        from pip._vendor.packaging.requirements import Requirement
    except Exception as exc:
        print(json.dumps({"ok": False, "fatal": "Python 中缺少 pip/packaging，无法解析依赖清单：%s" % exc}))
        raise SystemExit(3)

def normalized_vcs_url(value):
    value = value.strip()
    if re.match(r"^(?:git|hg|svn|bzr)\+", value, re.IGNORECASE):
        value = value.split("+", 1)[1]
    value = value.split("#", 1)[0].rstrip("/")
    git_revision = value.lower().find(".git@")
    if git_revision >= 0:
        value = value[:git_revision + 4]
    else:
        last_slash = value.rfind("/")
        revision = value.rfind("@")
        if revision > last_slash:
            value = value[:revision]
    if value.lower().endswith(".git"):
        value = value[:-4]
    return value.rstrip("/").casefold()

def installed_vcs_distribution(raw):
    expected_url = normalized_vcs_url(raw)
    for distribution in metadata.distributions():
        try:
            direct_url_text = distribution.read_text("direct_url.json")
            if not direct_url_text:
                continue
            direct_url = json.loads(direct_url_text).get("url", "")
            if normalized_vcs_url(direct_url) != expected_url:
                continue
            name = distribution.metadata.get("Name") or distribution.name
            return name, distribution.version
        except Exception:
            continue
    return None

def check_file(root):
    seen_files = set()
    raw_requirements = []
    parse_errors = []

    def read_file(path):
        path = os.path.abspath(path)
        if path in seen_files:
            return
        seen_files.add(path)
        try:
            with open(path, "r", encoding="utf-8-sig") as stream:
                physical = stream.readlines()
        except Exception as exc:
            parse_errors.append("无法读取 %s：%s" % (path, exc))
            return

        logical = []
        pending = ""
        for raw in physical:
            line = raw.rstrip("\r\n")
            if line.rstrip().endswith("\\"):
                pending += line.rstrip()[:-1] + " "
                continue
            logical.append(pending + line)
            pending = ""
        if pending:
            logical.append(pending)

        base = os.path.dirname(path)
        for line in logical:
            line = re.split(r"\s+#", line, maxsplit=1)[0].strip()
            if not line or line.startswith("#"):
                continue
            include = re.match(r"^(?:-r|--requirement)\s+(.+)$", line)
            constraint = re.match(r"^(?:-c|--constraint)\s+(.+)$", line)
            if include or constraint:
                child = (include or constraint).group(1).strip().strip("\"'")
                read_file(os.path.join(base, child))
                continue
            if line.startswith("--hash=") or line.startswith("--"):
                continue
            if line.startswith("-e ") or line.startswith("--editable "):
                egg = re.search(r"[#&]egg=([^&]+)", line)
                if egg:
                    raw_requirements.append(egg.group(1))
                else:
                    parse_errors.append("无法识别可编辑依赖：%s" % line)
                continue
            raw_requirements.append(line)

    read_file(root)
    issues = []
    checked = 0
    for raw in raw_requirements:
        if re.match(r"^(?:git|hg|svn|bzr)\+\S+$", raw, re.IGNORECASE):
            checked += 1
            installed_vcs = installed_vcs_distribution(raw)
            if installed_vcs is None:
                repository = normalized_vcs_url(raw).rsplit("/", 1)[-1]
                issues.append("缺少 VCS 依赖：%s（来源：%s）" % (repository, raw))
            continue
        try:
            requirement = Requirement(raw)
        except Exception as exc:
            parse_errors.append("无法解析依赖“%s”：%s" % (raw, exc))
            continue
        try:
            if requirement.marker is not None and not requirement.marker.evaluate():
                continue
        except Exception as exc:
            parse_errors.append("无法计算依赖条件“%s”：%s" % (raw, exc))
            continue
        checked += 1
        try:
            installed = metadata.version(requirement.name)
        except metadata.PackageNotFoundError:
            issues.append("缺少依赖：%s" % requirement.name)
            continue
        except Exception as exc:
            issues.append("无法读取 %s 的版本：%s" % (requirement.name, exc))
            continue
        if requirement.specifier and not requirement.specifier.contains(installed, prereleases=True):
            issues.append("版本不符：%s 已安装 %s，需要 %s" %
                          (requirement.name, installed, requirement.specifier))

    return {
        "path": os.path.abspath(root),
        "ok": not issues and not parse_errors,
        "checked": checked,
        "issues": issues,
        "parseErrors": parse_errors,
    }

results = [check_file(path) for path in sys.argv[1:]]
ok = all(item["ok"] for item in results)
print(json.dumps({"ok": ok, "results": results}))
raise SystemExit(0 if ok else 2)
)PY");
    return checker;
}

// Kernel requirements live directly in the ComfyUI root; extension requirements live
// one directory below custom_nodes.  Use that layout for a readable display name.
QString requirementDisplayName(const QString &requirementsPath)
{
    const QDir parent(QFileInfo(requirementsPath).absolutePath());
    if (parent.dirName().compare(QStringLiteral("custom_nodes"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("ComfyUI");
    }
    return parent.dirName();
}

} // namespace

RuntimeManager::RuntimeManager(ConfigurationManager *configuration,
                               ApplicationSettings *settings,
                               QObject *parent)
    : QObject(parent),
      m_configuration(configuration),
      m_settings(settings),
      m_logModel(new LogModel(this)),
      m_network(new QNetworkAccessManager(this))
{
    m_network->setProxy(QNetworkProxy::NoProxy);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_dependencyCheck.setProcessChannelMode(QProcess::SeparateChannels);

    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_logModel->appendStandardOutput(m_process.readAllStandardOutput());
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_logModel->appendStandardError(m_process.readAllStandardError());
    });
    connect(&m_process, &QProcess::started, this, &RuntimeManager::handleProcessStarted);
    connect(&m_process, &QProcess::finished, this, &RuntimeManager::handleProcessFinished);
    connect(&m_process, &QProcess::errorOccurred, this, &RuntimeManager::handleProcessError);
    connect(&m_dependencyCheck, &QProcess::started,
            this, &RuntimeManager::handleDependencyCheckStarted);
    connect(&m_dependencyCheck, &QProcess::finished,
            this, &RuntimeManager::handleDependencyCheckFinished);
    connect(&m_dependencyCheck, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !m_stopRequested) {
            setLastError(tr("无法启动 Python 依赖检查：%1").arg(m_dependencyCheck.errorString()));
            setStatus(Failed);
            m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
        }
    });

    m_dependencyInstall.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_dependencyInstall, &QProcess::started,
            this, &RuntimeManager::handleDependencyInstallStarted);
    connect(&m_dependencyInstall, &QProcess::finished,
            this, &RuntimeManager::handleDependencyInstallFinished);
    connect(&m_dependencyInstall, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && !m_stopRequested) {
            setLastError(tr("无法启动依赖安装窗口：%1").arg(m_dependencyInstall.errorString()));
            setStatus(Failed);
            m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
        }
    });
    m_commandPrompt.setProcessChannelMode(QProcess::ForwardedChannels);
    connect(&m_commandPrompt, &QProcess::finished, this, [this] {
        m_commandPromptJob.reset();
    });

    m_readinessTimer.setInterval(800);
    connect(&m_readinessTimer, &QTimer::timeout, this, &RuntimeManager::checkReadiness);

    m_uptimeTimer.setInterval(1000);
    connect(&m_uptimeTimer, &QTimer::timeout, this, &RuntimeManager::updateUptime);

    m_forceStopTimer.setSingleShot(true);
    m_forceStopTimer.setInterval(3000);
    connect(&m_forceStopTimer, &QTimer::timeout, this, &RuntimeManager::forceStop);

    connect(m_configuration, &ConfigurationManager::currentProfileChanged,
            this, &RuntimeManager::updateCommandPreview);
    connect(m_configuration, &ConfigurationManager::parameterRevisionChanged,
            this, &RuntimeManager::updateCommandPreview);
    connect(m_configuration, &ConfigurationManager::parameterValueChanged,
            this, &RuntimeManager::updateCommandPreview);
    connect(m_configuration, &ConfigurationManager::environmentEntryChanged,
            this, &RuntimeManager::updateCommandPreview);
    connect(m_settings, &ApplicationSettings::proxyChanged,
            this, &RuntimeManager::updateCommandPreview);

    updateCommandPreview();
}

RuntimeManager::~RuntimeManager()
{
    shutdown();
}

RuntimeManager::Status RuntimeManager::status() const { return m_status; }

QString RuntimeManager::statusText() const
{
    switch (m_status) {
    case Stopped: return tr("已停止");
    case Starting: return tr("启动中");
    case Running: return tr("运行中");
    case Stopping: return tr("停止中");
    case Failed: return tr("启动失败");
    }
    return {};
}

bool RuntimeManager::canStart() const
{
    return (m_status == Stopped || m_status == Failed)
        && m_process.state() == QProcess::NotRunning
        && m_dependencyCheck.state() == QProcess::NotRunning
        && m_dependencyInstall.state() == QProcess::NotRunning
        && !m_processJob.isAttached()
        && !m_versionOperationBlocked;
}

bool RuntimeManager::canStop() const
{
    return active() && m_status != Stopping;
}

bool RuntimeManager::active() const
{
    return m_status == Starting || m_status == Running || m_status == Stopping
        || m_process.state() != QProcess::NotRunning
        || m_dependencyCheck.state() != QProcess::NotRunning
        || m_dependencyInstall.state() != QProcess::NotRunning
        || m_processJob.isAttached();
}

qint64 RuntimeManager::processId() const { return m_processId; }
QString RuntimeManager::uptime() const { return m_uptime; }
bool RuntimeManager::serviceReady() const { return m_serviceReady; }
QString RuntimeManager::serviceUrl() const { return m_serviceUrl; }
QString RuntimeManager::acceleratorSummary() const { return m_acceleratorSummary; }
QVariantList RuntimeManager::acceleratorDevices() const { return m_acceleratorDevices; }
QString RuntimeManager::commandPreview() const { return m_commandPreview; }
LogModel *RuntimeManager::logModel() const { return m_logModel; }
QString RuntimeManager::lastError() const { return m_lastError; }
int RuntimeManager::lastExitCode() const { return m_lastExitCode; }

void RuntimeManager::setVersionOperationBlocked(bool blocked)
{
    if (m_versionOperationBlocked == blocked) {
        return;
    }
    m_versionOperationBlocked = blocked;
    emit statusChanged();
}

void RuntimeManager::retranslate()
{
    emit statusChanged();
    emit runtimeInfoChanged();
}

void RuntimeManager::start()
{
    if (!canStart()) {
        return;
    }
    if (!m_configuration->isValid()) {
        setLastError(m_configuration->validationErrors().join(QLatin1Char('\n')));
        setStatus(Failed);
        return;
    }

    m_logModel->clear();
    m_logModel->appendSystemMessage(tr("正在启动配置“%1”…").arg(m_configuration->currentProfileName()),
                                    QStringLiteral("#0067c0"));
    m_processJob.reset();
    m_stopRequested = false;
    m_startupAborted = false;
    m_dependencyRecheckPhase = false;
    m_dependencyPendingInstalls.clear();
    m_dependencyRecheckPaths.clear();
    m_dependencyCurrentPath.clear();
    m_zludaPreparation = {};
    m_zludaRocmBin.clear();
    m_zludaEnabled = false;
    m_acceleratorSummary.clear();
    m_acceleratorDevices.clear();
    m_lastExitCode = 0;
    m_processId = 0;
    m_uptime = QStringLiteral("00:00:00");
    setServiceReady(false);
    setLastError({});
    setStatus(Starting);
    emit runtimeInfoChanged();
    beginZludaBootstrap();
}

void RuntimeManager::stop()
{
    if (!canStop()) {
        return;
    }
    m_stopRequested = true;
    setStatus(Stopping);
    m_logModel->appendSystemMessage(tr("正在停止 ComfyUI…"), QStringLiteral("#9d5d00"));
    cancelReadinessCheck();
    m_readinessTimer.stop();
    if (m_dependencyCheck.state() != QProcess::NotRunning) {
        m_dependencyCheck.terminate();
    }
    if (m_dependencyInstall.state() != QProcess::NotRunning) {
        m_dependencyInstall.terminate();
    }
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
    }
    if (m_dependencyCheck.state() != QProcess::NotRunning
        || m_dependencyInstall.state() != QProcess::NotRunning
        || m_process.state() != QProcess::NotRunning
        || m_processJob.isAttached()) {
        m_forceStopTimer.start();
    } else {
        setStatus(Stopped);
    }
}

void RuntimeManager::forceStop()
{
    if (!active()) {
        return;
    }
    m_stopRequested = true;
    m_logModel->appendSystemMessage(tr("正在强制终止 ComfyUI 进程树…"), QStringLiteral("#c42b1c"));
    terminateTrackedProcessTree();
    if (m_dependencyCheck.state() == QProcess::NotRunning
        && m_dependencyInstall.state() == QProcess::NotRunning
        && m_process.state() == QProcess::NotRunning) {
        setStatus(Stopped);
        emit runtimeInfoChanged();
    }
}

void RuntimeManager::shutdown()
{
    m_stopRequested = true;
    m_forceStopTimer.stop();
    m_readinessTimer.stop();
    m_uptimeTimer.stop();
    cancelReadinessCheck();
    if (m_commandPrompt.state() != QProcess::NotRunning) {
        m_commandPromptJob.terminate();
        m_commandPrompt.kill();
        m_commandPrompt.waitForFinished(2000);
    }
    m_commandPromptJob.reset();
    terminateTrackedProcessTree();
    m_processId = 0;
    m_elapsed.invalidate();
    setServiceReady(false);
    setStatus(Stopped);
    emit runtimeInfoChanged();
}

void RuntimeManager::beginZludaBootstrap()
{
    if (m_stopRequested || m_startupAborted) {
        return;
    }

    LaunchCommandBuilder::Result command =
        LaunchCommandBuilder::build(m_configuration->currentProfileSnapshot());
    m_settings->applyToProcessEnvironment(command.environment);
    const QString mode =
        command.environment.value(QStringLiteral("MINIFOX_ZLUDA_BOOTSTRAP"),
                                  QStringLiteral("auto"));
    const QStringList adapterNames = ZludaBootstrap::systemAdapterNames();
    const ZludaBootstrap::SystemAdapterKind adapterKind =
        ZludaBootstrap::classifySystemAdapters(adapterNames);
    if (!ZludaBootstrap::shouldProbeAdapters(adapterNames, mode)) {
        if (adapterKind == ZludaBootstrap::SystemAdapterKind::NvidiaOnly) {
            m_logModel->appendSystemMessage(
                tr("System NVIDIA adapter detected; using the normal CUDA startup path."));
        } else if (adapterKind == ZludaBootstrap::SystemAdapterKind::Mixed) {
            m_logModel->appendSystemMessage(
                tr("NVIDIA and AMD adapters detected; using the normal NVIDIA CUDA startup path."));
        } else if (adapterKind == ZludaBootstrap::SystemAdapterKind::Unknown) {
            m_logModel->appendSystemMessage(
                tr("No supported AMD-only adapter configuration was detected; ZLUDA is disabled."),
                QStringLiteral("#9d5d00"));
        }
        beginDependencyCheck();
        return;
    }

    const ZludaBootstrap::BackendKind installedBackend =
        ZludaBootstrap::classifyInstalledTorch(command.program);
    if (installedBackend == ZludaBootstrap::BackendKind::Rocm) {
        m_logModel->appendSystemMessage(
            tr("检测到原生 ROCm PyTorch 元数据，跳过 ZLUDA 引导。"));
        beginDependencyCheck();
        return;
    }

    m_logModel->appendSystemMessage(
        installedBackend == ZludaBootstrap::BackendKind::Nvidia
            ? tr("检测到 CUDA PyTorch 元数据，正在准备 ZLUDA 运行环境…")
            : tr("无法从 PyTorch 元数据确定后端，正在按 AMD-only 配置准备 ZLUDA…"),
        QStringLiteral("#0067c0"));
    if (!prepareZludaRuntime()) {
        return;
    }
    m_zludaEnabled = true;
    m_logModel->appendSystemMessage(
        tr("ZLUDA 运行环境已准备，设备可用性将由 ComfyUI 启动结果确认。"),
        QStringLiteral("#0f7b0f"));
    beginDependencyCheck();
}

bool RuntimeManager::prepareZludaRuntime()
{
    if (m_zludaPreparation.valid) {
        return true;
    }
    LaunchCommandBuilder::Result command =
        LaunchCommandBuilder::build(m_configuration->currentProfileSnapshot());
    m_settings->applyToProcessEnvironment(command.environment);
    m_zludaPreparation = ZludaBootstrap::prepare(
        command.program, command.workingDirectory, command.environment);
    if (!m_zludaPreparation.valid) {
        setLastError(m_zludaPreparation.error);
        setStatus(Failed);
        m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
        return false;
    }
    if (m_zludaPreparation.rocmBinCandidates.isEmpty()) {
        setLastError(tr("No usable HIP SDK runtime was found for ZLUDA."));
        setStatus(Failed);
        m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
        return false;
    }
    m_zludaRocmBin = m_zludaPreparation.rocmBinCandidates.constFirst();
    m_logModel->appendSystemMessage(
        tr("ZLUDA DLL 来源：%1")
            .arg(QDir::toNativeSeparators(m_zludaPreparation.sourceDirectory)));
    return true;
}

void RuntimeManager::updateAcceleratorSummaryFromSystemStats(const QByteArray &payload)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }

    QStringList deviceNames;
    QVariantList devices;
    for (const QJsonValue &value : document.object().value(QStringLiteral("devices")).toArray()) {
        const QJsonObject device = value.toObject();
        QString name = device.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) {
            name = device.value(QStringLiteral("device_name")).toString().trimmed();
        }
        if (!name.isEmpty()) {
            deviceNames.append(name);
            const double totalBytes = device.value(QStringLiteral("vram_total")).toDouble(-1.0);
            devices.append(QVariantMap {
                {QStringLiteral("name"), name},
                {QStringLiteral("type"), device.value(QStringLiteral("type")).toString()},
                {QStringLiteral("index"), device.value(QStringLiteral("index")).toInt(-1)},
                {QStringLiteral("memoryBytes"), totalBytes},
                {QStringLiteral("memoryText"),
                 totalBytes >= 0.0
                     ? QStringLiteral("%1 GB").arg(totalBytes / 1073741824.0, 0, 'f', 1)
                     : QString()}
            });
        }
    }
    if (!devices.isEmpty()) {
        m_acceleratorSummary = deviceNames.join(QStringLiteral(", "));
        m_acceleratorDevices = devices;
        emit runtimeInfoChanged();
    }
}

bool RuntimeManager::openCommandPrompt()
{
    if (m_commandPrompt.state() != QProcess::NotRunning) {
        setLastError(tr("启动命令提示符已经打开。"));
        return false;
    }

    LaunchCommandBuilder::Result command =
        LaunchCommandBuilder::build(m_configuration->currentProfileSnapshot());
    if (command.workingDirectory.isEmpty() || !QDir(command.workingDirectory).exists()) {
        setLastError(tr("ComfyUI 工作目录不存在。"));
        return false;
    }

    const QFileInfo python(command.program);
    if (!python.isFile()) {
        setLastError(tr("Python 可执行文件不存在：%1")
                         .arg(QDir::toNativeSeparators(command.program)));
        return false;
    }

    m_settings->applyToProcessEnvironment(command.environment);
    const CommandPromptBuilder::Result promptCommand = CommandPromptBuilder::build(
        python.absoluteFilePath(), command.workingDirectory, command.environment);

    m_commandPrompt.setWorkingDirectory(promptCommand.workingDirectory);
    m_commandPrompt.setProcessEnvironment(promptCommand.environment);
    m_commandPrompt.setProgram(promptCommand.program);
    m_commandPrompt.setNativeArguments(promptCommand.nativeArguments);
#ifdef Q_OS_WIN
    m_commandPrompt.setCreateProcessArgumentsModifier(configureInteractiveConsole);
#endif
    m_commandPrompt.start();
    if (!m_commandPrompt.waitForStarted(3000)) {
        setLastError(tr("无法打开启动命令提示符：%1").arg(m_commandPrompt.errorString()));
        return false;
    }
    if (!m_commandPromptJob.attach(m_commandPrompt.processId())) {
        const qint64 promptPid = m_commandPrompt.processId();
        m_commandPrompt.kill();
        forceKillProcessTree(promptPid);
        setLastError(tr("无法将启动命令提示符加入安全作业，已关闭该窗口。"));
        return false;
    }
    setLastError({});
    return true;
}

bool RuntimeManager::exportLog(const QUrl &fileUrl)
{
    QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) {
        return false;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path.append(QStringLiteral(".log"));
    }
    QString error;
    if (!m_logModel->exportToFile(path, m_settings->showTimestamps(), &error)) {
        setLastError(tr("无法导出日志：%1").arg(error));
        return false;
    }
    m_logModel->appendSystemMessage(tr("日志已导出到 %1").arg(QDir::toNativeSeparators(path)),
                                    QStringLiteral("#0f7b0f"));
    return true;
}

void RuntimeManager::beginDependencyCheck()
{
    const LaunchCommandBuilder::Result command =
        LaunchCommandBuilder::build(m_configuration->currentProfileSnapshot());
    const QString requirementsPath =
        QDir(command.workingDirectory).filePath(QStringLiteral("requirements.txt"));
    if (!QFileInfo::exists(requirementsPath)) {
        setLastError(tr("找不到 ComfyUI 依赖清单：%1")
                         .arg(QDir::toNativeSeparators(requirementsPath)));
        setStatus(Failed);
        m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
        return;
    }

    const QStringList requirementFiles = collectRequirementFiles();
    QProcessEnvironment environment = command.environment;
    m_settings->applyToProcessEnvironment(environment);
    m_dependencyCheck.setWorkingDirectory(command.workingDirectory);
    m_dependencyCheck.setProcessEnvironment(environment);
    m_dependencyCheck.setProgram(command.program);
    QStringList arguments{QStringLiteral("-c"), dependencyCheckerScript()};
    arguments.append(requirementFiles);
    m_dependencyCheck.setArguments(arguments);
    m_logModel->appendSystemMessage(
        tr("正在使用所选 Python 离线检查 %1 个依赖清单…").arg(requirementFiles.size()),
        QStringLiteral("#0067c0"));
    m_dependencyCheck.start();
}

void RuntimeManager::handleDependencyCheckStarted()
{
    const qint64 dependencyPid = m_dependencyCheck.processId();
    if (m_processJob.attach(dependencyPid)) {
        return;
    }

    m_startupAborted = true;
    setLastError(tr("无法将依赖检查进程加入安全作业，已中止启动。"));
    setStatus(Failed);
    m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
    forceKillProcessTree(dependencyPid);
    m_dependencyCheck.kill();
}

void RuntimeManager::handleDependencyCheckFinished(int exitCode,
                                                   QProcess::ExitStatus exitStatus)
{
    const QByteArray standardOutput = m_dependencyCheck.readAllStandardOutput();
    const QByteArray standardError = m_dependencyCheck.readAllStandardError();
    m_processJob.reset();

    if (m_stopRequested) {
        if (m_process.state() == QProcess::NotRunning
            && m_dependencyInstall.state() == QProcess::NotRunning) {
            m_forceStopTimer.stop();
            setStatus(Stopped);
            emit runtimeInfoChanged();
        }
        return;
    }
    if (m_startupAborted) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(standardOutput.trimmed(), &parseError);
    const QJsonObject result = document.isObject() ? document.object() : QJsonObject{};
    if (exitStatus != QProcess::NormalExit || parseError.error != QJsonParseError::NoError
        || result.isEmpty()) {
        QString detail = ProcessTextDecoder::decode(standardError).trimmed();
        if (detail.isEmpty()) {
            detail = ProcessTextDecoder::decode(standardOutput).trimmed();
        }
        if (detail.isEmpty()) {
            detail = tr("检查进程退出代码 %1。").arg(exitCode);
        }
        setLastError(tr("依赖检查未能完成：%1").arg(detail));
        setStatus(Failed);
        m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
        return;
    }

    const QString fatal = result.value(QStringLiteral("fatal")).toString();
    if (!fatal.isEmpty()) {
        setLastError(tr("依赖检查失败，ComfyUI 未启动。"));
        setStatus(Failed);
        m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
        m_logModel->appendSystemMessage(fatal, QStringLiteral("#c42b1c"));
        return;
    }

    int totalChecked = 0;
    QStringList failingPaths;
    const QJsonArray results = result.value(QStringLiteral("results")).toArray();
    for (const QJsonValue &value : results) {
        const QJsonObject item = value.toObject();
        totalChecked += item.value(QStringLiteral("checked")).toInt();
        if (item.value(QStringLiteral("ok")).toBool()) {
            continue;
        }
        const QString path = item.value(QStringLiteral("path")).toString();
        failingPaths.append(path);
        const QString name = requirementDisplayName(path);
        for (const QJsonValue &issue : item.value(QStringLiteral("issues")).toArray()) {
            m_logModel->appendSystemMessage(
                QStringLiteral("%1：%2").arg(name, issue.toString()),
                QStringLiteral("#c42b1c"));
        }
        for (const QJsonValue &issue : item.value(QStringLiteral("parseErrors")).toArray()) {
            m_logModel->appendSystemMessage(
                QStringLiteral("%1：%2").arg(name, issue.toString()),
                QStringLiteral("#c42b1c"));
        }
    }

    if (!result.value(QStringLiteral("ok")).toBool() || !failingPaths.isEmpty()) {
        if (m_dependencyRecheckPhase) {
            m_dependencyRecheckPhase = false;
            setLastError(tr("依赖复检仍未通过，ComfyUI 未启动。"));
            setStatus(Failed);
            m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
            return;
        }
        // Escape hatch for headless runs and tests: no console window can be
        // attended there, so keep the old fail-fast behavior instead of
        // opening install windows that would wait for input forever.
        if (qEnvironmentVariableIsSet("MINIFOX_SKIP_DEPENDENCY_INSTALL")) {
            setLastError(tr("依赖检查失败，ComfyUI 未启动。"));
            setStatus(Failed);
            m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
            return;
        }
        m_logModel->appendSystemMessage(
            tr("发现 %1 个依赖清单不满足，正在打开安装窗口…").arg(failingPaths.size()),
            QStringLiteral("#9d5d00"));
        m_dependencyPendingInstalls = failingPaths;
        m_dependencyRecheckPaths = failingPaths;
        startNextDependencyInstall();
        return;
    }

    if (m_dependencyRecheckPhase) {
        m_dependencyRecheckPhase = false;
        m_logModel->appendSystemMessage(
            tr("依赖复检通过（已检查 %1 项），准备启动 ComfyUI。").arg(totalChecked),
            QStringLiteral("#0f7b0f"));
    } else {
        m_logModel->appendSystemMessage(
            tr("依赖检查通过（已检查 %1 项），准备启动 ComfyUI。").arg(totalChecked),
            QStringLiteral("#0f7b0f"));
    }
    launchConfiguredProcess();
}

QStringList RuntimeManager::collectRequirementFiles() const
{
    const LaunchCommandBuilder::Result command =
        LaunchCommandBuilder::build(m_configuration->currentProfileSnapshot());
    const QDir root(command.workingDirectory);
    QStringList files;
    const QString kernelRequirements = root.filePath(QStringLiteral("requirements.txt"));
    if (QFileInfo::exists(kernelRequirements)) {
        files.append(kernelRequirements);
    }
    const QDir customNodes(root.filePath(QStringLiteral("custom_nodes")));
    if (customNodes.exists()) {
        const QFileInfoList entries = customNodes.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries) {
            const QString name = entry.fileName();
            if (name.compare(QStringLiteral("__pycache__"), Qt::CaseInsensitive) == 0
                || name.endsWith(QStringLiteral(".disabled"), Qt::CaseInsensitive)) {
                continue;
            }
            const QString requirements = QDir(entry.absoluteFilePath())
                .filePath(QStringLiteral("requirements.txt"));
            if (QFileInfo::exists(requirements)) {
                files.append(requirements);
            }
        }
    }
    return files;
}

void RuntimeManager::startNextDependencyInstall()
{
    if (m_dependencyPendingInstalls.isEmpty()) {
        // Every failing file got its install window; re-check exactly those files
        // before ComfyUI is allowed to start.
        m_dependencyRecheckPhase = true;
        const LaunchCommandBuilder::Result command =
            LaunchCommandBuilder::build(m_configuration->currentProfileSnapshot());
        QProcessEnvironment environment = command.environment;
        m_settings->applyToProcessEnvironment(environment);
        m_dependencyCheck.setWorkingDirectory(command.workingDirectory);
        m_dependencyCheck.setProcessEnvironment(environment);
        m_dependencyCheck.setProgram(command.program);
        QStringList arguments{QStringLiteral("-c"), dependencyCheckerScript()};
        arguments.append(m_dependencyRecheckPaths);
        m_dependencyCheck.setArguments(arguments);
        m_logModel->appendSystemMessage(tr("依赖安装结束，正在复检…"),
                                        QStringLiteral("#0067c0"));
        m_dependencyCheck.start();
        return;
    }

    m_dependencyCurrentPath = m_dependencyPendingInstalls.takeFirst();
    const QString name = requirementDisplayName(m_dependencyCurrentPath);
    const LaunchCommandBuilder::Result command =
        LaunchCommandBuilder::build(m_configuration->currentProfileSnapshot());

    QString directoryError;
    if (!PortablePaths::ensureDataDirectory(&directoryError)) {
        setLastError(tr("无法创建数据目录，依赖安装已中止：%1").arg(directoryError));
        setStatus(Failed);
        m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
        return;
    }
    const QString stamp = QStringLiteral("%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch());
    m_dependencyBatPath = QDir(PortablePaths::dataDirectory())
        .filePath(QStringLiteral("dep-startup-%1.bat").arg(stamp));

    // A failing install leaves the console open so the pip error stays readable;
    // the exit code still reaches the launcher through cmd.exe.
    const QString bat = QStringLiteral(
        "@echo off\r\n"
        "chcp 65001 >nul\r\n"
        "title Minifox - %1\r\n"
        "echo [Minifox] 正在安装依赖...\r\n"
        "echo [Minifox] Python: \"%2\"\r\n"
        "echo [Minifox] Requirements: \"%3\"\r\n"
        "echo.\r\n"
        "\"%2\" -m pip install -r \"%3\"\r\n"
        "set CODE=%ERRORLEVEL%\r\n"
        "if not \"%CODE%\"==\"0\" (\r\n"
        "    echo.\r\n"
        "    echo [Minifox] 依赖安装失败，按任意键关闭窗口…\r\n"
        "    pause >nul\r\n"
        ")\r\n"
        "exit /b %CODE%\r\n").arg(name,
                                QDir::toNativeSeparators(command.program),
                                QDir::toNativeSeparators(m_dependencyCurrentPath));
    QFile batFile(m_dependencyBatPath);
    if (!batFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setLastError(tr("无法创建 %1 的依赖安装脚本。").arg(name));
        setStatus(Failed);
        m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
        return;
    }
    batFile.write(bat.toUtf8());
    batFile.close();

    QProcessEnvironment environment = command.environment;
    m_settings->applyToProcessEnvironment(environment);
    m_dependencyInstall.setWorkingDirectory(command.workingDirectory);
    m_dependencyInstall.setProcessEnvironment(environment);
    m_dependencyInstall.setProgram(QStringLiteral("cmd.exe"));
    m_dependencyInstall.setArguments({QStringLiteral("/c"),
                                      QDir::toNativeSeparators(m_dependencyBatPath)});
#ifdef Q_OS_WIN
    m_dependencyInstall.setCreateProcessArgumentsModifier(configureInteractiveConsole);
#endif
    m_logModel->appendSystemMessage(
        tr("正在安装 %1 的依赖（安装窗口关闭后继续）…").arg(name),
        QStringLiteral("#9d5d00"));
    m_dependencyInstall.start();
}

void RuntimeManager::handleDependencyInstallStarted()
{
    const qint64 installPid = m_dependencyInstall.processId();
    if (m_processJob.attach(installPid)) {
        return;
    }

    m_startupAborted = true;
    setLastError(tr("无法将依赖安装进程加入安全作业，已中止启动。"));
    setStatus(Failed);
    m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
    forceKillProcessTree(installPid);
    m_dependencyInstall.kill();
}

void RuntimeManager::handleDependencyInstallFinished(int exitCode,
                                                     QProcess::ExitStatus exitStatus)
{
    m_processJob.reset();
    QFile::remove(m_dependencyBatPath);
    const QString name = requirementDisplayName(m_dependencyCurrentPath);

    if (m_stopRequested) {
        if (m_process.state() == QProcess::NotRunning
            && m_dependencyCheck.state() == QProcess::NotRunning) {
            m_forceStopTimer.stop();
            setStatus(Stopped);
            emit runtimeInfoChanged();
        }
        return;
    }
    if (m_startupAborted) {
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        m_logModel->appendSystemMessage(
            tr("%1 的依赖安装失败（退出代码 %2），继续处理其余清单。")
                .arg(name).arg(exitCode),
            QStringLiteral("#c42b1c"));
    } else {
        m_logModel->appendSystemMessage(tr("%1：依赖安装完成。").arg(name),
                                        QStringLiteral("#0f7b0f"));
    }
    startNextDependencyInstall();
}

void RuntimeManager::launchConfiguredProcess()
{
    if (m_stopRequested || m_startupAborted) {
        return;
    }

    LaunchCommandBuilder::Result command =
        LaunchCommandBuilder::build(m_configuration->currentProfileSnapshot());
    m_settings->applyToProcessEnvironment(command.environment);
    if (m_zludaEnabled) {
        ZludaBootstrap::apply(m_zludaPreparation, m_zludaRocmBin, command.environment);
    }
    QString progressBridgeError;
    const QString progressBridge = materializeProgressBridge(&progressBridgeError);
    if (!progressBridge.isEmpty()
        && !command.arguments.isEmpty()
        && command.arguments.constFirst() == QStringLiteral("main.py")) {
        command.environment.insert(
            QStringLiteral("MINIFOX_COMFY_MAIN"),
            QDir(command.workingDirectory).filePath(QStringLiteral("main.py")));
        command.arguments[0] = progressBridge;
    } else if (!progressBridgeError.isEmpty()) {
        m_logModel->appendSystemMessage(progressBridgeError, QStringLiteral("#9d5d00"));
    }
    m_logModel->appendSystemMessage(command.preview);
    if (m_settings->proxyMode() == QStringLiteral("manual")) {
        const QString proxyUrl = m_settings->proxyUrl();
        m_logModel->appendSystemMessage(proxyUrl.isEmpty()
                                            ? tr("手动代理未启用：代理主机为空或无效。")
                                            : tr("已为 ComfyUI 子进程设置代理：%1").arg(proxyUrl),
                                        proxyUrl.isEmpty() ? QStringLiteral("#9d5d00")
                                                           : QStringLiteral("#0067c0"));
    } else if (m_settings->proxyMode() == QStringLiteral("none")) {
        m_logModel->appendSystemMessage(tr("已移除 ComfyUI 子进程的代理环境变量。"));
    }

    m_process.setWorkingDirectory(command.workingDirectory);
    m_process.setProcessEnvironment(command.environment);
    m_process.setProgram(command.program);
    m_process.setArguments(command.arguments);
    m_process.start();
}

void RuntimeManager::terminateTrackedProcessTree()
{
    const qint64 dependencyPid = m_dependencyCheck.state() == QProcess::NotRunning
        ? 0 : m_dependencyCheck.processId();
    const qint64 installPid = m_dependencyInstall.state() == QProcess::NotRunning
        ? 0 : m_dependencyInstall.processId();
    const qint64 runtimePid = m_process.state() == QProcess::NotRunning
        ? 0 : (m_processId > 0 ? m_processId : m_process.processId());

    // taskkill is the fallback for the only unsafe case: Windows refused the Job
    // assignment.  When assignment succeeded, closing/terminating the Job also makes
    // abnormal launcher termination release every descendant and the listening port.
    if (!m_processJob.isAttached()) {
        forceKillProcessTree(dependencyPid);
        forceKillProcessTree(installPid);
        forceKillProcessTree(runtimePid);
    } else {
        m_processJob.terminate();
    }

    if (m_dependencyCheck.state() != QProcess::NotRunning) {
        m_dependencyCheck.kill();
        m_dependencyCheck.waitForFinished(2000);
    }
    if (m_dependencyInstall.state() != QProcess::NotRunning) {
        m_dependencyInstall.kill();
        m_dependencyInstall.waitForFinished(2000);
    }
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(3000);
    }
    m_processJob.reset();
}

void RuntimeManager::setStatus(Status status)
{
    if (status == m_status) {
        return;
    }
    m_status = status;
    emit statusChanged();
}

void RuntimeManager::setLastError(const QString &message)
{
    if (message == m_lastError) {
        return;
    }
    m_lastError = message;
    emit lastErrorChanged();
}

void RuntimeManager::setServiceReady(bool ready)
{
    if (ready == m_serviceReady) {
        return;
    }
    m_serviceReady = ready;
    emit serviceReadyChanged();
}

void RuntimeManager::updateCommandPreview()
{
    const LaunchCommandBuilder::Result command =
        LaunchCommandBuilder::build(m_configuration->currentProfileSnapshot());
    QStringList proxyCommands;
    if (m_settings->proxyMode() == QStringLiteral("manual")) {
        const QString proxyUrl = m_settings->proxyUrl();
        if (!proxyUrl.isEmpty()) {
            proxyCommands = {
                QStringLiteral("set \"HTTP_PROXY=%1\"").arg(proxyUrl),
                QStringLiteral("set \"HTTPS_PROXY=%1\"").arg(proxyUrl),
                QStringLiteral("set \"ALL_PROXY=%1\"").arg(proxyUrl)
            };
        }
    } else if (m_settings->proxyMode() == QStringLiteral("none")) {
        proxyCommands = {
            QStringLiteral("set \"HTTP_PROXY=\""),
            QStringLiteral("set \"HTTPS_PROXY=\""),
            QStringLiteral("set \"ALL_PROXY=\"")
        };
    }
    proxyCommands.append(command.preview);
    m_commandPreview = proxyCommands.join(QStringLiteral(" && "));

    QString listen = m_configuration->parameterValue(QStringLiteral("listen")).toString();
    listen = listen.split(QLatin1Char(','), Qt::SkipEmptyParts).value(0, QStringLiteral("127.0.0.1")).trimmed();
    if (listen.isEmpty() || listen == QStringLiteral("0.0.0.0") || listen == QStringLiteral("::")
        || listen == QStringLiteral("[::]")) {
        listen = QStringLiteral("127.0.0.1");
    }
    if (listen.contains(QLatin1Char(':')) && !listen.startsWith(QLatin1Char('['))) {
        listen = QStringLiteral("[%1]").arg(listen);
    }
    const bool tls = !m_configuration->parameterValue(QStringLiteral("tlsKeyfile")).toString().isEmpty()
        && !m_configuration->parameterValue(QStringLiteral("tlsCertfile")).toString().isEmpty();
    const int port = m_configuration->parameterValue(QStringLiteral("port")).toInt();
    m_serviceUrl = QStringLiteral("%1://%2:%3").arg(tls ? QStringLiteral("https") : QStringLiteral("http"), listen).arg(port);
    emit commandPreviewChanged();
}

void RuntimeManager::updateUptime()
{
    if (!m_elapsed.isValid()) {
        return;
    }
    const qint64 totalSeconds = m_elapsed.elapsed() / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    m_uptime = QStringLiteral("%1:%2:%3")
                   .arg(hours, 2, 10, QLatin1Char('0'))
                   .arg(minutes, 2, 10, QLatin1Char('0'))
                   .arg(seconds, 2, 10, QLatin1Char('0'));
    emit runtimeInfoChanged();
}

void RuntimeManager::checkReadiness()
{
    if (m_serviceReady || m_process.state() == QProcess::NotRunning || m_readinessReply) {
        return;
    }
    QNetworkRequest request(QUrl(m_serviceUrl + QStringLiteral("/system_stats")));
    request.setTransferTimeout(700);
    m_readinessReply = m_network->get(request);
    connect(m_readinessReply, &QNetworkReply::finished, this, &RuntimeManager::handleReadinessReply);
}

void RuntimeManager::cancelReadinessCheck()
{
    QNetworkReply *reply = m_readinessReply.data();
    m_readinessReply = nullptr;
    if (!reply) {
        return;
    }

    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void RuntimeManager::handleReadinessReply()
{
    QNetworkReply *reply = m_readinessReply.data();
    m_readinessReply = nullptr;
    if (!reply) {
        return;
    }
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ready = reply->error() == QNetworkReply::NoError
        && statusCode >= 200 && statusCode < 500;
    const QByteArray payload = ready ? reply->readAll() : QByteArray{};
    reply->deleteLater();
    if (ready) {
        updateAcceleratorSummaryFromSystemStats(payload);
        setServiceReady(true);
        setStatus(Running);
        m_readinessTimer.stop();
        m_logModel->appendSystemMessage(tr("ComfyUI 服务已就绪：%1").arg(m_serviceUrl),
                                        QStringLiteral("#0f7b0f"));
    }
}

void RuntimeManager::handleProcessStarted()
{
    m_processId = m_process.processId();
    if (!m_processJob.attach(m_processId)) {
        m_startupAborted = true;
        setLastError(tr("无法将 ComfyUI 加入安全作业，已中止启动以避免残留 Python 进程。"));
        setStatus(Failed);
        m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
        emit runtimeInfoChanged();
        terminateTrackedProcessTree();
        return;
    }

    m_elapsed.start();
    m_uptimeTimer.start();
    m_readinessTimer.start();
    m_logModel->appendSystemMessage(tr("进程已启动，PID %1。").arg(m_processId),
                                    QStringLiteral("#0067c0"));
    emit runtimeInfoChanged();
    checkReadiness();
}

void RuntimeManager::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_forceStopTimer.stop();
    m_readinessTimer.stop();
    cancelReadinessCheck();
    m_logModel->flush();
    m_lastExitCode = exitCode;
    m_processId = 0;
    m_uptimeTimer.stop();
    m_elapsed.invalidate();
    setServiceReady(false);
    m_processJob.reset();

    if (m_startupAborted) {
        setStatus(Failed);
        emit runtimeInfoChanged();
        return;
    }

    const bool successful = exitStatus == QProcess::NormalExit && exitCode == 0;
    if (m_stopRequested || successful) {
        setStatus(Stopped);
        m_logModel->appendSystemMessage(tr("ComfyUI 已停止，退出代码 %1。").arg(exitCode));
    } else {
        setStatus(Failed);
        setLastError(tr("ComfyUI 异常退出，退出代码 %1。").arg(exitCode));
        m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
    }
    emit runtimeInfoChanged();
}

void RuntimeManager::handleProcessError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        setLastError(tr("进程无法启动：%1").arg(m_process.errorString()));
        setStatus(Failed);
        m_logModel->appendSystemMessage(m_lastError, QStringLiteral("#c42b1c"));
    } else if (error != QProcess::Crashed || !m_stopRequested) {
        setLastError(m_process.errorString());
    }
}
