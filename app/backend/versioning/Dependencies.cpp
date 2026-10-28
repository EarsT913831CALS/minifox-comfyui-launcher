#include "Dependencies.h"

#include "DependencyInstaller.h"
#include "ProcessTextDecoder.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
constexpr int kCheckTimeoutMs = 180000;
constexpr qsizetype kOutputLimit = 256 * 1024;
}

Dependencies::Dependencies(QObject *parent) : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_timeout.setSingleShot(true);
    m_skipReport.setSingleShot(true);
    m_skipReport.setInterval(2000);
    connect(&m_skipReport, &QTimer::timeout, this, [this] {
        emit completed(false, tr("没有配置可用的 Python，跳过了 %1 的依赖安装。").arg(m_skipped.join(QStringLiteral("、"))));
        m_skipped.clear();
    });
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_timedOut = true;
        m_job.terminate();
        m_process.kill();
    });
    connect(&m_process, &QProcess::readyRead, this, [this] {
        m_output += m_process.readAll();
        if (m_output.size() > kOutputLimit) m_output.remove(0, m_output.size() - kOutputLimit);
    });
    // pip spawns its own children; the job takes them down with it.
    connect(&m_process, &QProcess::started, this, [this] {
        if (!m_job.attach(m_process.processId())) m_process.kill();
    });
    connect(&m_process, &QProcess::finished, this, &Dependencies::finish);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(-1, QProcess::CrashExit);
    });
}

Dependencies::~Dependencies()
{
    m_queue.clear();
    m_process.disconnect(this);
    m_job.terminate();
    m_process.kill();
    m_process.waitForFinished(3000);
}

void Dependencies::enqueue(const QString &directory, const QString &python, const QProcessEnvironment &environment)
{
    if (!QFileInfo(QDir(directory).filePath(QStringLiteral("requirements.txt"))).isFile()) return;
    const QString clean = QDir::cleanPath(directory);
    if (python.isEmpty() || !QFileInfo(python).isFile()) {
        // Without Python every install fails the same way; say it once per batch.
        m_skipped.append(QFileInfo(clean).fileName());
        m_skipReport.start();
        return;
    }
    if (m_running && m_current.directory == clean) return;
    for (const Item &item : std::as_const(m_queue)) {
        if (item.directory == clean) return;
    }
    m_queue.append({clean, python, environment});
    if (!m_running) startNext();
}

void Dependencies::cancel()
{
    if (!m_running) return;
    m_queue.clear();
    m_cancelled = true;
    m_job.terminate();
    m_process.kill();
}

QString Dependencies::name() const
{
    return QFileInfo(m_current.directory).fileName();
}

void Dependencies::startNext()
{
    m_cancelled = false;
    if (m_queue.isEmpty()) {
        setRunning(false);
        return;
    }
    m_current = m_queue.takeFirst();
    setRunning(true);
    start(false);
}

void Dependencies::start(bool install)
{
    m_installing = install;
    m_timedOut = false;
    m_done = false;
    m_output.clear();
    m_job.reset();
    const QString requirements = QDir(m_current.directory).filePath(QStringLiteral("requirements.txt"));
    m_process.setProcessEnvironment(m_current.environment);
    m_process.setWorkingDirectory(m_current.directory);
    if (install) {
        DependencyInstaller::configure(m_process, m_current.python, requirements);
        m_timeout.stop();
    } else {
        DependencyInstaller::configureCheck(m_process, m_current.python, {requirements});
        m_timeout.start(kCheckTimeoutMs);
    }
    m_process.start();
}

void Dependencies::finish(int exitCode, QProcess::ExitStatus status)
{
    // errorOccurred and finished can both arrive for one run.
    if (m_done) return;
    m_done = true;
    m_timeout.stop();
    m_job.reset();
    m_output += m_process.readAll();

    bool next = true;
    if (m_cancelled) {
        emit completed(false, tr("已取消 %1 的依赖安装，Python 环境里可能只装了一部分。").arg(name()));
    } else if (m_installing) {
        if (status == QProcess::NormalExit && exitCode == 0)
            emit completed(true, tr("已安装 %1 的依赖。").arg(name()));
        else
            emit completed(false, tr("%1 的依赖安装失败（退出代码 %2），详情见弹出的安装窗口。").arg(name()).arg(exitCode));
    } else {
        const QJsonObject result = QJsonDocument::fromJson(m_output.trimmed()).object();
        const QString fatal = result.value(QStringLiteral("fatal")).toString();
        if (m_timedOut) {
            emit completed(false, tr("检查 %1 的依赖超时。").arg(name()));
        } else if (!fatal.isEmpty()) {
            emit completed(false, tr("无法检查 %1 的依赖：%2").arg(name(), fatal));
        } else if (result.isEmpty()) {
            QString detail = ProcessTextDecoder::decode(m_output).trimmed().right(200);
            if (detail.isEmpty()) detail = tr("退出代码 %1").arg(exitCode);
            emit completed(false, tr("无法检查 %1 的依赖：%2").arg(name(), detail));
        } else if (!result.value(QStringLiteral("ok")).toBool()) {
            next = false;
        }
    }
    // Restart outside of this QProcess's own signal handler.
    QMetaObject::invokeMethod(this, [this, next] {
        if (next) startNext();
        else start(true);
    }, Qt::QueuedConnection);
}

void Dependencies::setRunning(bool running)
{
    if (m_running == running) return;
    m_running = running;
    emit runningChanged();
}
