#include "GitProcessRunner.h"

GitProcessRunner::GitProcessRunner(QObject *parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_inactivityTimer.setSingleShot(true);
    m_killTimer.setSingleShot(true);

    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_standardOutput += m_process.readAllStandardOutput();
        resetInactivityTimer();
        emit activity();
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_standardError += m_process.readAllStandardError();
        resetInactivityTimer();
        emit activity();
    });
    connect(&m_process, &QProcess::finished, this, &GitProcessRunner::finish);
    connect(&m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_failedToStart = true;
            QTimer::singleShot(0, this, [this] {
                if (!m_finishing && m_process.state() == QProcess::NotRunning) {
                    finish(-1, QProcess::CrashExit);
                }
            });
        }
    });
    connect(&m_inactivityTimer, &QTimer::timeout, this, [this] {
        if (m_process.state() == QProcess::NotRunning) return;
        m_timedOut = true;
        m_process.terminate();
        m_killTimer.start(3000);
    });
    connect(&m_killTimer, &QTimer::timeout, this, [this] {
        if (m_process.state() != QProcess::NotRunning) m_process.kill();
    });
}

bool GitProcessRunner::running() const
{
    return m_process.state() != QProcess::NotRunning;
}

void GitProcessRunner::start(const QString &program, const QStringList &arguments,
                             const QProcessEnvironment &environment,
                             int inactivityTimeoutMs)
{
    if (running()) return;
    m_standardOutput.clear();
    m_standardError.clear();
    m_inactivityTimeoutMs = inactivityTimeoutMs;
    m_timedOut = false;
    m_failedToStart = false;
    m_finishing = false;
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    m_process.setProcessEnvironment(environment);
    m_process.start();
    resetInactivityTimer();
}

void GitProcessRunner::cancel()
{
    if (!running()) return;
    m_process.terminate();
    m_killTimer.start(3000);
}

void GitProcessRunner::resetInactivityTimer()
{
    if (m_inactivityTimeoutMs > 0) m_inactivityTimer.start(m_inactivityTimeoutMs);
}

void GitProcessRunner::finish(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_finishing) return;
    m_finishing = true;
    m_inactivityTimer.stop();
    m_killTimer.stop();
    m_standardOutput += m_process.readAllStandardOutput();
    m_standardError += m_process.readAllStandardError();
    emit finished({exitCode, exitStatus, m_standardOutput, m_standardError,
                   m_timedOut, m_failedToStart});
}
