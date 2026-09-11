#include <QCoreApplication>
#include "SafeDataPath.h"
#include "RepositoryRecovery.h"
#include "PortablePaths.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message.isEmpty() ? QCoreApplication::translate("RepositoryRecovery", "恢复操作失败，已保留中断记录。") : message;
    return false;
}

struct GitCommand { QString program; RepositoryGitContext context; };
bool gitRun(const GitCommand &git, const QString &root, const QStringList &args,
            QByteArray *out, QString *error, const QString &index = {})
{
    QProcess p;
    auto env = git.context.environment;
    env.insert("GIT_TERMINAL_PROMPT", "0");
    env.insert("GIT_OPTIONAL_LOCKS", "0");
    if (!index.isEmpty()) env.insert("GIT_INDEX_FILE", index);
    p.setProcessEnvironment(env);
    p.start(git.program, git.context.prefixArguments + QStringList{"-c", "core.fsync=all", "-C", root} + args);
    if (!p.waitForStarted(5000) || !p.waitForFinished(120000)) {
        p.kill(); p.waitForFinished(1000);
        return fail(error, QCoreApplication::translate("RepositoryRecovery", "恢复资料 Git 操作超时：") + args.value(0));
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return fail(error, QString::fromLocal8Bit(p.readAllStandardError()));
    if (out) *out = p.readAllStandardOutput().trimmed();
    return true;
}

bool read(const QString &path, QByteArray *data, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail(error, path + ": " + f.errorString());
    *data = f.readAll();
    return f.error() == QFile::NoError || fail(error, f.errorString());
}

bool write(const QString &path, const QByteArray &data, QString *error)
{
    SafeDataPath guard;
    if (!guard.lock(path, error)) return false;
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return fail(error, path);
    if (!guard.lock(path, error)) return false;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
        return fail(error, path + ": " + f.errorString());
    return RepositoryRecovery::flushFile(path, error);
}

bool verifyArchive(const QString &path, QByteArray *hash, QString *error)
{
    const QString tar = QStandardPaths::findExecutable(QStringLiteral("tar"));
    if (tar.isEmpty()) return fail(error, QCoreApplication::translate("RepositoryRecovery", "无法找到备份校验工具 tar。"));
    QProcess process;
    // Decompress every member to a drained pipe; listing alone cannot detect
    // damaged payloads. Nothing is extracted over the user's working tree.
    QObject::connect(&process, &QProcess::readyReadStandardOutput, &process,
                     [&process] { process.readAllStandardOutput(); });
    process.start(tar, {"-xOf", path});
    if (!process.waitForStarted(5000) || !process.waitForFinished(600000)) {
        process.kill(); process.waitForFinished(1000);
        return fail(error, QCoreApplication::translate("RepositoryRecovery", "备份包校验超时：") + path);
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return fail(error, QCoreApplication::translate("RepositoryRecovery", "备份包解压校验失败：") + path);
    QFile archive(path);
    if (!archive.open(QIODevice::ReadWrite)) return fail(error, archive.errorString());
    QCryptographicHash digest(QCryptographicHash::Sha256);
    if (!digest.addData(&archive)) return fail(error, archive.errorString());
    bool flushed = archive.flush();
#ifdef Q_OS_WIN
    flushed = flushed && FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(archive.handle())));
#else
    flushed = flushed && ::fsync(archive.handle()) == 0;
#endif
    if (!flushed) return fail(error, QCoreApplication::translate("RepositoryRecovery", "无法确认备份包已写入磁盘：") + path);
    *hash = digest.result().toHex();
    return true;
}

bool safePath(const QString &root, const QString &relative)
{
    if (!SafeDataPath::canonicalRelative(relative)) return false;
    if (relative.isEmpty() || relative == "." || relative.contains('\\')
        || relative.contains(':') || QDir::isAbsolutePath(relative)) return false;
    QString current = root;
    for (const QString &part : relative.split('/')) {
        if (part.isEmpty() || part == ".." || part == "."
            || part.compare(".git", Qt::CaseInsensitive) == 0) return false;
        current = QDir(current).filePath(part);
        if (QFileInfo(current).isSymLink()) return false;
    }
    return true;
}

bool matches(const QString &path, bool exists, const QByteArray &data)
{
    QFileInfo info(path);
    if (info.isSymLink()) return false;
    if (!exists) return !info.exists();
    QByteArray actual;
    return info.isFile() && read(path, &actual, nullptr) && actual == data;
}

struct Journal {
    QJsonObject meta;
    QByteArray index;
    QList<RecoveryChange> changes;
};

bool load(const VersionTransactionEntry &entry, Journal *j, QString *error)
{
    QByteArray data;
    if (entry.recoveryPath.isEmpty()) return fail(error, QCoreApplication::translate("RepositoryRecovery", "缺少持久化恢复资料；请保留仓库并人工检查。"));
    if (!read(entry.recoveryPath, &data, error)) return false;
    if (data.size() < 32 || QCryptographicHash::hash(data.sliced(32), QCryptographicHash::Sha256) != data.first(32))
        return fail(error, QCoreApplication::translate("RepositoryRecovery", "恢复资料校验失败：") + entry.recoveryPath);
    data.remove(0, 32);
    QDataStream in(data);
    in.setVersion(QDataStream::Qt_6_0);
    QByteArray json;
    quint32 count;
    in >> json >> j->index >> count;
    j->meta = QJsonDocument::fromJson(json).object();
    if (in.status() != QDataStream::Ok || count > 1000000
        || j->meta.value("version").toInt() != 1
        || j->meta.value("id").toString() != entry.id
        || j->meta.value("root").toString() != QDir::cleanPath(entry.root))
        return fail(error, QCoreApplication::translate("RepositoryRecovery", "恢复资料与仓库事务不匹配。"));
    for (quint32 i = 0; i < count; ++i) {
        RecoveryChange c;
        in >> c.path >> c.originalData >> c.replacementData >> c.originalExists >> c.remove;
        if (in.status() != QDataStream::Ok || !safePath(entry.root, c.path))
            return fail(error, QCoreApplication::translate("RepositoryRecovery", "恢复资料包含无效文件路径。"));
        j->changes.append(c);
    }
    return in.atEnd() || fail(error, QCoreApplication::translate("RepositoryRecovery", "恢复资料格式错误。"));
}
} // namespace

bool RepositoryRecovery::flushFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite)) return fail(error, path + ": " + file.errorString());
    bool ok = file.flush();
#ifdef Q_OS_WIN
    ok = ok && FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(file.handle())));
#else
    ok = ok && ::fsync(file.handle()) == 0;
#endif
    return ok || fail(error, QCoreApplication::translate("RepositoryRecovery", "无法确认文件已写入磁盘：") + path);
}

bool RepositoryRecovery::prepare(const QString &git, VersionTransactionEntry entry,
                                 const QList<RecoveryChange> &changes, QString *error, const RepositoryGitContext &context)
{
    const GitCommand command{git, context};
    SafeDataPath guard;
    if (!guard.lock(QDir(entry.root).filePath(".minifox-path-guard"), error)) return false;
    if (error) error->clear();
    QByteArray head, gitDir, branch, target, indexTree, oldTarget;
    if (!gitRun(command, entry.root, {"rev-parse", "HEAD"}, &head, error)
        || !gitRun(command, entry.root, {"rev-parse", "--absolute-git-dir"}, &gitDir, error)
        || !gitRun(command, entry.root, {"rev-parse", "--verify", entry.targetRef + "^{commit}"}, &target, error)) return false;
    if (!entry.originalHead.isEmpty() && head != entry.originalHead.toLatin1())
        return fail(error, QCoreApplication::translate("RepositoryRecovery", "准备备份期间 HEAD 已变化，请重试。"));
    gitRun(command, entry.root, {"symbolic-ref", "-q", "HEAD"}, &branch, nullptr);
    if (!entry.targetBranch.isEmpty())
        gitRun(command, entry.root, {"rev-parse", "--verify", "refs/heads/" + entry.targetBranch}, &oldTarget, nullptr);
    QByteArray indexPath;
    if (!gitRun(command, entry.root, {"rev-parse", "--path-format=absolute", "--git-path", "index"}, &indexPath, error)) return false;
    QTemporaryDir temporary;
    const QString copy = temporary.filePath("index");
    if (!temporary.isValid() || !QFile::copy(QString::fromUtf8(indexPath), copy))
        return fail(error, QCoreApplication::translate("RepositoryRecovery", "无法备份 Git 索引。"));
    // Materialize split-index entries in the private copy, never the live index.
    if (!gitRun(command, entry.root, {"update-index", "--no-split-index"}, nullptr, error, copy)
        || !gitRun(command, entry.root, {"write-tree"}, &indexTree, error, copy)) return false;
    Journal j;
    if (!read(copy, &j.index, error)) return false;
    const QString pin = "refs/minifox-recovery/" + entry.id;
    if (!gitRun(command, entry.root, {"update-ref", pin + "/index", QString::fromLatin1(indexTree)}, nullptr, error)
        || !gitRun(command, entry.root, {"update-ref", pin + "/head", QString::fromLatin1(head)}, nullptr, error)
        || !gitRun(command, entry.root, {"update-ref", pin + "/target", QString::fromLatin1(target)}, nullptr, error)) return false;
    if (!oldTarget.isEmpty()
        && !gitRun(command, entry.root, {"update-ref", pin + "/branch", QString::fromLatin1(oldTarget)}, nullptr, error)) return false;
    j.meta = {{"version", 1}, {"id", entry.id}, {"root", QDir::cleanPath(entry.root)},
              {"head", QString::fromLatin1(head)}, {"branch", QString::fromUtf8(branch)},
              {"target", QString::fromLatin1(target)}, {"targetBranch", entry.targetBranch},
              {"oldTarget", QString::fromLatin1(oldTarget)}, {"indexTree", QString::fromLatin1(indexTree)}};
    for (const QString &key : {QStringLiteral("remote"), QStringLiteral("merge")}) {
        QByteArray value;
        if (!entry.targetBranch.isEmpty())
            gitRun(command, entry.root, {"config", "--get", "branch." + entry.targetBranch + "." + key}, &value, nullptr);
        j.meta.insert(key, QString::fromUtf8(value));
    }
    for (auto c : changes) {
        if (!guard.lock(c.path, error)) return false;
        c.path = QDir(entry.root).relativeFilePath(c.path);
        if (!safePath(entry.root, c.path)
            || !matches(QDir(entry.root).filePath(c.path), c.originalExists, c.originalData))
            return fail(error, QCoreApplication::translate("RepositoryRecovery", "备份期间文件已变化或路径不受支持：") + c.path);
        j.changes.append(c);
    }
    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << QJsonDocument(j.meta).toJson(QJsonDocument::Compact) << j.index << quint32(j.changes.size());
    for (const auto &c : j.changes)
        out << c.path << c.originalData << c.replacementData << c.originalExists << c.remove;
    if (out.status() != QDataStream::Ok) return fail(error, QCoreApplication::translate("RepositoryRecovery", "无法编码恢复资料。"));
    entry.recoveryPath = QDir(PortablePaths::dataDirectory()).filePath("recovery/" + entry.id + ".pack");
    if (!write(entry.recoveryPath, QCryptographicHash::hash(payload, QCryptographicHash::Sha256) + payload, error)) return false;
    Journal checked;
    if (!load(entry, &checked, error)) return false;
    QByteArray currentIndex, currentHead;
    if (!gitRun(command, entry.root, {"write-tree"}, &currentIndex, error)
        || !gitRun(command, entry.root, {"rev-parse", "HEAD"}, &currentHead, error)) return false;
    if (currentIndex != indexTree || currentHead != head)
        return fail(error, QCoreApplication::translate("RepositoryRecovery", "备份期间 Git 索引或 HEAD 已变化，仓库未被更新。"));
    entry.originalHead = QString::fromLatin1(head);
    entry.targetRef = QString::fromLatin1(target);
    entry.phase = "applying";
    // The durable link is published only after the complete backup is readable.
    return VersionTransactionStore::upsert(entry, error);
}

bool RepositoryRecovery::verify(const QString &git, const VersionTransactionEntry &entry, QString *error, const RepositoryGitContext &context)
{
    const GitCommand command{git, context};
    SafeDataPath guard;
    if (!guard.lock(QDir(entry.root).filePath(".minifox-path-guard"), error)) return false;
    Journal j;
    if (!guard.lock(entry.recoveryPath, error) || !load(entry, &j, error)) return false;
    for (const auto &change : j.changes)
        if (!safePath(entry.root, change.path)
            || !guard.lock(QDir(entry.root).filePath(change.path), error))
            return fail(error, QCoreApplication::translate("RepositoryRecovery", "恢复路径包含链接或不安全目录。"));
    if (j.meta.value("target").toString() != entry.targetRef)
        return fail(error, QCoreApplication::translate("RepositoryRecovery", "恢复记录的目标版本与备份不一致。"));
    for (const auto &c : j.changes)
        if (!matches(QDir(entry.root).filePath(c.path), !c.remove, c.replacementData))
            return fail(error, QCoreApplication::translate("RepositoryRecovery", "更新结果尚未完整写入：") + c.path);
    for (const auto &c : j.changes)
        if (!c.remove && !flushFile(QDir(entry.root).filePath(c.path), error)) return false;
    QByteArray head, tree, targetTree, branch;
    if (!gitRun(command, entry.root, {"rev-parse", "HEAD"}, &head, error)
        || !gitRun(command, entry.root, {"write-tree"}, &tree, error)
        || !gitRun(command, entry.root, {"rev-parse", entry.targetRef + "^{tree}"}, &targetTree, error)) return false;
    if (head != entry.targetRef.toLatin1() || tree != targetTree)
        return fail(error, QCoreApplication::translate("RepositoryRecovery", "HEAD 或索引尚未完成更新。"));
    const QString expectedBranch = entry.detachedTarget ? QString()
        : (entry.targetBranch.isEmpty() ? j.meta.value("branch").toString() : "refs/heads/" + entry.targetBranch);
    gitRun(command, entry.root, {"symbolic-ref", "-q", "HEAD"}, &branch, nullptr);
    if (branch != expectedBranch.toUtf8()) return fail(error, QCoreApplication::translate("RepositoryRecovery", "分支尚未完成切换。"));
    if (!entry.upstreamRef.isEmpty()) {
        QByteArray upstream;
        if (!gitRun(command, entry.root, {"rev-parse", "--abbrev-ref", "@{upstream}"}, &upstream, error)
            || upstream != entry.upstreamRef.toUtf8()) return fail(error, QCoreApplication::translate("RepositoryRecovery", "上游分支尚未配置完成。"));
        QByteArray config;
        if (!gitRun(command, entry.root, {"rev-parse", "--path-format=absolute", "--git-path", "config"}, &config, error)
            || !flushFile(QString::fromUtf8(config), error)) return false;
    }
    return true;
}

bool RepositoryRecovery::rollback(const QString &git, const VersionTransactionEntry &entry, QString *error, const RepositoryGitContext &context)
{
    const GitCommand command{git, context};
    SafeDataPath guard;
    if (!guard.lock(QDir(entry.root).filePath(".minifox-path-guard"), error)) return false;
    if (entry.phase == "preparing" && entry.recoveryPath.isEmpty()) {
        // All mutating stages require a published applying journal first.
        return VersionTransactionStore::remove(entry.id, error);
    }
    Journal j;
    if (!guard.lock(entry.recoveryPath, error) || !load(entry, &j, error)) return false;
    for (const auto &change : j.changes)
        if (!safePath(entry.root, change.path)
            || !guard.lock(QDir(entry.root).filePath(change.path), error))
            return fail(error, QCoreApplication::translate("RepositoryRecovery", "恢复路径包含链接或不安全目录。"));
    // Validate every file before changing any. Never overwrite edits made after a crash.
    for (const auto &c : j.changes) {
        const QString path = QDir(entry.root).filePath(c.path);
        if (!matches(path, c.originalExists, c.originalData)
            && !matches(path, !c.remove, c.replacementData))
            return fail(error, QCoreApplication::translate("RepositoryRecovery", "中断后文件又被修改，请先人工保留该文件：") + path);
    }
    const QString original = j.meta.value("head").toString();
    const QString target = j.meta.value("target").toString();
    const QString oldBranch = j.meta.value("branch").toString();
    const QString targetBranch = j.meta.value("targetBranch").toString();
    QByteArray head, branch, indexTree, targetTree, targetBranchHead;
    if (!gitRun(command, entry.root, {"rev-parse", "HEAD"}, &head, error)
        || !gitRun(command, entry.root, {"write-tree"}, &indexTree, error)
        || !gitRun(command, entry.root, {"rev-parse", target + "^{tree}"}, &targetTree, error)) return false;
    gitRun(command, entry.root, {"symbolic-ref", "-q", "HEAD"}, &branch, nullptr);
    if ((head != original.toLatin1() && head != target.toLatin1())
        || (indexTree != j.meta.value("indexTree").toString().toLatin1() && indexTree != targetTree)
        || (branch != oldBranch.toUtf8() && branch != ("refs/heads/" + targetBranch).toUtf8()
            && !(entry.detachedTarget && branch.isEmpty())))
        return fail(error, QCoreApplication::translate("RepositoryRecovery", "中断后 Git 状态已变化，已停止自动恢复并保留备份。"));
    if (!targetBranch.isEmpty()) {
        gitRun(command, entry.root, {"rev-parse", "--verify", "refs/heads/" + targetBranch}, &targetBranchHead, nullptr);
        if (!targetBranchHead.isEmpty() && targetBranchHead != target.toLatin1()
            && targetBranchHead != j.meta.value("oldTarget").toString().toLatin1())
            return fail(error, QCoreApplication::translate("RepositoryRecovery", "目标分支已被其他操作修改，已停止恢复。"));
    }
    for (const auto &c : j.changes) {
        const QString path = QDir(entry.root).filePath(c.path);
        if (c.originalExists) { if (!write(path, c.originalData, error)) return false; }
        else if (QFileInfo::exists(path) && !QFile::remove(path)) return fail(error, path);
    }
    if (!oldBranch.isEmpty()) {
        if (!gitRun(command, entry.root, {"update-ref", oldBranch, original}, nullptr, error)
            || !gitRun(command, entry.root, {"symbolic-ref", "HEAD", oldBranch}, nullptr, error)) return false;
    } else if (!gitRun(command, entry.root, {"update-ref", "--no-deref", "HEAD", original}, nullptr, error)) return false;
    if (!targetBranch.isEmpty() && "refs/heads/" + targetBranch != oldBranch) {
        const QString old = j.meta.value("oldTarget").toString();
        if (!gitRun(command, entry.root, old.isEmpty()
                    ? QStringList{"update-ref", "-d", "refs/heads/" + targetBranch}
                    : QStringList{"update-ref", "refs/heads/" + targetBranch, old}, nullptr, error)) return false;
    }
    for (const QString &key : {QStringLiteral("remote"), QStringLiteral("merge")}) {
        if (targetBranch.isEmpty()) break;
        const QString name = "branch." + targetBranch + "." + key;
        const QString value = j.meta.value(key).toString();
        if (value.isEmpty()) {
            QByteArray current;
            if (gitRun(command, entry.root, {"config", "--get", name}, &current, nullptr)
                && !gitRun(command, entry.root, {"config", "--unset-all", name}, nullptr, error)) return false;
        } else if (!gitRun(command, entry.root, {"config", "--replace-all", name, value}, nullptr, error)) return false;
    }
    QByteArray indexPath;
    if (!gitRun(command, entry.root, {"rev-parse", "--path-format=absolute", "--git-path", "index"}, &indexPath, error)
        || !write(QString::fromUtf8(indexPath), j.index, error)) return false;
    // Keep the pack and pinned Git objects as a recovery point; ZIP rotation must
    // never delete them. Only the interruption marker is removed after rollback.
    return VersionTransactionStore::remove(entry.id, error);
}

bool RepositoryRecovery::backupBeforeReset(const QString &git, VersionTransactionEntry entry, QString *error, const RepositoryGitContext &context)
{
    const GitCommand command{git, context};
    SafeDataPath guard;
    if (!guard.lock(QDir(entry.root).filePath(".minifox-path-guard"), error)) return false;
    QJsonObject archives;
    for (const auto &path : entry.backupArchives) {
        QByteArray hash;
        if (!verifyArchive(path, &hash, error)) return false;
        archives.insert(path, QString::fromLatin1(hash));
    }
    QByteArray head, stash;
    if (!gitRun(command, entry.root, {"rev-parse", "HEAD"}, &head, error)
        || !gitRun(command, entry.root, {"stash", "create", "Minifox before destructive reset"}, &stash, error)) return false;
    entry.originalHead = QString::fromLatin1(head);
    entry.trackedBackupRef = "refs/minifox-reset-backups/" + entry.id;
    // A clean tree still pins HEAD. For dirty trees stash's second parent
    // preserves the index separately from the working tree.
    const QString object = QString::fromLatin1(stash.isEmpty() ? head : stash);
    if (!gitRun(command, entry.root, {"update-ref", entry.trackedBackupRef, object}, nullptr, error)) return false;
    QByteArray checked;
    if (!gitRun(command, entry.root, {"rev-parse", entry.trackedBackupRef + "^{commit}"}, &checked, error)
        || checked != object.toLatin1()) return fail(error, QCoreApplication::translate("RepositoryRecovery", "重置备份校验失败。"));
    const QByteArray manifest = QJsonDocument(QJsonObject{
        {"version", 1}, {"id", entry.id}, {"root", entry.root},
        {"originalHead", entry.originalHead}, {"trackedBackupRef", entry.trackedBackupRef},
        {"hasLocalChanges", !stash.isEmpty()}, {"archivesSha256", archives}
    }).toJson();
    const QString manifestPath = QDir(PortablePaths::dataDirectory()).filePath("recovery/" + entry.id + ".reset.json");
    if (!write(manifestPath, manifest, error)) return false;
    QByteArray saved;
    if (!read(manifestPath, &saved, error) || saved != manifest)
        return fail(error, QCoreApplication::translate("RepositoryRecovery", "重置备份清单校验失败。"));
    entry.phase = "reset-backup-ready";
    return VersionTransactionStore::upsert(entry, error);
}
