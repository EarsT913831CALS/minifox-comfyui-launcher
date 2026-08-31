#include "VersionTransactionStore.h"

#include "PortablePaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>

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
            {QStringLiteral("startedAt"), entry.startedAt}};
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
        for (const QJsonValue &value : root.value(QStringLiteral("entries")).toArray()) {
            const VersionTransactionEntry entry = fromObject(value.toObject());
            if (!entry.id.isEmpty() && !entry.action.isEmpty() && !entry.root.isEmpty()) {
                result.append(entry);
            }
        }
        return result;
    }

    // Read the v1 single-entry marker written by launcher versions before 1.3.
    VersionTransactionEntry legacy = fromObject(root);
    if (!legacy.action.isEmpty() && !legacy.root.isEmpty()) {
        legacy.id = QStringLiteral("legacy");
        result.append(legacy);
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
    return true;
}

bool runGit(const QString &gitProgram, const QString &root,
            const QStringList &arguments, QByteArray *output)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setProgram(gitProgram);
    QStringList processArguments {
        QStringLiteral("-c"),
        QStringLiteral("safe.directory=%1").arg(QDir::cleanPath(root)),
        QStringLiteral("-C"), root
    };
    processArguments.append(arguments);
    process.setArguments(processArguments);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GCM_INTERACTIVE"), QStringLiteral("Never"));
    process.setProcessEnvironment(environment);
    process.start();
    if (!process.waitForStarted(3000) || !process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return false;
    if (output) *output = process.readAllStandardOutput().trimmed();
    return true;
}

} // namespace

QList<VersionTransactionEntry> VersionTransactionStore::entries(QString *error)
{
    if (error) error->clear();
    const QMutexLocker locker(&storeMutex());
    return readUnlocked(error);
}

bool VersionTransactionStore::upsert(const VersionTransactionEntry &entry, QString *error)
{
    if (error) error->clear();
    const QMutexLocker locker(&storeMutex());
    QList<VersionTransactionEntry> values = readUnlocked(error);
    if (error && !error->isEmpty()) return false;
    VersionTransactionEntry normalized = entry;
    if (normalized.startedAt.isEmpty()) {
        normalized.startedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    }
    const QString normalizedRoot = QDir::cleanPath(normalized.root);
    values.removeIf([&](const VersionTransactionEntry &value) {
        return value.id != normalized.id
            && QDir::cleanPath(value.root).compare(normalizedRoot, Qt::CaseInsensitive) == 0;
    });
    bool replaced = false;
    for (VersionTransactionEntry &value : values) {
        if (value.id == normalized.id) {
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
    if (error) error->clear();
    const QMutexLocker locker(&storeMutex());
    QList<VersionTransactionEntry> values = readUnlocked(error);
    if (error && !error->isEmpty()) return false;
    values.removeIf([&id](const VersionTransactionEntry &entry) { return entry.id == id; });
    return writeUnlocked(values, error);
}

bool VersionTransactionStore::reconcileCompleted(const QString &gitProgram, QString *error)
{
    if (error) error->clear();
    if (gitProgram.trimmed().isEmpty()) return true;
    const QList<VersionTransactionEntry> values = entries(error);
    if (error && !error->isEmpty()) return false;

    QStringList completedIds;
    for (const VersionTransactionEntry &entry : values) {
        if (!QFileInfo::exists(QDir(entry.root).filePath(QStringLiteral(".git")))) {
            if (entry.action.startsWith(QStringLiteral("extension-"))) {
                completedIds.append(entry.id);
            }
            continue;
        }
        QByteArray head;
        QByteArray target;
        if (!runGit(gitProgram, entry.root,
                    {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}, &head)
            || !runGit(gitProgram, entry.root,
                       {QStringLiteral("rev-parse"), entry.targetRef}, &target)) {
            continue;
        }
        if (!target.isEmpty() && head == target) {
            QByteArray indexTree;
            QByteArray targetTree;
            if (runGit(gitProgram, entry.root, {QStringLiteral("write-tree")}, &indexTree)
                && runGit(gitProgram, entry.root,
                          {QStringLiteral("rev-parse"), entry.targetRef + QStringLiteral("^{tree}")},
                           &targetTree)
                && indexTree == targetTree) {
                completedIds.append(entry.id);
                continue;
            }
        }
        if (entry.phase == QStringLiteral("fast-forward")
            && !entry.originalHead.isEmpty()
            && head == entry.originalHead.toLatin1()) {
            QByteArray status;
            if (runGit(gitProgram, entry.root,
                       {QStringLiteral("status"), QStringLiteral("--porcelain"),
                        QStringLiteral("--untracked-files=no")}, &status)
                && status.isEmpty()) {
                completedIds.append(entry.id);
            }
        }
    }
    for (const QString &id : std::as_const(completedIds)) {
        if (!remove(id, error)) return false;
    }
    return true;
}
