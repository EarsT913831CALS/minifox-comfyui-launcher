#include "Repository.h"

#include "ProcessTextDecoder.h"

#include <QCoreApplication>
#include <QDir>

namespace repository {
namespace {

const QString kDate = QStringLiteral("--date=format-local:%Y-%m-%d %H:%M:%S");

QString decode(const QByteArray &data)
{
    return ProcessTextDecoder::decode(data);
}

QStringList remoteBranches(const git::Tool &tool, const QString &root)
{
    const git::Result result = tool.run(root, {QStringLiteral("for-each-ref"),
                                               QStringLiteral("--format=%(refname:short)"),
                                               QStringLiteral("refs/remotes/origin/")});
    QStringList branches;
    for (const QString &branch : git::lines(result.out)) {
        if (branch != QStringLiteral("origin/HEAD") && branch != QStringLiteral("origin")) branches.append(branch);
    }
    return branches;
}

QList<Commit> parseLog(const QByteArray &output)
{
    QList<Commit> commits;
    for (const QByteArray &record : output.split('\x1e')) {
        const QList<QByteArray> parts = record.trimmed().split('\x1f');
        if (parts.size() < 4) continue;
        commits.append({decode(parts.at(0)), decode(parts.at(1)), decode(parts.at(2)),
                        decode(parts.at(3)).trimmed(), parts.size() > 4 ? decode(parts.at(4)) : QString()});
    }
    return commits;
}

}

RepositoryInfo inspect(const git::Tool &tool, const QString &root)
{
    RepositoryInfo info;
    const git::Result status = tool.run(root, {QStringLiteral("status"), QStringLiteral("--porcelain=v2"),
                                               QStringLiteral("--branch"), QStringLiteral("-z"),
                                               QStringLiteral("--untracked-files=no"),
                                               QStringLiteral("--no-renames")});
    if (!status.ok()) {
        info.error = status.message();
        return info;
    }
    info.repository = true;
    for (const QByteArray &record : git::fields(status.out)) {
        if (record.startsWith("# branch.oid ")) {
            const QString oid = decode(record.mid(13)).trimmed();
            if (git::isObjectId(oid)) info.head = oid;
        } else if (record.startsWith("# branch.head ")) {
            const QString head = decode(record.mid(14)).trimmed();
            if (head != QStringLiteral("(detached)")) info.branch = head;
        } else if (record.startsWith("# branch.upstream ")) {
            info.upstream = decode(record.mid(18)).trimmed();
        } else if (record.startsWith("# branch.ab ")) {
            const QList<QByteArray> counts = record.mid(12).split(' ');
            if (counts.size() == 2) {
                info.ahead = counts.at(0).mid(1).toInt();
                info.behind = counts.at(1).mid(1).toInt();
            }
        } else if (record.startsWith("1 ") || record.startsWith("u ")) {
            // Ordinary entries have 8 fields before the path, unmerged ones 10.
            const int pathField = record.startsWith("1 ") ? 8 : 10;
            int position = 0;
            for (int field = 0; field < pathField && position >= 0; ++field)
                position = record.indexOf(' ', position) + 1;
            if (position > 0) info.changes.append(decode(record.mid(position)));
        }
    }
    if (info.head.isEmpty()) return info;

    const git::Result log = tool.run(root, {QStringLiteral("log"), QStringLiteral("-1"), kDate,
                                            QStringLiteral("--format=%H%x1f%h%x1f%ad%x1f%s"), QStringLiteral("HEAD")});
    const QList<QByteArray> parts = log.out.trimmed().split('\x1f');
    if (log.ok() && parts.size() >= 4) {
        info.shortHead = decode(parts.at(1));
        info.date = decode(parts.at(2));
        info.subject = decode(parts.at(3)).trimmed();
    }
    const git::Result url = tool.run(root, {QStringLiteral("config"), QStringLiteral("--get"),
                                            QStringLiteral("remote.origin.url")});
    if (url.ok()) info.remoteUrl = decode(url.out).trimmed();
    return info;
}

QString defaultRemoteBranch(const git::Tool &tool, const QString &root)
{
    const git::Result head = tool.run(root, {QStringLiteral("symbolic-ref"), QStringLiteral("-q"),
                                             QStringLiteral("--short"), QStringLiteral("refs/remotes/origin/HEAD")});
    const QStringList branches = remoteBranches(tool, root);
    const QString named = decode(head.out).trimmed();
    if (head.ok() && branches.contains(named)) return named;
    for (const QString &preferred : {QStringLiteral("origin/main"), QStringLiteral("origin/master")}) {
        if (branches.contains(preferred)) return preferred;
    }
    return branches.value(0);
}

QString updateTarget(const git::Tool &tool, const QString &root, const RepositoryInfo &info)
{
    const QStringList branches = remoteBranches(tool, root);
    if (!info.upstream.isEmpty() && branches.contains(info.upstream)) return info.upstream;
    // Older launcher versions parked repositories on minifox/* helper branches.
    const bool ownBranch = !info.branch.isEmpty() && !info.branch.startsWith(QStringLiteral("minifox/"));
    if (ownBranch && branches.contains(QStringLiteral("origin/") + info.branch))
        return QStringLiteral("origin/") + info.branch;
    return defaultRemoteBranch(tool, root);
}

QList<Commit> history(const git::Tool &tool, const QString &root, const QStringList &range, int limit)
{
    QStringList arguments{QStringLiteral("log"), QStringLiteral("-n"), QString::number(limit), kDate,
                          QStringLiteral("--format=%H%x1f%h%x1f%ad%x1f%s%x1e")};
    arguments << range << QStringLiteral("--");
    return parseLog(tool.run(root, arguments).out);
}

QList<Commit> releases(const git::Tool &tool, const QString &root, int limit)
{
    // Annotated tags point at tag objects; %(*...) reads the tagged commit.
    const git::Result tags = tool.run(root, {
        QStringLiteral("for-each-ref"), QStringLiteral("--sort=-v:refname"),
        QStringLiteral("--count=") + QString::number(limit),
        QStringLiteral("--format=%(if)%(*objectname)%(then)%(*objectname)%(else)%(objectname)%(end)%1f"
                       "%(if)%(*objectname)%(then)%(*objectname:short)%(else)%(objectname:short)%(end)%1f"
                       "%(if)%(*objectname)%(then)%(*committerdate:format-local:%Y-%m-%d %H:%M:%S)"
                       "%(else)%(committerdate:format-local:%Y-%m-%d %H:%M:%S)%(end)%1f"
                       "%(if)%(*objectname)%(then)%(*subject)%(else)%(subject)%(end)%1f"
                       "%(refname:short)%1e"),
        QStringLiteral("refs/tags/v[0-9]*")});
    QList<Commit> commits = parseLog(tags.out);
    if (!commits.isEmpty()) return commits;
    // Mirrors without tags still carry the release commits ("ComfyUI v0.3.68").
    const QString line = defaultRemoteBranch(tool, root);
    if (line.isEmpty()) return {};
    return history(tool, root, {QStringLiteral("--regexp-ignore-case"), QStringLiteral("--grep=^ComfyUI v"), line},
                   limit);
}

git::Result fetch(const git::Tool &tool, const QString &root, const std::atomic_bool *cancel)
{
    return tool.run(root, {QStringLiteral("fetch"), QStringLiteral("--prune"), QStringLiteral("--tags"),
                           QStringLiteral("--force"), QStringLiteral("--quiet"), QStringLiteral("origin")},
                    {.idleTimeoutMs = 60000, .cancel = cancel});
}

int behind(const git::Tool &tool, const QString &root, const QString &revision)
{
    const git::Result result = tool.run(root, {QStringLiteral("rev-list"), QStringLiteral("--count"),
                                               QStringLiteral("HEAD..") + revision});
    bool ok = false;
    const int count = decode(result.out).trimmed().toInt(&ok);
    return result.ok() && ok ? count : -1;
}

QString blob(const git::Tool &tool, const QString &root, const QString &revision, const QString &path)
{
    const git::Result result = tool.run(root, {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                                               QStringLiteral("--quiet"), revision + QLatin1Char(':') + path});
    return result.ok() ? decode(result.out).trimmed() : QString();
}

QString resolveCommit(const git::Tool &tool, const QString &root, const QString &revision)
{
    const git::Result result = tool.run(root, {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
                                               QStringLiteral("--quiet"), revision + QStringLiteral("^{commit}")});
    const QString id = decode(result.out).trimmed();
    return result.ok() && git::isObjectId(id) ? id : QString();
}

QString gitDirectory(const git::Tool &tool, const QString &root)
{
    const git::Result result = tool.run(root, {QStringLiteral("rev-parse"), QStringLiteral("--absolute-git-dir")});
    return result.ok() ? QDir::cleanPath(decode(result.out).trimmed()) : QString();
}

}
