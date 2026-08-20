#include "LogModel.h"

#include <QFile>
#include <QFontMetricsF>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>

#include <algorithm>
#include <utility>

namespace {
QString styledText(QString text)
{
    text = text.toHtmlEscaped();
    text.replace(QLatin1Char('\t'), QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;"));

    QString result;
    result.reserve(text.size());
    bool atLineStart = true;
    for (qsizetype index = 0; index < text.size();) {
        if (text.at(index) != QLatin1Char(' ')) {
            result += text.at(index++);
            atLineStart = false;
            continue;
        }

        qsizetype end = index;
        while (end < text.size() && text.at(end) == QLatin1Char(' ')) {
            ++end;
        }
        const qsizetype count = end - index;
        if (!atLineStart) {
            result += QLatin1Char(' ');
        }
        const qsizetype nonBreakingCount = atLineStart ? count : count - 1;
        for (qsizetype space = 0; space < nonBreakingCount; ++space) {
            result += QStringLiteral("&nbsp;");
        }
        index = end;
    }
    return result;
}

QString coloredSpan(const QString &text, const QString &color)
{
    if (color.isEmpty()) {
        return text;
    }
    return QStringLiteral("<font color=\"%1\">%2</font>").arg(color, text);
}

enum class SemanticLevel {
    None,
    Info,
    Warning,
    Error,
    Debug,
    Success
};

SemanticLevel semanticLevel(const QString &name)
{
    if (name == QStringLiteral("INFO")) {
        return SemanticLevel::Info;
    }
    if (name == QStringLiteral("WARNING") || name == QStringLiteral("WARN")) {
        return SemanticLevel::Warning;
    }
    if (name == QStringLiteral("ERROR") || name == QStringLiteral("CRITICAL")) {
        return SemanticLevel::Error;
    }
    if (name == QStringLiteral("DEBUG")) {
        return SemanticLevel::Debug;
    }
    if (name == QStringLiteral("START") || name == QStringLiteral("DONE")) {
        return SemanticLevel::Success;
    }
    return SemanticLevel::None;
}

QString semanticColor(SemanticLevel level,
                      const QString &secondaryColor,
                      const QString &infoColor,
                      const QString &successColor,
                      const QString &warningColor,
                      const QString &errorColor)
{
    switch (level) {
    case SemanticLevel::Info: return successColor;
    case SemanticLevel::Warning: return warningColor;
    case SemanticLevel::Error: return errorColor;
    case SemanticLevel::Debug: return secondaryColor;
    case SemanticLevel::Success: return infoColor;
    case SemanticLevel::None: return {};
    }
    return {};
}

QString semanticStyledText(const QString &text,
                           const QString &baseColor,
                           const QString &secondaryColor,
                           const QString &infoColor,
                           const QString &successColor,
                           const QString &warningColor,
                           const QString &errorColor)
{
    static const QRegularExpression levelExpression(
        QStringLiteral(R"(\[(INFO|WARNING|WARN|ERROR|CRITICAL|DEBUG|START|DONE)\])"));

    QString output;
    qsizetype cursor = 0;
    auto matches = levelExpression.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        output += coloredSpan(styledText(text.mid(cursor, match.capturedStart() - cursor)),
                              baseColor);
        output += coloredSpan(styledText(match.captured(0)),
                              semanticColor(semanticLevel(match.captured(1)),
                                            secondaryColor,
                                            infoColor,
                                            successColor,
                                            warningColor,
                                            errorColor));
        cursor = match.capturedEnd();
    }
    output += coloredSpan(styledText(text.mid(cursor)), baseColor);
    return output;
}

bool hasSemanticLevel(const QString &text)
{
    static const QRegularExpression levelExpression(
        QStringLiteral(R"(\[(?:INFO|WARNING|WARN|ERROR|CRITICAL|DEBUG|START|DONE)\])"));
    return levelExpression.match(text).hasMatch();
}

bool looksLikeActualError(const QString &text)
{
    static const QRegularExpression errorExpression(
        QStringLiteral(
            R"(^\s*(?:Traceback \(most recent call last\):|Exception in callback\b|During handling of the above exception\b|The above exception was the direct cause\b|(?:[\w.]*Error|[\w.]*Exception|Fatal):))"),
        QRegularExpression::CaseInsensitiveOption);
    return errorExpression.match(text).hasMatch();
}

QString visibleStreamLabel(const QString &stream, const QString &text)
{
    if (stream == QStringLiteral("system")) {
        return QStringLiteral("SYS");
    }
    if (stream == QStringLiteral("stderr")
        && !hasSemanticLevel(text)
        && looksLikeActualError(text)) {
        return QStringLiteral("ERR");
    }
    return {};
}

QColor semanticColorValue(SemanticLevel level,
                          const QColor &secondaryColor,
                          const QColor &infoColor,
                          const QColor &successColor,
                          const QColor &warningColor,
                          const QColor &errorColor)
{
    switch (level) {
    case SemanticLevel::Info: return successColor;
    case SemanticLevel::Warning: return warningColor;
    case SemanticLevel::Error: return errorColor;
    case SemanticLevel::Debug: return secondaryColor;
    case SemanticLevel::Success: return infoColor;
    case SemanticLevel::None: return {};
    }
    return {};
}

QTextCharFormat documentTextFormat(const QColor &color)
{
    QTextCharFormat format;
    if (color.isValid()) {
        format.setForeground(color);
    }
    format.setFontWeight(QFont::Normal);
    return format;
}

void insertSemanticDocumentText(QTextCursor &cursor,
                                const QString &text,
                                const QColor &baseColor,
                                const QColor &secondaryColor,
                                const QColor &infoColor,
                                const QColor &successColor,
                                const QColor &warningColor,
                                const QColor &errorColor)
{
    static const QRegularExpression expression(
        QStringLiteral(R"(\[(INFO|WARNING|WARN|ERROR|CRITICAL|DEBUG|START|DONE)\])"));

    qsizetype offset = 0;
    auto matches = expression.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        cursor.insertText(
            text.mid(offset, match.capturedStart() - offset),
            documentTextFormat(baseColor));
        cursor.insertText(
            match.captured(0),
            documentTextFormat(semanticColorValue(semanticLevel(match.captured(1)),
                                                  secondaryColor,
                                                  infoColor,
                                                  successColor,
                                                  warningColor,
                                                  errorColor)));
        offset = match.capturedEnd();
    }
    cursor.insertText(text.mid(offset), documentTextFormat(baseColor));
}
}

LogModel::LogModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_progressHideTimer.setSingleShot(true);
    m_progressHideTimer.setInterval(1200);
    connect(&m_progressHideTimer, &QTimer::timeout, this, [this] {
        if (!m_progressActive) {
            return;
        }
        m_progressActive = false;
        emit progressChanged();
    });
}

int LogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const auto &entry = m_entries.at(index.row());
    switch (role) {
    case TimestampRole:
        return entry.timestamp.toString(QStringLiteral("HH:mm:ss.zzz"));
    case TextRole:
        return entry.text;
    case StreamRole:
        return entry.stream;
    case ColorRole:
        return entry.color;
    case LineNumberRole:
        return index.row() + 1;
    default:
        return {};
    }
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {
        {TimestampRole, "timestamp"},
        {TextRole, "text"},
        {StreamRole, "stream"},
        {ColorRole, "ansiColor"},
        {LineNumberRole, "lineNumber"}
    };
}

bool LogModel::progressActive() const { return m_progressActive; }
bool LogModel::progressIndeterminate() const { return m_progressIndeterminate; }
double LogModel::progressValue() const { return m_progressValue; }
int LogModel::progressPercent() const { return m_progressPercent; }
qint64 LogModel::progressCurrent() const { return m_progressCurrent; }
qint64 LogModel::progressTotal() const { return m_progressTotal; }
QString LogModel::progressLabel() const { return m_progressLabel; }
QString LogModel::progressElapsed() const { return m_progressElapsed; }
QString LogModel::progressRemaining() const { return m_progressRemaining; }
QString LogModel::progressRate() const { return m_progressRate; }

void LogModel::appendStandardOutput(const QByteArray &data)
{
    appendData(m_stdout, data, QStringLiteral("stdout"));
}

void LogModel::appendStandardError(const QByteArray &data)
{
    appendData(m_stderr, data, QStringLiteral("stderr"));
}

void LogModel::appendSystemMessage(const QString &message, const QString &color)
{
    appendEntry({QDateTime::currentDateTime(), message, QStringLiteral("system"), color});
}

void LogModel::flush()
{
    if (!m_stdout.partial.isEmpty()) {
        QString line = std::exchange(m_stdout.partial, {});
        if (!updateProgressFromLine(line)) {
            appendLine(m_stdout, std::move(line), QStringLiteral("stdout"));
        }
    }
    if (!m_stderr.partial.isEmpty()) {
        QString line = std::exchange(m_stderr.partial, {});
        if (!updateProgressFromLine(line)) {
            appendLine(m_stderr, std::move(line), QStringLiteral("stderr"));
        }
    }
    m_stdout.suppressNextLineFeed = false;
    m_stderr.suppressNextLineFeed = false;
}

void LogModel::clear()
{
    beginResetModel();
    m_entries.clear();
    m_stdout.partial.clear();
    m_stdout.color.clear();
    m_stdout.suppressNextLineFeed = false;
    m_stdout.decoder.resetState();
    m_stderr.partial.clear();
    m_stderr.color.clear();
    m_stderr.suppressNextLineFeed = false;
    m_stderr.decoder.resetState();
    endResetModel();
    emit countChanged();
    resetProgress();
}

QString LogModel::displayText(bool showTimestamps, bool compact) const
{
    QString output;
    output.reserve(m_entries.size() * 96);

    const int lineNumberWidth = QString::number(m_entries.size()).size();
    for (qsizetype index = 0; index < m_entries.size(); ++index) {
        const auto &entry = m_entries.at(index);
        if (!compact) {
            output += QString::number(index + 1).rightJustified(lineNumberWidth);
            output += QLatin1Char(' ');
            if (showTimestamps) {
                output += entry.timestamp.toString(QStringLiteral("HH:mm:ss.zzz"));
                output += QLatin1Char(' ');
            }
            output += visibleStreamLabel(entry.stream, entry.text).leftJustified(4);
        }
        output += entry.text;
        if (index + 1 < m_entries.size()) {
            output += QLatin1Char('\n');
        }
    }
    return output;
}

QString LogModel::displayStyledText(bool showTimestamps,
                                    bool compact,
                                    const QString &secondaryColor,
                                    const QString &foregroundColor,
                                    const QString &warningColor,
                                    const QString &infoColor,
                                    const QString &successColor,
                                    const QString &errorColor) const
{
    return displayStyledTextRange(0,
                                  showTimestamps,
                                  compact,
                                  secondaryColor,
                                  foregroundColor,
                                  warningColor,
                                  infoColor,
                                  successColor,
                                  errorColor);
}

QString LogModel::displayStyledTextRange(int firstRow,
                                         bool showTimestamps,
                                         bool compact,
                                         const QString &secondaryColor,
                                         const QString &foregroundColor,
                                         const QString &warningColor,
                                         const QString &infoColor,
                                         const QString &successColor,
                                         const QString &errorColor) const
{
    firstRow = std::clamp(firstRow, 0, static_cast<int>(m_entries.size()));
    if (firstRow >= m_entries.size()) {
        return {};
    }

    QString output;
    output.reserve((m_entries.size() - firstRow) * 160);
    output += QStringLiteral(
        "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" border=\"0\">");

    for (qsizetype index = firstRow; index < m_entries.size(); ++index) {
        const auto &entry = m_entries.at(index);
        output += QStringLiteral("<tr>");
        if (!compact) {
            output += QStringLiteral(
                "<td width=\"48\" nowrap align=\"right\" valign=\"top\">");
            output += coloredSpan(styledText(QString::number(index + 1)), secondaryColor);
            output += QStringLiteral(
                "</td><td width=\"8\" nowrap valign=\"top\">&nbsp;</td>");
            if (showTimestamps) {
                output += QStringLiteral("<td width=\"104\" nowrap valign=\"top\">");
                output += coloredSpan(
                    styledText(entry.timestamp.toString(QStringLiteral("HH:mm:ss.zzz"))),
                    secondaryColor);
                output += QStringLiteral(
                    "</td><td width=\"8\" nowrap valign=\"top\">&nbsp;</td>");
            }
            output += QStringLiteral("<td width=\"32\" nowrap valign=\"top\">");
            const QString streamLabel = visibleStreamLabel(entry.stream, entry.text);
            if (streamLabel == QStringLiteral("SYS")) {
                output += coloredSpan(QStringLiteral("SYS"), infoColor);
            } else if (streamLabel == QStringLiteral("ERR")) {
                output += coloredSpan(QStringLiteral("ERR"), errorColor);
            } else {
                output += QStringLiteral("&nbsp;&nbsp;&nbsp;");
            }
            output += QStringLiteral(
                "</td><td width=\"8\" nowrap valign=\"top\">&nbsp;</td>");
        }

        const QString contentColor = entry.color.isEmpty() ? foregroundColor : entry.color;
        output += QStringLiteral("<td valign=\"top\">");
        output += semanticStyledText(entry.text,
                                     contentColor,
                                     secondaryColor,
                                     infoColor,
                                     successColor,
                                     warningColor,
                                     errorColor);
        if (entry.text.isEmpty()) {
            output += QStringLiteral("&nbsp;");
        }
        output += QStringLiteral("</td></tr>");
    }
    output += QStringLiteral("</table>");
    return output;
}

void LogModel::appendStyledTextRangeToDocument(
    QQuickTextDocument *quickDocument,
    int firstRow,
    bool showTimestamps,
    bool compact,
    const QColor &secondaryColor,
    const QColor &foregroundColor,
    const QColor &warningColor,
    const QColor &infoColor,
    const QColor &successColor,
    const QColor &errorColor) const
{
    if (!quickDocument) {
        return;
    }

    firstRow = std::clamp(firstRow, 0, static_cast<int>(m_entries.size()));
    if (firstRow >= m_entries.size()) {
        return;
    }

    QTextDocument *document = quickDocument->textDocument();
    QTextCursor appendCursor(document);
    appendCursor.movePosition(QTextCursor::End);
    appendCursor.beginEditBlock();

    const int prefixCharacters = compact
        ? 0
        : 6 + 2 + (showTimestamps ? 12 + 2 : 0) + 3 + 2;
    const QFontMetricsF metrics(document->defaultFont());
    const qreal prefixWidth =
        metrics.horizontalAdvance(QString(prefixCharacters, QLatin1Char('0')));

    bool documentIsEmpty = document->isEmpty();
    for (qsizetype row = firstRow; row < m_entries.size(); ++row) {
        QTextBlockFormat blockFormat;
        blockFormat.setTopMargin(0);
        blockFormat.setBottomMargin(0);
        blockFormat.setLeftMargin(prefixWidth);
        blockFormat.setTextIndent(-prefixWidth);

        if (documentIsEmpty) {
            appendCursor.setBlockFormat(blockFormat);
            documentIsEmpty = false;
        } else {
            appendCursor.insertBlock(blockFormat);
        }

        const Entry &entry = m_entries.at(row);
        if (!compact) {
            appendCursor.insertText(
                QString::number(row + 1).rightJustified(6),
                documentTextFormat(secondaryColor));
            appendCursor.insertText(
                QStringLiteral("  "),
                documentTextFormat(secondaryColor));

            if (showTimestamps) {
                appendCursor.insertText(
                    entry.timestamp.toString(QStringLiteral("HH:mm:ss.zzz")),
                    documentTextFormat(secondaryColor));
                appendCursor.insertText(
                    QStringLiteral("  "),
                    documentTextFormat(secondaryColor));
            }

            const QString streamLabel = visibleStreamLabel(entry.stream, entry.text);
            const QColor streamColor = streamLabel == QStringLiteral("SYS")
                ? infoColor
                : (streamLabel == QStringLiteral("ERR") ? errorColor : secondaryColor);
            appendCursor.insertText(
                streamLabel.leftJustified(3),
                documentTextFormat(streamColor));
            appendCursor.insertText(
                QStringLiteral("  "),
                documentTextFormat(secondaryColor));
        }

        const QColor ansiColor(entry.color);
        insertSemanticDocumentText(
            appendCursor,
            entry.text,
            ansiColor.isValid() ? ansiColor : foregroundColor,
            secondaryColor,
            infoColor,
            successColor,
            warningColor,
            errorColor);
    }

    appendCursor.endEditBlock();
}

bool LogModel::exportToFile(const QString &path, bool showTimestamps, QString *errorMessage) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    for (const auto &entry : m_entries) {
        if (showTimestamps) {
            stream << '[' << entry.timestamp.toString(Qt::ISODateWithMs) << "] ";
        }
        if (entry.stream != QStringLiteral("stdout")) {
            stream << '[' << entry.stream << "] ";
        }
        stream << entry.text << '\n';
    }
    return stream.status() == QTextStream::Ok;
}

void LogModel::appendData(StreamState &state, const QByteArray &data, const QString &stream)
{
    state.partial.append(state.decoder(data));

    while (true) {
        const qsizetype carriageReturn = state.partial.indexOf(QLatin1Char('\r'));
        const qsizetype lineFeed = state.partial.indexOf(QLatin1Char('\n'));
        qsizetype delimiter = -1;
        if (carriageReturn >= 0 && lineFeed >= 0) {
            delimiter = std::min(carriageReturn, lineFeed);
        } else {
            delimiter = std::max(carriageReturn, lineFeed);
        }
        if (delimiter < 0) {
            break;
        }

        const QChar delimiterCharacter = state.partial.at(delimiter);
        const bool isCrLf = delimiterCharacter == QLatin1Char('\r')
            && delimiter + 1 < state.partial.size()
            && state.partial.at(delimiter + 1) == QLatin1Char('\n');
        QString line = state.partial.first(delimiter);
        state.partial.remove(0, delimiter + (isCrLf ? 2 : 1));

        if (delimiterCharacter == QLatin1Char('\n')
            && state.suppressNextLineFeed
            && line.isEmpty()) {
            state.suppressNextLineFeed = false;
            continue;
        }
        state.suppressNextLineFeed =
            delimiterCharacter == QLatin1Char('\r') && !isCrLf;

        if (updateProgressFromLine(line)) {
            continue;
        }
        if (!line.isEmpty() || delimiterCharacter != QLatin1Char('\r')) {
            appendLine(state, std::move(line), stream);
        }
    }

    while (state.partial.size() > MaximumPartialCharacters) {
        QString line = state.partial.first(MaximumPartialCharacters);
        state.partial.remove(0, MaximumPartialCharacters);
        if (!updateProgressFromLine(line)) {
            appendLine(state, std::move(line), stream);
        }
    }
    if (!state.partial.isEmpty()) {
        updateProgressFromLine(state.partial);
    }
}

void LogModel::appendLine(StreamState &state, QString line, const QString &stream)
{
    static const QRegularExpression sgrExpression(QStringLiteral("\\x1B\\[([0-9;]*)m"));
    auto iterator = sgrExpression.globalMatch(line);
    while (iterator.hasNext()) {
        const auto match = iterator.next();
        const QStringList codes = match.captured(1).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        if (codes.isEmpty()) {
            state.color.clear();
        }
        for (const QString &codeText : codes) {
            const int code = codeText.toInt();
            if (code == 0 || code == 39) {
                state.color.clear();
            } else {
                const QString mappedColor = ansiColor(code);
                if (!mappedColor.isEmpty()) {
                    state.color = mappedColor;
                }
            }
        }
    }
    line.remove(sgrExpression);
    static const QRegularExpression otherEscape(QStringLiteral("\\x1B(?:[@-_][0-?]*[ -/]*[@-~])"));
    line.remove(otherEscape);

    appendEntry({QDateTime::currentDateTime(), line, stream, state.color});
}

void LogModel::appendEntry(Entry entry)
{
    if (entry.text.size() > MaximumPartialCharacters) {
        entry.text.truncate(MaximumPartialCharacters);
        entry.text.append(QStringLiteral(" ... [truncated]"));
    }

    constexpr int trimBatch = 5000;
    if (m_entries.size() >= MaximumEntryCount) {
        const int removalCount = qMin(trimBatch, m_entries.size());
        beginRemoveRows({}, 0, removalCount - 1);
        m_entries.remove(0, removalCount);
        endRemoveRows();
        emit historyTrimmed();
    }

    const int row = m_entries.size();
    beginInsertRows({}, row, row);
    m_entries.append(std::move(entry));
    endInsertRows();
    emit countChanged();
}

bool LogModel::updateProgressFromLine(const QString &line)
{
    const QString normalized = normalizeProgressText(line);
    if (normalized.isEmpty()) {
        return false;
    }

    static const QRegularExpression percentExpression(
        QStringLiteral(R"((\d{1,3}(?:[.,]\d+)?)\s*%)"));
    static const QRegularExpression countExpression(
        QStringLiteral(R"((\d[\d,]*)\s*/\s*(\d[\d,]*))"));
    const QRegularExpressionMatch percentMatch = percentExpression.match(normalized);
    const QRegularExpressionMatch countMatch = countExpression.match(normalized);
    const bool hasProgressMarker = normalized.contains(QStringLiteral("%|"))
        || normalized.contains(QStringLiteral("it/s"), Qt::CaseInsensitive)
        || normalized.contains(QStringLiteral("s/it"), Qt::CaseInsensitive);
    if (!percentMatch.hasMatch() || !countMatch.hasMatch() || !hasProgressMarker) {
        return false;
    }

    bool percentOk = false;
    QString percentText = percentMatch.captured(1);
    percentText.replace(QLatin1Char(','), QLatin1Char('.'));
    const double parsedPercent = percentText.toDouble(&percentOk);

    bool currentOk = false;
    bool totalOk = false;
    QString currentText = countMatch.captured(1);
    QString totalText = countMatch.captured(2);
    currentText.remove(QLatin1Char(','));
    totalText.remove(QLatin1Char(','));
    const qint64 current = currentText.toLongLong(&currentOk);
    const qint64 total = totalText.toLongLong(&totalOk);
    if (!percentOk || !currentOk || !totalOk) {
        return false;
    }

    ProgressInfo progress;
    progress.percent = std::clamp(qRound(parsedPercent), 0, 100);
    progress.value = std::clamp(parsedPercent / 100.0, 0.0, 1.0);
    progress.current = current;
    progress.total = total;
    progress.indeterminate = total <= 0;

    QString prefix = normalized.first(percentMatch.capturedStart()).trimmed();
    static const QRegularExpression trailingDecoration(
        QStringLiteral(R"([|:>\-\s]+$)"));
    prefix.remove(trailingDecoration);
    if (!prefix.isEmpty() && prefix.size() <= 120) {
        progress.label = prefix;
    }

    static const QRegularExpression timingExpression(
        QStringLiteral(R"(\[\s*([^<,\]]+)\s*<\s*([^,\]]+)(?:,\s*([^,\]]+))?)"));
    const QRegularExpressionMatch timingMatch = timingExpression.match(normalized);
    if (timingMatch.hasMatch()) {
        progress.elapsed = timingMatch.captured(1).trimmed();
        progress.remaining = timingMatch.captured(2).trimmed();
        const QString rate = timingMatch.captured(3).trimmed();
        if (!rate.startsWith(QLatin1Char('?'))) {
            progress.rate = rate;
        }
    }

    applyProgress(progress);
    return true;
}

void LogModel::applyProgress(const ProgressInfo &progress)
{
    ProgressInfo effectiveProgress = progress;
    const bool continuesCurrentTask = m_progressActive
        && progress.percent >= m_progressPercent
        && progress.current >= m_progressCurrent;
    if (effectiveProgress.label.isEmpty() && continuesCurrentTask) {
        effectiveProgress.label = m_progressLabel;
    }

    const bool changed = !m_progressActive
        || m_progressIndeterminate != effectiveProgress.indeterminate
        || !qFuzzyCompare(m_progressValue + 1.0, effectiveProgress.value + 1.0)
        || m_progressPercent != effectiveProgress.percent
        || m_progressCurrent != effectiveProgress.current
        || m_progressTotal != effectiveProgress.total
        || m_progressLabel != effectiveProgress.label
        || m_progressElapsed != effectiveProgress.elapsed
        || m_progressRemaining != effectiveProgress.remaining
        || m_progressRate != effectiveProgress.rate;

    m_progressActive = true;
    m_progressIndeterminate = effectiveProgress.indeterminate;
    m_progressValue = effectiveProgress.value;
    m_progressPercent = effectiveProgress.percent;
    m_progressCurrent = effectiveProgress.current;
    m_progressTotal = effectiveProgress.total;
    m_progressLabel = effectiveProgress.label;
    m_progressElapsed = effectiveProgress.elapsed;
    m_progressRemaining = effectiveProgress.remaining;
    m_progressRate = effectiveProgress.rate;

    if (effectiveProgress.percent >= 100
        || (effectiveProgress.total > 0
            && effectiveProgress.current >= effectiveProgress.total)) {
        m_progressHideTimer.start();
    } else {
        m_progressHideTimer.stop();
    }

    if (changed) {
        emit progressChanged();
    }
}

void LogModel::resetProgress()
{
    m_progressHideTimer.stop();
    const bool changed = m_progressActive
        || m_progressIndeterminate
        || !qFuzzyIsNull(m_progressValue)
        || m_progressPercent != 0
        || m_progressCurrent != 0
        || m_progressTotal != 0
        || !m_progressLabel.isEmpty()
        || !m_progressElapsed.isEmpty()
        || !m_progressRemaining.isEmpty()
        || !m_progressRate.isEmpty();

    m_progressActive = false;
    m_progressIndeterminate = false;
    m_progressValue = 0.0;
    m_progressPercent = 0;
    m_progressCurrent = 0;
    m_progressTotal = 0;
    m_progressLabel.clear();
    m_progressElapsed.clear();
    m_progressRemaining.clear();
    m_progressRate.clear();
    if (changed) {
        emit progressChanged();
    }
}

QString LogModel::normalizeProgressText(QString line)
{
    static const QRegularExpression escapeExpression(
        QStringLiteral("\\x1B(?:\\[[0-?]*[ -/]*[@-~]|[@-_])"));
    static const QRegularExpression controlExpression(
        QStringLiteral("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F\\x7F]"));
    line.remove(escapeExpression);
    line.remove(controlExpression);
    line.remove(QChar::ReplacementCharacter);
    line.replace(QLatin1Char('\t'), QLatin1Char(' '));
    return line.trimmed();
}

QString LogModel::ansiColor(int code)
{
    switch (code) {
    case 30: return QStringLiteral("#000000");
    case 31: return QStringLiteral("#c42b1c");
    case 32: return QStringLiteral("#0f7b0f");
    case 33: return QStringLiteral("#9d5d00");
    case 34: return QStringLiteral("#0067c0");
    case 35: return QStringLiteral("#881798");
    case 36: return QStringLiteral("#038387");
    case 37: return QStringLiteral("#d6d6d6");
    case 90: return QStringLiteral("#767676");
    case 91: return QStringLiteral("#ff99a4");
    case 92: return QStringLiteral("#6ccb5f");
    case 93: return QStringLiteral("#fce100");
    case 94: return QStringLiteral("#60cdff");
    case 95: return QStringLiteral("#d8b7ff");
    case 96: return QStringLiteral("#5de2e7");
    case 97: return QStringLiteral("#ffffff");
    default: return {};
    }
}
