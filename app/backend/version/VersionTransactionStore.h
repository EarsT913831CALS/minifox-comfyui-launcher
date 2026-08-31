#pragma once

#include <QList>
#include <QString>

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
};

class VersionTransactionStore final
{
public:
    static QList<VersionTransactionEntry> entries(QString *error = nullptr);
    static bool upsert(const VersionTransactionEntry &entry, QString *error = nullptr);
    static bool remove(const QString &id, QString *error = nullptr);
    static bool reconcileCompleted(const QString &gitProgram, QString *error = nullptr);
};
