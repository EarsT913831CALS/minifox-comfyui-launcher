// Launch commands, the child process and its environment.
#include "tst_Runtime.h"

void RuntimeTest::runtimeFreezesProfileAndBlocksSharedPython_data()
{
    QTest::addColumn<int>("port");
    QTest::newRow("default-port") << 8188;
    QTest::newRow("custom-port") << 8931;
}

void RuntimeTest::runtimeFreezesProfileAndBlocksSharedPython()
{
    QFETCH(int, port);
    const QString python = QStandardPaths::findExecutable("python.exe");
    QVERIFY(!python.isEmpty());
    QTemporaryDir temp;
    QFile main(temp.filePath("main.py")); QVERIFY(main.open(QIODevice::WriteOnly));
    main.write("import time\ntime.sleep(10)\n"); main.close();
    QFile requirements(temp.filePath("requirements.txt")); QVERIFY(requirements.open(QIODevice::WriteOnly)); requirements.close();
    ConfigurationManager configuration(temp.filePath("profiles.json"));
    configuration.setComfyRoot(temp.path()); configuration.setPythonPath(python);
    configuration.setParameterValue("port", port);
    ApplicationSettings settings(temp.filePath("settings.json"));
    RuntimeManager runtime(&configuration, &settings);
    runtime.start();
    QVERIFY(runtime.active());
    configuration.setPythonPath(temp.filePath("missing.exe"));
    configuration.setParameterValue("port", 8932);
    QVERIFY(runtime.serviceUrl().endsWith(QString(":%1").arg(port)));
    OperationLease update;
    QString error;
    QVERIFY(!update.acquire({OperationLease::pythonEnvironment(python)}, &error));
    QTRY_VERIFY_WITH_TIMEOUT(runtime.processId() > 0, 5000);
    runtime.shutdown();
    QCoreApplication::processEvents();
    QVERIFY2(update.acquire({OperationLease::pythonEnvironment(python)}, &error), qPrintable(error));
}

void RuntimeTest::serviceUrlFollowsCustomArguments_data()
{
    QTest::addColumn<QString>("custom");
    QTest::addColumn<QString>("url");
    QTest::newRow("separate") << "--port 9237 --listen 127.0.0.1" << "http://127.0.0.1:9237";
    QTest::newRow("equals") << "--port=9240" << "http://127.0.0.1:9240";
    QTest::newRow("wildcard-listen") << "--listen --port 9241" << "http://127.0.0.1:9241";
    QTest::newRow("ipv6") << "--listen ::1 --port 9242" << "http://[::1]:9242";
}

void RuntimeTest::serviceUrlFollowsCustomArguments()
{
    QFETCH(QString, custom);
    QFETCH(QString, url);
    QTemporaryDir temp;
    ConfigurationManager configuration(temp.filePath("profiles.json"));
    configuration.setComfyRoot(temp.path());
    configuration.setCustomArguments(custom);
    ApplicationSettings settings(temp.filePath("settings.json"));
    RuntimeManager runtime(&configuration, &settings);
    QCOMPARE(runtime.serviceUrl(), url);
}

void RuntimeTest::disabledCustomNodesDoNotGateStartup_data()
{
    QTest::addColumn<QString>("whitelist");
    QTest::addColumn<bool>("starts");
    QTest::newRow("broken-disabled") << "allowed" << true;
    QTest::newRow("broken-whitelisted") << "allowed\nbroken" << false;
}

void RuntimeTest::disabledCustomNodesDoNotGateStartup()
{
    QFETCH(QString, whitelist);
    QFETCH(bool, starts);
    const QString python = QStandardPaths::findExecutable("python.exe");
    if (python.isEmpty()) QSKIP("python.exe is not available on PATH");
    qputenv("MINIFOX_SKIP_DEPENDENCY_INSTALL", "1");
    QTemporaryDir temp;
    QDir root(temp.path());
    const QString marker = temp.filePath("started.txt");
    QFile main(temp.filePath("main.py")); QVERIFY(main.open(QIODevice::WriteOnly));
    main.write(QStringLiteral("from pathlib import Path\nPath(r'%1').write_text('x')\n")
                   .arg(QDir::toNativeSeparators(marker)).toUtf8());
    main.close();
    QFile core(temp.filePath("requirements.txt")); QVERIFY(core.open(QIODevice::WriteOnly)); core.close();
    QVERIFY(root.mkpath("custom_nodes/broken")); QVERIFY(root.mkpath("custom_nodes/allowed"));
    QFile broken(temp.filePath("custom_nodes/broken/requirements.txt"));
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("minifox-definitely-missing-dependency-xyz==1.0\n"); broken.close();
    QFile allowed(temp.filePath("custom_nodes/allowed/requirements.txt"));
    QVERIFY(allowed.open(QIODevice::WriteOnly)); allowed.close();
    ConfigurationManager configuration(temp.filePath("profiles.json"));
    configuration.setComfyRoot(temp.path()); configuration.setPythonPath(python);
    configuration.setParameterValue("disableCustomNodes", true);
    configuration.setParameterValue("whitelistCustomNodes", whitelist);
    ApplicationSettings settings(temp.filePath("settings.json"));
    RuntimeManager runtime(&configuration, &settings);
    runtime.start();
    if (starts) {
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker), 8000);
    } else {
        QTRY_COMPARE_WITH_TIMEOUT(runtime.status(), RuntimeManager::Failed, 8000);
        QVERIFY(!QFileInfo::exists(marker));
    }
    runtime.shutdown();
}

void RuntimeTest::readinessRequiresComfySystemStats_data()
{
    QTest::addColumn<QByteArray>("payload");
    QTest::addColumn<bool>("ready");
    QTest::newRow("plain-text") << QByteArray("not a ComfyUI server") << false;
    QTest::newRow("system-stats") << QByteArray(R"({"system":{"os":"nt"},"devices":[]})") << true;
}

void RuntimeTest::readinessRequiresComfySystemStats()
{
    QFETCH(QByteArray, payload);
    QFETCH(bool, ready);
    const QString python = QStandardPaths::findExecutable("python.exe");
    if (python.isEmpty()) QSKIP("python.exe is not available on PATH");
    QTcpServer probe; QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 port = probe.serverPort(); probe.close();
    QTemporaryDir temp;
    QFile main(temp.filePath("main.py")); QVERIFY(main.open(QIODevice::WriteOnly));
    main.write(QStringLiteral(
        "import http.server\n"
        "class H(http.server.BaseHTTPRequestHandler):\n"
        "    def do_GET(self):\n"
        "        self.send_response(200); self.end_headers(); self.wfile.write(%1)\n"
        "    def log_message(self, *args): pass\n"
        "http.server.HTTPServer(('127.0.0.1', %2), H).serve_forever()\n")
                   .arg(QStringLiteral("b'%1'").arg(QString::fromUtf8(payload)))
                   .arg(port).toUtf8());
    main.close();
    QFile requirements(temp.filePath("requirements.txt")); QVERIFY(requirements.open(QIODevice::WriteOnly)); requirements.close();
    ConfigurationManager configuration(temp.filePath("profiles.json"));
    configuration.setComfyRoot(temp.path()); configuration.setPythonPath(python);
    configuration.setParameterValue("port", port);
    ApplicationSettings settings(temp.filePath("settings.json"));
    RuntimeManager runtime(&configuration, &settings);
    runtime.start();
    QTRY_VERIFY_WITH_TIMEOUT(runtime.processId() > 0, 8000);
    if (ready) {
        QTRY_VERIFY_WITH_TIMEOUT(runtime.serviceReady(), 10000);
        QCOMPARE(runtime.status(), RuntimeManager::Running);
    } else {
        QTest::qWait(3000);
        QVERIFY(!runtime.serviceReady());
        QCOMPARE(runtime.status(), RuntimeManager::Starting);
    }
    runtime.shutdown();
}

void RuntimeTest::occupiedPortBlocksLaunch()
{
    const QString python = QStandardPaths::findExecutable("python.exe");
    if (python.isEmpty()) QSKIP("python.exe is not available on PATH");
    QTcpServer occupant; QVERIFY(occupant.listen(QHostAddress::LocalHost, 0));
    QTemporaryDir temp;
    const QString marker = temp.filePath("started.txt");
    QFile main(temp.filePath("main.py")); QVERIFY(main.open(QIODevice::WriteOnly));
    main.write(QStringLiteral("from pathlib import Path\nPath(r'%1').write_text('x')\n")
                   .arg(QDir::toNativeSeparators(marker)).toUtf8());
    main.close();
    QFile requirements(temp.filePath("requirements.txt")); QVERIFY(requirements.open(QIODevice::WriteOnly)); requirements.close();
    ConfigurationManager configuration(temp.filePath("profiles.json"));
    configuration.setComfyRoot(temp.path()); configuration.setPythonPath(python);
    configuration.setParameterValue("port", occupant.serverPort());
    ApplicationSettings settings(temp.filePath("settings.json"));
    RuntimeManager runtime(&configuration, &settings);
    runtime.start();
    QTRY_COMPARE_WITH_TIMEOUT(runtime.status(), RuntimeManager::Failed, 8000);
    QVERIFY(runtime.lastError().contains(QString::number(occupant.serverPort())));
    QVERIFY(!QFileInfo::exists(marker));
}

void RuntimeTest::initTestCase()
{
    qputenv("MINIFOX_ZLUDA_BOOTSTRAP", "off");
}

void RuntimeTest::defaultsStayImplicit()
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

void RuntimeTest::commandPromptActivatesSelectedEnvironment()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    // cmd expands %NAME% inside quotes; such legal folder names must survive.
    QVERIFY(QDir(temporaryDirectory.path()).mkpath(
        QStringLiteral("percent-%MINIFOX_AUDIT_SUB%-path & (x)")));
    QDir root(temporaryDirectory.filePath(QStringLiteral("percent-%MINIFOX_AUDIT_SUB%-path & (x)")));
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
    environment.insert(QStringLiteral("MINIFOX_AUDIT_SUB"), QStringLiteral("expanded"));
    const CommandPromptBuilder::Result result = CommandPromptBuilder::build(
        pythonPath, root.filePath(QStringLiteral("ComfyUI")), environment);

    QCOMPARE(result.program, QStringLiteral("cmd.exe"));
    QVERIFY(result.nativeArguments.startsWith(QStringLiteral("/D /K ")));
    QVERIFY(result.nativeArguments.contains(QStringLiteral("chcp 65001")));
    QVERIFY(result.nativeArguments.contains(QStringLiteral("call")));
    QCOMPARE(QDir::cleanPath(result.activationScript), QDir::cleanPath(activationPath));
    QVERIFY(!result.nativeArguments.contains(QStringLiteral("MINIFOX_AUDIT_SUB")));
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
    probeArguments.append(QStringLiteral(" && echo ACT=!MINIFOX_TEST_ACTIVATED! DIR=!CD!"));
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
    QVERIFY2(activationOutput.contains("ACT=1"), activationOutput.constData());
    QVERIFY2(activationOutput.contains(
                 ("DIR=" + QDir::toNativeSeparators(root.filePath(QStringLiteral("ComfyUI")))).toUtf8()),
             activationOutput.constData());

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

void RuntimeTest::explicitModesAndCustomArguments()
{
    const QVariantMap parameters {
        {QStringLiteral("browser"), QStringLiteral("disable")},
        {QStringLiteral("port"), 9000},
        {QStringLiteral("vramMode"), QStringLiteral("low")},
        {QStringLiteral("pinnedMemory"), QStringLiteral("disable")},
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
    QVERIFY(result.arguments.contains(QStringLiteral("--disable-pinned-memory")));
    QVERIFY(result.arguments.contains(QStringLiteral("--async-offload")));
    QCOMPARE(result.arguments.at(result.arguments.indexOf(QStringLiteral("--port")) + 1), QStringLiteral("9000"));
    QCOMPARE(result.arguments.constLast(), QStringLiteral("two words"));
    QVERIFY(result.preview.contains(QStringLiteral("--port 9000")));
    QVERIFY(result.preview.contains(QStringLiteral("--extra-test \"two words\"")));
    QVERIFY(!result.preview.contains(QStringLiteral("[自由文本参数已隐藏]")));
}

void RuntimeTest::progressBridgeEnablesArgumentParsingBeforeProgressImport()
{
    Q_INIT_RESOURCE(minifox_python_bridge);
    QFile bridge(QStringLiteral(":/minifox/python/minifox_progress_bridge.py"));
    QVERIFY2(bridge.open(QIODevice::ReadOnly), qPrintable(bridge.errorString()));
    QByteArray source = bridge.readAll();
    source.replace("\r\n", "\n");

    QVERIFY(source.contains("comfy.options.enable_args_parsing()"));
    const qsizetype parsingCall = source.lastIndexOf("\nenable_comfy_argument_parsing()\n");
    const qsizetype progressCall = source.lastIndexOf("\nenable_cli_progress()\n");
    QVERIFY(parsingCall >= 0);
    QVERIFY(source.contains("class MinifoxCLIProgressHandler"));
    QVERIFY(source.contains("metadata.get(\"title\")"));
    QVERIFY(source.contains("_is_core_progress_node"));
    QVERIFY(source.contains("\"samplercustom\""));
    QVERIFY(source.contains("\"textencode\""));
    QVERIFY(source.contains("\"vae\""));
    QVERIFY(source.contains("\"unet\""));
    QVERIFY(progressCall > parsingCall);
}

void RuntimeTest::environmentIsAppliedAndFullyDisplayed()
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
    QVERIFY(result.preview.contains(QStringLiteral("SERVICE_API_KEY=top-secret")));
    QVERIFY(!result.preview.contains(QStringLiteral("DISABLED_ENV")));
}

void RuntimeTest::sensitiveProfileSnapshotsCanBeRedacted()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    ConfigurationManager manager(
        temporaryDirectory.filePath(QStringLiteral("profiles.json")));

    const int ordinaryIndex = manager.addEnvironmentEntry();
    manager.updateEnvironmentEntry(ordinaryIndex, QStringLiteral("CUSTOM_ENV"),
                                   QStringLiteral("ordinary-value"), true);
    const int secretIndex = manager.addEnvironmentEntry();
    manager.updateEnvironmentEntry(secretIndex, QStringLiteral("SERVICE_API_KEY"),
                                   QStringLiteral("top-secret"), true);
    QVERIFY(manager.hasSensitiveEnvironmentValues());

    const QVariantList fullEnvironment = manager.currentProfileSnapshot(true)
                                             .value(QStringLiteral("environment"))
                                             .toList();
    QCOMPARE(fullEnvironment.at(secretIndex).toMap()
                 .value(QStringLiteral("value")).toString(),
             QStringLiteral("top-secret"));
    QVERIFY(fullEnvironment.at(secretIndex).toMap()
                .value(QStringLiteral("enabled")).toBool());

    const QVariantList redactedEnvironment = manager.currentProfileSnapshot(false)
                                                 .value(QStringLiteral("environment"))
                                                 .toList();
    QVERIFY(redactedEnvironment.isEmpty());
}

void RuntimeTest::proxySettingsAreAppliedToChildEnvironment()
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

void RuntimeTest::bundledPythonRepairsSystemPythonProfile()
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

void RuntimeTest::runtimeSurvivesFastChildFailure()
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

void RuntimeTest::runtimeAcceptsBareVcsDependencies()
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

void RuntimeTest::runtimeBlocksMissingDependencies()
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

void RuntimeTest::progressBridgeRejectsAdjacentModules()
{
    QTemporaryDir temp;
    QDir root(temp.path()); QVERIFY(root.mkpath("runtime")); QVERIFY(root.mkpath("comfy"));
    QFile resource(":/minifox/python/minifox_progress_bridge.py"); QVERIFY(resource.open(QIODevice::ReadOnly));
    const auto write = [](const QString &path, const QByteArray &bytes) {
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    };
    const QString script = root.filePath("runtime/bridge.py");
    QVERIFY(write(script, resource.readAll()));
    QVERIFY(write(root.filePath("runtime/pkgutil.py"), "raise RuntimeError('ADJACENT_MODULE_LOADED')\n"));
    QVERIFY(write(root.filePath("runtime/runpy.py"), "raise RuntimeError('ADJACENT_MODULE_LOADED')\n"));
    QVERIFY(write(root.filePath("comfy/main.py"), "print('COMFY_CONTROL_OK')\n"));
    QProcess process; auto env = QProcessEnvironment::systemEnvironment();
    env.insert("MINIFOX_COMFY_MAIN", root.filePath("comfy/main.py"));
    process.setProcessEnvironment(env); process.setWorkingDirectory(root.filePath("comfy"));
    process.start(QStandardPaths::findExecutable("python"), {script});
    QVERIFY(process.waitForFinished(15000));
    const auto error = process.readAllStandardError();
    QVERIFY2(process.exitCode() == 0, error.constData());
    QVERIFY(process.readAllStandardOutput().contains("COMFY_CONTROL_OK"));
    QVERIFY(!error.contains("ADJACENT_MODULE_LOADED"));
}

void RuntimeTest::runtimeShutdownReleasesChildPort()
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

QTEST_MAIN(RuntimeTest)
