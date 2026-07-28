#pragma once

#include "ProcessJob.h"
#include "ZludaBootstrap.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QUrl>

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
    void handleZludaProbeFinished(int exitCode, QProcess::ExitStatus exitStatus);
    bool prepareZludaRuntime();
    void startNextZludaBootstrapDetection();
    void finishZludaBootstrap();
    void drainZludaProbeOutput();
    void handleZludaProbeTimeout();
    void beginDependencyCheck();
    void handleDependencyCheckStarted();
    void handleDependencyCheckFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void startNextDependencyInstall();
    void handleDependencyInstallStarted();
    void handleDependencyInstallFinished(int exitCode, QProcess::ExitStatus exitStatus);
    QStringList collectRequirementFiles() const;
    void launchConfiguredProcess();
    void terminateTrackedProcessTree();

    ConfigurationManager *m_configuration;
    ApplicationSettings *m_settings;
    QProcess m_process;
    QProcess m_zludaProbe;
    QProcess m_dependencyCheck;
    QProcess m_dependencyInstall;
    QStringList m_dependencyPendingInstalls;
    QStringList m_dependencyRecheckPaths;
    QString m_dependencyBatPath;
    QString m_dependencyCurrentPath;
    bool m_dependencyRecheckPhase = false;
    enum class ZludaProbeStage {
        None,
        Detection,
        BootstrappedDetection
    };
    ZludaProbeStage m_zludaProbeStage = ZludaProbeStage::None;
    ZludaBootstrap::Preparation m_zludaPreparation;
    QStringList m_zludaRocmCandidates;
    QString m_zludaRocmBin;
    QString m_zludaLastProbeError;
    QByteArray m_zludaStandardOutput;
    QByteArray m_zludaStandardError;
    bool m_zludaOutputTruncated = false;
    bool m_zludaEnabled = false;
    LogModel *m_logModel;
    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_readinessReply;
    QTimer m_readinessTimer;
    QTimer m_uptimeTimer;
    QTimer m_forceStopTimer;
    QTimer m_zludaProbeTimer;
    QElapsedTimer m_elapsed;
    ProcessJob m_processJob;
    Status m_status = Stopped;
    qint64 m_processId = 0;
    QString m_uptime = QStringLiteral("00:00:00");
    bool m_serviceReady = false;
    QString m_serviceUrl;
    QString m_commandPreview;
    QString m_lastError;
    int m_lastExitCode = 0;
    bool m_stopRequested = false;
    bool m_startupAborted = false;
};
