#include "LogModel.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <utility>

LogModel::LogModel(QObject *parent)
    : QAbstractListModel(parent)
{
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
        appendLine(m_stdout, std::exchange(m_stdout.partial, {}), QStringLiteral("stdout"));
    }
    if (!m_stderr.partial.isEmpty()) {
        appendLine(m_stderr, std::exchange(m_stderr.partial, {}), QStringLiteral("stderr"));
    }
}

void LogModel::clear()
{
    beginResetModel();
    m_entries.clear();
    m_stdout.partial.clear();
    m_stdout.color.clear();
    m_stdout.decoder.resetState();
    m_stderr.partial.clear();
    m_stderr.color.clear();
    m_stderr.decoder.resetState();
    endResetModel();
    emit countChanged();
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
    qsizetype newline = -1;
    while ((newline = state.partial.indexOf(QLatin1Char('\n'))) >= 0) {
        QString line = state.partial.first(newline);
        state.partial.remove(0, newline + 1);
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        appendLine(state, line, stream);
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
