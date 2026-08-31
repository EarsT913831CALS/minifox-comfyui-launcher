#pragma once

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

struct ProcessResult {
    int exitCode = -1;
    QProcess::ExitStatus exitStatus = QProcess::CrashExit;
    QByteArray standardOutput;
    QByteArray standardError;
    bool timedOut = false;
    bool failedToStart = false;
};

class GitProcessRunner final : public QObject
{
    Q_OBJECT

public:
    explicit GitProcessRunner(QObject *parent = nullptr);

    bool running() const;
    void start(const QString &program, const QStringList &arguments,
               const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment(),
               int inactivityTimeoutMs = 120000);
    void cancel();

signals:
    void activity();
    void finished(const ProcessResult &result);

private:
    void resetInactivityTimer();
    void finish(int exitCode, QProcess::ExitStatus exitStatus);

    QProcess m_process;
    QTimer m_inactivityTimer;
    QTimer m_killTimer;
    QByteArray m_standardOutput;
    QByteArray m_standardError;
    int m_inactivityTimeoutMs = 0;
    bool m_timedOut = false;
    bool m_failedToStart = false;
    bool m_finishing = false;
};
