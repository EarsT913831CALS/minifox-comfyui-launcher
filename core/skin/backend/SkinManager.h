#pragma once

#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class ApplicationSettings;

class SkinManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList skins READ skins NOTIFY skinsChanged)
    Q_PROPERTY(QString activeSkinId READ activeSkinId NOTIFY activeSkinChanged)
    Q_PROPERTY(QString activeSkinName READ activeSkinName NOTIFY activeSkinChanged)
    Q_PROPERTY(bool activeSkinBuiltin READ activeSkinBuiltin NOTIFY activeSkinChanged)
    Q_PROPERTY(QVariantMap effectiveAppearance READ effectiveAppearance NOTIFY appearanceChanged)
    Q_PROPERTY(QVariantList homeItems READ homeItems NOTIFY homeItemsChanged)
    Q_PROPERTY(bool editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit SkinManager(ApplicationSettings *settings,
                         const QString &storageDirectory = {},
                         QObject *parent = nullptr);

    QVariantList skins() const;
    QString activeSkinId() const;
    QString activeSkinName() const;
    bool activeSkinBuiltin() const;
    QVariantMap effectiveAppearance() const;
    QVariantList homeItems() const;
    bool editing() const;
    bool canUndo() const;
    bool canRedo() const;
    QString lastError() const;
    void retranslate();
    static bool isAllowedLocalFolderPath(const QString &path);
    static bool isAllowedExternalLink(const QUrl &url);

    Q_INVOKABLE QString createSkin(const QString &name = {});
    Q_INVOKABLE QString duplicateActiveSkin(const QString &name = {});
    Q_INVOKABLE bool renameActiveSkin(const QString &name);
    Q_INVOKABLE bool removeSkin(const QString &id = {});
    Q_INVOKABLE bool selectSkin(const QString &id);

    Q_INVOKABLE bool importSkin(const QUrl &source);
    Q_INVOKABLE bool exportSkin(const QUrl &destination);
    Q_INVOKABLE QString importImage(const QUrl &source);
    Q_INVOKABLE bool setBackgroundImage(const QUrl &source);
    Q_INVOKABLE QString assetUrl(const QString &assetReference) const;
    Q_INVOKABLE bool openLocalFolder(const QString &path);
    Q_INVOKABLE bool openExternalLink(const QUrl &url);

    Q_INVOKABLE void setAppearanceValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void beginEdit();
    Q_INVOKABLE QString addItem(const QString &type);
    Q_INVOKABLE bool duplicateItem(const QString &id);
    Q_INVOKABLE bool updateItem(const QString &id, const QVariantMap &changes);
    Q_INVOKABLE bool removeItem(const QString &id);
    Q_INVOKABLE bool moveItemLayer(const QString &id, int direction);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE bool commitEdit();
    Q_INVOKABLE void cancelEdit();
    Q_INVOKABLE void resetHomeLayout();

signals:
    void skinsChanged();
    void activeSkinChanged();
    void appearanceChanged();
    void homeItemsChanged();
    void editingChanged();
    void historyChanged();
    void lastErrorChanged();

private:
    struct SkinEntry {
        QString id;
        QString name;
        QString directory;
        bool builtin = false;
        QJsonObject document;
    };

    static QJsonObject defaultSkinDocument(ApplicationSettings *settings);
    static QJsonObject defaultHome();
    static QJsonObject normalizedDocument(const QJsonObject &document,
                                          const QString &fallbackId,
                                          const QString &fallbackName,
                                          QString *error);
    static QJsonObject normalizedItem(const QJsonObject &item);
    static bool isSupportedItemType(const QString &type);

    SkinEntry *activeEntry();
    const SkinEntry *activeEntry() const;
    QJsonObject currentDocument() const;
    bool ensureWritableSkin();
    bool load();
    bool loadCustomSkin(const QString &directory, SkinEntry *entry);
    bool saveIndex();
    bool saveEntry(SkinEntry &entry);
    void refreshAfterDocumentChange(bool appearance = true, bool home = true);
    void pushHistory();
    void setLastError(const QString &message);
    QString uniqueName(const QString &requested) const;
    QString skinDirectory(const QString &id) const;
    QString resolveAssetPath(const SkinEntry &entry, const QString &reference) const;
    QString copyAssetIntoSkin(const QString &sourcePath);

    ApplicationSettings *m_settings;
    QString m_storageDirectory;
    QList<SkinEntry> m_entries;
    int m_activeIndex = 0;
    bool m_editing = false;
    QJsonObject m_draft;
    QList<QJsonObject> m_undo;
    QList<QJsonObject> m_redo;
    QString m_lastError;
};
