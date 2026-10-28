// Application settings, palette and rich-text safety.
#include "tst_Runtime.h"

void RuntimeTest::safeNoticeDoesNotFetchImages()
{
    QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
    const QString source = QString("**bold** ![probe](http://127.0.0.1:%1/pixel) [web](https://example.com)").arg(server.serverPort());
    QQmlEngine engine; QQmlComponent component(&engine);
    component.setData("import QtQuick\nText { width: 300; height: 100; textFormat: Text.RichText }", QUrl());
    QScopedPointer<QObject> item(component.create()); QVERIFY2(item, qPrintable(component.errorString()));
    item->setProperty("text", ExternalContent::markdown(source));
    QTest::qWait(250);
    QVERIFY(!server.hasPendingConnections());
    QVERIFY(item->property("text").toString().contains("https://example.com"));
}

void RuntimeTest::appearanceSettingsExposeEffectiveValues()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ApplicationSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.json")));

    settings.setThemeMode(QStringLiteral("dark"));
    QVERIFY(settings.effectiveDark());
    QCOMPARE(QGuiApplication::palette().color(QPalette::Window), QColor(QStringLiteral("#202020")));
    QCOMPARE(QGuiApplication::palette().color(QPalette::WindowText), QColor(QStringLiteral("#ffffff")));
    QCOMPARE(QGuiApplication::palette().color(QPalette::Text), QColor(QStringLiteral("#ffffff")));
    QCOMPARE(QGuiApplication::palette().color(QPalette::ButtonText), QColor(QStringLiteral("#ffffff")));
    settings.setThemeMode(QStringLiteral("light"));
    QVERIFY(!settings.effectiveDark());
    QCOMPARE(QGuiApplication::palette().color(QPalette::Window), QColor(QStringLiteral("#f7f7f7")));
    QCOMPARE(QGuiApplication::palette().color(QPalette::WindowText), QColor(QStringLiteral("#1b1b1b")));
    QCOMPARE(QGuiApplication::palette().color(QPalette::Text), QColor(QStringLiteral("#1b1b1b")));
    QCOMPARE(QGuiApplication::palette().color(QPalette::ButtonText), QColor(QStringLiteral("#1b1b1b")));

    settings.setAccentMode(QStringLiteral("custom"));
    settings.setAccentColor(QStringLiteral("#7a52cc"));
    QCOMPARE(QGuiApplication::palette().color(QPalette::Accent), QColor(QStringLiteral("#7a52cc")));
    QCOMPARE(QGuiApplication::palette().color(QPalette::Highlight), QColor(QStringLiteral("#7a52cc")));
    settings.setAccentMode(QStringLiteral("system"));
    QCOMPARE(QGuiApplication::palette().color(QPalette::Accent),
             QColor(settings.effectiveAccentColor()));
    settings.setAccentMode(QStringLiteral("custom"));

    const QString systemFamily = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    const QStringList installedFamilies = QFontDatabase::families();
    QVERIFY(!installedFamilies.isEmpty());
    const auto alternativeFamily = std::find_if(
        installedFamilies.cbegin(), installedFamilies.cend(), [&systemFamily](const QString &family) {
            return family != systemFamily;
        });
    const QString requestedFamily = installedFamilies.contains(QStringLiteral("Microsoft YaHei UI"))
        ? QStringLiteral("Microsoft YaHei UI")
        : alternativeFamily != installedFamilies.cend() ? *alternativeFamily : systemFamily;

    settings.setFontFamily(requestedFamily);
    settings.setFontPointSize(18.0);
    QCOMPARE(settings.effectiveFontFamily(), requestedFamily);
    QCOMPARE(QGuiApplication::font().families().constFirst(), requestedFamily);
    QCOMPARE(QGuiApplication::font().pointSizeF(), 18.0);
    settings.setFontFamily({});
    QCOMPARE(settings.effectiveFontFamily(), systemFamily);
    QCOMPARE(QGuiApplication::font().family(), systemFamily);
}

void RuntimeTest::quickControlsUseTheApplicationPaletteWhenCreated()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ApplicationSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.json")));
    settings.setThemeMode(QStringLiteral("dark"));

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls

        Control {
            Label {
                objectName: "defaultLabel"
                text: "Label"
            }
            Button {
                objectName: "defaultButton"
                text: "Button"
            }
        }
    )", QUrl(QStringLiteral("inmemory:/PaletteProbe.qml")));

    QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 5000);
    QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    QObject *label = root->findChild<QObject *>(QStringLiteral("defaultLabel"));
    QObject *button = root->findChild<QObject *>(QStringLiteral("defaultButton"));
    QVERIFY(label);
    QVERIFY(button);

    QCOMPARE(label->property("color").value<QColor>(), QColor(QStringLiteral("#ffffff")));
    QCOMPARE(button->property("palette").value<QPalette>().color(QPalette::ButtonText),
             QColor(QStringLiteral("#ffffff")));

    settings.setThemeMode(QStringLiteral("light"));
    QCoreApplication::processEvents();
    root.reset(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    label = root->findChild<QObject *>(QStringLiteral("defaultLabel"));
    button = root->findChild<QObject *>(QStringLiteral("defaultButton"));
    QVERIFY(label);
    QVERIFY(button);
    QCOMPARE(label->property("color").value<QColor>(), QColor(QStringLiteral("#1b1b1b")));
    QCOMPARE(button->property("palette").value<QPalette>().color(QPalette::ButtonText),
             QColor(QStringLiteral("#1b1b1b")));
}

void RuntimeTest::directControlPaletteBindingsOverrideStyleDefaults()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls

        Item {
            id: root
            property bool darkTheme: true
            readonly property color foreground: darkTheme ? "#ffffff" : "#1b1b1b"

            Label {
                objectName: "label"
                color: root.foreground
            }
            Button {
                objectName: "button"
                text: "Button"
                palette.buttonText: root.foreground
            }
            ToolButton {
                objectName: "toolButton"
                text: "Tool"
                palette.buttonText: root.foreground
            }
            ComboBox {
                objectName: "comboBox"
                model: ["Value"]
                palette.text: root.foreground
                palette.buttonText: root.foreground
            }
            SpinBox {
                objectName: "spinBox"
                palette.buttonText: root.foreground
            }
            Switch {
                objectName: "switchControl"
                text: "Switch"
                palette.text: root.foreground
            }
            ItemDelegate {
                objectName: "itemDelegate"
                text: "Delegate"
                palette.buttonText: root.foreground
            }
            TextField {
                objectName: "textField"
                text: "Field"
                color: root.foreground
                palette.text: root.foreground
            }
            TextArea {
                objectName: "textArea"
                text: "Area"
                color: root.foreground
                palette.text: root.foreground
            }
        }
    )", QUrl(QStringLiteral("inmemory:/DirectPaletteProbe.qml")));

    QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 5000);
    QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));

    const QStringList contentControls {
        QStringLiteral("button"),
        QStringLiteral("toolButton"),
        QStringLiteral("comboBox"),
        QStringLiteral("spinBox"),
        QStringLiteral("switchControl"),
        QStringLiteral("itemDelegate")
    };
    const auto verifyForeground = [&root, &contentControls](const QColor &expected) {
        QObject *label = root->findChild<QObject *>(QStringLiteral("label"));
        QObject *textField = root->findChild<QObject *>(QStringLiteral("textField"));
        QObject *textArea = root->findChild<QObject *>(QStringLiteral("textArea"));
        QVERIFY(label);
        QVERIFY(textField);
        QVERIFY(textArea);
        QCOMPARE(label->property("color").value<QColor>(), expected);
        QCOMPARE(textField->property("color").value<QColor>(), expected);
        QCOMPARE(textArea->property("color").value<QColor>(), expected);

        for (const QString &name : contentControls) {
            QObject *control = root->findChild<QObject *>(name);
            QVERIFY2(control, qPrintable(name));
            QObject *contentItem = control->property("contentItem").value<QObject *>();
            QVERIFY2(contentItem, qPrintable(name));
            QCOMPARE(contentItem->property("color").value<QColor>(), expected);
        }
    };

    verifyForeground(QColor(QStringLiteral("#ffffff")));
    root->setProperty("darkTheme", false);
    QCoreApplication::processEvents();
    verifyForeground(QColor(QStringLiteral("#1b1b1b")));
}

void RuntimeTest::applicationSettingsPersistAcrossInstances()
{
    QCOMPARE(ApplicationSettings::effectiveLanguage(QStringLiteral("system"), QLocale::Chinese),
             QStringLiteral("zh_CN"));
    QCOMPARE(ApplicationSettings::effectiveLanguage(QStringLiteral("system"), QLocale::French),
             QStringLiteral("en_US"));
    QCOMPARE(ApplicationSettings::effectiveLanguage(QStringLiteral("zh_CN"), QLocale::English),
             QStringLiteral("zh_CN"));
    QCOMPARE(ApplicationSettings::effectiveLanguage(QStringLiteral("en_US"), QLocale::Chinese),
             QStringLiteral("en_US"));

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString storagePath = temporaryDirectory.filePath(QStringLiteral("settings.json"));

    {
        ApplicationSettings settings(storagePath);
        QCOMPARE(settings.accentMode(), QStringLiteral("system"));
        QVERIFY(QColor(settings.effectiveAccentColor()).isValid());
        settings.setThemeMode(QStringLiteral("dark"));
        settings.setLanguage(QStringLiteral("en_US"));
        settings.setFontFamily(QStringLiteral("Microsoft YaHei UI"));
        settings.setFontPointSize(15.0);
        settings.setAccentMode(QStringLiteral("custom"));
        settings.setAccentColor(QStringLiteral("#7a52cc"));
        settings.setConsoleTheme(QStringLiteral("light"));
        settings.setConsoleFontFamily(QStringLiteral("Consolas"));
        settings.setConsoleFontSize(13.0);
        settings.setConsoleWordWrap(true);
        settings.setShowTimestamps(false);
        settings.setReducedMotion(true);
        settings.setProxyMode(QStringLiteral("manual"));
        settings.setProxyHost(QStringLiteral("127.0.0.1"));
        settings.setProxyPort(8899);
    }

    ApplicationSettings restored(storagePath);
    QCOMPARE(restored.themeMode(), QStringLiteral("dark"));
    QCOMPARE(restored.language(), QStringLiteral("en_US"));
    QCOMPARE(restored.fontFamily(), QStringLiteral("Microsoft YaHei UI"));
    QCOMPARE(restored.fontPointSize(), 15.0);
    QCOMPARE(restored.accentMode(), QStringLiteral("custom"));
    QCOMPARE(restored.accentColor(), QStringLiteral("#7a52cc"));
    QCOMPARE(restored.effectiveAccentColor(), QStringLiteral("#7a52cc"));
    QCOMPARE(restored.consoleTheme(), QStringLiteral("light"));
    QCOMPARE(restored.consoleFontFamily(), QStringLiteral("Consolas"));
    QCOMPARE(restored.consoleFontSize(), 13.0);
    QVERIFY(restored.consoleWordWrap());
    QVERIFY(!restored.showTimestamps());
    QVERIFY(restored.reducedMotion());
    QCOMPARE(restored.proxyMode(), QStringLiteral("manual"));
    QCOMPARE(restored.proxyHost(), QStringLiteral("127.0.0.1"));
    QCOMPARE(restored.proxyPort(), 8899);
}
