#include <QCoreApplication>
#include <QLockFile>
#include "OperationLease.h"
#include "VersionTransactionStore.h"

#include "PortablePaths.h"
#include "RepositoryRecovery.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QRegularExpression>

#include <utility>

namespace {

QMutex &storeMutex()
{
    static QMutex mutex;
    return mutex;
}

VersionTransactionEntry fromObject(const QJsonObject &object)
{
    VersionTransactionEntry entry;
    entry.id = object.value(QStringLiteral("id")).toString().trimmed();
    entry.action = object.value(QStringLiteral("action")).toString().trimmed();
    entry.root = QDir::cleanPath(object.value(QStringLiteral("root")).toString().trimmed());
    entry.displayName = object.value(QStringLiteral("displayName")).toString().trimmed();
    entry.targetRef = object.value(QStringLiteral("targetRef")).toString();
    entry.targetBranch = object.value(QStringLiteral("targetBranch")).toString();
    entry.originalHead = object.value(QStringLiteral("originalHead")).toString();
    entry.phase = object.value(QStringLiteral("phase")).toString();
    entry.startedAt = object.value(QStringLiteral("startedAt")).toString();
    entry.recoveryPath = object.value(QStringLiteral("recoveryPath")).toString();
    entry.upstreamRef = object.value(QStringLiteral("upstreamRef")).toString();
    entry.trackedBackupRef = object.value(QStringLiteral("trackedBackupRef")).toString();
    entry.detachedTarget = object.value(QStringLiteral("detachedTarget")).toBool();
    for (const auto &path : object.value(QStringLiteral("backupArchives")).toArray())
        entry.backupArchives.append(path.toString());
    return entry;
}

QJsonObject toObject(const VersionTransactionEntry &entry)
{
    return {{QStringLiteral("id"), entry.id},
            {QStringLiteral("action"), entry.action},
            {QStringLiteral("root"), QDir::cleanPath(entry.root)},
            {QStringLiteral("displayName"), entry.displayName},
            {QStringLiteral("targetRef"), entry.targetRef},
            {QStringLiteral("targetBranch"), entry.targetBranch},
            {QStringLiteral("originalHead"), entry.originalHead},
            {QStringLiteral("phase"), entry.phase},
            {QStringLiteral("startedAt"), entry.startedAt},
            {QStringLiteral("recoveryPath"), entry.recoveryPath},
            {QStringLiteral("upstreamRef"), entry.upstreamRef},
            {QStringLiteral("trackedBackupRef"), entry.trackedBackupRef},
            {QStringLiteral("backupArchives"), QJsonArray::fromStringList(entry.backupArchives)},
            {QStringLiteral("detachedTarget"), entry.detachedTarget}};
}

QList<VersionTransactionEntry> readUnlocked(QString *error)
{
    QFile file(PortablePaths::versionOperationFile());
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = parseError.errorString();
        return {};
    }

    QList<VersionTransactionEntry> result;
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() == 2) {
        if (!root.value(QStringLiteral("entries")).isArray()) {
            if (error) *error = QCoreApplication::translate("VersionTransactionStore", "中断记录缺少 entries 数组。");
            return {};
        }
        for (const QJsonValue &value : root.value(QStringLiteral("entries")).toArray()) {
            const VersionTransactionEntry entry = fromObject(value.toObject());
            if (!value.isObject() || entry.id.isEmpty() || entry.action.isEmpty()
                || !QDir::isAbsolutePath(entry.root)) {
                if (error) *error = QCoreApplication::translate("VersionTransactionStore", "中断记录包含无效事务。");
                return {};
            }
            result.append(entry);
        }
        return result;
    }

    // Read the v1 single-entry marker written by launcher versions before 1.3.
    VersionTransactionEntry legacy = fromObject(root);
    if ((!root.contains(QStringLiteral("version")) || root.value(QStringLiteral("version")).toInt() == 1)
        && !legacy.action.isEmpty() && QDir::isAbsolutePath(legacy.root)) {
        legacy.id = QStringLiteral("legacy");
        result.append(legacy);
    } else {
        if (error) *error = QCoreApplication::translate("VersionTransactionStore", "无法识别中断记录格式。");
    }
    return result;
}

bool writeUnlocked(const QList<VersionTransactionEntry> &entries, QString *error)
{
    if (entries.isEmpty()) {
        const QString path = PortablePaths::versionOperationFile();
        if (!QFileInfo::exists(path)) return true;
        QFile file(path);
        if (file.remove()) return true;
        if (error) *error = file.errorString();
        return false;
    }
    QString directoryError;
    if (!PortablePaths::ensureDataDirectory(&directoryError)) {
        if (error) *error = directoryError;
        return false;
    }
    QJsonArray array;
    for (const VersionTransactionEntry &entry : entries) array.append(toObject(entry));
    const QByteArray data = QJsonDocument(QJsonObject {
        {QStringLiteral("version"), 2},
        {QStringLiteral("entries"), array}
    }).toJson(QJsonDocument::Compact);
    QSaveFile file(PortablePaths::versionOperationFile());
    if (!file.open(QIODevice::WriteOnly)
        || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return RepositoryRecovery::flushFile(PortablePaths::versionOperationFile(), error);
}

} // namespace

QList<VersionTransactionEntry> VersionTransactionStore::entries(QString *error)
{
    if (error) error->clear();
    const QMutexLocker locker(&storeMutex());
    QDir().mkpath(PortablePaths::dataDirectory());
    QLockFile processLock(PortablePaths::versionOperationFile() + ".lock");
    processLock.setStaleLockTime(0);
    if (!processLock.tryLock(0)) {
        if (error) *error = QCoreApplication::translate("VersionTransactionStore", "其他进程正在读写版本事务，请稍后重试。");
        return {};
    }
    return readUnlocked(error);
}

bool VersionTransactionStore::upsert(const VersionTransactionEntry &entry, QString *error)
{
    QString localError;
    if (!error) error = &localError;
    if (error) error->clear();
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]+$")).match(entry.id).hasMatch()
        || entry.action.isEmpty() || !QDir::isAbsolutePath(entry.root)) {
        *error = QCoreApplication::translate("VersionTransactionStore", "无效的版本事务，未修改中断记录。");
        return false;
    }
    const QMutexLocker locker(&storeMutex());
    QDir().mkpath(PortablePaths::dataDirectory());
    QLockFile processLock(PortablePaths::versionOperationFile() + ".lock");
    processLock.setStaleLockTime(0);
    if (!processLock.tryLock(0)) {
        if (error) *error = QCoreApplication::translate("VersionTransactionStore", "其他进程正在读写版本事务，请稍后重试。");
        return {};
    }
    QList<VersionTransactionEntry> values = readUnlocked(error);
    if (error && !error->isEmpty()) return false;
    VersionTransactionEntry normalized = entry;
    if (normalized.startedAt.isEmpty()) {
        normalized.startedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    }
    const QString normalizedRoot = QDir::cleanPath(normalized.root);
    for (const auto &value : values) {
        if (value.id != normalized.id
            && QDir::cleanPath(value.root).compare(normalizedRoot, Qt::CaseInsensitive) == 0) {
            *error = QCoreApplication::translate("VersionTransactionStore", "该仓库存在未完成的操作，必须先恢复，不能覆盖原恢复记录。");
            return false;
        }
    }
    bool replaced = false;
    for (VersionTransactionEntry &value : values) {
        if (value.id == normalized.id) {
            if (normalized.recoveryPath.isEmpty()) normalized.recoveryPath = value.recoveryPath;
            if (normalized.originalHead.isEmpty()) normalized.originalHead = value.originalHead;
            if (normalized.trackedBackupRef.isEmpty()) normalized.trackedBackupRef = value.trackedBackupRef;
            if (normalized.backupArchives.isEmpty()) normalized.backupArchives = value.backupArchives;
            value = normalized;
            replaced = true;
            break;
        }
    }
    if (!replaced) values.append(normalized);
    return writeUnlocked(values, error);
}

bool VersionTransactionStore::remove(const QString &id, QString *error)
{
    QString localError;
    if (!error) error = &localError;
    if (error) error->clear();
    const QMutexLocker locker(&storeMutex());
    QDir().mkpath(PortablePaths::dataDirectory());
    QLockFile processLock(PortablePaths::versionOperationFile() + ".lock");
    processLock.setStaleLockTime(0);
    if (!processLock.tryLock(0)) {
        if (error) *error = QCoreApplication::translate("VersionTransactionStore", "其他进程正在读写版本事务，请稍后重试。");
        return {};
    }
    QList<VersionTransactionEntry> values = readUnlocked(error);
    if (error && !error->isEmpty()) return false;
    values.removeIf([&id](const VersionTransactionEntry &entry) { return entry.id == id; });
    return writeUnlocked(values, error);
}

bool VersionTransactionStore::reconcileCompleted(const QString &gitProgram, QString *error,
                                                const RepositoryGitContext &context)
{
    QString localError;
    if (!error) error = &localError;
    if (error) error->clear();
    if (gitProgram.trimmed().isEmpty()) return true;
    const QList<VersionTransactionEntry> values = entries(error);
    if (error && !error->isEmpty()) return false;
    if (values.isEmpty()) return true;
    OperationLease lease;
    QStringList resources{PortablePaths::dataDirectory()};
    for (const auto &entry : values) resources.append(entry.root);
    if (!lease.acquire(resources, error)) return false;

    QStringList completedIds;
    for (const VersionTransactionEntry &entry : values) {
        // Legacy HEAD/index heuristics cannot distinguish a partial multi-file
        // update from intentionally preserved local edits. Only a verified,
        // explicitly committed journal may release the startup guard.
        if (entry.phase != QStringLiteral("committed") || entry.recoveryPath.isEmpty()) continue;
        QString verifyError;
        if (RepositoryRecovery::verify(gitProgram, entry, &verifyError, context)) completedIds.append(entry.id);
    }
    for (const QString &id : std::as_const(completedIds)) {
        if (!remove(id, error)) return false;
    }
    return true;
}
