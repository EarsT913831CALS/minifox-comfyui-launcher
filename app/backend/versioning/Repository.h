#pragma once

#include "Git.h"

#include <QString>
#include <QStringList>

struct RepositoryInfo {
    bool repository = false;
    QString head;
    QString shortHead;
    QString subject;
    QString date;
    // Empty while HEAD is detached.
    QString branch;
    QString upstream;
    int ahead = 0;
    int behind = 0;
    QString remoteUrl;
    // Tracked paths whose index or working file differs from HEAD.
    QStringList changes;
    QString error;
};

struct Commit {
    QString id;
    QString shortId;
    QString date;
    QString subject;
    QString tag;
};

namespace repository {

// Local state only; never touches the network.
RepositoryInfo inspect(const git::Tool &tool, const QString &root);

// The remote's default branch as "origin/<name>", from origin/HEAD when set.
QString defaultRemoteBranch(const git::Tool &tool, const QString &root);

// What an update moves a repository to: the upstream of its branch, the
// remote branch of the same name, or the remote's default branch.
QString updateTarget(const git::Tool &tool, const QString &root, const RepositoryInfo &info);

// Newest first. `range` is anything `git log` accepts.
QList<Commit> history(const git::Tool &tool, const QString &root, const QStringList &range, int limit);

// Release tags (v1.2.3), newest version first.
QList<Commit> releases(const git::Tool &tool, const QString &root, int limit);

git::Result fetch(const git::Tool &tool, const QString &root, const std::atomic_bool *cancel = nullptr);

// Commits in `revision` that HEAD does not have; -1 when unknown.
int behind(const git::Tool &tool, const QString &root, const QString &revision);

// Blob id of a tracked file at a revision; empty when it is not there.
QString blob(const git::Tool &tool, const QString &root, const QString &revision, const QString &path);

QString resolveCommit(const git::Tool &tool, const QString &root, const QString &revision);

// Absolute path of the repository's Git directory, or empty.
QString gitDirectory(const git::Tool &tool, const QString &root);

}
