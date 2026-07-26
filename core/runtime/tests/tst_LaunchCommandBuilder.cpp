#include "LaunchCommandBuilder.h"
#include "ApplicationSettings.h"
#include "CommandPromptBuilder.h"
#include "ConfigurationManager.h"
#include "HardwareManager.h"
#include "LogModel.h"
#include "RuntimeManager.h"
#include "ZludaBootstrap.h"

#include <QFontDatabase>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPalette>
#include <QHostAddress>
#include <QRegularExpression>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QQuickWindow>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QWheelEvent>

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
    void initTestCase();
    void defaultsStayImplicit();
    void commandPromptActivatesSelectedEnvironment();
    void explicitModesAndCustomArguments();
    void environmentIsAppliedAndSecretsAreMasked();
    void proxySettingsAreAppliedToChildEnvironment();
    void appearanceSettingsExposeEffectiveValues();
    void quickControlsUseTheApplicationPaletteWhenCreated();
    void directControlPaletteBindingsOverrideStyleDefaults();
    void applicationSettingsPersistAcrossInstances();
    void environmentEntriesAreValidatedAndLegacyDefaultsMigrated();
    void bundledPythonRepairsSystemPythonProfile();
    void runtimeSurvivesFastChildFailure();
    void runtimeAcceptsBareVcsDependencies();
    void runtimeBlocksMissingDependencies();
    void zludaBackendClassificationProtectsNvidia();
    void zludaRuntimePreparationStagesAliases();
    void zludaLocalIntegrationWhenConfigured();
    void zludaRuntimeManagerIntegrationWhenConfigured();
    void hardwareManagerDetectsPortableZludaWhenConfigured();
    void runtimeShutdownReleasesChildPort();
    void profilesPersistWithoutLeavingTheTestDirectory();
    void tqdmProgressIsSeparatedFromConsoleLog();
    void carriageReturnLineEndingsRemainNormalLogLines();
    void consoleDisplayTextSupportsDocumentSelection();
    void consoleViewRefreshesWhenLogCountChanges();
    void catalogDefaultOptionsAreDescriptive();
    void englishCatalogContainsNoChineseLabels();
};

void LaunchCommandBuilderTest::initTestCase()
{
    qputenv("MINIFOX_ZLUDA_BOOTSTRAP", "off");
}

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
    QCOMPARE(result.environment.value(QStringLiteral("PYTHONUTF8")), QStringLiteral("1"));
    QCOMPARE(result.environment.value(QStringLiteral("PYTHONIOENCODING")), QStringLiteral("utf-8"));
    QCOMPARE(result.environment.value(QStringLiteral("PYTHONUNBUFFERED")), QStringLiteral("1"));
    QVERIFY(!result.preview.contains(QStringLiteral("--port")));
    QVERIFY(!result.preview.contains(QStringLiteral("--listen")));
}

void LaunchCommandBuilderTest::commandPromptActivatesSelectedEnvironment()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    QDir root(temporaryDirectory.path());
    QVERIFY(root.mkpath(QStringLiteral("environment/Scripts")));
    QVERIFY(root.mkpath(QStringLiteral("ComfyUI")));

    const QString pythonPath = root.filePath(QStringLiteral("environment/Scripts/python.exe"));
    QFile python(pythonPath);
    QVERIFY(python.open(QIODevice::WriteOnly));
    python.close();

    const QString activationPath = root.filePath(QStringLiteral("environment/Scripts/activate.bat"));
    QFile activation(activationPath);
    QVERIFY(activation.open(QIODevice::WriteOnly));
    activation.write("@set MINIFOX_TEST_ACTIVATED=1\r\n");
    activation.close();

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONHOME"), QStringLiteral("C:/WrongPython"));
    const CommandPromptBuilder::Result result = CommandPromptBuilder::build(
        pythonPath, root.filePath(QStringLiteral("ComfyUI")), environment);

    QCOMPARE(result.program, QStringLiteral("cmd.exe"));
    QVERIFY(result.nativeArguments.startsWith(QStringLiteral("/D /K ")));
    QVERIFY(result.nativeArguments.contains(QStringLiteral("call")));
    QVERIFY(result.nativeArguments.contains(QDir::toNativeSeparators(activationPath)));
    QCOMPARE(QDir::cleanPath(result.environmentRoot),
             QDir::cleanPath(root.filePath(QStringLiteral("environment"))));
    QCOMPARE(QDir::cleanPath(result.environment.value(QStringLiteral("VIRTUAL_ENV"))),
             QDir::cleanPath(root.filePath(QStringLiteral("environment"))));
    QCOMPARE(QDir::cleanPath(result.environment.value(QStringLiteral("MINIFOX_PYTHON"))),
             QDir::cleanPath(pythonPath));
    QVERIFY(!result.environment.contains(QStringLiteral("PYTHONHOME")));
    QCOMPARE(result.environment.value(QStringLiteral("PATH")).split(QDir::listSeparator()).first(),
             QDir::toNativeSeparators(QFileInfo(pythonPath).absolutePath()));

    QProcess activationProbe;
    QString probeArguments = result.nativeArguments;
    probeArguments.replace(QStringLiteral("/D /K "), QStringLiteral("/D /V:ON /C "));
    probeArguments.append(QStringLiteral(" && echo !MINIFOX_TEST_ACTIVATED!"));
    activationProbe.setWorkingDirectory(result.workingDirectory);
    activationProbe.setProcessEnvironment(result.environment);
    activationProbe.setProcessChannelMode(QProcess::MergedChannels);
    activationProbe.setProgram(result.program);
    activationProbe.setNativeArguments(probeArguments);
    activationProbe.start();
    QVERIFY(activationProbe.waitForFinished(5000));
    const QByteArray activationOutput = activationProbe.readAll();
    QCOMPARE(activationProbe.exitStatus(), QProcess::NormalExit);
    QVERIFY2(activationProbe.exitCode() == 0, activationOutput.constData());
    QVERIFY2(activationOutput.contains("1"), activationOutput.constData());

    QVERIFY(root.mkpath(QStringLiteral("portable-python")));
    const QString portablePythonPath =
        root.filePath(QStringLiteral("portable-python/python.exe"));
    QFile portablePython(portablePythonPath);
    QVERIFY(portablePython.open(QIODevice::WriteOnly));
    portablePython.close();

    const CommandPromptBuilder::Result portableResult = CommandPromptBuilder::build(
        portablePythonPath, root.filePath(QStringLiteral("ComfyUI")), environment);
    QVERIFY(portableResult.activationScript.isEmpty());
    QVERIFY(portableResult.nativeArguments.contains(QStringLiteral("prompt (ComfyUI)")));
    QCOMPARE(QDir::cleanPath(portableResult.environment.value(QStringLiteral("VIRTUAL_ENV"))),
             QDir::cleanPath(root.filePath(QStringLiteral("portable-python"))));
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
        QVariantMap{{QStringLiteral("name"), QStringLiteral("CUSTOM_ENV")},
                    {QStringLiteral("value"), QStringLiteral("value with spaces")},
                    {QStringLiteral("enabled"), true}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("SERVICE_API_KEY")},
                    {QStringLiteral("value"), QStringLiteral("top-secret")},
                    {QStringLiteral("enabled"), true}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("DISABLED_ENV")},
                    {QStringLiteral("value"), QStringLiteral("ignored")},
                    {QStringLiteral("enabled"), false}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("INVALID-NAME")},
                    {QStringLiteral("value"), QStringLiteral("ignored")},
                    {QStringLiteral("enabled"), true}}
    };
    const QVariantMap profile {
        {QStringLiteral("pythonPath"), QStringLiteral("C:/Python/python.exe")},
        {QStringLiteral("comfyRoot"), QStringLiteral("C:/ComfyUI")},
        {QStringLiteral("parameters"), QVariantMap{}},
        {QStringLiteral("environment"), environment}
    };

    const auto result = LaunchCommandBuilder::build(profile);
    QCOMPARE(result.environment.value(QStringLiteral("CUSTOM_ENV")), QStringLiteral("value with spaces"));
    QVERIFY(!result.environment.contains(QStringLiteral("DISABLED_ENV")));
    QVERIFY(!result.environment.contains(QStringLiteral("INVALID-NAME")));
    QVERIFY(result.preview.contains(QStringLiteral("set \"CUSTOM_ENV=value with spaces\"")));
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
        QVERIFY(manager.environmentEntries().isEmpty());
        manager.addProfile(QStringLiteral("GPU profile"));
        manager.setCustomArguments(QStringLiteral("--custom value"));
        const int environmentIndex = manager.addEnvironmentEntry();
        QCOMPARE(environmentIndex, 0);
        manager.updateEnvironmentEntry(environmentIndex, QStringLiteral("CUSTOM_ENV"), QStringLiteral("enabled"), true);
        QCOMPARE(manager.profileNames().size(), 2);
        QCOMPARE(manager.currentProfileName(), QStringLiteral("GPU profile"));
    }

    ConfigurationManager restored(storagePath);
    QCOMPARE(restored.profileNames().size(), 2);
    QCOMPARE(restored.currentProfileName(), QStringLiteral("GPU profile"));
    QCOMPARE(restored.customArguments(), QStringLiteral("--custom value"));
    QCOMPARE(restored.environmentEntries().at(0).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("CUSTOM_ENV"));
    QVERIFY(restored.removeCurrentProfile());
    QCOMPARE(restored.profileNames().size(), 1);
    QVERIFY(!restored.removeCurrentProfile());
}

void LaunchCommandBuilderTest::environmentEntriesAreValidatedAndLegacyDefaultsMigrated()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString storagePath = temporaryDirectory.filePath(QStringLiteral("profiles.json"));

    const QJsonArray environment {
        QJsonObject{{QStringLiteral("name"), QStringLiteral("PYTHONUTF8")},
                    {QStringLiteral("value"), QStringLiteral("1")},
                    {QStringLiteral("enabled"), true}},
        QJsonObject{{QStringLiteral("name"), QStringLiteral("PYTHONUNBUFFERED")},
                    {QStringLiteral("value"), QStringLiteral("1")},
                    {QStringLiteral("enabled"), false}},
        QJsonObject{{QStringLiteral("name"), QStringLiteral("CUSTOM_KEEP")},
                    {QStringLiteral("value"), QStringLiteral("kept")},
                    {QStringLiteral("enabled"), true}}
    };
    const QJsonObject profile {
        {QStringLiteral("id"), QStringLiteral("legacy-profile")},
        {QStringLiteral("name"), QStringLiteral("Legacy")},
        {QStringLiteral("parameters"), QJsonObject{}},
        {QStringLiteral("environment"), environment}
    };
    const QJsonObject root {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("currentProfileId"), QStringLiteral("legacy-profile")},
        {QStringLiteral("profiles"), QJsonArray{profile}}
    };

    QFile sourceFile(storagePath);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    const QByteArray sourceData = QJsonDocument(root).toJson();
    QCOMPARE(sourceFile.write(sourceData), static_cast<qint64>(sourceData.size()));
    sourceFile.close();

    ConfigurationManager manager(storagePath);
    QCOMPARE(manager.environmentEntries().size(), 1);
    QCOMPARE(manager.environmentEntries().constFirst().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("CUSTOM_KEEP"));

    {
        QFile migratedFile(storagePath);
        QVERIFY(migratedFile.open(QIODevice::ReadOnly));
        const QJsonObject migratedRoot = QJsonDocument::fromJson(migratedFile.readAll()).object();
        QCOMPARE(migratedRoot.value(QStringLiteral("schemaVersion")).toInt(), 2);
        QCOMPARE(migratedRoot.value(QStringLiteral("profiles")).toArray().at(0).toObject()
                     .value(QStringLiteral("environment")).toArray().size(),
                 1);
    }

    const int invalidIndex = manager.addEnvironmentEntry();
    QVERIFY(!manager.environmentEntries().at(invalidIndex).toMap().value(QStringLiteral("error")).toString().isEmpty());
    manager.updateEnvironmentEntry(invalidIndex, QStringLiteral("1INVALID"), QStringLiteral("value"), true);
    QVERIFY(!manager.environmentEntries().at(invalidIndex).toMap().value(QStringLiteral("error")).toString().isEmpty());
    manager.updateEnvironmentEntry(invalidIndex, QStringLiteral("CUSTOM_ENV"), QStringLiteral("value"), true);
    QVERIFY(manager.environmentEntries().at(invalidIndex).toMap().value(QStringLiteral("error")).toString().isEmpty());

    const int duplicateIndex = manager.addEnvironmentEntry();
    manager.updateEnvironmentEntry(duplicateIndex, QStringLiteral("custom_env"), QStringLiteral("other"), true);
    QVERIFY(!manager.environmentEntries().at(invalidIndex).toMap().value(QStringLiteral("error")).toString().isEmpty());
    QVERIFY(!manager.environmentEntries().at(duplicateIndex).toMap().value(QStringLiteral("error")).toString().isEmpty());
    manager.updateEnvironmentEntry(duplicateIndex, QStringLiteral("custom_env"), QStringLiteral("other"), false);
    QVERIFY(manager.environmentEntries().at(invalidIndex).toMap().value(QStringLiteral("error")).toString().isEmpty());
}

void LaunchCommandBuilderTest::bundledPythonRepairsSystemPythonProfile()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    QDir packageDirectory(temporaryDirectory.path());
    QVERIFY(packageDirectory.mkpath(QStringLiteral("ComfyUI")));
    QVERIFY(packageDirectory.mkpath(QStringLiteral("python")));
    QFile mainFile(packageDirectory.filePath(QStringLiteral("ComfyUI/main.py")));
    QVERIFY(mainFile.open(QIODevice::WriteOnly));
    mainFile.close();
    QFile bundledPython(packageDirectory.filePath(QStringLiteral("python/python.exe")));
    QVERIFY(bundledPython.open(QIODevice::WriteOnly));
    bundledPython.close();

    const QString systemPython = QStandardPaths::findExecutable(QStringLiteral("python.exe"));
    if (systemPython.isEmpty()) {
        QSKIP("python.exe is not available on PATH");
    }

    const QString storagePath = temporaryDirectory.filePath(QStringLiteral("profiles.json"));
    const QJsonObject profile {
        {QStringLiteral("id"), QStringLiteral("bundled-profile")},
        {QStringLiteral("name"), QStringLiteral("Bundled")},
        {QStringLiteral("pythonPath"), systemPython},
        {QStringLiteral("comfyRoot"), packageDirectory.filePath(QStringLiteral("ComfyUI"))},
        {QStringLiteral("parameters"), QJsonObject{}},
        {QStringLiteral("environment"), QJsonArray{}}
    };
    const QJsonObject root {
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("currentProfileId"), QStringLiteral("bundled-profile")},
        {QStringLiteral("profiles"), QJsonArray{profile}}
    };
    QFile sourceFile(storagePath);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write(QJsonDocument(root).toJson());
    sourceFile.close();

    ConfigurationManager manager(storagePath);
    QCOMPARE(QDir::cleanPath(manager.pythonPath()),
             QDir::cleanPath(bundledPython.fileName()));
}

void LaunchCommandBuilderTest::runtimeSurvivesFastChildFailure()
{
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python.exe"));
    if (python.isEmpty()) {
        QSKIP("python.exe is not available on PATH");
    }

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QFile mainFile(temporaryDirectory.filePath(QStringLiteral("main.py")));
    QVERIFY(mainFile.open(QIODevice::WriteOnly));
    mainFile.write("import time\ntime.sleep(0.1)\nraise SystemExit(7)\n");
    mainFile.close();
    QFile requirementsFile(temporaryDirectory.filePath(QStringLiteral("requirements.txt")));
    QVERIFY(requirementsFile.open(QIODevice::WriteOnly));
    requirementsFile.close();

    ConfigurationManager configuration(
        temporaryDirectory.filePath(QStringLiteral("profiles.json")));
    configuration.setComfyRoot(temporaryDirectory.path());
    configuration.setPythonPath(python);
    configuration.setParameterValue(QStringLiteral("listen"), QStringLiteral("10.255.255.1"));
    ApplicationSettings settings(
        temporaryDirectory.filePath(QStringLiteral("settings.json")));
    RuntimeManager runtime(&configuration, &settings);

    runtime.start();
    QTRY_COMPARE_WITH_TIMEOUT(runtime.status(), RuntimeManager::Failed, 3000);
    QCOMPARE(runtime.lastExitCode(), 7);
}

void LaunchCommandBuilderTest::runtimeAcceptsBareVcsDependencies()
{
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python.exe"));
    if (python.isEmpty()) {
        QSKIP("python.exe is not available on PATH");
    }

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString launchMarker = temporaryDirectory.filePath(QStringLiteral("main-started.txt"));

    QFile mainFile(temporaryDirectory.filePath(QStringLiteral("main.py")));
    QVERIFY(mainFile.open(QIODevice::WriteOnly));
    mainFile.write(QStringLiteral("from pathlib import Path\nPath(r'%1').write_text('started')\n")
                       .arg(QDir::toNativeSeparators(launchMarker)).toUtf8());
    mainFile.close();

    QFile requirementsFile(temporaryDirectory.filePath(QStringLiteral("requirements.txt")));
    QVERIFY(requirementsFile.open(QIODevice::WriteOnly));
    requirementsFile.write("git+https://github.com/facebookresearch/sam2\n");
    requirementsFile.close();

    QDir root(temporaryDirectory.path());
    QVERIFY(root.mkdir(QStringLiteral("SAM_2-1.0.dist-info")));
    QFile metadataFile(root.filePath(QStringLiteral("SAM_2-1.0.dist-info/METADATA")));
    QVERIFY(metadataFile.open(QIODevice::WriteOnly));
    metadataFile.write("Metadata-Version: 2.1\nName: SAM-2\nVersion: 1.0\n");
    metadataFile.close();
    QFile directUrlFile(root.filePath(QStringLiteral("SAM_2-1.0.dist-info/direct_url.json")));
    QVERIFY(directUrlFile.open(QIODevice::WriteOnly));
    directUrlFile.write(
        R"({"url":"https://github.com/facebookresearch/sam2","vcs_info":{"vcs":"git"}})");
    directUrlFile.close();

    ConfigurationManager configuration(
        temporaryDirectory.filePath(QStringLiteral("profiles.json")));
    configuration.setComfyRoot(temporaryDirectory.path());
    configuration.setPythonPath(python);
    ApplicationSettings settings(
        temporaryDirectory.filePath(QStringLiteral("settings.json")));
    RuntimeManager runtime(&configuration, &settings);

    runtime.start();
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(launchMarker), 5000);
    runtime.shutdown();
}

void LaunchCommandBuilderTest::zludaBackendClassificationProtectsNvidia()
{
    using Backend = ZludaBootstrap::BackendKind;
    using Adapter = ZludaBootstrap::SystemAdapterKind;

    QCOMPARE(ZludaBootstrap::classifyBackend(
                 QStringLiteral("11.8"), {}, {QStringLiteral("NVIDIA GeForce RTX 4090")}),
             Backend::Nvidia);
    QCOMPARE(ZludaBootstrap::classifyBackend(
                 QStringLiteral("11.8"), {}, {QStringLiteral("AMD Radeon 780M Graphics [ZLUDA]")}),
             Backend::Zluda);
    QCOMPARE(ZludaBootstrap::classifyBackend(
                 {}, QStringLiteral("6.2"), {QStringLiteral("AMD Radeon RX 7900 XTX")}),
             Backend::Rocm);
    QCOMPARE(ZludaBootstrap::classifyBackend(
                 QStringLiteral("11.8"), {},
                 {QStringLiteral("AMD Radeon 780M Graphics [ZLUDA]"),
                  QStringLiteral("NVIDIA GeForce RTX 4060")}),
             Backend::Nvidia);

    QVERIFY(ZludaBootstrap::shouldProbeAdapters(
        {QStringLiteral("AMD Radeon 780M Graphics VEN_1002")}));
    QVERIFY(!ZludaBootstrap::shouldProbeAdapters(
        {QStringLiteral("NVIDIA GeForce RTX 4090 VEN_10DE")}));
    QVERIFY(!ZludaBootstrap::shouldProbeAdapters(
        {QStringLiteral("AMD Radeon 780M Graphics VEN_1002"),
         QStringLiteral("NVIDIA GeForce RTX 4060 VEN_10DE")}));
    QVERIFY(!ZludaBootstrap::shouldProbeAdapters(
        {QStringLiteral("AMD Radeon 780M Graphics")}, QStringLiteral("off")));
    QVERIFY(!ZludaBootstrap::shouldProbeAdapters(
        {QStringLiteral("NVIDIA GeForce RTX 4090")}, QStringLiteral("force")));
    QVERIFY(!ZludaBootstrap::shouldProbeAdapters(
        {QStringLiteral("AMD Radeon 780M Graphics"),
         QStringLiteral("NVIDIA GeForce RTX 4090")}, QStringLiteral("force")));
    QVERIFY(ZludaBootstrap::shouldProbeAdapters(
        {QStringLiteral("AMD Radeon 780M Graphics")}, QStringLiteral("force")));

    QCOMPARE(ZludaBootstrap::classifySystemAdapters(
                 {QStringLiteral("AMD Radeon 780M Graphics VEN_1002")}),
             Adapter::AmdOnly);
    QCOMPARE(ZludaBootstrap::classifySystemAdapters(
                 {QStringLiteral("NVIDIA GeForce RTX 4090 VEN_10DE")}),
             Adapter::NvidiaOnly);
    QCOMPARE(ZludaBootstrap::classifySystemAdapters(
                 {QStringLiteral("AMD Radeon 780M Graphics VEN_1002"),
                  QStringLiteral("NVIDIA GeForce RTX 4060 VEN_10DE")}),
             Adapter::Mixed);
    QCOMPARE(ZludaBootstrap::classifySystemAdapters({}), Adapter::Unknown);
}

void LaunchCommandBuilderTest::zludaRuntimePreparationStagesAliases()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QDir root(temporaryDirectory.path());
    QVERIFY(root.mkpath(QStringLiteral("ComfyUI")));
    QVERIFY(root.mkpath(QStringLiteral("python/Lib/site-packages/torch/lib")));
    QVERIFY(root.mkpath(QStringLiteral("rocm/bin/rocblas/library")));

    const auto writeFile = [](const QString &path, const QByteArray &contents) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        return file.write(contents) == contents.size();
    };

    const QString pythonPath = root.filePath(QStringLiteral("python/python.exe"));
    QVERIFY(writeFile(pythonPath, QByteArrayLiteral("python")));
    const QString torchLib = root.filePath(QStringLiteral("python/Lib/site-packages/torch/lib"));
    QVERIFY(writeFile(QDir(torchLib).filePath(QStringLiteral("cublas64_11.dll")), QByteArrayLiteral("native")));
    QVERIFY(writeFile(QDir(torchLib).filePath(QStringLiteral("cusparse64_11.dll")), QByteArrayLiteral("native")));
    QVERIFY(writeFile(QDir(torchLib).filePath(QStringLiteral("nvrtc64_112_0.dll")), QByteArrayLiteral("native")));

    const QString rocmBin = root.filePath(QStringLiteral("rocm/bin"));
    QVERIFY(writeFile(QDir(rocmBin).filePath(QStringLiteral("amdhip64.dll")), QByteArrayLiteral("hip")));
    const QString tensileLibrary =
        QDir(rocmBin).filePath(QStringLiteral("rocblas/library"));
    QVERIFY(writeFile(QDir(tensileLibrary).filePath(
                          QStringLiteral("TensileLibrary_lazy_gfx903.dat")),
                      QByteArrayLiteral("gfx903")));

    QProcessEnvironment environment;
    environment.insert(QStringLiteral("PYTHONPATH"), QStringLiteral("C:/existing"));
    environment.insert(QStringLiteral("HIP_PATH"), root.filePath(QStringLiteral("rocm")));
    environment.insert(QStringLiteral("MINIFOX_ZLUDA_GFX_ARCH"), QStringLiteral("gfx903"));

    const ZludaBootstrap::Preparation preparation = ZludaBootstrap::prepare(
        pythonPath, root.filePath(QStringLiteral("ComfyUI")), environment);
    QVERIFY2(preparation.valid, qPrintable(preparation.error));
    QCOMPARE(QDir::cleanPath(preparation.sourceDirectory),
             QDir::cleanPath(root.filePath(QStringLiteral(".minifox/packages/zluda"))));
    QCOMPARE(preparation.gfxArchitecture, QStringLiteral("gfx903"));
    QCOMPARE(QDir::cleanPath(preparation.tensileLibraryDirectory),
             QDir::cleanPath(tensileLibrary));
    QCOMPARE(preparation.preloadNames,
             QStringList({QStringLiteral("nvcuda.dll"),
                          QStringLiteral("nvrtc64_112_0.dll"),
                          QStringLiteral("cublas64_11.dll"),
                          QStringLiteral("cusparse64_11.dll"),
                          QStringLiteral("nvml.dll")}));
    QVERIFY(QFileInfo::exists(
        QDir(preparation.runtimeDirectory).filePath(QStringLiteral("cublas64_11.dll"))));
    QVERIFY(QFileInfo::exists(
        QDir(preparation.bootstrapDirectory).filePath(QStringLiteral("sitecustomize.py"))));
    QFile bootstrapFile(
        QDir(preparation.bootstrapDirectory).filePath(QStringLiteral("sitecustomize.py")));
    QVERIFY(bootstrapFile.open(QIODevice::ReadOnly));
    const QByteArray bootstrapScript = bootstrapFile.readAll();
    QVERIFY(bootstrapScript.contains(
        "HIP_HOME = _join_rocm_home('hip') if ROCM_HOME else None"));
    QVERIFY(bootstrapScript.contains("HIP_HOME = ROCM_HOME"));
    QVERIFY(bootstrapScript.contains("module.backends.cudnn.enabled = False"));
    QVERIFY(bootstrapScript.contains("_force_flash_sdp_off"));
    QVERIFY(bootstrapScript.contains("_force_mem_efficient_sdp_off"));
    QVERIFY(bootstrapScript.contains("class _MinifoxTorchLoader"));
    QVERIFY(!bootstrapScript.contains("import torch as"));
    QVERIFY(QFileInfo::exists(
        root.filePath(QStringLiteral(".minifox/packages/zluda.extpack"))));
    QVERIFY(!QFileInfo::exists(
        root.filePath(QStringLiteral(".minifox/packages/tensile-gfx903.extpack"))));
    QVERIFY(QFileInfo::exists(QDir(preparation.tensileLibraryDirectory).filePath(
        QStringLiteral("TensileLibrary_lazy_gfx903.dat"))));
    QVERIFY(QFileInfo(root.filePath(QStringLiteral(".cache"))).isDir());
    QVERIFY(QFileInfo(preparation.zludaCacheDirectory).isDir());
    QVERIFY(QFileInfo(preparation.tritonCacheDirectory).isDir());
    QVERIFY(QFileInfo(preparation.torchInductorCacheDirectory).isDir());
    QVERIFY(preparation.rocmBinCandidates.contains(QDir::cleanPath(rocmBin)));

    ZludaBootstrap::apply(preparation, rocmBin, environment);
    QCOMPARE(environment.value(QStringLiteral("MINIFOX_ZLUDA_BOOTSTRAP")),
             QStringLiteral("1"));
    QCOMPARE(environment.value(QStringLiteral("MINIFOX_ROCM_BIN")), rocmBin);
    QCOMPARE(environment.value(QStringLiteral("ZLUDA_NVRTC_LIB")),
             QDir(preparation.runtimeDirectory).filePath(QStringLiteral("nvrtc64_112_0.dll")));
    QCOMPARE(environment.value(QStringLiteral("ZLUDA_CACHE_DIR")),
             preparation.zludaCacheDirectory);
    QCOMPARE(environment.value(QStringLiteral("TRITON_CACHE_DIR")),
             preparation.tritonCacheDirectory);
    QCOMPARE(environment.value(QStringLiteral("TORCHINDUCTOR_CACHE_DIR")),
             preparation.torchInductorCacheDirectory);
    QCOMPARE(environment.value(QStringLiteral("ROCBLAS_TENSILE_LIBPATH")),
             preparation.tensileLibraryDirectory);
    QCOMPARE(environment.value(QStringLiteral("DISABLE_ADDMM_CUDA_LT")),
             QStringLiteral("1"));
    QCOMPARE(environment.value(QStringLiteral("TORCH_BLAS_PREFER_HIPBLASLT")),
             QStringLiteral("0"));
    QCOMPARE(environment.value(QStringLiteral("HIP_PATH")),
             QDir::cleanPath(QDir(rocmBin).absoluteFilePath(QStringLiteral(".."))));
    QCOMPARE(environment.value(QStringLiteral("PATH")).split(QDir::listSeparator()).at(0),
             rocmBin);
    QCOMPARE(environment.value(QStringLiteral("PATH")).split(QDir::listSeparator()).at(1),
             preparation.runtimeDirectory);
    QCOMPARE(environment.value(QStringLiteral("PYTHONPATH")).split(QDir::listSeparator()).constFirst(),
             preparation.bootstrapDirectory);

}

void LaunchCommandBuilderTest::zludaLocalIntegrationWhenConfigured()
{
    const QString pythonPath = qEnvironmentVariable("MINIFOX_TEST_ZLUDA_PYTHON");
    const QString comfyRoot = qEnvironmentVariable("MINIFOX_TEST_ZLUDA_COMFY_ROOT");
    if (pythonPath.isEmpty() || comfyRoot.isEmpty()) {
        QSKIP("Local ZLUDA integration paths were not provided");
    }
    QVERIFY2(ZludaBootstrap::shouldProbeSystem(),
             qPrintable(ZludaBootstrap::systemAdapterNames().join(QStringLiteral(" | "))));

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString rocmPath = qEnvironmentVariable("MINIFOX_TEST_ROCM_PATH");
    if (!rocmPath.isEmpty()) {
        environment.insert(QStringLiteral("HIP_PATH"), rocmPath);
    }

    const ZludaBootstrap::Preparation preparation =
        ZludaBootstrap::prepare(pythonPath, comfyRoot, environment);
    QVERIFY2(preparation.valid, qPrintable(preparation.error));
    QVERIFY(!preparation.rocmBinCandidates.isEmpty());
    ZludaBootstrap::apply(preparation, preparation.rocmBinCandidates.constFirst(), environment);

    QProcess probe;
    probe.setWorkingDirectory(comfyRoot);
    probe.setProcessEnvironment(environment);
    probe.setProgram(pythonPath);
    probe.setArguments({QStringLiteral("-c"), ZludaBootstrap::preflightScript()});
    probe.start();
    QVERIFY2(probe.waitForFinished(45000), qPrintable(probe.errorString()));
    const QByteArray output = probe.readAllStandardOutput();
    const QByteArray error = probe.readAllStandardError();
    QVERIFY2(probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0,
             qPrintable(QString::fromUtf8(error)));
    QVERIFY(output.contains("\"ok\": true"));
    QVERIFY(output.contains("\"active\": true"));
    QVERIFY(output.contains("\"cudnn_enabled\": false"));
    QVERIFY(output.contains("\"flash_sdp_enabled\": false"));
    QVERIFY(output.contains("\"mem_efficient_sdp_enabled\": false"));
    QVERIFY(output.contains("\"hip_include\":"));
}

void LaunchCommandBuilderTest::zludaRuntimeManagerIntegrationWhenConfigured()
{
    const QString pythonPath = qEnvironmentVariable("MINIFOX_TEST_ZLUDA_PYTHON");
    const QString rocmPath = qEnvironmentVariable("MINIFOX_TEST_ROCM_PATH");
    if (pythonPath.isEmpty() || rocmPath.isEmpty()) {
        QSKIP("Local ZLUDA integration paths were not provided");
    }

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QDir root(temporaryDirectory.path());
    QVERIFY(root.mkdir(QStringLiteral("ComfyUI")));
    const QString comfyRoot = root.filePath(QStringLiteral("ComfyUI"));
    const QString marker = root.filePath(QStringLiteral("runtime-started.txt"));

    QFile mainFile(QDir(comfyRoot).filePath(QStringLiteral("main.py")));
    QVERIFY(mainFile.open(QIODevice::WriteOnly));
    mainFile.write(QStringLiteral(
        "from pathlib import Path\n"
        "import os\n"
        "assert os.environ.get('MINIFOX_ZLUDA_ACTIVE') == '1'\n"
        "Path(r'%1').write_text('started', encoding='utf-8')\n")
                       .arg(QDir::toNativeSeparators(marker)).toUtf8());
    mainFile.close();
    QFile requirementsFile(QDir(comfyRoot).filePath(QStringLiteral("requirements.txt")));
    QVERIFY(requirementsFile.open(QIODevice::WriteOnly));
    requirementsFile.close();

    ConfigurationManager configuration(
        root.filePath(QStringLiteral("profiles.json")));
    configuration.setComfyRoot(comfyRoot);
    configuration.setPythonPath(pythonPath);
    int environmentIndex = configuration.addEnvironmentEntry();
    configuration.updateEnvironmentEntry(
        environmentIndex, QStringLiteral("MINIFOX_ZLUDA_BOOTSTRAP"),
        QStringLiteral("force"), true);
    environmentIndex = configuration.addEnvironmentEntry();
    configuration.updateEnvironmentEntry(
        environmentIndex, QStringLiteral("HIP_PATH"),
        rocmPath, true);

    ApplicationSettings settings(root.filePath(QStringLiteral("settings.json")));
    RuntimeManager runtime(&configuration, &settings);
    runtime.start();
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker), 90000);
    runtime.shutdown();
}

void LaunchCommandBuilderTest::hardwareManagerDetectsPortableZludaWhenConfigured()
{
    const QString pythonPath = qEnvironmentVariable("MINIFOX_TEST_ZLUDA_PYTHON");
    const QString rocmPath = qEnvironmentVariable("MINIFOX_TEST_ROCM_PATH");
    if (pythonPath.isEmpty() || rocmPath.isEmpty()) {
        QSKIP("Local ZLUDA integration paths were not provided");
    }

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QDir root(temporaryDirectory.path());
    QVERIFY(root.mkdir(QStringLiteral("ComfyUI")));

    ConfigurationManager configuration(
        root.filePath(QStringLiteral("profiles.json")));
    configuration.setComfyRoot(root.filePath(QStringLiteral("ComfyUI")));
    configuration.setPythonPath(pythonPath);
    int environmentIndex = configuration.addEnvironmentEntry();
    configuration.updateEnvironmentEntry(
        environmentIndex, QStringLiteral("HIP_PATH"), rocmPath, true);

    HardwareManager hardware(&configuration);
    QTRY_VERIFY_WITH_TIMEOUT(hardware.hasCuda() || !hardware.lastError().isEmpty(), 60000);
    QVERIFY2(hardware.hasCuda(), qPrintable(hardware.lastError()));
    QVERIFY2(hardware.detectionSource().contains(QStringLiteral("ZLUDA")),
             qPrintable(QStringLiteral("source=%1 torch=%2 cuda=%3")
                            .arg(hardware.detectionSource(),
                                 hardware.torchVersion(),
                                 hardware.cudaRuntimeVersion())));
    QVERIFY(!hardware.cudaDevices().isEmpty());
    QVERIFY(hardware.cudaDevices().constFirst().toMap()
                .value(QStringLiteral("name")).toString()
                .contains(QStringLiteral("AMD"), Qt::CaseInsensitive));
}

void LaunchCommandBuilderTest::runtimeBlocksMissingDependencies()
{
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python.exe"));
    if (python.isEmpty()) {
        QSKIP("python.exe is not available on PATH");
    }

    // The interactive install windows need an attending user; the test only
    // verifies that unsatisfiable dependencies block the launch.
    qputenv("MINIFOX_SKIP_DEPENDENCY_INSTALL", "1");

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString launchMarker = temporaryDirectory.filePath(QStringLiteral("main-started.txt"));
    QFile mainFile(temporaryDirectory.filePath(QStringLiteral("main.py")));
    QVERIFY(mainFile.open(QIODevice::WriteOnly));
    mainFile.write(QStringLiteral("from pathlib import Path\nPath(r'%1').write_text('started')\n")
                       .arg(QDir::toNativeSeparators(launchMarker)).toUtf8());
    mainFile.close();
    QFile requirementsFile(temporaryDirectory.filePath(QStringLiteral("requirements.txt")));
    QVERIFY(requirementsFile.open(QIODevice::WriteOnly));
    requirementsFile.write("minifox-definitely-missing-dependency-xyz==1.0\n");
    requirementsFile.close();

    ConfigurationManager configuration(
        temporaryDirectory.filePath(QStringLiteral("profiles.json")));
    configuration.setComfyRoot(temporaryDirectory.path());
    configuration.setPythonPath(python);
    ApplicationSettings settings(
        temporaryDirectory.filePath(QStringLiteral("settings.json")));
    RuntimeManager runtime(&configuration, &settings);

    runtime.start();
    QTRY_COMPARE_WITH_TIMEOUT(runtime.status(), RuntimeManager::Failed, 5000);
    QVERIFY(!QFileInfo::exists(launchMarker));
    QVERIFY(!runtime.lastError().isEmpty());
}

void LaunchCommandBuilderTest::runtimeShutdownReleasesChildPort()
{
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python.exe"));
    if (python.isEmpty()) {
        QSKIP("python.exe is not available on PATH");
    }

    QTcpServer portProbe;
    QVERIFY(portProbe.listen(QHostAddress::LocalHost, 0));
    const quint16 port = portProbe.serverPort();
    portProbe.close();

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString readyMarker = QDir::fromNativeSeparators(
        temporaryDirectory.filePath(QStringLiteral("child-ready.txt")));
    const QString childCode = QStringLiteral(
        "import pathlib,socket,time;"
        "s=socket.socket();"
        "s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);"
        "s.bind(('127.0.0.1',%1));"
        "s.listen();"
        "pathlib.Path(r'%2').write_text('ready');"
        "time.sleep(120)")
                                  .arg(port)
                                  .arg(readyMarker);
    const QString mainCode = QStringLiteral(
        "import subprocess,sys,time\n"
        "subprocess.Popen([sys.executable, '-c', %1])\n"
        "time.sleep(120)\n")
                                 .arg(QStringLiteral("%1").arg(childCode).replace(
                                     QLatin1Char('\\'), QStringLiteral("\\\\"))
                                          .replace(QLatin1Char('\''), QStringLiteral("\\'"))
                                          .prepend(QLatin1Char('\''))
                                          .append(QLatin1Char('\'')));

    QFile mainFile(temporaryDirectory.filePath(QStringLiteral("main.py")));
    QVERIFY(mainFile.open(QIODevice::WriteOnly));
    mainFile.write(mainCode.toUtf8());
    mainFile.close();
    QFile requirementsFile(temporaryDirectory.filePath(QStringLiteral("requirements.txt")));
    QVERIFY(requirementsFile.open(QIODevice::WriteOnly));
    requirementsFile.close();

    ConfigurationManager configuration(
        temporaryDirectory.filePath(QStringLiteral("profiles.json")));
    configuration.setComfyRoot(temporaryDirectory.path());
    configuration.setPythonPath(python);
    configuration.setParameterValue(QStringLiteral("listen"), QStringLiteral("127.0.0.1"));
    configuration.setParameterValue(QStringLiteral("port"), port);
    ApplicationSettings settings(
        temporaryDirectory.filePath(QStringLiteral("settings.json")));
    RuntimeManager runtime(&configuration, &settings);

    runtime.start();
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(readyMarker), 5000);
    runtime.shutdown();
    QCOMPARE(runtime.status(), RuntimeManager::Stopped);
    QVERIFY(!runtime.active());

    const auto portIsFree = [port] {
        QTcpServer server;
        return server.listen(QHostAddress::LocalHost, port);
    };
    QTRY_VERIFY_WITH_TIMEOUT(portIsFree(), 5000);
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

void LaunchCommandBuilderTest::consoleDisplayTextSupportsDocumentSelection()
{
    LogModel model;
    model.appendStandardOutput(QByteArrayLiteral("first line\n"));
    model.appendStandardError(QByteArrayLiteral("[INFO] second line\n"));
    model.appendStandardError(QByteArrayLiteral("[WARNING] third line\n"));
    model.appendStandardError(QByteArrayLiteral("RuntimeError: fourth line\n"));
    model.appendSystemMessage(QStringLiteral("fifth line"));

    const QString withTimestamps = model.displayText(true);
    QVERIFY(withTimestamps.contains(
        QRegularExpression(QStringLiteral(R"(^1 \d{2}:\d{2}:\d{2}\.\d{3} {5}first line$)"),
                           QRegularExpression::MultilineOption)));
    QVERIFY(withTimestamps.contains(
        QRegularExpression(QStringLiteral(R"(^2 \d{2}:\d{2}:\d{2}\.\d{3} {5}\[INFO\] second line$)"),
                           QRegularExpression::MultilineOption)));
    QVERIFY(withTimestamps.contains(
        QRegularExpression(QStringLiteral(R"(^3 \d{2}:\d{2}:\d{2}\.\d{3} {5}\[WARNING\] third line$)"),
                           QRegularExpression::MultilineOption)));
    QVERIFY(withTimestamps.contains(
        QRegularExpression(QStringLiteral(R"(^4 \d{2}:\d{2}:\d{2}\.\d{3} ERR RuntimeError: fourth line$)"),
                           QRegularExpression::MultilineOption)));
    QVERIFY(withTimestamps.contains(
        QRegularExpression(QStringLiteral(R"(^5 \d{2}:\d{2}:\d{2}\.\d{3} SYS fifth line$)"),
                           QRegularExpression::MultilineOption)));

    QCOMPARE(model.displayText(false, true),
             QStringLiteral("first line\n[INFO] second line\n[WARNING] third line\n"
                            "RuntimeError: fourth line\nfifth line"));

    const QString styled = model.displayStyledText(
        false, false, QStringLiteral("#777777"), QStringLiteral("#ffffff"),
        QStringLiteral("#ffff00"), QStringLiteral("#00aaff"),
        QStringLiteral("#00ff00"), QStringLiteral("#ff0000"));
    QVERIFY(!styled.contains(QStringLiteral(">ERR</font><font color=\"#00ff00\">[INFO]")));
    QVERIFY(styled.contains(QStringLiteral("<font color=\"#00ff00\">[INFO]</font>")));
    QVERIFY(styled.contains(QStringLiteral("<font color=\"#ffff00\">[WARNING]</font>")));
    QVERIFY(styled.contains(QStringLiteral("<font color=\"#ff0000\">ERR</font>")));
    QVERIFY(styled.contains(QStringLiteral("<font color=\"#00aaff\">SYS</font>")));
    QVERIFY(styled.contains(QStringLiteral("first line")));
    QVERIFY(styled.startsWith(QStringLiteral("<table width=\"100%\"")));
    QVERIFY(styled.contains(QStringLiteral("<td width=\"48\"")));
    QVERIFY(styled.contains(QStringLiteral("<td width=\"8\"")));
    QVERIFY(styled.contains(QStringLiteral("<td width=\"32\"")));

    const QString timestampedRange = model.displayStyledTextRange(
        1, true, false, QStringLiteral("#777777"), QStringLiteral("#ffffff"),
        QStringLiteral("#ffff00"), QStringLiteral("#00aaff"),
        QStringLiteral("#00ff00"), QStringLiteral("#ff0000"));
    QVERIFY(timestampedRange.contains(QStringLiteral("<td width=\"48\"")));
    QVERIFY(timestampedRange.contains(QStringLiteral("<td width=\"104\"")));
    QVERIFY(timestampedRange.contains(QStringLiteral("<td width=\"32\"")));
}

void LaunchCommandBuilderTest::consoleViewRefreshesWhenLogCountChanges()
{
    LogModel logModel;
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("testLogModel"), &logModel);

    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Window
        import Minifox.Runtime

        Window {
            id: root
            width: 960
            height: 640
            visible: true

            property QtObject fakeSettings: QtObject {
                property string consoleFontFamily: "Consolas"
                property real consoleFontSize: 11
                property bool consoleWordWrap: true
                property bool showTimestamps: true
                property string consoleTheme: "dark"
            }
            property QtObject fakeRuntime: QtObject {
                property var logModel: testLogModel
            }
            property QtObject fakeContext: QtObject {
                property var settings: root.fakeSettings
                property var runtime: root.fakeRuntime
            }

            ConsoleView {
                objectName: "consoleView"
                anchors.fill: parent
                appContext: root.fakeContext
            }
        }
    )", QUrl(QStringLiteral("inmemory:/ConsoleRefreshProbe.qml")));

    QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 5000);
    QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.data());
    QVERIFY(window);
    QTRY_VERIFY_WITH_TIMEOUT(window->isVisible(), 1000);

    QObject *consoleView = root->findChild<QObject *>(QStringLiteral("consoleView"));
    QObject *consoleScroll = root->findChild<QObject *>(QStringLiteral("consoleScroll"));
    QObject *consoleText = root->findChild<QObject *>(QStringLiteral("consoleText"));
    QObject *resumeTailButton =
        root->findChild<QObject *>(QStringLiteral("resumeTailButton"));
    QVERIFY(consoleView);
    QVERIFY(consoleScroll);
    QVERIFY(consoleText);
    QVERIFY(resumeTailButton);
    QCOMPARE(resumeTailButton->property("visible").toBool(), false);
    QCOMPARE(consoleText->property("persistentSelection").toBool(), false);
    QCOMPARE(consoleText->property("selectedTextColor").value<QColor>(),
             consoleText->property("color").value<QColor>());
    QCOMPARE(consoleText->property("font").value<QFont>().weight(), QFont::Normal);
    QObject *viewport = consoleScroll->property("contentItem").value<QObject *>();
    QVERIFY(viewport);
    auto *consoleScrollItem = qobject_cast<QQuickItem *>(consoleScroll);
    QVERIFY(consoleScrollItem);
    auto *quickDocument =
        consoleText->property("textDocument").value<QQuickTextDocument *>();
    QVERIFY(quickDocument);

    for (int line = 1; line <= 79; ++line) {
        logModel.appendSystemMessage(QStringLiteral("refresh-probe-line-%1").arg(line));
    }
    logModel.appendSystemMessage(QStringLiteral("[DONE] Security scan"));

    QTRY_VERIFY_WITH_TIMEOUT(
        quickDocument->textDocument()->toPlainText().contains(
            QStringLiteral("[DONE] Security scan")),
        2000);

    QTRY_VERIFY_WITH_TIMEOUT(
        viewport->property("contentHeight").toReal() > viewport->property("height").toReal(),
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        qAbs(viewport->property("contentY").toReal()
             - (viewport->property("contentHeight").toReal() - viewport->property("height").toReal()))
            < 2.0,
        2000);
    const qreal initialTailPosition = viewport->property("contentY").toReal();

    // Exercise the actual QQuickWindow event route. Calling handleWheelInput()
    // directly is insufficient because ScrollView/TextArea may consume a real
    // wheel event before a sibling handler sees it.
    const QPointF wheelPosition =
        consoleScrollItem->mapToScene(consoleScrollItem->boundingRect().center());
    QWheelEvent wheelUp(
        wheelPosition,
        window->mapToGlobal(wheelPosition.toPoint()),
        QPoint(),
        QPoint(0, 120),
        Qt::NoButton,
        Qt::NoModifier,
        Qt::NoScrollPhase,
        false);
    QCoreApplication::sendEvent(window, &wheelUp);
    QTRY_VERIFY_WITH_TIMEOUT(consoleView->property("manualNavigation").toBool(), 1000);
    QCOMPARE(consoleView->property("followTail").toBool(), false);
    QTRY_VERIFY_WITH_TIMEOUT(
        !viewport->property("moving").toBool() && !viewport->property("flicking").toBool(),
        1000);

    const qreal wheelInspectionPosition = viewport->property("contentY").toReal();
    QVERIFY2(wheelInspectionPosition < initialTailPosition,
             "The real upward wheel event did not move the ScrollView away from the tail.");
    logModel.appendSystemMessage(QStringLiteral("wheel-routing-probe"));
    QTRY_VERIFY_WITH_TIMEOUT(
        quickDocument->textDocument()->toPlainText().contains(
            QStringLiteral("wheel-routing-probe")),
        2000);
    QCOMPARE(consoleView->property("followTail").toBool(), false);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), true);
    QCOMPARE(resumeTailButton->property("visible").toBool(), true);
    QVERIFY2(qAbs(viewport->property("contentY").toReal() - wheelInspectionPosition) < 2.0,
             "A real wheel event did not stop automatic tail following.");

    // Reaching the live edge, including with another real wheel event, must not
    // restore following. Recovery is an explicit action on the floating button.
    const qreal postWheelMaximum = std::max(
        0.0,
        viewport->property("contentHeight").toReal() - viewport->property("height").toReal());
    QVERIFY(viewport->setProperty("contentY", postWheelMaximum));
    QCoreApplication::processEvents();
    QCOMPARE(consoleView->property("followTail").toBool(), false);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), true);
    QCOMPARE(resumeTailButton->property("visible").toBool(), true);

    QWheelEvent wheelDown(
        wheelPosition,
        window->mapToGlobal(wheelPosition.toPoint()),
        QPoint(),
        QPoint(0, -120),
        Qt::NoButton,
        Qt::NoModifier,
        Qt::NoScrollPhase,
        false);
    QCoreApplication::sendEvent(window, &wheelDown);
    QTest::qWait(100);
    QCOMPARE(consoleView->property("followTail").toBool(), false);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), true);
    QCOMPARE(resumeTailButton->property("visible").toBool(), true);

    auto *resumeTailButtonItem = qobject_cast<QQuickItem *>(resumeTailButton);
    QVERIFY(resumeTailButtonItem);
    const QPointF resumeButtonPosition =
        resumeTailButtonItem->mapToScene(resumeTailButtonItem->boundingRect().center());
    QTest::mouseClick(
        window,
        Qt::LeftButton,
        Qt::NoModifier,
        resumeButtonPosition.toPoint());
    QTRY_VERIFY_WITH_TIMEOUT(consoleView->property("followTail").toBool(), 1000);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), false);
    QCOMPARE(resumeTailButton->property("visible").toBool(), false);
    QTRY_VERIFY_WITH_TIMEOUT(
        qAbs(viewport->property("contentY").toReal()
             - (viewport->property("contentHeight").toReal()
                - viewport->property("height").toReal()))
            < 2.0,
        1000);

    // Pointer movement can be reported without any wheel delta. It must not
    // disable automatic following.
    QVERIFY(QMetaObject::invokeMethod(
        consoleView,
        "handleWheelInput",
        Q_ARG(QVariant, QVariant(0)),
        Q_ARG(QVariant, QVariant(0))));
    QCOMPARE(consoleView->property("followTail").toBool(), true);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), false);

    // Background logs must use their own QTextCursor. Reusing TextArea's active
    // cursor changes the live selection and can leak its character format into
    // preceding rows.
    const int requestedSelectionStart =
        quickDocument->textDocument()->toPlainText().indexOf(QStringLiteral("[DONE]"));
    QVERIFY(requestedSelectionStart >= 0);
    const int requestedSelectionEnd =
        requestedSelectionStart + QStringLiteral("[DONE]").size();
    QVERIFY(QMetaObject::invokeMethod(
        consoleText,
        "select",
        Q_ARG(int, requestedSelectionStart),
        Q_ARG(int, requestedSelectionEnd)));
    QTRY_VERIFY_WITH_TIMEOUT(consoleView->property("selectionActive").toBool(), 1000);
    QCOMPARE(consoleView->property("followTail").toBool(), true);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), false);
    const int selectionStart = consoleText->property("selectionStart").toInt();
    const int selectionEnd = consoleText->property("selectionEnd").toInt();
    const int cursorPosition = consoleText->property("cursorPosition").toInt();
    const QString selectedText = consoleText->property("selectedText").toString();
    QCOMPARE(selectionStart, requestedSelectionStart);
    QCOMPARE(selectionEnd, requestedSelectionEnd);

    for (int line = 81; line <= 90; ++line) {
        logModel.appendSystemMessage(QStringLiteral("refresh-probe-line-%1").arg(line));
    }
    QTRY_VERIFY_WITH_TIMEOUT(
        quickDocument->textDocument()->toPlainText().contains(
            QStringLiteral("refresh-probe-line-90")),
        2000);
    QCOMPARE(consoleView->property("renderedLogCount").toInt(), 91);
    QCOMPARE(consoleText->property("selectionStart").toInt(), selectionStart);
    QCOMPARE(consoleText->property("selectionEnd").toInt(), selectionEnd);
    QCOMPARE(consoleText->property("cursorPosition").toInt(), cursorPosition);
    QCOMPARE(consoleText->property("selectedText").toString(), selectedText);
    QVERIFY(consoleView->property("selectionActive").toBool());

    for (QTextBlock block = quickDocument->textDocument()->begin();
         block.isValid();
         block = block.next()) {
        for (QTextBlock::Iterator fragmentIterator = block.begin();
             !fragmentIterator.atEnd();
             ++fragmentIterator) {
            const QTextFragment fragment = fragmentIterator.fragment();
            if (fragment.isValid()) {
                QVERIFY2(fragment.charFormat().fontWeight() <= QFont::Normal,
                         qPrintable(fragment.text()));
            }
        }
    }

    QVERIFY(QMetaObject::invokeMethod(consoleText, "deselect"));
    QTRY_VERIFY_WITH_TIMEOUT(!consoleView->property("selectionActive").toBool(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(
        qAbs(viewport->property("contentY").toReal()
             - (viewport->property("contentHeight").toReal() - viewport->property("height").toReal()))
            < 2.0,
        2000);

    // Real startup output arrives in multiple batches. Growing the document moves
    // the scrollbar away from its former end before the queued follow-tail update;
    // that programmatic movement must not be mistaken for a user scroll.
    for (int line = 91; line <= 170; ++line) {
        logModel.appendSystemMessage(QStringLiteral("refresh-probe-line-%1").arg(line));
    }

    QTRY_VERIFY_WITH_TIMEOUT(
        quickDocument->textDocument()->toPlainText().contains(
            QStringLiteral("refresh-probe-line-170")),
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        qAbs(viewport->property("contentY").toReal()
             - (viewport->property("contentHeight").toReal() - viewport->property("height").toReal()))
            < 2.0,
        2000);

    QVERIFY(QMetaObject::invokeMethod(consoleView, "beginUserNavigation"));
    const qreal inspectionPosition = viewport->property("contentY").toReal() / 2.0;
    QVERIFY(viewport->setProperty("contentY", inspectionPosition));
    QCOMPARE(consoleView->property("followTail").toBool(), false);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), true);
    QCOMPARE(resumeTailButton->property("visible").toBool(), true);
    QSignalSpy contentYChanges(viewport, SIGNAL(contentYChanged()));
    QVERIFY(contentYChanges.isValid());

    // While the user moves toward the latest row, background output must never
    // restore an earlier anchor or fight over contentY.
    for (int line = 171; line <= 180; ++line) {
        const qreal maximumY = std::max(
            0.0,
            viewport->property("contentHeight").toReal()
                - viewport->property("height").toReal());
        const qreal currentY = viewport->property("contentY").toReal();
        const qreal requestedY = std::min(maximumY - 4.0, currentY + 12.0);
        QVERIFY(viewport->setProperty("contentY", requestedY));
        QCoreApplication::processEvents();
        contentYChanges.clear();
        logModel.appendSystemMessage(QStringLiteral("refresh-probe-line-%1").arg(line));
        QTRY_VERIFY_WITH_TIMEOUT(
            quickDocument->textDocument()->toPlainText().contains(
                QStringLiteral("refresh-probe-line-%1").arg(line)),
            2000);
        QVERIFY2(qAbs(viewport->property("contentY").toReal() - requestedY) < 2.0,
                 qPrintable(QStringLiteral("line %1 moved from %2 to %3")
                                .arg(line)
                                .arg(requestedY)
                                .arg(viewport->property("contentY").toReal())));
        QVERIFY2(contentYChanges.isEmpty(),
                 qPrintable(QStringLiteral("background append changed contentY on line %1")
                                .arg(line)));
    }
    QCOMPARE(consoleView->property("manualNavigation").toBool(), true);

    const qreal latestPosition = std::max(
        0.0,
        viewport->property("contentHeight").toReal() - viewport->property("height").toReal());
    QVERIFY(viewport->setProperty("contentY", latestPosition));
    QCoreApplication::processEvents();
    QCOMPARE(consoleView->property("followTail").toBool(), false);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), true);
    QCOMPARE(resumeTailButton->property("visible").toBool(), true);
    QVERIFY(QMetaObject::invokeMethod(consoleView, "resumeTailFollowing"));
    QCOMPARE(consoleView->property("followTail").toBool(), true);
    QCOMPARE(consoleView->property("manualNavigation").toBool(), false);
    QCOMPARE(resumeTailButton->property("visible").toBool(), false);

    logModel.appendSystemMessage(QStringLiteral("refresh-probe-line-181"));
    QTRY_VERIFY_WITH_TIMEOUT(
        quickDocument->textDocument()->toPlainText().contains(
            QStringLiteral("refresh-probe-line-181")),
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        qAbs(viewport->property("contentY").toReal()
             - (viewport->property("contentHeight").toReal() - viewport->property("height").toReal()))
            < 2.0,
        2000);
}

void LaunchCommandBuilderTest::englishCatalogContainsNoChineseLabels()
{
    const QLocale previousLocale;
    QLocale::setDefault(QLocale(QStringLiteral("en_US")));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ConfigurationManager manager(directory.filePath(QStringLiteral("profiles.json")));
    const QRegularExpression chinese(QStringLiteral("[\\x{4e00}-\\x{9fff}]"));

    const QVariantList categories = manager.categories();
    QVERIFY(!categories.isEmpty());
    for (const QVariant &categoryEntry : categories) {
        const QVariantMap category = categoryEntry.toMap();
        QVERIFY(!chinese.match(category.value(QStringLiteral("title")).toString()).hasMatch());
        QVERIFY(!chinese.match(category.value(QStringLiteral("description")).toString()).hasMatch());

        const QVariantList parameters = manager.parametersForCategory(
            category.value(QStringLiteral("key")).toString());
        for (const QVariant &parameterEntry : parameters) {
            const QVariantMap parameter = parameterEntry.toMap();
            QVERIFY(!chinese.match(parameter.value(QStringLiteral("title")).toString()).hasMatch());
            QVERIFY(!chinese.match(parameter.value(QStringLiteral("description")).toString()).hasMatch());
            for (const QVariant &optionEntry : parameter.value(QStringLiteral("options")).toList()) {
                QVERIFY(!chinese.match(optionEntry.toMap().value(QStringLiteral("label")).toString()).hasMatch());
            }
        }
    }
    QLocale::setDefault(previousLocale);
}

void LaunchCommandBuilderTest::catalogDefaultOptionsAreDescriptive()
{
    const QLocale previousLocale;
    QLocale::setDefault(QLocale(QStringLiteral("zh_CN")));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ConfigurationManager manager(directory.filePath(QStringLiteral("profiles.json")));

    for (const QVariant &categoryEntry : manager.categories()) {
        const QVariantMap category = categoryEntry.toMap();
        const QVariantList parameters = manager.parametersForCategory(
            category.value(QStringLiteral("key")).toString());
        for (const QVariant &parameterEntry : parameters) {
            const QVariantMap parameter = parameterEntry.toMap();
            for (const QVariant &optionEntry : parameter.value(QStringLiteral("options")).toList()) {
                const QString label = optionEntry.toMap().value(QStringLiteral("label")).toString();
                QVERIFY2(label != QStringLiteral("默认"),
                         qPrintable(parameter.value(QStringLiteral("key")).toString()));
            }
        }
    }
    QLocale::setDefault(previousLocale);
}

QTEST_MAIN(LaunchCommandBuilderTest)

#include "tst_LaunchCommandBuilder.moc"
