#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include "RepositoryGitContext.h"

class QThreadPool;

struct SafeMergeRequest {
    QString gitProgram;
    QString workingRoot;
    QString baseSnapshotRoot;
    QString localSnapshotRoot;
    QString targetSnapshotRoot;
    QString transactionId;
    RepositoryGitContext gitContext;
};

enum class SafeMergeError {
    None,
    SnapshotRead,
    MergeProcess,
    WorkingTreeChanged,
    RecoveryBackup,
    FileWrite
};

struct SafeMergeResult {
    bool success = false;
    SafeMergeError error = SafeMergeError::None;
    QString detail;
    // Files where a local edit and the target changed the same place. The
    // target version won; the local file and a readable log were copied to
    // conflictDirectory first (only for journaled runs with a transaction id).
    QStringList conflictPaths = {};
    QString conflictDirectory = {};
};

// Paths needed to build sparse base, local, and target snapshots.

struct LocalPathEntry {
    QString path;
    bool added = false;
    bool deleted = false;
};

struct RemotePathEntry {
    QString path;
    bool added = false;
    bool deleted = false;
};

struct SparseSnapshotPaths {
    QStringList basePaths;
    QStringList localPaths;
    QStringList targetPaths;
};

class SafeMergeEngine final
{
public:
    static SafeMergeResult run(const SafeMergeRequest &request);

    // Share one disk/CPU worker across plugin updates and version switches.
    static QThreadPool *workerPool();
    static void discardSnapshots(const QStringList &paths);

    // Parses `git status --porcelain -z --untracked-files=no`.
    static QList<LocalPathEntry> parseStatusEntries(const QByteArray &data);

    // Parses `git diff --no-renames --name-status -z <a> <b>`.
    static QList<RemotePathEntry> parseNameStatusEntries(const QByteArray &data);

    // Selects changed paths present in each snapshot.
    static SparseSnapshotPaths planSnapshots(const QList<LocalPathEntry> &local,
                                             const QList<RemotePathEntry> &remote);

    // Encodes paths for `checkout-index --stdin -z`.
    static QByteArray encodePathList(const QStringList &paths);
};
