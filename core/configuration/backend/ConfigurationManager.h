#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class ConfigurationManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList profileNames READ profileNames NOTIFY profilesChanged)
    Q_PROPERTY(int currentProfileIndex READ currentProfileIndex WRITE setCurrentProfileIndex NOTIFY currentProfileChanged)
    Q_PROPERTY(QString currentProfileName READ currentProfileName WRITE setCurrentProfileName NOTIFY currentProfileChanged)
    Q_PROPERTY(QString pythonPath READ pythonPath WRITE setPythonPath NOTIFY currentProfileChanged)
    Q_PROPERTY(QString comfyRoot READ comfyRoot WRITE setComfyRoot NOTIFY currentProfileChanged)
    Q_PROPERTY(QString customArguments READ customArguments WRITE setCustomArguments NOTIFY currentProfileChanged)
    Q_PROPERTY(QVariantList environmentEntries READ environmentEntries NOTIFY currentProfileChanged)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY catalogChanged)
    Q_PROPERTY(int parameterRevision READ parameterRevision NOTIFY parameterRevisionChanged)
    Q_PROPERTY(bool valid READ isValid NOTIFY validationChanged)
    Q_PROPERTY(QStringList validationErrors READ validationErrors NOTIFY validationChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit ConfigurationManager(const QString &storagePath = {}, QObject *parent = nullptr);

    QStringList profileNames() const;
    int currentProfileIndex() const;
    void setCurrentProfileIndex(int index);

    QString currentProfileName() const;
    void setCurrentProfileName(const QString &name);
    QString pythonPath() const;
    void setPythonPath(const QString &path);
    QString comfyRoot() const;
    void setComfyRoot(const QString &path);
    QString customArguments() const;
    void setCustomArguments(const QString &arguments);

    QVariantList environmentEntries() const;
    QVariantList categories() const;
    int parameterRevision() const;
    bool isValid() const;
    QStringList validationErrors() const;
    QString lastError() const;

    Q_INVOKABLE QVariantList parametersForCategory(const QString &category) const;
    Q_INVOKABLE QVariant parameterValue(const QString &key) const;
    Q_INVOKABLE void setParameterValue(const QString &key, const QVariant &value);

    Q_INVOKABLE void addProfile(const QString &name = {});
    Q_INVOKABLE void duplicateCurrentProfile();
    Q_INVOKABLE bool removeCurrentProfile();

    Q_INVOKABLE int addEnvironmentEntry();
    Q_INVOKABLE void updateEnvironmentEntry(int index, const QString &name, const QString &value, bool enabled);
    Q_INVOKABLE void removeEnvironmentEntry(int index);

    QVariantMap currentProfileSnapshot() const;
    void retranslate();

signals:
    void profilesChanged();
    void currentProfileChanged();
    void parameterRevisionChanged();
    void catalogChanged();
    void validationChanged();
    void lastErrorChanged();

private:
    struct EnvironmentEntry {
        QString name;
        QString value;
        bool enabled = true;
    };

    struct Profile {
        QString id;
        QString name;
        QString pythonPath;
        QString comfyRoot;
        QString customArguments;
        QVariantMap parameters;
        QList<EnvironmentEntry> environment;
    };

    Profile &currentProfile();
    const Profile &currentProfile() const;
    Profile makeDefaultProfile(const QString &name) const;
    void load();
    bool save();
    void updateAfterEdit(bool parametersChanged = false);
    void validate();
    QStringList environmentEntryErrors(const Profile &profile) const;
    void setLastError(const QString &message);

    static QString normalizePath(const QString &path);
    static QString findDefaultComfyRoot();
    static QString findDefaultPython(const QString &comfyRoot);

    QString m_storagePath;
    QList<Profile> m_profiles;
    int m_currentProfileIndex = 0;
    int m_parameterRevision = 0;
    QStringList m_validationErrors;
    QString m_lastError;
};
