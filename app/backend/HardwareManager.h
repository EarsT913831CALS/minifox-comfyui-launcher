#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>

class ConfigurationManager;

class HardwareManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool detecting READ detecting NOTIFY detectionChanged)
    Q_PROPERTY(bool hasCuda READ hasCuda NOTIFY detectionChanged)
    Q_PROPERTY(QVariantList cudaDevices READ cudaDevices NOTIFY detectionChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY detectionChanged)
    Q_PROPERTY(QString detectionSource READ detectionSource NOTIFY detectionChanged)
    Q_PROPERTY(QString torchVersion READ torchVersion NOTIFY detectionChanged)
    Q_PROPERTY(QString cudaRuntimeVersion READ cudaRuntimeVersion NOTIFY detectionChanged)
    Q_PROPERTY(QString driverVersion READ driverVersion NOTIFY detectionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY detectionChanged)

public:
    explicit HardwareManager(ConfigurationManager *configuration, QObject *parent = nullptr);

    bool detecting() const;
    bool hasCuda() const;
    QVariantList cudaDevices() const;
    QString summary() const;
    QString detectionSource() const;
    QString torchVersion() const;
    QString cudaRuntimeVersion() const;
    QString driverVersion() const;
    QString lastError() const;

    Q_INVOKABLE void detect();
    Q_INVOKABLE void applyAllDevices();
    Q_INVOKABLE void useDevice(int index);
    void retranslate();

signals:
    void detectionChanged();

private:
    enum class DetectionMode {
        None,
        Python,
        NvidiaSmi
    };

    void startPythonDetection(const QString &pythonPath);
    void startNvidiaSmiDetection();
    void startProcess(DetectionMode mode, const QString &program, const QStringList &arguments);
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    bool parsePythonResult(const QByteArray &output);
    bool parseNvidiaSmiResult(const QByteArray &output);
    void finishWithError(const QString &message);
    QString findNvidiaSmi() const;

    ConfigurationManager *m_configuration;
    QProcess m_process;
    QTimer m_timeout;
    DetectionMode m_mode = DetectionMode::None;
    QVariantList m_cudaDevices;
    QString m_detectionSource;
    QString m_torchVersion;
    QString m_cudaRuntimeVersion;
    QString m_driverVersion;
    QString m_lastError;
    QString m_lastPythonPath;
    bool m_detecting = false;
};
