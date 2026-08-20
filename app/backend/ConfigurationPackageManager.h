#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>

class ConfigurationManager;

class ConfigurationPackageManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY stateChanged)

public:
    explicit ConfigurationPackageManager(ConfigurationManager *configuration,
                                         QObject *parent = nullptr);

    bool restartRequired() const;
    QString lastError() const;
    QString lastMessage() const;

    Q_INVOKABLE bool switchProfile(int index);
    Q_INVOKABLE bool addProfile();
    Q_INVOKABLE bool duplicateCurrentProfile();
    Q_INVOKABLE bool importPackage(const QUrl &source);
    Q_INVOKABLE bool exportPackage(const QUrl &destination,
                                   bool includeSensitiveValues = false);
    Q_INVOKABLE bool deleteProfiles(const QStringList &profileIds);
    Q_INVOKABLE bool restartLauncher();

signals:
    void stateChanged();

private:
    QString snapshotPath(const QString &profileId) const;
    bool captureCurrentProfile(const QString &destination,
                               bool includeSensitiveValues = true);
    bool applyPackageState(const QString &packagePath, bool *changed);
    void setError(const QString &message);
    void setMessage(const QString &message);

    ConfigurationManager *m_configuration;
    bool m_restartRequired = false;
    QString m_lastError;
    QString m_lastMessage;
};
