#include "LaunchCommandBuilder.h"
#include "ApplicationSettings.h"
#include "ConfigurationManager.h"
#include "LogModel.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

class LaunchCommandBuilderTest final : public QObject
{
    Q_OBJECT

public:
    static void initMain()
    {
        QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));
    }

private slots:
    void defaultsStayImplicit();
    void explicitModesAndCustomArguments();
    void environmentIsAppliedAndSecretsAreMasked();
    void proxySettingsAreAppliedToChildEnvironment();
    void appearanceSettingsExposeEffectiveValues();
    void quickControlsUseTheApplicationPaletteWhenCreated();
    void directControlPaletteBindingsOverrideStyleDefaults();
    void applicationSettingsPersistAcrossInstances();
    void profilesPersistWithoutLeavingTheTestDirectory();
    void tqdmProgressIsSeparatedFromConsoleLog();
    void carriageReturnLineEndingsRemainNormalLogLines();
};

void LaunchCommandBuilderTest::defaultsStayImplicit()
{
    const QVariantMap profile {
        {QStringLiteral("pythonPath"), QStringLiteral("C:/Python/python.exe")},
        {QStringLiteral("comfyRoot"), QStringLiteral("C:/ComfyUI")},
        {QStringLiteral("parameters"), QVariantMap{}},
        {QStringLiteral("environment"), QVariantList{}}
    };

    const auto result = LaunchCommandBuilder::build(profile);
    QCOMPARE(result.arguments, QStringList{QStringLiteral("main.py")});
    QVERIFY(!result.preview.contains(QStringLiteral("--port")));
    QVERIFY(!result.preview.contains(QStringLiteral("--listen")));
}

void LaunchCommandBuilderTest::explicitModesAndCustomArguments()
{
    const QVariantMap parameters {
        {QStringLiteral("browser"), QStringLiteral("disable")},
        {QStringLiteral("port"), 9000},
        {QStringLiteral("vramMode"), QStringLiteral("low")},
        {QStringLiteral("asyncOffload"), QStringLiteral("enable")},
        {QStringLiteral("asyncOffloadStreams"), QStringLiteral("4")}
    };
    const QVariantMap profile {
        {QStringLiteral("pythonPath"), QStringLiteral("C:/Python/python.exe")},
        {QStringLiteral("comfyRoot"), QStringLiteral("C:/ComfyUI")},
        {QStringLiteral("customArguments"), QStringLiteral("--extra-test \"two words\"")},
        {QStringLiteral("parameters"), parameters},
        {QStringLiteral("environment"), QVariantList{}}
    };

    const auto result = LaunchCommandBuilder::build(profile);
    QVERIFY(result.arguments.contains(QStringLiteral("--disable-auto-launch")));
    QVERIFY(result.arguments.contains(QStringLiteral("--lowvram")));
    QVERIFY(result.arguments.contains(QStringLiteral("--async-offload")));
    QCOMPARE(result.arguments.at(result.arguments.indexOf(QStringLiteral("--port")) + 1), QStringLiteral("9000"));
    QCOMPARE(result.arguments.constLast(), QStringLiteral("two words"));
}

void LaunchCommandBuilderTest::environmentIsAppliedAndSecretsAreMasked()
{
    const QVariantList environment {
        QVariantMap{{QStringLiteral("name"), QStringLiteral("PYTHONUTF8")},
                    {QStringLiteral("value"), QStringLiteral("1")},
                    {QStringLiteral("enabled"), true}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("SERVICE_API_KEY")},
                    {QStringLiteral("value"), QStringLiteral("top-secret")},
                    {QStringLiteral("enabled"), true}}
    };
    const QVariantMap profile {
        {QStringLiteral("pythonPath"), QStringLiteral("C:/Python/python.exe")},
        {QStringLiteral("comfyRoot"), QStringLiteral("C:/ComfyUI")},
        {QStringLiteral("parameters"), QVariantMap{}},
        {QStringLiteral("environment"), environment}
    };

    const auto result = LaunchCommandBuilder::build(profile);
    QCOMPARE(result.environment.value(QStringLiteral("PYTHONUTF8")), QStringLiteral("1"));
    QVERIFY(result.preview.contains(QStringLiteral("SERVICE_API_KEY=••••••••")));
    QVERIFY(!result.preview.contains(QStringLiteral("top-secret")));
}

void LaunchCommandBuilderTest::proxySettingsAreAppliedToChildEnvironment()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ApplicationSettings settings(temporaryDirectory.filePath(QStringLiteral("settings.json")));
    QCOMPARE(settings.accentMode(), QStringLiteral("system"));
    QCOMPARE(QGuiApplication::palette().color(QPalette::Accent),
             QColor(settings.effectiveAccentColor()));

    settings.setProxyMode(QStringLiteral("manual"));
    settings.setProxyHost(QStringLiteral("127.0.0.1"));
    settings.setProxyPort(7891);

    QProcessEnvironment environment;
    environment.insert(QStringLiteral("HTTP_PROXY"), QStringLiteral("http://old.invalid:1"));
    environment.insert(QStringLiteral("HTTPS_PROXY"), QStringLiteral("http://old.invalid:1"));
    settings.applyToProcessEnvironment(environment);
    QCOMPARE(environment.value(QStringLiteral("HTTP_PROXY")), QStringLiteral("http://127.0.0.1:7891"));
    QCOMPARE(environment.value(QStringLiteral("HTTPS_PROXY")), QStringLiteral("http://127.0.0.1:7891"));
    QCOMPARE(environment.value(QStringLiteral("ALL_PROXY")), QStringLiteral("http://127.0.0.1:7891"));

    settings.setProxyMode(QStringLiteral("none"));
    settings.applyToProcessEnvironment(environment);
    QVERIFY(!environment.contains(QStringLiteral("HTTP_PROXY")));
    QVERIFY(!environment.contains(QStringLiteral("HTTPS_PROXY")));
    QVERIFY(!environment.contains(QStringLiteral("ALL_PROXY")));

    settings.setProxyMode(QStringLiteral("system"));
    environment.insert(QStringLiteral("HTTP_PROXY"), QStringLiteral("http://system.invalid:2"));
    settings.applyToProcessEnvironment(environment);
    QCOMPARE(environment.value(QStringLiteral("HTTP_PROXY")), QStringLiteral("http://system.invalid:2"));
}

void LaunchCommandBuilderTest::appearanceSettingsExposeEffectiveValues()
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

void LaunchCommandBuilderTest::quickControlsUseTheApplicationPaletteWhenCreated()
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

void LaunchCommandBuilderTest::directControlPaletteBindingsOverrideStyleDefaults()
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

void LaunchCommandBuilderTest::applicationSettingsPersistAcrossInstances()
{
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

void LaunchCommandBuilderTest::profilesPersistWithoutLeavingTheTestDirectory()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString storagePath = temporaryDirectory.filePath(QStringLiteral("profiles.json"));

    {
        ConfigurationManager manager(storagePath);
        QCOMPARE(manager.profileNames().size(), 1);
        manager.addProfile(QStringLiteral("GPU profile"));
        manager.setCustomArguments(QStringLiteral("--custom value"));
        manager.addEnvironmentEntry();
        manager.updateEnvironmentEntry(2, QStringLiteral("CUSTOM_ENV"), QStringLiteral("enabled"), true);
        QCOMPARE(manager.profileNames().size(), 2);
        QCOMPARE(manager.currentProfileName(), QStringLiteral("GPU profile"));
    }

    ConfigurationManager restored(storagePath);
    QCOMPARE(restored.profileNames().size(), 2);
    QCOMPARE(restored.currentProfileName(), QStringLiteral("GPU profile"));
    QCOMPARE(restored.customArguments(), QStringLiteral("--custom value"));
    QCOMPARE(restored.environmentEntries().at(2).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("CUSTOM_ENV"));
    QVERIFY(restored.removeCurrentProfile());
    QCOMPARE(restored.profileNames().size(), 1);
    QVERIFY(!restored.removeCurrentProfile());
}

void LaunchCommandBuilderTest::tqdmProgressIsSeparatedFromConsoleLog()
{
    LogModel model;
    QSignalSpy progressSpy(&model, &LogModel::progressChanged);

    model.appendStandardError(
        QByteArrayLiteral("\r  0%|          | 0/8 [00:00<?, ?it/s, Model Initializing ...]"));
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.progressActive());
    QCOMPARE(model.progressPercent(), 0);
    QCOMPARE(model.progressCurrent(), 0);
    QCOMPARE(model.progressTotal(), 8);
    QCOMPARE(model.progressLabel(), QStringLiteral("Model Initializing ..."));

    QByteArray damagedProgressFrame = QByteArrayLiteral("\r 25%|");
    damagedProgressFrame.append(char(0xff));
    damagedProgressFrame.append(
        QByteArrayLiteral("         | 2/8 [00:00<00:02, 2.42it/s]"));
    model.appendStandardError(damagedProgressFrame);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.progressPercent(), 25);
    QCOMPARE(model.progressCurrent(), 2);

    model.appendStandardError(
        QByteArrayLiteral("\r 50%|#####     | 4/8 [00:01<00:01, 2.47it/s]"));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.progressPercent(), 50);
    QCOMPARE(model.progressCurrent(), 4);
    QCOMPARE(model.progressRemaining(), QStringLiteral("00:01"));
    QCOMPARE(model.progressRate(), QStringLiteral("2.47it/s"));
    QCOMPARE(model.progressLabel(), QStringLiteral("Model Initializing ..."));

    model.appendStandardError(
        QByteArrayLiteral("\r100%|##########| 8/8 [00:03<00:00, 2.41it/s]\n"));
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.progressPercent(), 100);
    QCOMPARE(model.progressCurrent(), 8);
    QVERIFY(progressSpy.count() >= 3);
    QTRY_VERIFY_WITH_TIMEOUT(!model.progressActive(), 2000);
}

void LaunchCommandBuilderTest::carriageReturnLineEndingsRemainNormalLogLines()
{
    LogModel model;

    model.appendStandardOutput(QByteArrayLiteral("first line\r"));
    QCOMPARE(model.rowCount(), 1);
    model.appendStandardOutput(QByteArrayLiteral("\nsecond line\r\n"));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), LogModel::TextRole).toString(),
             QStringLiteral("first line"));
    QCOMPARE(model.data(model.index(1, 0), LogModel::TextRole).toString(),
             QStringLiteral("second line"));

    model.appendStandardOutput(QByteArrayLiteral("Memory 50% (1/2)\n"));
    QCOMPARE(model.rowCount(), 3);
    QVERIFY(!model.progressActive());
}

QTEST_MAIN(LaunchCommandBuilderTest)

#include "tst_LaunchCommandBuilder.moc"
