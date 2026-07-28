#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class ApplicationSettings;

class ApplicationIconManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY stateChanged)
    Q_PROPERTY(QUrl pendingIconSource READ pendingIconSource NOTIFY stateChanged)
    Q_PROPERTY(QUrl activeIconSource READ activeIconSource NOTIFY stateChanged)
    Q_PROPERTY(bool customIconAvailable READ customIconAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit ApplicationIconManager(ApplicationSettings *settings, QObject *parent = nullptr);

    QString mode() const;
    Q_INVOKABLE void setMode(const QString &mode);
    QUrl pendingIconSource() const;
    QUrl activeIconSource() const;
    bool customIconAvailable() const;
    bool restartRequired() const;
    QString lastError() const;

    Q_INVOKABLE bool importCustomIcon(const QUrl &source);
    Q_INVOKABLE void reset();

signals:
    void stateChanged();
    void lastErrorChanged();

private:
    QUrl sourceForMode(const QString &mode) const;
    QString keyForMode(const QString &mode) const;
    QString customIconPath() const;
    void applyStartupIcon(const QUrl &source);
    void setLastError(const QString &message);

    ApplicationSettings *m_settings;
    QString m_activeIconKey;
    QUrl m_activeIconSource;
    QString m_lastError;
};
