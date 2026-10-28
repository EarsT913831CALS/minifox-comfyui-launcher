#pragma once

#include "ProcessJob.h"

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTimer>

// Installs requirements.txt of updated repositories, one at a time. An
// offline check runs first; pip (in a visible console) only when it fails.
class Dependencies final : public QObject
{
    Q_OBJECT

public:
    explicit Dependencies(QObject *parent = nullptr);
    ~Dependencies() override;

    void enqueue(const QString &directory, const QString &python, const QProcessEnvironment &environment);
    bool running() const { return m_running; }
    void cancel();

signals:
    void runningChanged();
    void completed(bool success, const QString &message);

private:
    struct Item {
        QString directory;
        QString python;
        QProcessEnvironment environment;
    };

    void startNext();
    void start(bool install);
    void finish(int exitCode, QProcess::ExitStatus status);
    void setRunning(bool running);
    QString name() const;

    QList<Item> m_queue;
    QStringList m_skipped;
    Item m_current;
    QProcess m_process;
    ProcessJob m_job;
    QTimer m_timeout;
    QTimer m_skipReport;
    QByteArray m_output;
    bool m_running = false;
    bool m_installing = false;
    bool m_cancelled = false;
    bool m_timedOut = false;
    bool m_done = false;
};
