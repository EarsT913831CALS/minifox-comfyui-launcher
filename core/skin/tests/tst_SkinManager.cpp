#include "ApplicationSettings.h"
#include "ConfigurationManager.h"
#include "SkinManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

class EnglishFirstRunTranslator final : public QTranslator
{
public:
    QString translate(const char *, const char *sourceText,
                      const char * = nullptr, int = -1) const override
    {
        const QString source = QString::fromUtf8(sourceText);
        if (source == QStringLiteral("默认配置")) {
            return QStringLiteral("Default Profile");
        }
        if (source == QStringLiteral("默认皮肤")) {
            return QStringLiteral("Default Skin");
        }
        return {};
    }
};

class SkinManagerTest final : public QObject
{
    Q_OBJECT

private slots:
    void createsEditsAndPersistsSkin();
    void copiesAndDeduplicatesImageAssets();
    void exportsAndImportsPortablePackage();
    void rejectsDamagedPackage();
    void migratesLegacyBackgroundMaterials();
    void folderItemsAreLimitedAndKeepCustomPaths();
    void externalTargetsAreRestricted();
    void firstRunDataUsesInstalledLanguageAndBuiltinRetranslates();
};

void SkinManagerTest::firstRunDataUsesInstalledLanguageAndBuiltinRetranslates()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    EnglishFirstRunTranslator english;
    QVERIFY(QCoreApplication::installTranslator(&english));
    ApplicationSettings englishSettings(
        temporary.filePath(QStringLiteral("settings-en.json")));
    ConfigurationManager englishConfiguration(
        temporary.filePath(QStringLiteral("profiles-en.json")));
    SkinManager englishSkins(
        &englishSettings, temporary.filePath(QStringLiteral("skins-en")));
    QCOMPARE(englishConfiguration.currentProfileName(),
             QStringLiteral("Default Profile"));
    QCOMPARE(englishSkins.activeSkinName(), QStringLiteral("Default Skin"));

    QCoreApplication::removeTranslator(&english);
    englishConfiguration.retranslate();
    englishSkins.retranslate();
    // Profile names are user data and stay in the language chosen on first
    // creation. The built-in skin is generated state and follows the current
    // UI language immediately.
    QCOMPARE(englishConfiguration.currentProfileName(),
             QStringLiteral("Default Profile"));
    QCOMPARE(englishSkins.activeSkinName(), QStringLiteral("默认皮肤"));

    ApplicationSettings chineseSettings(
        temporary.filePath(QStringLiteral("settings-zh.json")));
    ConfigurationManager chineseConfiguration(
        temporary.filePath(QStringLiteral("profiles-zh.json")));
    SkinManager chineseSkins(
        &chineseSettings, temporary.filePath(QStringLiteral("skins-zh")));
    QCOMPARE(chineseConfiguration.currentProfileName(), QStringLiteral("默认配置"));
    QCOMPARE(chineseSkins.activeSkinName(), QStringLiteral("默认皮肤"));
}

void SkinManagerTest::createsEditsAndPersistsSkin()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString settingsPath = temporary.filePath(QStringLiteral("settings.json"));
    const QString skinsPath = temporary.filePath(QStringLiteral("skins"));
    ApplicationSettings settings(settingsPath);
    SkinManager manager(&settings, skinsPath);

    QCOMPARE(manager.activeSkinId(), QStringLiteral("builtin.default"));
    QVERIFY(manager.activeSkinBuiltin());
    const QString id = manager.createSkin(QStringLiteral("Test"));
    QVERIFY(!id.isEmpty());
    QCOMPARE(manager.activeSkinId(), id);
    QVERIFY(!manager.activeSkinBuiltin());

    manager.beginEdit();
    QVERIFY(manager.editing());
    const QString textId = manager.addItem(QStringLiteral("text"));
    QVERIFY(!textId.isEmpty());
    const QString imageId = manager.addItem(QStringLiteral("image"));
    QVERIFY(!imageId.isEmpty());
    QVERIFY(manager.canUndo());
    QVERIFY(manager.updateItem(textId, {
        {QStringLiteral("x"), 2.0},
        {QStringLiteral("w"), 0.5},
        {QStringLiteral("opacity"), 0.0}
    }));
    const QVariantList edited = manager.homeItems();
    const QVariantMap item = edited.at(1).toMap();
    QCOMPARE(item.value(QStringLiteral("x")).toDouble(), 0.5);
    QCOMPARE(item.value(QStringLiteral("opacity")).toDouble(), 0.05);
    QVERIFY(manager.updateItem(imageId, {
        {QStringLiteral("properties"), QVariantMap{
            {QStringLiteral("fillMode"), QStringLiteral("invalid")},
            {QStringLiteral("focusX"), -1.0},
            {QStringLiteral("focusY"), 2.0},
            {QStringLiteral("zoom"), 9.0}
        }}
    }));
    const QVariantMap imageProperties =
        manager.homeItems().constLast().toMap().value(QStringLiteral("properties")).toMap();
    QCOMPARE(imageProperties.value(QStringLiteral("fillMode")).toString(),
             QStringLiteral("cover"));
    QCOMPARE(imageProperties.value(QStringLiteral("focusX")).toDouble(), 0.0);
    QCOMPARE(imageProperties.value(QStringLiteral("focusY")).toDouble(), 1.0);
    QCOMPARE(imageProperties.value(QStringLiteral("zoom")).toDouble(), 4.0);
    manager.undo();
    QVERIFY(manager.canRedo());
    manager.redo();
    QVERIFY(manager.commitEdit());

    SkinManager restored(&settings, skinsPath);
    QCOMPARE(restored.activeSkinId(), id);
    QCOMPARE(restored.lastError(), QString{});
    QCOMPARE(restored.homeItems().size(), 3);
    QVERIFY(restored.removeSkin(id));
    QCOMPARE(restored.activeSkinId(), QStringLiteral("builtin.default"));
}

void SkinManagerTest::folderItemsAreLimitedAndKeepCustomPaths()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    ApplicationSettings settings(temporary.filePath(QStringLiteral("settings.json")));
    SkinManager manager(&settings, temporary.filePath(QStringLiteral("skins")));
    QVERIFY(!manager.createSkin(QStringLiteral("Folders")).isEmpty());

    manager.beginEdit();
    const QString folderId = manager.addItem(QStringLiteral("folders"));
    QVERIFY(!folderId.isEmpty());

    QVariantList folders;
    for (int index = 0; index < 10; ++index) {
        folders.append(QVariantMap{
            {QStringLiteral("title"), QStringLiteral("文件夹 %1").arg(index + 1)},
            {QStringLiteral("path"), QStringLiteral("D:/Folder%1").arg(index + 1)}
        });
    }
    QVERIFY(manager.updateItem(folderId, {
        {QStringLiteral("properties"), QVariantMap{
            {QStringLiteral("folders"), folders}
        }}
    }));

    QVariantMap folderItem;
    for (const QVariant &entry : manager.homeItems()) {
        const QVariantMap item = entry.toMap();
        if (item.value(QStringLiteral("id")).toString() == folderId) {
            folderItem = item;
            break;
        }
    }
    QVERIFY(!folderItem.isEmpty());
    const QVariantList normalizedFolders = folderItem.value(QStringLiteral("properties"))
                                               .toMap()
                                               .value(QStringLiteral("folders"))
                                               .toList();
    QCOMPARE(normalizedFolders.size(), 8);
    QCOMPARE(normalizedFolders.constFirst().toMap()
                 .value(QStringLiteral("path")).toString(),
             QStringLiteral("D:/Folder1"));
    QCOMPARE(normalizedFolders.constLast().toMap()
                 .value(QStringLiteral("path")).toString(),
             QStringLiteral("D:/Folder8"));
}

void SkinManagerTest::externalTargetsAreRestricted()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    QVERIFY(SkinManager::isAllowedLocalFolderPath(temporary.path()));
    QVERIFY(!SkinManager::isAllowedLocalFolderPath(
        temporary.filePath(QStringLiteral("missing"))));
    QVERIFY(!SkinManager::isAllowedLocalFolderPath(
        QStringLiteral("file:///C:/Windows")));
    QVERIFY(!SkinManager::isAllowedLocalFolderPath(
        QStringLiteral("//server/share")));

    QVERIFY(SkinManager::isAllowedExternalLink(
        QUrl(QStringLiteral("https://example.com/project"))));
    QVERIFY(SkinManager::isAllowedExternalLink(
        QUrl(QStringLiteral("http://example.com"))));
    QVERIFY(!SkinManager::isAllowedExternalLink(
        QUrl(QStringLiteral("file:///C:/Windows/System32/cmd.exe"))));
    QVERIFY(!SkinManager::isAllowedExternalLink(
        QUrl(QStringLiteral("ms-settings:windowsupdate"))));
    QVERIFY(!SkinManager::isAllowedExternalLink(
        QUrl(QStringLiteral("https:///missing-host"))));
}

void SkinManagerTest::copiesAndDeduplicatesImageAssets()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    ApplicationSettings settings(temporary.filePath(QStringLiteral("settings.json")));
    SkinManager manager(&settings, temporary.filePath(QStringLiteral("skins")));
    QVERIFY(!manager.createSkin(QStringLiteral("Images")).isEmpty());

    const QString source = temporary.filePath(QStringLiteral("source.png"));
    QImage image(32, 24, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(QStringLiteral("#ff3366")));
    QVERIFY(image.save(source));

    const QString first = manager.importImage(QUrl::fromLocalFile(source));
    const QString second = manager.importImage(QUrl::fromLocalFile(source));
    QVERIFY(first.startsWith(QStringLiteral("assets/")));
    QCOMPARE(first, second);
    QVERIFY(manager.setBackgroundImage(QUrl::fromLocalFile(source)));
    const QVariantMap appearance = manager.effectiveAppearance();
    QVERIFY(!appearance.value(QStringLiteral("backgroundSource")).toString().isEmpty());
    QCOMPARE(appearance.value(QStringLiteral("materials")).toMap()
                 .value(QStringLiteral("pageOpacity")).toDouble(), 0.62);
    QVERIFY(QFileInfo::exists(QUrl(manager.assetUrl(first)).toLocalFile()));
    QVERIFY(QFile::remove(source));
    QVERIFY(QFileInfo::exists(QUrl(manager.assetUrl(first)).toLocalFile()));
}

void SkinManagerTest::exportsAndImportsPortablePackage()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    ApplicationSettings settings(temporary.filePath(QStringLiteral("settings.json")));
    SkinManager source(&settings, temporary.filePath(QStringLiteral("source-skins")));
    QVERIFY(!source.createSkin(QStringLiteral("Portable")).isEmpty());

    const QString imagePath = temporary.filePath(QStringLiteral("background.png"));
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QVERIFY(image.save(imagePath));
    const QString asset = source.importImage(QUrl::fromLocalFile(imagePath));
    QVERIFY(!asset.isEmpty());
    source.setAppearanceValue(QStringLiteral("background.asset"), asset);

    const QString package = temporary.filePath(QStringLiteral("portable.minifoxskin"));
    QVERIFY(source.exportSkin(QUrl::fromLocalFile(package)));
    QVERIFY(QFileInfo(package).size() > 0);

    SkinManager destination(&settings, temporary.filePath(QStringLiteral("destination-skins")));
    QVERIFY(destination.importSkin(QUrl::fromLocalFile(package)));
    QCOMPARE(destination.activeSkinName(), QStringLiteral("Portable"));
    QVERIFY(!destination.effectiveAppearance()
                 .value(QStringLiteral("backgroundSource")).toString().isEmpty());
}

void SkinManagerTest::rejectsDamagedPackage()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString package = temporary.filePath(QStringLiteral("damaged.minifoxskin"));
    QFile file(package);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not a zip");
    file.close();

    ApplicationSettings settings(temporary.filePath(QStringLiteral("settings.json")));
    SkinManager manager(&settings, temporary.filePath(QStringLiteral("skins")));
    QVERIFY(!manager.importSkin(QUrl::fromLocalFile(package)));
    QVERIFY(!manager.lastError().isEmpty());
    QCOMPARE(manager.activeSkinId(), QStringLiteral("builtin.default"));
}

void SkinManagerTest::migratesLegacyBackgroundMaterials()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString id = QStringLiteral("legacy-background");
    const QString skinsPath = temporary.filePath(QStringLiteral("skins"));
    const QString skinPath = QDir(skinsPath).filePath(id);
    const QString assetsPath = QDir(skinPath).filePath(QStringLiteral("assets"));
    QVERIFY(QDir().mkpath(assetsPath));

    QImage background(24, 24, QImage::Format_RGB32);
    background.fill(QColor(QStringLiteral("#4f84c4")));
    QVERIFY(background.save(QDir(assetsPath).filePath(QStringLiteral("background.png"))));

    const QJsonObject legacySkin{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), QStringLiteral("Legacy")},
        {QStringLiteral("appearance"), QJsonObject{
            {QStringLiteral("materials"), QJsonObject{
                {QStringLiteral("pageOpacity"), 0.94},
                {QStringLiteral("panelOpacity"), 0.94},
                {QStringLiteral("sidebarOpacity"), 0.96},
                {QStringLiteral("titleBarOpacity"), 0.98}
            }},
            {QStringLiteral("background"), QJsonObject{
                {QStringLiteral("asset"), QStringLiteral("assets/background.png")},
                {QStringLiteral("opacity"), 0.35}
            }}
        }},
        {QStringLiteral("home"), QJsonObject{
            {QStringLiteral("items"), QJsonArray{}}
        }}
    };

    QFile skinFile(QDir(skinPath).filePath(QStringLiteral("skin.json")));
    QVERIFY(skinFile.open(QIODevice::WriteOnly));
    skinFile.write(QJsonDocument(legacySkin).toJson(QJsonDocument::Indented));
    skinFile.close();

    QFile indexFile(QDir(skinsPath).filePath(QStringLiteral("index.json")));
    QVERIFY(indexFile.open(QIODevice::WriteOnly));
    indexFile.write(QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("activeSkinId"), id}
    }).toJson(QJsonDocument::Indented));
    indexFile.close();

    ApplicationSettings settings(temporary.filePath(QStringLiteral("settings.json")));
    SkinManager manager(&settings, skinsPath);
    QCOMPARE(manager.activeSkinId(), id);
    const QVariantMap materials =
        manager.effectiveAppearance().value(QStringLiteral("materials")).toMap();
    QCOMPARE(materials.value(QStringLiteral("pageOpacity")).toDouble(), 0.62);
    QCOMPARE(materials.value(QStringLiteral("panelOpacity")).toDouble(), 0.84);
    QCOMPARE(materials.value(QStringLiteral("sidebarOpacity")).toDouble(), 0.88);
    QCOMPARE(materials.value(QStringLiteral("titleBarOpacity")).toDouble(), 0.90);
    QCOMPARE(manager.effectiveAppearance().value(QStringLiteral("background")).toMap()
                 .value(QStringLiteral("opacity")).toDouble(), 1.0);

    QVERIFY(skinFile.open(QIODevice::ReadOnly));
    const QJsonObject persisted =
        QJsonDocument::fromJson(skinFile.readAll()).object();
    QCOMPARE(persisted.value(QStringLiteral("schemaVersion")).toInt(), 2);
    QCOMPARE(persisted.value(QStringLiteral("appearance")).toObject()
                 .value(QStringLiteral("materials")).toObject()
                 .value(QStringLiteral("pageOpacity")).toDouble(), 0.62);
}

QTEST_MAIN(SkinManagerTest)
#include "tst_SkinManager.moc"
