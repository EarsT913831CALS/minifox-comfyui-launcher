#include "LogModel.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <utility>

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
    const int row = m_entries.size();
    beginInsertRows({}, row, row);
    m_entries.append({QDateTime::currentDateTime(), message, QStringLiteral("system"), color});
    endInsertRows();
    emit countChanged();
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

    const int row = m_entries.size();
    beginInsertRows({}, row, row);
    m_entries.append({QDateTime::currentDateTime(), line, stream, state.color});
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

    static const QRegularExpression bracketExpression(
        QStringLiteral(R"(\[([^\]]*)\]\s*$)"));
    const QRegularExpressionMatch bracketMatch = bracketExpression.match(normalized);
    if (progress.label.isEmpty() && bracketMatch.hasMatch()) {
        const QStringList parts =
            bracketMatch.captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (qsizetype index = 2; index < parts.size(); ++index) {
            const QString candidate = parts.at(index).trimmed();
            if (!candidate.isEmpty()
                && !candidate.contains(QStringLiteral("it/s"), Qt::CaseInsensitive)
                && !candidate.contains(QStringLiteral("s/it"), Qt::CaseInsensitive)) {
                progress.label = candidate.left(120);
                break;
            }
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
