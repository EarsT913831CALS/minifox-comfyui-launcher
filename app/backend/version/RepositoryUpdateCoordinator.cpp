#include "RepositoryUpdateCoordinator.h"

#include <utility>

RepositoryUpdateCoordinator::RepositoryUpdateCoordinator(QObject *parent)
    : QObject(parent)
{
}

bool RepositoryUpdateCoordinator::running() const
{
    return !m_queue.isEmpty() || !m_activeJobs.isEmpty();
}

void RepositoryUpdateCoordinator::start(const QList<RepositoryUpdateSpec> &specs,
                                        int maximumParallelJobs)
{
    if (running() || specs.isEmpty()) return;
    for (const RepositoryUpdateSpec &spec : specs) m_queue.enqueue(spec);
    m_maximumParallelJobs = qBound(1, maximumParallelJobs, 2);
    m_errors.clear();
    m_incompleteNames.clear();
    m_succeeded = 0;
    m_failed = 0;
    startAvailableJobs();
}

void RepositoryUpdateCoordinator::cancel()
{
    m_queue.clear();
    for (RepositoryUpdateJob *job : std::as_const(m_activeJobs)) job->cancel();
}

void RepositoryUpdateCoordinator::startAvailableJobs()
{
    while (m_activeJobs.size() < m_maximumParallelJobs && !m_queue.isEmpty()) {
        auto *job = new RepositoryUpdateJob(m_queue.dequeue(), this);
        m_activeJobs.insert(job);
        connect(job, &RepositoryUpdateJob::statusChanged,
                this, &RepositoryUpdateCoordinator::statusChanged);
        connect(job, &RepositoryUpdateJob::finished, this,
                [this, job](const RepositoryUpdateResult &result) {
            handleJobFinished(job, result);
        });
        job->start();
    }
}

void RepositoryUpdateCoordinator::handleJobFinished(
    RepositoryUpdateJob *job, const RepositoryUpdateResult &result)
{
    m_activeJobs.remove(job);
    job->deleteLater();
    if (result.success) {
        ++m_succeeded;
    } else {
        ++m_failed;
        m_errors.append(result.error);
        if (result.incomplete && !m_incompleteNames.contains(result.displayName)) {
            m_incompleteNames.append(result.displayName);
        }
    }
    emit jobFinished(result);
    startAvailableJobs();
    if (!running()) {
        emit finished(m_succeeded, m_failed, m_errors, m_incompleteNames);
    }
}
