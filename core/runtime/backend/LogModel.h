#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QDateTime>
#include <QQuickTextDocument>
#include <QStringConverter>
#include <QTimer>

class LogModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool progressActive READ progressActive NOTIFY progressChanged)
    Q_PROPERTY(bool progressIndeterminate READ progressIndeterminate NOTIFY progressChanged)
    Q_PROPERTY(double progressValue READ progressValue NOTIFY progressChanged)
    Q_PROPERTY(int progressPercent READ progressPercent NOTIFY progressChanged)
    Q_PROPERTY(qint64 progressCurrent READ progressCurrent NOTIFY progressChanged)
    Q_PROPERTY(qint64 progressTotal READ progressTotal NOTIFY progressChanged)
    Q_PROPERTY(QString progressLabel READ progressLabel NOTIFY progressChanged)
    Q_PROPERTY(QString progressElapsed READ progressElapsed NOTIFY progressChanged)
    Q_PROPERTY(QString progressRemaining READ progressRemaining NOTIFY progressChanged)
    Q_PROPERTY(QString progressRate READ progressRate NOTIFY progressChanged)

public:
    static constexpr int MaximumEntryCount = 50000;
    static constexpr int MaximumPartialCharacters = 1024 * 1024;

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

    bool progressActive() const;
    bool progressIndeterminate() const;
    double progressValue() const;
    int progressPercent() const;
    qint64 progressCurrent() const;
    qint64 progressTotal() const;
    QString progressLabel() const;
    QString progressElapsed() const;
    QString progressRemaining() const;
    QString progressRate() const;

    void appendStandardOutput(const QByteArray &data);
    void appendStandardError(const QByteArray &data);
    void appendSystemMessage(const QString &message, const QString &color = {});
    void flush();
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString displayText(bool showTimestamps, bool compact = false) const;
    Q_INVOKABLE QString displayStyledText(bool showTimestamps,
                                          bool compact,
                                          const QString &secondaryColor,
                                          const QString &foregroundColor,
                                          const QString &warningColor,
                                          const QString &infoColor,
                                          const QString &successColor,
                                          const QString &errorColor) const;
    Q_INVOKABLE QString displayStyledTextRange(int firstRow,
                                               bool showTimestamps,
                                               bool compact,
                                               const QString &secondaryColor,
                                               const QString &foregroundColor,
                                               const QString &warningColor,
                                               const QString &infoColor,
                                               const QString &successColor,
                                               const QString &errorColor) const;
    Q_INVOKABLE void appendStyledTextRangeToDocument(
        QQuickTextDocument *document,
        int firstRow,
        bool showTimestamps,
        bool compact,
        const QColor &secondaryColor,
        const QColor &foregroundColor,
        const QColor &warningColor,
        const QColor &infoColor,
        const QColor &successColor,
        const QColor &errorColor) const;
    bool exportToFile(const QString &path, bool showTimestamps, QString *errorMessage = nullptr) const;

signals:
    void countChanged();
    void historyTrimmed();
    void progressChanged();

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
        bool suppressNextLineFeed = false;
    };

    struct ProgressInfo {
        bool indeterminate = false;
        double value = 0.0;
        int percent = 0;
        qint64 current = 0;
        qint64 total = 0;
        QString label;
        QString elapsed;
        QString remaining;
        QString rate;
    };

    void appendData(StreamState &state, const QByteArray &data, const QString &stream);
    void appendLine(StreamState &state, QString line, const QString &stream);
    void appendEntry(Entry entry);
    bool updateProgressFromLine(const QString &line);
    void applyProgress(const ProgressInfo &progress);
    void resetProgress();
    static QString normalizeProgressText(QString line);
    static QString ansiColor(int code);

    QList<Entry> m_entries;
    StreamState m_stdout;
    StreamState m_stderr;
    QTimer m_progressHideTimer;
    bool m_progressActive = false;
    bool m_progressIndeterminate = false;
    double m_progressValue = 0.0;
    int m_progressPercent = 0;
    qint64 m_progressCurrent = 0;
    qint64 m_progressTotal = 0;
    QString m_progressLabel;
    QString m_progressElapsed;
    QString m_progressRemaining;
    QString m_progressRate;
};
