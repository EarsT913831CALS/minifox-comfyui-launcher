#pragma once

#include <QtGlobal>

class ProcessJob final
{
public:
    ProcessJob() = default;
    ~ProcessJob();

    ProcessJob(const ProcessJob &) = delete;
    ProcessJob &operator=(const ProcessJob &) = delete;

    bool attach(qint64 processId);
    bool terminate(unsigned int exitCode = 1);
    void reset();
    bool isAttached() const;

private:
    void *m_jobHandle = nullptr;
    qint64 m_processId = 0;
};
