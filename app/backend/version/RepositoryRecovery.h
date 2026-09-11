#pragma once

#include "VersionTransactionStore.h"
#include "RepositoryGitContext.h"
#include <QByteArray>
#include <QList>

struct RecoveryChange {
    QString path;
    QByteArray originalData;
    QByteArray replacementData;
    bool originalExists = false;
    bool remove = false;
};

// Run on the bounded repository worker. The journal is a single checksummed,
// atomically committed file, independent of disposable merge snapshots/ZIPs.
class RepositoryRecovery final
{
public:
    static bool prepare(const QString &git, VersionTransactionEntry entry,
                        const QList<RecoveryChange> &changes, QString *error, const RepositoryGitContext &context = {});
    static bool verify(const QString &git, const VersionTransactionEntry &entry, QString *error, const RepositoryGitContext &context = {});
    static bool rollback(const QString &git, const VersionTransactionEntry &entry, QString *error, const RepositoryGitContext &context = {});
    // Destructive reset/clean keeps a Git stash-shaped commit without applying
    // or dropping the user's stash. It includes index-only edits and deletions.
    static bool backupBeforeReset(const QString &git, VersionTransactionEntry entry, QString *error, const RepositoryGitContext &context = {});
    static bool flushFile(const QString &path, QString *error);
};
