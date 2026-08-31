#pragma once

#include "RepositoryUpdateJob.h"

#include <QObject>
#include <QQueue>
#include <QSet>

class RepositoryUpdateCoordinator final : public QObject
{
    Q_OBJECT

public:
    explicit RepositoryUpdateCoordinator(QObject *parent = nullptr);

    bool running() const;
    void start(const QList<RepositoryUpdateSpec> &specs, int maximumParallelJobs);
    void cancel();

signals:
    void statusChanged(const QString &message);
    void jobFinished(const RepositoryUpdateResult &result);
    void finished(int succeeded, int failed, const QStringList &errors,
                  const QStringList &incompleteNames);

private:
    void startAvailableJobs();
    void handleJobFinished(RepositoryUpdateJob *job,
                           const RepositoryUpdateResult &result);

    QQueue<RepositoryUpdateSpec> m_queue;
    QSet<RepositoryUpdateJob *> m_activeJobs;
    QStringList m_errors;
    QStringList m_incompleteNames;
    int m_maximumParallelJobs = 1;
    int m_succeeded = 0;
    int m_failed = 0;
};
