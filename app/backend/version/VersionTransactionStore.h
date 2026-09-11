#pragma once
#include "RepositoryGitContext.h"

#include <QList>
#include <QString>
#include <QStringList>

struct VersionTransactionEntry {
    QString id;
    QString action;
    QString root;
    QString displayName;
    QString targetRef;
    QString targetBranch;
    QString originalHead;
    QString phase;
    QString startedAt;
    QString recoveryPath;
    QString upstreamRef;
    QString trackedBackupRef;
    QStringList backupArchives;
    bool detachedTarget = false;
};

class VersionTransactionStore final
{
public:
    static QList<VersionTransactionEntry> entries(QString *error = nullptr);
    static bool upsert(const VersionTransactionEntry &entry, QString *error = nullptr);
    static bool remove(const QString &id, QString *error = nullptr);
    static bool reconcileCompleted(const QString &gitProgram, QString *error = nullptr,
                                   const RepositoryGitContext &context = {});
};
