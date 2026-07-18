#include "ProcessJob.h"

#ifdef Q_OS_WIN
#include <QHash>
#include <QList>
#include <QSet>

#include <qt_windows.h>
#include <tlhelp32.h>
#endif

#ifdef Q_OS_WIN
namespace {

using ProcessChildren = QMultiHash<DWORD, DWORD>;

ProcessChildren processChildren()
{
    ProcessChildren children;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return children;
    }

    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            children.insert(entry.th32ParentProcessID, entry.th32ProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return children;
}

void collectDescendants(
    DWORD processId,
    const ProcessChildren &children,
    QSet<DWORD> &visited,
    QList<DWORD> &descendants)
{
    for (const DWORD childId : children.values(processId)) {
        if (childId == 0 || visited.contains(childId)) {
            continue;
        }
        visited.insert(childId);
        collectDescendants(childId, children, visited, descendants);
        descendants.append(childId);
    }
}

bool terminateProcess(DWORD processId, UINT exitCode)
{
    const HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, processId);
    if (!process) {
        return GetLastError() == ERROR_INVALID_PARAMETER;
    }
    const bool terminated = TerminateProcess(process, exitCode);
    if (terminated) {
        WaitForSingleObject(process, 1000);
    }
    CloseHandle(process);
    return terminated;
}

bool terminateProcessTree(DWORD processId, UINT exitCode)
{
    const ProcessChildren children = processChildren();
    QList<DWORD> descendants;
    QSet<DWORD> visited {processId};
    collectDescendants(processId, children, visited, descendants);

    bool terminated = true;
    for (const DWORD descendantId : descendants) {
        terminated = terminateProcess(descendantId, exitCode) && terminated;
    }
    return terminateProcess(processId, exitCode) && terminated;
}

} // namespace
#endif

ProcessJob::~ProcessJob()
{
    reset();
}

bool ProcessJob::attach(qint64 processId)
{
    reset();
    m_processId = processId;

#ifdef Q_OS_WIN
    if (processId <= 0) {
        return false;
    }
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
    const UINT nativeExitCode = static_cast<UINT>(exitCode);
    if (m_jobHandle
        && TerminateJobObject(static_cast<HANDLE>(m_jobHandle), nativeExitCode)) {
        return true;
    }
    return m_processId > 0
        && terminateProcessTree(static_cast<DWORD>(m_processId), nativeExitCode);
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
    m_processId = 0;
}

bool ProcessJob::isAttached() const
{
    return m_jobHandle != nullptr;
}
