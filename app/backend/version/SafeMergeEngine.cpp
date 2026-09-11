#include "SafeDataPath.h"
#include "SafeMergeEngine.h"

#include "ProcessTextDecoder.h"
#include "RepositoryRecovery.h"

#include <QDir>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtConcurrentRun>

#include <algorithm>
#include <utility>

namespace {

using WorkingTreeChange = RecoveryChange;

struct LineEdit {
    int baseStart = 0;
    int baseCount = 0;
    QList<QByteArray> replacement;
    bool local = false;
};

struct DiffHunk {
    int baseStart;
    int baseCount;
    int variantStart;
    int variantCount;
};

using FileHunks = QList<QList<DiffHunk>>;

bool isRegularFile(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() && info.isFile() && !info.isSymLink();
}

QSet<QString> snapshotFiles(const QString &root)
{
    QSet<QString> files;
    QDirIterator iterator(root, QDir::Files | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    const QDir directory(root);
    while (iterator.hasNext()) {
        iterator.next();
        files.insert(directory.relativeFilePath(iterator.filePath()).replace(
            QLatin1Char('\\'), QLatin1Char('/')));
    }
    return files;
}

QByteArray readFile(const QString &path, bool *ok)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok) *ok = false;
        return {};
    }
    if (ok) *ok = true;
    return file.readAll();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    SafeDataPath guard;
    if (!guard.lock(path) || !QDir().mkpath(QFileInfo(path).absolutePath()) || !guard.lock(path)) return false;
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(data) == data.size()
        && file.commit();
}

QList<QByteArray> splitLines(const QByteArray &data)
{
    QList<QByteArray> lines;
    qsizetype start = 0;
    while (start < data.size()) {
        const qsizetype newline = data.indexOf('\n', start);
        if (newline < 0) {
            lines.append(data.mid(start));
            break;
        }
        lines.append(data.mid(start, newline - start + 1));
        start = newline + 1;
    }
    return lines;
}

QByteArray lineContent(QByteArray line)
{
    if (line.endsWith('\n')) line.chop(1);
    if (line.endsWith('\r')) line.chop(1);
    return line;
}

QByteArray normalizedLineEndings(QByteArray data)
{
    data.replace("\r\n", "\n");
    return data;
}

QByteArray normalizeLineEnding(QByteArray line, const QByteArray &lineEnding)
{
    const bool terminated = line.endsWith('\n');
    if (terminated) line.chop(1);
    if (line.endsWith('\r')) line.chop(1);
    if (terminated) line += lineEnding;
    return line;
}

// Numeric aliases keep patch headers independent of repository filenames (including
// tabs, Unicode and names which resemble patch syntax). Only conflicting text
// files enter these directories; binary and clean files never get text-diffed.
bool collectBatchHunks(const QString &git, const QString &root,
                       const QString &variant, int fileCount,
                       FileHunks *hunks, QString *error)
{
    hunks->resize(fileCount);
    QProcess process;
    process.setWorkingDirectory(root);
    process.setProgram(git);
    process.setArguments({QStringLiteral("diff"), QStringLiteral("--no-index"),
                         QStringLiteral("--no-prefix"), QStringLiteral("--no-color"),
                         QStringLiteral("--no-ext-diff"), QStringLiteral("--no-textconv"),
                         QStringLiteral("--no-renames"), QStringLiteral("--diff-algorithm=myers"),
                         QStringLiteral("--unified=0"), QStringLiteral("--text"),
                         QStringLiteral("--ignore-space-at-eol"),
                         QStringLiteral("--ignore-cr-at-eol"), QStringLiteral("--"),
                         QStringLiteral("base"), variant});

    const QRegularExpression hunkExpression(
        QStringLiteral("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@"));
    const QByteArray filePrefix = "+++ " + variant.toUtf8() + '/';
    int currentFile = -1;
    bool inHunk = false;
    bool invalid = false;
    QByteArray line;
    bool discardLine = false;
    const auto parseLine = [&] {
        if (line.startsWith("diff --git ")) {
            currentFile = -1;
            inHunk = false;
        } else if (!inHunk && line.startsWith(filePrefix)) {
            bool ok = false;
            currentFile = line.mid(filePrefix.size()).trimmed().toInt(&ok);
            if (!ok || currentFile < 0 || currentFile >= fileCount) invalid = true;
        } else if (line.startsWith("@@ ")) {
            inHunk = true;
            const auto match = hunkExpression.match(QString::fromLatin1(line));
            if (!match.hasMatch() || currentFile < 0 || currentFile >= fileCount) {
                invalid = true;
                return;
            }
            bool oldOk = false, newOk = false, oldCountOk = true, newCountOk = true;
            const int oldLine = match.captured(1).toInt(&oldOk);
            const int oldCount = match.captured(2).isEmpty()
                ? 1 : match.captured(2).toInt(&oldCountOk);
            const int newLine = match.captured(3).toInt(&newOk);
            const int newCount = match.captured(4).isEmpty()
                ? 1 : match.captured(4).toInt(&newCountOk);
            if (!oldOk || !newOk || !oldCountOk || !newCountOk) {
                invalid = true;
                return;
            }
            (*hunks)[currentFile].append({oldCount == 0 ? oldLine : oldLine - 1,
                                         oldCount, newCount == 0 ? newLine : newLine - 1,
                                         newCount});
        }
    };
    // Drain stdout while Git runs. Retain only bounded header lines and hunk
    // coordinates, rather than buffering two potentially enormous patches.
    const auto drain = [&] {
        const QByteArray chunk = process.readAllStandardOutput();
        qsizetype start = 0;
        while (start < chunk.size()) {
            const qsizetype newline = chunk.indexOf('\n', start);
            const qsizetype end = newline < 0 ? chunk.size() : newline;
            if (!discardLine) {
                if (line.size() + end - start > 1024) {
                    line.append(chunk.constData() + start, 1024 - line.size());
                    // Git may append a long function-context label to a hunk
                    // header. Its coordinates fit in the retained prefix.
                    if (line.startsWith("@@ ")) parseLine();
                    line.clear();
                    discardLine = true;
                } else line.append(chunk.constData() + start, end - start);
            }
            if (newline < 0) break;
            if (!discardLine) parseLine();
            line.clear();
            discardLine = false;
            start = newline + 1;
        }
    };
    QObject::connect(&process, &QProcess::readyReadStandardOutput, &process, drain);
    process.start();
    if (!process.waitForStarted(5000)) {
        if (error) *error = process.errorString();
        return false;
    }
    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(2000);
        if (error) *error = QCoreApplication::translate("SafeMergeEngine", "批量 Git 差异计算超时。");
        return false;
    }
    drain();
    if (!line.isEmpty() && !discardLine) parseLine();
    if (process.exitStatus() != QProcess::NormalExit
        || process.exitCode() < 0 || process.exitCode() > 1 || invalid) {
        if (error) {
            *error = ProcessTextDecoder::decode(process.readAllStandardError()).trimmed();
            if (error->isEmpty()) *error = QCoreApplication::translate("SafeMergeEngine", "批量 Git 差异输出无效。");
        }
        return false;
    }
    return true;
}

bool collectLineEdits(const QList<DiffHunk> &hunks,
                      const QList<QByteArray> &baseLines,
                      const QList<QByteArray> &variantLines,
                      bool local, QList<LineEdit> *edits, QString *error)
{
    for (const DiffHunk &hunk : hunks) {
        const int baseStart = hunk.baseStart;
        const int oldCount = hunk.baseCount;
        const int variantStart = hunk.variantStart;
        const int newCount = hunk.variantCount;
        if (baseStart < 0 || variantStart < 0
            || qsizetype(baseStart) + oldCount > baseLines.size()
            || qsizetype(variantStart) + newCount > variantLines.size()) {
            if (error) *error = QCoreApplication::translate("SafeMergeEngine", "零上下文差异数据无效。");
            return false;
        }

        // Equal-sized replacements are split per line. This is the key to
        // preserving a locally edited line without suppressing an adjacent
        // remote line change that Git would otherwise group into one hunk.
        if (oldCount == newCount && oldCount > 1) {
            for (int index = 0; index < oldCount; ++index) {
                const QByteArray replacement = variantLines.at(variantStart + index);
                if (lineContent(baseLines.at(baseStart + index))
                    != lineContent(replacement)) {
                    edits->append({baseStart + index, 1, {replacement}, local});
                }
            }
            continue;
        }

        QList<QByteArray> replacement;
        replacement.reserve(newCount);
        for (int index = 0; index < newCount; ++index) {
            replacement.append(variantLines.at(variantStart + index));
        }
        edits->append({baseStart, oldCount, replacement, local});
    }
    return true;
}

bool editsOverlap(const LineEdit &left, const LineEdit &right)
{
    if (left.baseCount == 0 && right.baseCount == 0) {
        return left.baseStart == right.baseStart;
    }
    if (left.baseCount == 0) {
        return left.baseStart > right.baseStart
            && left.baseStart < right.baseStart + right.baseCount;
    }
    if (right.baseCount == 0) {
        return right.baseStart > left.baseStart
            && right.baseStart < left.baseStart + left.baseCount;
    }
    return left.baseStart < right.baseStart + right.baseCount
        && right.baseStart < left.baseStart + left.baseCount;
}

bool mergeText(const QList<DiffHunk> &localHunks, const QList<DiffHunk> &remoteHunks,
               const QByteArray &baseData, const QByteArray &localData,
               const QByteArray &targetData, const QByteArray &workingData,
               QByteArray *mergedData,
               QString *error)
{
    const QList<QByteArray> baseLines = splitLines(baseData);
    const QList<QByteArray> localLines = splitLines(localData);
    const QList<QByteArray> targetLines = splitLines(targetData);
    QList<LineEdit> localEdits;
    QList<LineEdit> remoteEdits;
    if (!collectLineEdits(localHunks, baseLines, localLines,
                          true, &localEdits, error)
        || !collectLineEdits(remoteHunks, baseLines, targetLines,
                             false, &remoteEdits, error)) {
        return false;
    }

    QList<LineEdit> mergedEdits = localEdits;
    qsizetype firstLocal = 0;
    for (const LineEdit &remoteEdit : std::as_const(remoteEdits)) {
        while (firstLocal < localEdits.size()
               && localEdits[firstLocal].baseStart + localEdits[firstLocal].baseCount
                   <= remoteEdit.baseStart
               && !editsOverlap(remoteEdit, localEdits[firstLocal])) {
            ++firstLocal;
        }
        bool overlapsLocal = false;
        for (qsizetype index = firstLocal; index < localEdits.size(); ++index) {
            const LineEdit &localEdit = localEdits[index];
            if (localEdit.baseStart > remoteEdit.baseStart + remoteEdit.baseCount) break;
            if (editsOverlap(remoteEdit, localEdit)) {
                overlapsLocal = true;
                break;
            }
        }
        if (!overlapsLocal) mergedEdits.append(remoteEdit);
    }
    std::sort(mergedEdits.begin(), mergedEdits.end(),
              [](const LineEdit &left, const LineEdit &right) {
        if (left.baseStart != right.baseStart) return left.baseStart < right.baseStart;
        // An insertion at the boundary precedes a replacement at that position.
        return left.baseCount < right.baseCount;
    });

    const QByteArray lineEnding = workingData.contains("\r\n")
        ? QByteArray("\r\n") : QByteArray("\n");
    QByteArray output;
    qsizetype cursor = 0;
    for (const LineEdit &edit : std::as_const(mergedEdits)) {
        while (cursor < edit.baseStart) {
            output += normalizeLineEnding(baseLines[cursor++], lineEnding);
        }
        for (const QByteArray &line : edit.replacement) {
            output += normalizeLineEnding(line, lineEnding);
        }
        cursor = edit.baseStart + edit.baseCount;
    }
    while (cursor < baseLines.size()) {
        output += normalizeLineEnding(baseLines[cursor++], lineEnding);
    }
    if (mergedData) *mergedData = output;
    return true;
}

} // namespace

SafeMergeResult SafeMergeEngine::run(const SafeMergeRequest &request)
{
    SafeDataPath guard;
    const QDir baseRoot(request.baseSnapshotRoot);
    const QDir localRoot(request.localSnapshotRoot);
    const QDir targetRoot(request.targetSnapshotRoot);
    QSet<QString> paths = snapshotFiles(request.baseSnapshotRoot);
    paths.unite(snapshotFiles(request.localSnapshotRoot));
    paths.unite(snapshotFiles(request.targetSnapshotRoot));
    QList<WorkingTreeChange> changes;
    QTemporaryDir batchDirectory;
    QStringList textPaths;

    for (const QString &relativePath : std::as_const(paths)) {
        const QString basePath = baseRoot.filePath(relativePath);
        const QString localPath = localRoot.filePath(relativePath);
        const QString targetPath = targetRoot.filePath(relativePath);
        const QString actualPath = QDir(request.workingRoot).filePath(relativePath);
        QString pathError;
        if (!SafeDataPath::canonicalRelative(relativePath)
            || !guard.lock(actualPath, &pathError) || !guard.lock(basePath, &pathError)
            || !guard.lock(localPath, &pathError) || !guard.lock(targetPath, &pathError))
            return {false, SafeMergeError::FileWrite, relativePath + ": " + pathError};
        const bool baseExists = isRegularFile(basePath);
        const bool localExists = isRegularFile(localPath);
        const bool targetExists = isRegularFile(targetPath);

        // Files absent from both tracked snapshots are untracked and must be preserved.
        if (!baseExists && !localExists) {
            if (!targetExists || QFileInfo::exists(actualPath)) continue;
            bool ok = false;
            const QByteArray targetData = readFile(targetPath, &ok);
            if (!ok) return {false, SafeMergeError::SnapshotRead, relativePath};
            changes.append({actualPath, {}, targetData, false, false});
            continue;
        }

        bool baseOk = false;
        bool localOk = false;
        bool targetOk = false;
        const QByteArray baseData = baseExists ? readFile(basePath, &baseOk) : QByteArray();
        const QByteArray localData = localExists ? readFile(localPath, &localOk) : QByteArray();
        const QByteArray targetData = targetExists ? readFile(targetPath, &targetOk) : QByteArray();
        if ((baseExists && !baseOk) || (localExists && !localOk)
            || (targetExists && !targetOk)) {
            return {false, SafeMergeError::SnapshotRead, relativePath};
        }

        bool actualOk = false;
        const QByteArray actualData = localExists ? readFile(actualPath, &actualOk) : QByteArray();
        if (localExists
            && (!actualOk
                || normalizedLineEndings(actualData)
                    != normalizedLineEndings(localData))) {
            return {false, SafeMergeError::WorkingTreeChanged, relativePath};
        }

        const bool localChanged = baseExists != localExists || baseData != localData;
        const bool targetChanged = baseExists != targetExists || baseData != targetData;
        if (!targetChanged) continue;
        if (!localChanged) {
            changes.append({actualPath, actualData, targetData, localExists, !targetExists});
            continue;
        }
        if (!targetExists || !localExists) continue;
        if (baseData.contains('\0') || localData.contains('\0') || targetData.contains('\0')) {
            continue;
        }

        const QString alias = QString::number(textPaths.size());
        const QDir batch(batchDirectory.path());
        if (!batchDirectory.isValid()
            || !writeFile(batch.filePath("base/" + alias), baseData)
            || !writeFile(batch.filePath("local/" + alias), localData)
            || !writeFile(batch.filePath("target/" + alias), targetData)
            || !writeFile(batch.filePath("working/" + alias), actualData)) {
            return {false, SafeMergeError::SnapshotRead, relativePath};
        }
        textPaths.append(relativePath);
    }

    if (!textPaths.isEmpty()) {
        FileHunks localHunks, remoteHunks;
        QString error;
        if (!collectBatchHunks(request.gitProgram, batchDirectory.path(),
                               QStringLiteral("local"), textPaths.size(), &localHunks, &error)
            || !collectBatchHunks(request.gitProgram, batchDirectory.path(),
                                  QStringLiteral("target"), textPaths.size(), &remoteHunks, &error)) {
            return {false, SafeMergeError::MergeProcess, error};
        }
        const QDir batch(batchDirectory.path());
        for (qsizetype index = 0; index < textPaths.size(); ++index) {
            const QString alias = QString::number(index);
            bool baseOk = false, localOk = false, targetOk = false, actualOk = false;
            const QByteArray baseData = readFile(batch.filePath("base/" + alias), &baseOk);
            const QByteArray localData = readFile(batch.filePath("local/" + alias), &localOk);
            const QByteArray targetData = readFile(batch.filePath("target/" + alias), &targetOk);
            const QByteArray actualData = readFile(batch.filePath("working/" + alias), &actualOk);
            if (!baseOk || !localOk || !targetOk || !actualOk) {
                return {false, SafeMergeError::SnapshotRead, textPaths[index]};
            }
            QByteArray mergedData;
            if (!mergeText(localHunks[index], remoteHunks[index],
                           baseData, localData, targetData, actualData, &mergedData, &error)) {
                return {false, SafeMergeError::MergeProcess, textPaths[index] + ": " + error};
            }
            if (mergedData != actualData) {
                changes.append({QDir(request.workingRoot).filePath(textPaths[index]),
                                actualData, mergedData, true, false});
            }
        }
    }

    // Snapshot preparation is asynchronous. Refuse to overwrite later edits.
    for (const WorkingTreeChange &change : std::as_const(changes)) {
        if (!change.originalExists) {
            if (QFileInfo::exists(change.path)) {
                return {false, SafeMergeError::WorkingTreeChanged, change.path};
            }
            continue;
        }
        if (!isRegularFile(change.path)) {
            return {false, SafeMergeError::WorkingTreeChanged, change.path};
        }
        bool ok = false;
        if (readFile(change.path, &ok) != change.originalData || !ok) {
            return {false, SafeMergeError::WorkingTreeChanged, change.path};
        }
    }

    if (!request.transactionId.isEmpty()) {
        QString error;
        const auto entries = VersionTransactionStore::entries(&error);
        bool prepared = false;
        for (const auto &entry : entries) {
            if (entry.id == request.transactionId) {
                prepared = RepositoryRecovery::prepare(request.gitProgram, entry, changes, &error, request.gitContext);
                break;
            }
        }
        if (!prepared) return {false, SafeMergeError::RecoveryBackup, error};
    }
    QList<WorkingTreeChange> applied;
    for (const WorkingTreeChange &change : std::as_const(changes)) {
        bool currentOk = false;
        const QByteArray currentData = change.originalExists ? readFile(change.path, &currentOk) : QByteArray();
        if ((change.originalExists && (!currentOk || currentData != change.originalData))
            || (!change.originalExists && QFileInfo::exists(change.path))) {
            // Some earlier files may already be written. Keep the journal and
            // require conflict-aware recovery, rather than discarding evidence.
            return {false, SafeMergeError::FileWrite, change.path};
        }
        const bool changed = change.remove
            ? (!QFileInfo::exists(change.path) || QFile::remove(change.path))
            : writeFile(change.path, change.replacementData);
        if (changed) {
            applied.append(change);
            continue;
        }
        for (auto iterator = applied.crbegin(); iterator != applied.crend(); ++iterator) {
            if (iterator->originalExists) writeFile(iterator->path, iterator->originalData);
            else if (isRegularFile(iterator->path)) QFile::remove(iterator->path);
        }
        return {false, SafeMergeError::FileWrite, change.path};
    }
    return {true, SafeMergeError::None, {}};
}

QList<LocalPathEntry> SafeMergeEngine::parseStatusEntries(const QByteArray &data)
{
    QList<LocalPathEntry> entries;
    const QList<QByteArray> records = data.split('\0');
    for (const QByteArray &record : records) {
        if (record.size() < 4 || record.at(2) != ' ') continue;
        LocalPathEntry entry;
        entry.path = QString::fromUtf8(record.mid(3));
        if (entry.path.isEmpty()) continue;
        entry.added = record.at(0) == 'A' || record.at(1) == 'A';
        entry.deleted = record.at(0) == 'D' || record.at(1) == 'D';
        entries.append(entry);
    }
    return entries;
}

QList<RemotePathEntry> SafeMergeEngine::parseNameStatusEntries(const QByteArray &data)
{
    QList<RemotePathEntry> entries;
    const QList<QByteArray> fields = data.split('\0');
    for (int index = 0; index + 1 < fields.size(); index += 2) {
        const QByteArray status = fields.at(index);
        const QByteArray path = fields.at(index + 1);
        if (status.isEmpty() || path.isEmpty()) continue;
        RemotePathEntry entry;
        entry.path = QString::fromUtf8(path);
        entry.added = status.at(0) == 'A';
        entry.deleted = status.at(0) == 'D';
        entries.append(entry);
    }
    return entries;
}

SparseSnapshotPaths SafeMergeEngine::planSnapshots(const QList<LocalPathEntry> &local,
                                                   const QList<RemotePathEntry> &remote)
{
    QSet<QString> localAdded;
    QSet<QString> localDeleted;
    QSet<QString> remoteAdded;
    QSet<QString> remoteDeleted;
    QSet<QString> changed;

    for (const LocalPathEntry &entry : local) {
        if (entry.path.isEmpty()) continue;
        if (entry.added) localAdded.insert(entry.path);
        if (entry.deleted) localDeleted.insert(entry.path);
    }
    for (const RemotePathEntry &entry : remote) {
        if (entry.path.isEmpty()) continue;
        changed.insert(entry.path);
        if (entry.added) remoteAdded.insert(entry.path);
        if (entry.deleted) remoteDeleted.insert(entry.path);
    }

    SparseSnapshotPaths plan;
    for (const QString &path : std::as_const(changed)) {
        const bool inHead = !localAdded.contains(path) && !remoteAdded.contains(path);
        // The working snapshot stages tracked changes onto HEAD.
        const bool inLocal = inHead && !localDeleted.contains(path);
        // Keep local additions untracked unless the target also adds the path.
        const bool inTarget = remoteAdded.contains(path)
            || (!localAdded.contains(path) && !remoteDeleted.contains(path));
        if (inHead) plan.basePaths.append(path);
        if (inLocal) plan.localPaths.append(path);
        if (inTarget) plan.targetPaths.append(path);
    }
    plan.basePaths.sort();
    plan.localPaths.sort();
    plan.targetPaths.sort();
    return plan;
}

QByteArray SafeMergeEngine::encodePathList(const QStringList &paths)
{
    QByteArray result;
    for (const QString &path : paths) {
        result += path.toUtf8();
        result += '\0';
    }
    return result;
}

QThreadPool *SafeMergeEngine::workerPool()
{
    static QThreadPool pool;
    static const bool configured = [] {
        pool.setMaxThreadCount(1);
        pool.setThreadPriority(QThread::LowPriority);
        return true;
    }();
    Q_UNUSED(configured);
    return &pool;
}

void SafeMergeEngine::discardSnapshots(const QStringList &paths)
{
    QStringList pending;
    for (const QString &path : paths) {
        if (!path.isEmpty()) pending.append(path);
    }
    if (pending.isEmpty()) return;
    // Snapshot paths are private, UUID-named artifacts. The same single worker
    // owns merging and cleanup, so deleting an owner cannot race its merge.
    (void)QtConcurrent::run(workerPool(), [pending] {
        for (const QString &path : pending) {
            const QFileInfo info(path);
            if (info.isDir() && !info.isSymLink()) QDir(path).removeRecursively();
            else QFile::remove(path);
        }
    });
}
