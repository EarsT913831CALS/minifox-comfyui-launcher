#include "ProcessJob.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

ProcessJob::~ProcessJob()
{
    reset();
}

bool ProcessJob::attach(qint64 processId)
{
    reset();

#ifdef Q_OS_WIN
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        return false;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION information {};
    information.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(
            job,
            JobObjectExtendedLimitInformation,
            &information,
            sizeof(information))) {
        CloseHandle(job);
        return false;
    }

    HANDLE process = OpenProcess(
        PROCESS_SET_QUOTA | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        static_cast<DWORD>(processId));
    if (!process) {
        CloseHandle(job);
        return false;
    }

    const bool assigned = AssignProcessToJobObject(job, process);
    CloseHandle(process);
    if (!assigned) {
        CloseHandle(job);
        return false;
    }

    m_jobHandle = job;
    return true;
#else
    Q_UNUSED(processId)
    return false;
#endif
}

bool ProcessJob::terminate(unsigned int exitCode)
{
#ifdef Q_OS_WIN
    return m_jobHandle
        && TerminateJobObject(static_cast<HANDLE>(m_jobHandle), static_cast<UINT>(exitCode));
#else
    Q_UNUSED(exitCode)
    return false;
#endif
}

void ProcessJob::reset()
{
#ifdef Q_OS_WIN
    if (m_jobHandle) {
        CloseHandle(static_cast<HANDLE>(m_jobHandle));
    }
#endif
    m_jobHandle = nullptr;
}

bool ProcessJob::isAttached() const
{
    return m_jobHandle != nullptr;
}
