#pragma once

#include "Git.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>

// Moves a working tree to another commit while keeping local edits.
//
// Local edits are captured as a commit (never on a branch), merged with the
// target by `git merge-tree`, and the result is written with `read-tree -m -u`,
// which only touches files that differ. Where both sides changed the same
// lines the target wins; the local version is copied to a backup folder.
// Every step is journaled so an interrupted update can be rolled back.
namespace transaction {

struct Request {
    QString root;
    // Shown in backups and messages.
    QString name;
    // Commit id to move to.
    QString target;
    // Local branch to leave HEAD on; empty detaches HEAD at the target.
    QString branch;
    // "origin/<name>" to track; empty keeps the branch's current upstream.
    QString upstream;
    // False discards local edits (after backing them up) instead of merging.
    bool keepLocal = true;
    // Backups go to <backupRoot>/<date>/<name>-<time>/.
    QString backupRoot;
    // Journals go here, one file per transaction.
    QString journalDirectory;
    // Tests only: stop as if the launcher died once this phase's work is
    // done (1 moving, 2 applying, 3 refs), leaving the journal behind.
    int interruptAfter = 0;
};

struct Outcome {
    bool ok = false;
    QString error;
    QString oldHead;
    QString newHead;
    // Paths whose local edits lost to the target, or were discarded.
    QStringList conflicts;
    // Untracked files moved away because the target adds the same path.
    QStringList moved;
    // Empty when nothing needed backing up.
    QString backupDirectory;
    bool hadLocalChanges = false;
};

struct Journal {
    QString file;
    QString id;
    QString root;
    QString name;
    QString phase;
    QString startedAt;
    QString oldHead;
    QString oldBranch;
    QString local;
    QString localTree;
    QString merged;
    QString target;
    QString branch;
    // Value of refs/heads/<branch> before; empty when it did not exist.
    QString branchPrevious;
    QString upstreamRemote;
    QString upstreamMerge;
    QString backupDirectory;
    QStringList moved;
};

Outcome apply(const git::Tool &tool, const Request &request, const std::atomic_bool *cancel = nullptr);

// Transactions that never finished, oldest first.
QList<Journal> pending(const QString &journalDirectory);

// Puts the repository back where the journal started. Untracked files that
// were moved come back too; backups of conflicting edits are kept.
bool rollback(const git::Tool &tool, const Journal &journal, QString *error);

// Discards local edits like apply() with keepLocal = false, then moves every
// untracked file Git does not ignore into the backup folder.
Outcome clean(const git::Tool &tool, const Request &request);

// Keeps the newest `keep` dated folders under the backup root.
void pruneBackups(const QString &backupRoot, int keep);

// Backup commits of local edits; newest first.
QStringList backupRefs(const git::Tool &tool, const QString &root);

}
