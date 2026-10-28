#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include "RepositoryGitContext.h"

class QThreadPool;

// Blob ids of a target change to a path without local edits.
struct UntouchedPath {
    QByteArray oldId;
    QByteArray newId;
};

struct SafeMergeRequest {
    QString gitProgram;
    QString workingRoot;
    QString baseSnapshotRoot;
    QString localSnapshotRoot;
    QString targetSnapshotRoot;
    QString transactionId;
    RepositoryGitContext gitContext;
    // Paths without local edits have no local snapshot: their working file
    // is compared with the base by blob id before the base is ever read.
    QHash<QString, UntouchedPath> untouched = {};
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
    QByteArray oldId = {};
    QByteArray newId = {};
};

struct SparseSnapshotPaths {
    QStringList basePaths;
    QStringList localPaths;
    QStringList targetPaths;
    // Base paths with local edits (including deletions), staged onto HEAD
    // to build the local snapshot.
    QStringList editedPaths = {};
    QHash<QString, UntouchedPath> untouched = {};
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

    // Parses `git diff --no-renames --raw --no-abbrev -z <a> <b>`.
    static QList<RemotePathEntry> parseRawDiffEntries(const QByteArray &data);

    // Selects changed paths present in each snapshot. Paths without local
    // edits skip the local snapshot and are listed in `untouched`.
    static SparseSnapshotPaths planSnapshots(const QList<LocalPathEntry> &local,
                                             const QList<RemotePathEntry> &remote);

    // Encodes paths for `checkout-index --stdin -z`.
    static QByteArray encodePathList(const QStringList &paths);
};
