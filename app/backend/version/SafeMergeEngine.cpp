#include "SafeMergeEngine.h"

#include "ProcessTextDecoder.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <utility>

namespace {

struct WorkingTreeChange {
    QString path;
    QByteArray originalData;
    QByteArray replacementData;
    bool originalExists = false;
    bool remove = false;
};

struct LineEdit {
    int baseStart = 0;
    int baseCount = 0;
    QList<QByteArray> replacement;
    bool local = false;
};

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
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
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

bool collectLineEdits(const SafeMergeRequest &request,
                      const QString &basePath, const QString &variantPath,
                      const QList<QByteArray> &baseLines,
                      const QList<QByteArray> &variantLines,
                      bool local, QList<LineEdit> *edits, QString *error)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setProgram(request.gitProgram);
    process.setArguments({QStringLiteral("diff"), QStringLiteral("--no-index"),
                          QStringLiteral("--no-color"), QStringLiteral("--no-ext-diff"),
                          QStringLiteral("--unified=0"), QStringLiteral("--text"),
                          QStringLiteral("--ignore-space-at-eol"),
                          QStringLiteral("--ignore-cr-at-eol"), QStringLiteral("--"),
                          basePath, variantPath});
    process.start();
    if (!process.waitForStarted(5000)) {
        if (error) *error = process.errorString();
        return false;
    }
    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(2000);
        if (error) *error = QStringLiteral("git diff timed out: %1").arg(variantPath);
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit
        || process.exitCode() < 0 || process.exitCode() > 1) {
        if (error) {
            const QString detail = ProcessTextDecoder::decode(
                process.readAllStandardError()).trimmed();
            *error = detail.isEmpty()
                ? QStringLiteral("git diff failed for %1 (exit code %2)")
                      .arg(variantPath).arg(process.exitCode())
                : detail;
        }
        return false;
    }
    if (process.exitCode() == 0) return true;

    const QString diff = QString::fromUtf8(process.readAllStandardOutput());
    const QRegularExpression hunkExpression(
        QStringLiteral("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@"),
        QRegularExpression::MultilineOption);
    auto matches = hunkExpression.globalMatch(diff);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const int oldLine = match.captured(1).toInt();
        const int oldCount = match.captured(2).isEmpty() ? 1 : match.captured(2).toInt();
        const int newLine = match.captured(3).toInt();
        const int newCount = match.captured(4).isEmpty() ? 1 : match.captured(4).toInt();
        const int baseStart = oldCount == 0 ? oldLine : oldLine - 1;
        const int variantStart = newCount == 0 ? newLine : newLine - 1;
        if (baseStart < 0 || variantStart < 0
            || baseStart + oldCount > baseLines.size()
            || variantStart + newCount > variantLines.size()) {
            if (error) *error = QStringLiteral("Invalid zero-context diff for %1").arg(variantPath);
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

bool mergeText(const SafeMergeRequest &request, const QString &localPath,
               const QString &basePath, const QString &targetPath,
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
    if (!collectLineEdits(request, basePath, localPath, baseLines, localLines,
                          true, &localEdits, error)
        || !collectLineEdits(request, basePath, targetPath, baseLines, targetLines,
                             false, &remoteEdits, error)) {
        return false;
    }

    QList<LineEdit> mergedEdits = localEdits;
    for (const LineEdit &remoteEdit : std::as_const(remoteEdits)) {
        bool overlapsLocal = false;
        for (const LineEdit &localEdit : std::as_const(localEdits)) {
            if (editsOverlap(remoteEdit, localEdit)) {
                overlapsLocal = true;
                break;
            }
        }
        if (!overlapsLocal) mergedEdits.append(remoteEdit);
    }
    std::sort(mergedEdits.begin(), mergedEdits.end(),
              [](const LineEdit &left, const LineEdit &right) {
        if (left.baseStart != right.baseStart) return left.baseStart > right.baseStart;
        return left.baseCount > right.baseCount;
    });

    QList<QByteArray> result = baseLines;
    const QByteArray lineEnding = workingData.contains("\r\n")
        ? QByteArray("\r\n") : QByteArray("\n");
    for (LineEdit edit : std::as_const(mergedEdits)) {
        for (QByteArray &line : edit.replacement) {
            line = normalizeLineEnding(line, lineEnding);
        }
        for (int count = 0; count < edit.baseCount; ++count) {
            result.removeAt(edit.baseStart);
        }
        for (auto iterator = edit.replacement.crbegin();
             iterator != edit.replacement.crend(); ++iterator) {
            result.insert(edit.baseStart, *iterator);
        }
    }
    for (QByteArray &line : result) line = normalizeLineEnding(line, lineEnding);
    QByteArray output;
    for (const QByteArray &line : std::as_const(result)) output += line;
    if (mergedData) *mergedData = output;
    return true;
}

} // namespace

SafeMergeResult SafeMergeEngine::run(const SafeMergeRequest &request)
{
    const QDir baseRoot(request.baseSnapshotRoot);
    const QDir localRoot(request.localSnapshotRoot);
    const QDir targetRoot(request.targetSnapshotRoot);
    QSet<QString> paths = snapshotFiles(request.baseSnapshotRoot);
    paths.unite(snapshotFiles(request.localSnapshotRoot));
    paths.unite(snapshotFiles(request.targetSnapshotRoot));
    QList<WorkingTreeChange> changes;

    for (const QString &relativePath : std::as_const(paths)) {
        const QString basePath = baseRoot.filePath(relativePath);
        const QString localPath = localRoot.filePath(relativePath);
        const QString targetPath = targetRoot.filePath(relativePath);
        const QString actualPath = QDir(request.workingRoot).filePath(relativePath);
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

        QByteArray mergedData;
        QString mergeError;
        if (!mergeText(request, localPath, basePath, targetPath,
                       baseData, localData, targetData, actualData,
                       &mergedData, &mergeError)) {
            return {false, SafeMergeError::MergeProcess,
                    mergeError.isEmpty() ? relativePath : mergeError};
        }
        if (mergedData != actualData) {
            changes.append({actualPath, actualData, mergedData, true, false});
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

    QList<WorkingTreeChange> applied;
    for (const WorkingTreeChange &change : std::as_const(changes)) {
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
