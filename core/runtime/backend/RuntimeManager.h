#pragma once
#include "OperationLease.h"
#include "LaunchCommandBuilder.h"

#include "ProcessJob.h"
#include "ZludaBootstrap.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

class ConfigurationManager;
class ApplicationSettings;
class LogModel;
class QNetworkAccessManager;
class QNetworkReply;

class RuntimeManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool canStart READ canStart NOTIFY statusChanged)
    Q_PROPERTY(bool canStop READ canStop NOTIFY statusChanged)
    Q_PROPERTY(bool active READ active NOTIFY statusChanged)
    Q_PROPERTY(qint64 processId READ processId NOTIFY runtimeInfoChanged)
    Q_PROPERTY(QString uptime READ uptime NOTIFY runtimeInfoChanged)
    Q_PROPERTY(bool serviceReady READ serviceReady NOTIFY serviceReadyChanged)
    Q_PROPERTY(QString serviceUrl READ serviceUrl NOTIFY commandPreviewChanged)
    Q_PROPERTY(QString acceleratorSummary READ acceleratorSummary NOTIFY runtimeInfoChanged)
    Q_PROPERTY(QVariantList acceleratorDevices READ acceleratorDevices NOTIFY runtimeInfoChanged)
    Q_PROPERTY(QString commandPreview READ commandPreview NOTIFY commandPreviewChanged)
    Q_PROPERTY(LogModel *logModel READ logModel CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(int lastExitCode READ lastExitCode NOTIFY runtimeInfoChanged)

public:
    enum Status {
        Stopped,
        Starting,
        Running,
        Stopping,
        Failed
    };
    Q_ENUM(Status)

    explicit RuntimeManager(ConfigurationManager *configuration,
                            ApplicationSettings *settings,
                            QObject *parent = nullptr);
    ~RuntimeManager() override;

    Status status() const;
    QString statusText() const;
    bool canStart() const;
    bool canStop() const;
    bool active() const;
    qint64 processId() const;
    QString uptime() const;
    bool serviceReady() const;
    QString serviceUrl() const;
    QString acceleratorSummary() const;
    QVariantList acceleratorDevices() const;
    QString commandPreview() const;
    LogModel *logModel() const;
    QString lastError() const;
    int lastExitCode() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void forceStop();
    Q_INVOKABLE void shutdown();
    Q_INVOKABLE bool openCommandPrompt();
    Q_INVOKABLE bool exportLog(const QUrl &fileUrl);
    void setVersionOperationBlocked(bool blocked);
    void retranslate();

signals:
    void statusChanged();
    void runtimeInfoChanged();
    void serviceReadyChanged();
    void commandPreviewChanged();
    void lastErrorChanged();

private:
    void setStatus(Status status);
    void setLastError(const QString &message);
    void setServiceReady(bool ready);
    void updateCommandPreview();
    void updateUptime();
    void checkReadiness();
    void cancelReadinessCheck();
    void handleReadinessReply();
    void handleProcessStarted();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessError(QProcess::ProcessError error);
    void beginZludaBootstrap();
    bool prepareZludaRuntime();
    void updateAcceleratorSummaryFromSystemStats(const QByteArray &payload);
    void beginDependencyCheck();
    void handleDependencyCheckStarted();
    void handleDependencyCheckFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void startNextDependencyInstall();
    void handleDependencyInstallStarted();
    void handleDependencyInstallFinished(int exitCode, QProcess::ExitStatus exitStatus);
    QStringList collectRequirementFiles() const;
    void launchConfiguredProcess();
    void terminateTrackedProcessTree();

    LaunchCommandBuilder::Result operationCommand() const;
    QVariant operationParameter(const QString &key) const;
    OperationLease m_operationLease;
    QVariantMap m_startProfile;
    LaunchCommandBuilder::Result m_startCommand;
    QString m_startProxyMode, m_startProxyUrl;
    ConfigurationManager *m_configuration;
    ApplicationSettings *m_settings;
    QProcess m_process;
    QProcess m_dependencyCheck;
    QProcess m_dependencyInstall;
    QProcess m_commandPrompt;
    QStringList m_dependencyPendingInstalls;
    QStringList m_dependencyRecheckPaths;
    QString m_dependencyCurrentPath;
    bool m_dependencyRecheckPhase = false;
    ZludaBootstrap::Preparation m_zludaPreparation;
    QString m_zludaRocmBin;
    bool m_zludaEnabled = false;
    LogModel *m_logModel;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_readinessReply;
    QTimer m_readinessTimer;
    QTimer m_uptimeTimer;
    QTimer m_forceStopTimer;
    QElapsedTimer m_elapsed;
    ProcessJob m_processJob;
    ProcessJob m_commandPromptJob;
    Status m_status = Stopped;
    qint64 m_processId = 0;
    QString m_uptime = QStringLiteral("00:00:00");
    bool m_serviceReady = false;
    QString m_serviceUrl;
    QString m_acceleratorSummary;
    QVariantList m_acceleratorDevices;
    QString m_commandPreview;
    QString m_lastError;
    int m_lastExitCode = 0;
    bool m_stopRequested = false;
    bool m_startupAborted = false;
    bool m_versionOperationBlocked = false;
};
