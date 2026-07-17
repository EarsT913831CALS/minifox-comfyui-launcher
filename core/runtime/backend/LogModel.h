#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QStringConverter>

class LogModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        TimestampRole = Qt::UserRole + 1,
        TextRole,
        StreamRole,
        ColorRole,
        LineNumberRole
    };

    explicit LogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void appendStandardOutput(const QByteArray &data);
    void appendStandardError(const QByteArray &data);
    void appendSystemMessage(const QString &message, const QString &color = {});
    void flush();
    Q_INVOKABLE void clear();
    bool exportToFile(const QString &path, bool showTimestamps, QString *errorMessage = nullptr) const;

signals:
    void countChanged();

private:
    struct Entry {
        QDateTime timestamp;
        QString text;
        QString stream;
        QString color;
    };

    struct StreamState {
        QStringDecoder decoder {QStringDecoder::Utf8};
        QString partial;
        QString color;
    };

    void appendData(StreamState &state, const QByteArray &data, const QString &stream);
    void appendLine(StreamState &state, QString line, const QString &stream);
    static QString ansiColor(int code);

    QList<Entry> m_entries;
    StreamState m_stdout;
    StreamState m_stderr;
};
