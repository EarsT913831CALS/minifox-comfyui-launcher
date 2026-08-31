#pragma once

#include <QString>

struct SafeMergeRequest {
    QString gitProgram;
    QString workingRoot;
    QString baseSnapshotRoot;
    QString localSnapshotRoot;
    QString targetSnapshotRoot;
};

enum class SafeMergeError {
    None,
    SnapshotRead,
    MergeProcess,
    WorkingTreeChanged,
    FileWrite
};

struct SafeMergeResult {
    bool success = false;
    SafeMergeError error = SafeMergeError::None;
    QString detail;
};

class SafeMergeEngine final
{
public:
    static SafeMergeResult run(const SafeMergeRequest &request);
};
