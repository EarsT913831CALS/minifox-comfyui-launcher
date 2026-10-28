#include "Transaction.h"

#include "ProcessTextDecoder.h"
#include "Repository.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <utility>

namespace transaction {
namespace {

const QStringList kPhases{QStringLiteral("prepared"), QStringLiteral("moving"), QStringLiteral("applying"),
                          QStringLiteral("refs")};
enum Phase { Prepared, Moving, Applying, Refs };
constexpr int kKeptBackups = 20;
constexpr qint64 kStaleLockSeconds = 600;
const QString kBackupRefs = QStringLiteral("refs/minifox/backups/");

QString tr(const char *text)
{
    return QCoreApplication::translate("transaction", text);
}

QString decode(const QByteArray &data)
{
    return ProcessTextDecoder::decode(data);
}

int rank(const QString &phase)
{
    return kPhases.indexOf(phase);
}

// Runs Git in one repository and remembers why the last call failed.
class Session final
{
public:
    Session(const git::Tool &tool, QString root, const std::atomic_bool *cancel)
        : m_tool(tool), m_root(std::move(root)), m_cancel(cancel) {}

    git::Result run(const QStringList &arguments, git::Options options = {}) const
    {
        options.cancel = m_cancel;
        return m_tool.run(m_root, arguments, options);
    }

    bool call(const QStringList &arguments, QString *output = nullptr, git::Options options = {})
    {
        const git::Result result = run(arguments, std::move(options));
        if (!result.ok()) {
            error = result.message();
            return false;
        }
        if (output) *output = decode(result.out).trimmed();
        return true;
    }

    // Past the point of no return a cancel would leave a half-written tree.
    void finishCancellable() { m_cancel = nullptr; }
    bool cancelled() const { return m_cancel && m_cancel->load(); }

    QString error;

private:
    const git::Tool &m_tool;
    QString m_root;
    const std::atomic_bool *m_cancel;
};

QStringList decodeFields(const QByteArray &output)
{
    QStringList result;
    for (const QByteArray &field : git::fields(output)) {
        if (!field.isEmpty()) result.append(decode(field));
    }
    return result;
}

bool save(const Journal &journal, QString *error)
{
    const QJsonObject object{
        {QStringLiteral("id"), journal.id},
        {QStringLiteral("root"), journal.root},
        {QStringLiteral("name"), journal.name},
        {QStringLiteral("phase"), journal.phase},
        {QStringLiteral("startedAt"), journal.startedAt},
        {QStringLiteral("oldHead"), journal.oldHead},
        {QStringLiteral("oldBranch"), journal.oldBranch},
        {QStringLiteral("local"), journal.local},
        {QStringLiteral("localTree"), journal.localTree},
        {QStringLiteral("merged"), journal.merged},
        {QStringLiteral("target"), journal.target},
        {QStringLiteral("branch"), journal.branch},
        {QStringLiteral("branchPrevious"), journal.branchPrevious},
        {QStringLiteral("upstreamRemote"), journal.upstreamRemote},
        {QStringLiteral("upstreamMerge"), journal.upstreamMerge},
        {QStringLiteral("backupDirectory"), journal.backupDirectory},
        {QStringLiteral("moved"), QJsonArray::fromStringList(journal.moved)},
    };
    QDir().mkpath(QFileInfo(journal.file).absolutePath());
    QSaveFile file(journal.file);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(object).toJson()) < 0 || !file.commit()) {
        if (error) *error = tr("无法写入更新记录：%1").arg(file.errorString());
        return false;
    }
    return true;
}

bool load(const QString &path, Journal *journal)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    const auto text = [&](const char *key) { return object.value(QLatin1String(key)).toString(); };
    journal->file = path;
    journal->id = text("id");
    journal->root = text("root");
    journal->name = text("name");
    journal->phase = text("phase");
    journal->startedAt = text("startedAt");
    journal->oldHead = text("oldHead");
    journal->oldBranch = text("oldBranch");
    journal->local = text("local");
    journal->localTree = text("localTree");
    journal->merged = text("merged");
    journal->target = text("target");
    journal->branch = text("branch");
    journal->branchPrevious = text("branchPrevious");
    journal->upstreamRemote = text("upstreamRemote");
    journal->upstreamMerge = text("upstreamMerge");
    journal->backupDirectory = text("backupDirectory");
    for (const QJsonValue &value : object.value(QStringLiteral("moved")).toArray())
        journal->moved.append(value.toString());
    return !journal->id.isEmpty() && !journal->root.isEmpty() && rank(journal->phase) >= 0
           && git::isObjectId(journal->oldHead);
}

// A crashed Git leaves index.lock behind and every later command fails.
// Only one launcher operation runs per repository, so an old lock is stale.
bool clearStaleLock(const QString &gitDirectory, QString *error)
{
    const QFileInfo lock(QDir(gitDirectory).filePath(QStringLiteral("index.lock")));
    if (!lock.exists()) return true;
    if (lock.lastModified().secsTo(QDateTime::currentDateTime()) > kStaleLockSeconds
        && QFile::remove(lock.absoluteFilePath()))
        return true;
    *error = tr("另一个 Git 程序正在使用这个仓库，请稍后再试。");
    return false;
}

struct Merge {
    bool ok = false;
    bool clean = false;
    QString tree;
    QStringList conflicts;
};

Merge mergeTree(Session &git, const QString &base, const QString &ours, const QString &theirs,
                const QStringList &options)
{
    QStringList arguments{QStringLiteral("merge-tree"), QStringLiteral("--write-tree"), QStringLiteral("-z"),
                          QStringLiteral("--name-only"), QStringLiteral("--no-messages"),
                          QStringLiteral("--merge-base=") + base};
    arguments << options << ours << theirs;
    const git::Result result = git.run(arguments);
    Merge merge;
    // Exit code 1 means "merged, with conflicts"; anything else is a failure.
    if (!result.started || result.timedOut || result.cancelled || (result.exitCode != 0 && result.exitCode != 1)) {
        git.error = result.message();
        return merge;
    }
    QStringList fields = decodeFields(result.out);
    if (fields.isEmpty() || !git::isObjectId(fields.constFirst())) {
        git.error = tr("Git 合并没有返回结果。");
        return merge;
    }
    merge.ok = true;
    merge.clean = result.exitCode == 0;
    merge.tree = fields.takeFirst();
    merge.conflicts = fields;
    return merge;
}

// Replaces `paths` in `tree` with what `target` has there (or removes them).
QString takeTarget(Session &git, const QString &tree, const QString &target, const QStringList &paths,
                   const QString &index)
{
    if (!git.call({QStringLiteral("read-tree"), tree}, nullptr, {.index = index})) return {};
    const QByteArray absent = "0 " + QByteArray(target.size(), '0') + '\t';
    QByteArray info;
    for (const QString &path : paths) {
        const git::Result entries = git.run({QStringLiteral("ls-tree"), QStringLiteral("-r"), QStringLiteral("-z"),
                                             QStringLiteral("--full-tree"), target, QStringLiteral("--"), path});
        bool exact = false;
        for (const QByteArray &entry : git::fields(entries.out)) {
            // "<mode> blob <oid>\t<path>"
            const qsizetype tab = entry.indexOf('\t');
            const QList<QByteArray> head = entry.left(tab).split(' ');
            if (tab < 0 || head.size() != 3) continue;
            info += head.at(0) + ' ' + head.at(2) + entry.mid(tab) + '\0';
            exact = exact || decode(entry.mid(tab + 1)) == path;
        }
        if (!exact) info += absent + path.toUtf8() + '\0';
    }
    QString result;
    if (!git.call({QStringLiteral("update-index"), QStringLiteral("-z"), QStringLiteral("--index-info")}, nullptr,
                  {.index = index, .input = info})
        || !git.call({QStringLiteral("write-tree")}, &result, {.index = index}))
        return {};
    return result;
}

// Paths `read-tree -u` would refuse or overwrite: untracked files (ignored
// ones too) where the merged tree adds a file, or files where it needs a folder.
QStringList untrackedCollisions(Session &git, const QString &root, const QString &localTree, const QString &merged)
{
    const QStringList added = decodeFields(
        git.run({QStringLiteral("diff-tree"), QStringLiteral("-r"), QStringLiteral("-z"), QStringLiteral("--name-only"),
                 QStringLiteral("--no-renames"), QStringLiteral("--diff-filter=A"), localTree, merged})
            .out);
    if (added.isEmpty()) return {};

    // Windows paths ignore case, so a tracked "Foo.py" shadows an added "foo.py".
    QSet<QString> tracked;
    const QStringList files = decodeFields(git.run({QStringLiteral("ls-tree"), QStringLiteral("-r"), QStringLiteral("-z"),
                                                    QStringLiteral("--name-only"), QStringLiteral("--full-tree"),
                                                    localTree})
                                               .out);
    for (const QString &file : files) {
        for (QString path = file.toLower(); !path.isEmpty() && !tracked.contains(path);
             path = path.section(QLatin1Char('/'), 0, -2))
            tracked.insert(path);
    }

    QStringList collisions;
    QSet<QString> seen;
    const QDir directory(root);
    for (const QString &path : added) {
        for (QString candidate = path; !candidate.isEmpty(); candidate = candidate.section(QLatin1Char('/'), 0, -2)) {
            if (tracked.contains(candidate.toLower())) break;
            const QFileInfo info(directory.filePath(candidate));
            const bool blocks = candidate == path ? info.exists() || info.isSymLink() : info.isFile();
            if (blocks) {
                if (!seen.contains(candidate.toLower())) collisions.append(candidate);
                seen.insert(candidate.toLower());
                break;
            }
            if (info.isDir()) break;
        }
    }
    return collisions;
}

QString safeName(const QString &name)
{
    QString result;
    for (const QChar character : name.trimmed()) {
        const bool invalid = character.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(character);
        result.append(invalid ? QLatin1Char('_') : character);
    }
    return result.isEmpty() ? QStringLiteral("repository") : result.left(60);
}

QString createBackupDirectory(const QString &backupRoot, const QString &name, QString *error)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDir day(QDir(backupRoot).filePath(now.toString(QStringLiteral("yyyy-MM-dd"))));
    const QString base = safeName(name) + now.toString(QStringLiteral("-HHmmss"));
    for (int attempt = 1; attempt < 100; ++attempt) {
        const QString candidate = day.filePath(attempt == 1 ? base : base + QStringLiteral("-%1").arg(attempt));
        if (QFileInfo::exists(candidate)) continue;
        if (QDir().mkpath(candidate)) return candidate;
        break;
    }
    *error = tr("无法创建备份文件夹：%1").arg(QDir::toNativeSeparators(day.path()));
    return {};
}

bool movePath(const QString &from, const QString &to)
{
    QDir().mkpath(QFileInfo(to).absolutePath());
    return QDir().rename(from, to);
}

bool removePath(const QString &path)
{
    const QFileInfo info(path);
    if (info.isDir() && !info.isSymLink()) return QDir(path).removeRecursively();
    return !info.exists() && !info.isSymLink() ? true : QFile::remove(path);
}

void removeIfEmpty(const QString &path)
{
    if (path.isEmpty()) return;
    QDir directory(path);
    if (directory.exists() && directory.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        directory.removeRecursively();
}

// Writes what the user needs to get their edits back: the files themselves,
// a patch from the merged result to their version, and a plain description.
void writeBackup(Session &git, const Request &request, const Journal &journal, const QStringList &conflicts,
                 const QStringList &collisions, bool discarding)
{
    const QDir backup(journal.backupDirectory);
    const QDir root(request.root);
    for (const QString &path : conflicts) {
        const QString source = root.filePath(path);
        if (!QFileInfo(source).isFile()) continue;
        const QString destination = backup.filePath(QStringLiteral("files/") + path);
        QDir().mkpath(QFileInfo(destination).absolutePath());
        QFile::copy(source, destination);
    }

    if (!conflicts.isEmpty()) {
        QByteArray patch;
        for (qsizetype start = 0; start < conflicts.size(); start += 200) {
            QStringList arguments{QStringLiteral("diff"), QStringLiteral("--no-color"), QStringLiteral("--no-ext-diff"),
                                  QStringLiteral("--binary"), journal.merged, journal.local, QStringLiteral("--")};
            arguments << conflicts.mid(start, 200);
            patch += git.run(arguments).out;
        }
        QSaveFile file(backup.filePath(QStringLiteral("conflicts.patch")));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(patch);
            file.commit();
        }
    }

    QStringList log{
        QStringLiteral("Minifox %1").arg(journal.startedAt),
        tr("仓库：%1").arg(QDir::toNativeSeparators(request.root)),
        tr("原版本：%1").arg(journal.oldHead),
        tr("目标版本：%1").arg(journal.target),
        QString(),
    };
    if (!conflicts.isEmpty()) {
        log << (discarding ? tr("以下文件的本地修改已放弃，修改后的文件保存在 files 文件夹：")
                           : tr("以下文件的本地修改与新版本冲突，已采用新版本；修改后的文件保存在 files 文件夹，"
                                "conflicts.patch 可以把修改重新应用回去："));
        for (const QString &path : conflicts) log << QStringLiteral("  ") + path;
        log << QString();
    }
    if (!collisions.isEmpty()) {
        log << tr("新版本自带以下文件，原来同名的未跟踪文件已移到 untracked 文件夹：");
        for (const QString &path : collisions) log << QStringLiteral("  ") + path;
        log << QString();
    }
    if (journal.local != journal.oldHead)
        log << tr("全部本地修改也保存在 Git 提交 %1（%2%3）。").arg(journal.local, kBackupRefs, journal.id);

    QSaveFile file(backup.filePath(QStringLiteral("conflicts.log")));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(log.join(QStringLiteral("\r\n")).toUtf8() + "\r\n");
        file.commit();
    }
}

void pruneBackupRefs(Session &git)
{
    const QStringList refs = git::lines(git.run({QStringLiteral("for-each-ref"), QStringLiteral("--sort=-creatordate"),
                                                 QStringLiteral("--format=%(refname)"), kBackupRefs})
                                            .out);
    for (qsizetype index = kKeptBackups; index < refs.size(); ++index)
        git.run({QStringLiteral("update-ref"), QStringLiteral("-d"), refs.at(index)});
}

// Leftovers of a merge, cherry-pick or rebase the user started by hand would
// make later Git commands think one is still in progress.
void clearOperationState(const QString &gitDirectory)
{
    const QDir directory(gitDirectory);
    for (const char *file : {"MERGE_HEAD", "MERGE_MSG", "MERGE_MODE", "AUTO_MERGE", "CHERRY_PICK_HEAD", "REVERT_HEAD"})
        QFile::remove(directory.filePath(QString::fromLatin1(file)));
    for (const char *folder : {"rebase-merge", "rebase-apply", "sequencer"})
        QDir(directory.filePath(QString::fromLatin1(folder))).removeRecursively();
}

QString indexPath(const QString &gitDirectory, const QString &id, const char *suffix)
{
    return QDir(gitDirectory).filePath(QStringLiteral("minifox-%1%2.index").arg(id, QString::fromLatin1(suffix)));
}

}

Outcome apply(const git::Tool &tool, const Request &request, const std::atomic_bool *cancel)
{
    Outcome outcome;
    Session git(tool, request.root, cancel);
    const auto fail = [&](const QString &message) {
        outcome.error = git.cancelled() ? tr("操作已取消。") : message;
        return outcome;
    };

    if (!git::isObjectId(request.target)) return fail(tr("目标版本无效。"));
    const QString gitDirectory = repository::gitDirectory(tool, request.root);
    if (gitDirectory.isEmpty()) return fail(tr("这里不是 Git 仓库。"));
    const QString oldHead = repository::resolveCommit(tool, request.root, QStringLiteral("HEAD"));
    if (oldHead.isEmpty()) return fail(tr("仓库还没有任何版本。"));
    outcome.oldHead = oldHead;
    QString error;
    if (!clearStaleLock(gitDirectory, &error)) return fail(error);

    Journal journal;
    journal.id = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-"))
                 + QUuid::createUuid().toString(QUuid::Id128).left(8);
    journal.file = QDir(request.journalDirectory).filePath(journal.id + QStringLiteral(".json"));
    journal.root = request.root;
    journal.name = request.name;
    journal.startedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    journal.oldHead = oldHead;
    journal.target = request.target;
    journal.branch = request.branch;
    const git::Result symbolic = git.run({QStringLiteral("symbolic-ref"), QStringLiteral("-q"), QStringLiteral("HEAD")});
    if (symbolic.ok()) journal.oldBranch = decode(symbolic.out).trimmed();

    const QString index = indexPath(gitDirectory, journal.id, "");
    const QString scratchIndex = indexPath(gitDirectory, journal.id, "-scratch");
    struct Cleanup {
        QStringList files;
        ~Cleanup() { for (const QString &file : files) QFile::remove(file); }
    } cleanup{{index, scratchIndex}};

    // 1. Capture every tracked edit, staged or not, as a commit.
    const QString realIndex = QDir(gitDirectory).filePath(QStringLiteral("index"));
    if (QFileInfo::exists(realIndex) ? !QFile::copy(realIndex, index)
                                     : !git.call({QStringLiteral("read-tree"), oldHead}, nullptr, {.index = index}))
        return fail(tr("无法读取 Git 索引。"));
    QString headTree;
    if (!git.call({QStringLiteral("add"), QStringLiteral("-u")}, nullptr, {.index = index})
        || !git.call({QStringLiteral("write-tree")}, &journal.localTree, {.index = index})
        || !git.call({QStringLiteral("rev-parse"), oldHead + QStringLiteral("^{tree}")}, &headTree))
        return fail(git.error);
    outcome.hadLocalChanges = journal.localTree != headTree;
    journal.local = oldHead;
    if (outcome.hadLocalChanges
        && !git.call({QStringLiteral("commit-tree"), journal.localTree, QStringLiteral("-p"), oldHead,
                      QStringLiteral("-m"), QStringLiteral("Minifox: local edits of %1").arg(request.name)},
                     &journal.local, {.identity = true}))
        return fail(git.error);

    // 2. Decide the new tree.
    QStringList conflicts;
    if (!git.call({QStringLiteral("rev-parse"), request.target + QStringLiteral("^{tree}")}, &journal.merged))
        return fail(tr("找不到目标版本，请先刷新。"));
    if (outcome.hadLocalChanges && !request.keepLocal) {
        conflicts = decodeFields(git.run({QStringLiteral("diff-tree"), QStringLiteral("-r"), QStringLiteral("-z"),
                                          QStringLiteral("--name-only"), QStringLiteral("--no-renames"), headTree,
                                          journal.localTree})
                                     .out);
    } else if (outcome.hadLocalChanges) {
        if (tool.version() < git::kMergeTreeVersion)
            return fail(tr("Git 版本过旧（需要 2.40 或更新），无法在保留本地修改的同时更新。"));
        // Target first: "ours" in -X ours is the side that wins.
        const Merge plain = mergeTree(git, oldHead, request.target, journal.local, {});
        if (!plain.ok) return fail(git.error);
        journal.merged = plain.tree;
        if (!plain.clean) {
            conflicts = plain.conflicts;
            Merge resolved = mergeTree(git, oldHead, request.target, journal.local,
                                       {QStringLiteral("-X"), QStringLiteral("ours")});
            if (!resolved.ok) resolved = plain;
            journal.merged = resolved.tree;
            // Modify/delete and similar conflicts have no lines to pick from.
            if (!resolved.clean) {
                journal.merged = takeTarget(git, resolved.tree, request.target, resolved.conflicts, scratchIndex);
                if (journal.merged.isEmpty()) return fail(git.error);
            }
        }
    }
    outcome.conflicts = conflicts;
    const QStringList collisions = untrackedCollisions(git, request.root, journal.localTree, journal.merged);
    outcome.moved = collisions;
    if (git.cancelled()) return fail({});

    // 3. Record where everything was before touching anything.
    if (!conflicts.isEmpty() || !collisions.isEmpty()) {
        journal.backupDirectory = createBackupDirectory(request.backupRoot, request.name, &error);
        if (journal.backupDirectory.isEmpty()) return fail(error);
        writeBackup(git, request, journal, conflicts, collisions, !request.keepLocal);
    }
    if (!request.branch.isEmpty()) {
        journal.branchPrevious = repository::resolveCommit(tool, request.root, QStringLiteral("refs/heads/") + request.branch);
        const auto config = [&](const char *key) {
            return decode(git.run({QStringLiteral("config"), QStringLiteral("--get"),
                                   QStringLiteral("branch.%1.%2").arg(request.branch, QString::fromLatin1(key))})
                              .out)
                .trimmed();
        };
        journal.upstreamRemote = config("remote");
        journal.upstreamMerge = config("merge");
    }
    journal.moved = collisions;
    journal.phase = kPhases.at(Prepared);
    if (git.cancelled() || !save(journal, &error)) return fail(error);
    git.finishCancellable();

    const auto abort = [&](const QString &message) {
        QString rollbackError;
        if (!rollback(tool, journal, &rollbackError))
            return fail(message + QLatin1Char('\n') + tr("撤销也失败了：%1").arg(rollbackError));
        return fail(message);
    };

    // 4. Move untracked files out of the way.
    journal.phase = kPhases.at(Moving);
    if (!save(journal, &error)) return abort(error);
    const QDir root(request.root);
    for (const QString &path : collisions) {
        const QString destination = QDir(journal.backupDirectory).filePath(QStringLiteral("untracked/") + path);
        if (!movePath(root.filePath(path), destination))
            return abort(tr("无法移走文件 %1，它可能正被其他程序使用。").arg(path));
    }

    if (request.interruptAfter == Moving) return fail(QStringLiteral("interrupted"));

    // 5. Write the files that differ.
    journal.phase = kPhases.at(Applying);
    if (!save(journal, &error)) return abort(error);
    if (!git.call({QStringLiteral("read-tree"), QStringLiteral("-m"), QStringLiteral("-u"), journal.localTree,
                   journal.merged},
                  nullptr, {.index = index}))
        return abort(tr("写入文件失败：%1").arg(git.error));
    if (request.interruptAfter == Applying) return fail(QStringLiteral("interrupted"));

    // 6. Point the branch and HEAD at the target; local edits stay unstaged.
    journal.phase = kPhases.at(Refs);
    if (!save(journal, &error)) return abort(error);
    bool refs = true;
    if (!request.branch.isEmpty()) {
        const QString ref = QStringLiteral("refs/heads/") + request.branch;
        refs = git.call({QStringLiteral("update-ref"), ref, request.target})
               && git.call({QStringLiteral("symbolic-ref"), QStringLiteral("HEAD"), ref});
        const qsizetype slash = request.upstream.indexOf(QLatin1Char('/'));
        if (refs && slash > 0) {
            const QString prefix = QStringLiteral("branch.%1.").arg(request.branch);
            refs = git.call({QStringLiteral("config"), prefix + QStringLiteral("remote"), request.upstream.left(slash)})
                   && git.call({QStringLiteral("config"), prefix + QStringLiteral("merge"),
                                QStringLiteral("refs/heads/") + request.upstream.mid(slash + 1)});
        }
    } else {
        refs = git.call({QStringLiteral("update-ref"), QStringLiteral("--no-deref"), QStringLiteral("HEAD"),
                         request.target});
    }
    if (!refs || !git.call({QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("--mixed"), QStringLiteral("HEAD")}))
        return abort(tr("更新分支失败：%1").arg(git.error));
    if (request.interruptAfter == Refs) return fail(QStringLiteral("interrupted"));

    // 7. The working tree must now be exactly the merged tree.
    const git::Result verify = git.run({QStringLiteral("diff"), QStringLiteral("--quiet"), QStringLiteral("--no-ext-diff"),
                                        journal.merged});
    if (verify.exitCode != 0 || !verify.started) return abort(tr("更新后的文件与预期不一致，已撤销本次更新。"));

    if (outcome.hadLocalChanges) {
        git.run({QStringLiteral("update-ref"), kBackupRefs + journal.id, journal.local});
        pruneBackupRefs(git);
    }
    clearOperationState(gitDirectory);
    if (!journal.backupDirectory.isEmpty()) {
        removeIfEmpty(QDir(journal.backupDirectory).filePath(QStringLiteral("untracked")));
        outcome.backupDirectory = journal.backupDirectory;
    }
    QFile::remove(journal.file);
    outcome.newHead = request.target;
    outcome.ok = true;
    return outcome;
}

QList<Journal> pending(const QString &journalDirectory)
{
    QList<Journal> journals;
    const QDir directory(journalDirectory);
    const QStringList files = directory.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &file : files) {
        Journal journal;
        if (load(directory.filePath(file), &journal)) journals.append(journal);
        // Unreadable journals describe nothing that could be undone.
        else QFile::remove(directory.filePath(file));
    }
    return journals;
}

bool rollback(const git::Tool &tool, const Journal &journal, QString *error)
{
    Session git(tool, journal.root, nullptr);
    const int phase = rank(journal.phase);
    const QString gitDirectory = repository::gitDirectory(tool, journal.root);
    if (gitDirectory.isEmpty()) {
        *error = tr("找不到仓库：%1").arg(QDir::toNativeSeparators(journal.root));
        return false;
    }
    clearStaleLock(gitDirectory, error);
    QStringList failures;

    // Files the merge may have written go back to the captured local state.
    // Starting from the merged tree makes read-tree rewrite exactly those.
    if (phase >= Applying) {
        const QString index = indexPath(gitDirectory, journal.id, "-rollback");
        const bool restored = git.call({QStringLiteral("read-tree"), journal.merged}, nullptr, {.index = index})
                              && git.call({QStringLiteral("read-tree"), QStringLiteral("--reset"), QStringLiteral("-u"),
                                           journal.localTree},
                                          nullptr, {.index = index});
        QFile::remove(index);
        if (!restored) failures << git.error;
    }

    if (phase >= Moving) {
        const QDir root(journal.root);
        for (const QString &path : journal.moved) {
            const QString backup = QDir(journal.backupDirectory).filePath(QStringLiteral("untracked/") + path);
            if (!QFileInfo::exists(backup) && !QFileInfo(backup).isSymLink()) continue;
            const QString original = root.filePath(path);
            if (!removePath(original) || !movePath(backup, original))
                failures << tr("无法把 %1 移回原处，它还在 %2").arg(path, QDir::toNativeSeparators(backup));
        }
        removeIfEmpty(QDir(journal.backupDirectory).filePath(QStringLiteral("untracked")));
    }

    if (phase >= Refs) {
        if (!journal.branch.isEmpty()) {
            const QString ref = QStringLiteral("refs/heads/") + journal.branch;
            if (!journal.branchPrevious.isEmpty())
                git.call({QStringLiteral("update-ref"), ref, journal.branchPrevious});
            else if (ref != journal.oldBranch)
                git.call({QStringLiteral("update-ref"), QStringLiteral("-d"), ref});
            const QString prefix = QStringLiteral("branch.%1.").arg(journal.branch);
            for (const auto &[key, value] : {std::pair{QStringLiteral("remote"), journal.upstreamRemote},
                                             std::pair{QStringLiteral("merge"), journal.upstreamMerge}}) {
                if (value.isEmpty()) git.run({QStringLiteral("config"), QStringLiteral("--unset"), prefix + key});
                else git.call({QStringLiteral("config"), prefix + key, value});
            }
        }
        const bool head = journal.oldBranch.isEmpty()
                              ? git.call({QStringLiteral("update-ref"), QStringLiteral("--no-deref"), QStringLiteral("HEAD"),
                                          journal.oldHead})
                              : git.call({QStringLiteral("symbolic-ref"), QStringLiteral("HEAD"), journal.oldBranch});
        if (!head || !git.call({QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("--mixed"), QStringLiteral("HEAD")}))
            failures << git.error;
    }

    if (!failures.isEmpty()) {
        *error = failures.join(QLatin1Char('\n'));
        return false;
    }
    if (journal.local != journal.oldHead)
        git.run({QStringLiteral("update-ref"), kBackupRefs + journal.id, journal.local});
    for (const char *suffix : {"", "-scratch"}) QFile::remove(indexPath(gitDirectory, journal.id, suffix));
    removeIfEmpty(journal.backupDirectory);
    QFile::remove(journal.file);
    return true;
}

Outcome clean(const git::Tool &tool, const Request &request)
{
    Request discard = request;
    discard.keepLocal = false;
    Outcome outcome = apply(tool, discard);
    if (!outcome.ok) return outcome;

    const git::Result listing = tool.run(request.root, {QStringLiteral("ls-files"), QStringLiteral("-z"),
                                                        QStringLiteral("--others"), QStringLiteral("--exclude-standard"),
                                                        QStringLiteral("--directory")});
    if (!listing.ok()) {
        outcome.ok = false;
        outcome.error = listing.message();
        return outcome;
    }
    QStringList untracked;
    for (QString path : decodeFields(listing.out)) {
        while (path.endsWith(QLatin1Char('/'))) path.chop(1);
        if (!path.isEmpty()) untracked.append(path);
    }
    if (untracked.isEmpty()) return outcome;

    QString error;
    if (outcome.backupDirectory.isEmpty()) {
        outcome.backupDirectory = createBackupDirectory(request.backupRoot, request.name, &error);
        if (outcome.backupDirectory.isEmpty()) {
            outcome.ok = false;
            outcome.error = error;
            return outcome;
        }
    }
    const QDir root(request.root);
    const QDir backup(outcome.backupDirectory);
    QStringList log{QString(), tr("完全清理移走的未跟踪文件（保存在 untracked 文件夹）：")};
    QStringList failed;
    for (const QString &path : untracked) {
        if (movePath(root.filePath(path), backup.filePath(QStringLiteral("untracked/") + path))) {
            outcome.moved.append(path);
            log << QStringLiteral("  ") + path;
        } else {
            failed.append(path);
        }
    }
    QFile file(backup.filePath(QStringLiteral("conflicts.log")));
    if (file.open(QIODevice::Append)) file.write(log.join(QStringLiteral("\r\n")).toUtf8() + "\r\n");
    if (!failed.isEmpty()) {
        outcome.ok = false;
        outcome.error = QCoreApplication::translate("transaction", "有 %n 个文件无法移走，可能正被其他程序使用：%1",
                                                    nullptr, int(failed.size()))
                            .arg(failed.mid(0, 3).join(QStringLiteral("、")));
    }
    return outcome;
}

void pruneBackups(const QString &backupRoot, int keep)
{
    static const QRegularExpression day(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
    QStringList days;
    for (const QString &name : QDir(backupRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed)) {
        if (day.match(name).hasMatch()) days.append(name);
    }
    for (qsizetype index = keep; index < days.size(); ++index)
        QDir(QDir(backupRoot).filePath(days.at(index))).removeRecursively();
}

QStringList backupRefs(const git::Tool &tool, const QString &root)
{
    return git::lines(tool.run(root, {QStringLiteral("for-each-ref"), QStringLiteral("--sort=-creatordate"),
                                      QStringLiteral("--format=%(refname)"), kBackupRefs})
                          .out);
}

}
