#include "RuntimeManager.h"

#include "ApplicationSettings.h"
#include "ConfigurationManager.h"
#include "LaunchCommandBuilder.h"
#include "LogModel.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkProxy>
#include <QUrlQuery>

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

    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_logModel->appendStandardOutput(m_process.readAllStandardOutput());
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_logModel->appendStandardError(m_process.readAllStandardError());
    });
    connect(&m_process, &QProcess::started, this, &RuntimeManager::handleProcessStarted);
    connect(&m_process, &QProcess::finished, this, &RuntimeManager::handleProcessFinished);
    connect(&m_process, &QProcess::errorOccurred, this, &RuntimeManager::handleProcessError);

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
    connect(m_settings, &ApplicationSettings::proxyChanged,
            this, &RuntimeManager::updateCommandPreview);

    updateCommandPreview();
}

RuntimeManager::~RuntimeManager()
{
    cancelReadinessReply();
    if (m_process.state() != QProcess::NotRunning) {
        m_processJob.terminate();
        m_process.kill();
        m_process.waitForFinished(2000);
    }
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
    return (m_status == Stopped || m_status == Failed) && m_process.state() == QProcess::NotRunning;
}

bool RuntimeManager::canStop() const
{
    return m_process.state() != QProcess::NotRunning && m_status != Stopping;
}

qint64 RuntimeManager::processId() const { return m_processId; }
QString RuntimeManager::uptime() const { return m_uptime; }
bool RuntimeManager::serviceReady() const { return m_serviceReady; }
QString RuntimeManager::serviceUrl() const { return m_serviceUrl; }
QString RuntimeManager::commandPreview() const { return m_commandPreview; }
LogModel *RuntimeManager::logModel() const { return m_logModel; }
QString RuntimeManager::lastError() const { return m_lastError; }
int RuntimeManager::lastExitCode() const { return m_lastExitCode; }

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

    LaunchCommandBuilder::Result command =
        LaunchCommandBuilder::build(m_configuration->currentProfileSnapshot());
    m_settings->applyToProcessEnvironment(command.environment);

    m_logModel->clear();
    m_logModel->appendSystemMessage(tr("正在启动配置“%1”…").arg(m_configuration->currentProfileName()),
                                    QStringLiteral("#0067c0"));
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
    m_processJob.reset();
    m_stopRequested = false;
    m_lastExitCode = 0;
    m_processId = 0;
    m_uptime = QStringLiteral("00:00:00");
    setServiceReady(false);
    setLastError({});
    setStatus(Starting);
    emit runtimeInfoChanged();

    m_process.setWorkingDirectory(command.workingDirectory);
    m_process.setProcessEnvironment(command.environment);
    m_process.setProgram(command.program);
    m_process.setArguments(command.arguments);
    m_process.start();
}

void RuntimeManager::stop()
{
    if (!canStop()) {
        return;
    }
    m_stopRequested = true;
    setStatus(Stopping);
    m_logModel->appendSystemMessage(tr("正在停止 ComfyUI…"), QStringLiteral("#9d5d00"));
    m_process.terminate();
    m_forceStopTimer.start();
}

void RuntimeManager::forceStop()
{
    if (m_process.state() == QProcess::NotRunning) {
        return;
    }
    m_stopRequested = true;
    m_logModel->appendSystemMessage(tr("正在强制终止 ComfyUI 进程树…"), QStringLiteral("#c42b1c"));
    if (!m_processJob.terminate()) {
        m_logModel->appendSystemMessage(
            tr("无法完整终止 ComfyUI 进程树，正在终止主进程。"),
            QStringLiteral("#c42b1c"));
    }
    m_process.kill();
}

bool RuntimeManager::openWebUi()
{
    const QUrl url(m_serviceUrl);
    if (!url.isValid()) {
        setLastError(tr("服务地址无效。"));
        return false;
    }
    const bool opened = QDesktopServices::openUrl(url);
    if (!opened) {
        setLastError(tr("无法打开 WebUI。"));
    }
    return opened;
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

void RuntimeManager::cancelReadinessReply()
{
    QNetworkReply *reply = m_readinessReply.data();
    if (!reply) {
        return;
    }
    m_readinessReply = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void RuntimeManager::handleReadinessReply()
{
    QNetworkReply *reply = m_readinessReply.data();
    if (!reply) {
        return;
    }
    m_readinessReply = nullptr;
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ready = reply->error() == QNetworkReply::NoError
        && statusCode >= 200 && statusCode < 500;
    reply->deleteLater();
    if (ready) {
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
    m_elapsed.start();
    m_uptimeTimer.start();
    m_readinessTimer.start();
    if (!m_processJob.attach(m_processId)) {
        m_logModel->appendSystemMessage(
            tr("无法关联 Windows Job Object；强制停止时将使用进程树终止回退。"),
            QStringLiteral("#9d5d00"));
    }
    m_logModel->appendSystemMessage(tr("进程已启动，PID %1。").arg(m_processId),
                                    QStringLiteral("#0067c0"));
    emit runtimeInfoChanged();
    checkReadiness();
}

void RuntimeManager::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_forceStopTimer.stop();
    m_readinessTimer.stop();
    cancelReadinessReply();
    m_logModel->flush();
    m_lastExitCode = exitCode;
    m_processId = 0;
    m_uptimeTimer.stop();
    m_elapsed.invalidate();
    setServiceReady(false);
    m_processJob.reset();

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
