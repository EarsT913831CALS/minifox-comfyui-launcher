#include "VersionManager.h"
#include "ApplicationSettings.h"
#include "ConfigurationManager.h"
#include "PortablePaths.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include "RepositoryUpdateJob.h"
#include "RepositoryRecovery.h"
#include "VersionTransactionStore.h"
#include <QScopeGuard>

class VersionManagerTest : public QObject
{
    Q_OBJECT
    QString git;
    QByteArray output;
    bool run(const QString &root, const QStringList &args) {
        QProcess p;
        p.start(git, QStringList{"-C", root} + args);
        if (!p.waitForFinished(15000)) { p.kill(); p.waitForFinished(); return false; }
        output = p.readAllStandardOutput().trimmed();
        return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    }
    bool write(const QString &path, const QByteArray &data) {
        QSaveFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
    }
    // A pip shadow on PYTHONPATH: `python -m pip` runs this __main__, which records the
    // call and never touches the interpreter; pip._vendor still resolves to the real pip
    // so the offline checker keeps working.
    bool writeFakePip(const QString &directory) {
        return QDir().mkpath(directory + "/pip")
            && write(directory + "/pip/__init__.py",
                "import importlib.machinery, os, sys\n"
                "_here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))\n"
                "_spec = importlib.machinery.PathFinder.find_spec('pip', [p for p in sys.path if os.path.abspath(p or '.') != _here])\n"
                "if _spec and _spec.submodule_search_locations:\n"
                "    __path__.extend(_spec.submodule_search_locations)\n")
            && write(directory + "/pip/__main__.py",
                "import ctypes, json, os, sys, time\n"
                "console = bool(ctypes.windll.kernel32.GetConsoleWindow()) if os.name == 'nt' else True\n"
                "with open(os.environ['MINIFOX_TEST_PIP_LOG'], 'a', encoding='utf-8') as log:\n"
                "    log.write(json.dumps({'argv': sys.argv[1:], 'cwd': os.getcwd(), 'console': console}) + '\\n')\n"
                "time.sleep(float(os.environ.get('MINIFOX_TEST_PIP_SLEEP', '0')))\n"
                "sys.exit(int(os.environ.get('MINIFOX_TEST_PIP_EXIT', '0')))\n");
    }
    QList<QJsonObject> pipCalls(const QString &log) {
        QFile file(log);
        QList<QJsonObject> calls;
        if (!file.open(QIODevice::ReadOnly)) return calls;
        for (const QByteArray &line : file.readAll().split('\n'))
            if (!line.trimmed().isEmpty()) calls.append(QJsonDocument::fromJson(line).object());
        return calls;
    }
    QProcessEnvironment fakePipEnvironment(const QString &fakePip, const QString &log) {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("PYTHONPATH", fakePip);
        environment.insert("MINIFOX_TEST_PIP_LOG", log);
        environment.insert("MINIFOX_DEPENDENCY_FAILURE_PAUSE", "0");
        return environment;
    }
private slots:
    void dependencyQueueInstallsEachProjectInItsOwnDirectory() {
        const QString python = QStandardPaths::findExecutable("python");
        if (python.isEmpty()) QSKIP("python is not available on PATH");
        QTemporaryDir temp;
        const QString fakePip = temp.filePath("fake-pip"), log = temp.filePath("pip.log");
        QVERIFY(writeFakePip(fakePip));
        const QString a = temp.filePath("nodes/A"), b = temp.filePath("nodes/B"), c = temp.filePath("nodes/C");
        for (const QString &dir : {a, b, c}) QVERIFY(QDir().mkpath(dir));
        QVERIFY(write(a + "/requirements.txt", "minifox-definitely-missing-a==1.0\n"));
        QVERIFY(write(b + "/requirements.txt", "./minifox_local_b-1.0-py3-none-any.whl\n"));
        QVERIFY(write(c + "/requirements.txt", ""));
        ConfigurationManager config(temp.filePath("profiles.json"));
        ApplicationSettings settings(temp.filePath("settings.json"));
        VersionManager manager(&config, &settings);
        manager.m_operationPython = python;
        manager.m_operationGit.environment = fakePipEnvironment(fakePip, log);
        // pip's own quiet mode must not decide whether anything gets installed.
        manager.m_operationGit.environment.insert("PIP_QUIET", "1");
        QSignalSpy results(&manager, &VersionManager::dependencyInstallCompleted);
        manager.queueDependencyCheck(a);
        manager.queueDependencyCheck(b);
        manager.queueDependencyCheck(c);
        QVERIFY(manager.installingDependencies());
        QTRY_VERIFY_WITH_TIMEOUT(!manager.installingDependencies(), 60000);
        const QList<QJsonObject> calls = pipCalls(log);
        QCOMPARE(calls.size(), 2);
        // Package changes happen in a visible console window, not a hidden pipe.
        QVERIFY(calls.at(0).value("console").toBool());
        QCOMPARE(QDir::cleanPath(calls.at(0).value("cwd").toString()), QDir::cleanPath(a));
        QCOMPARE(calls.at(0).value("argv").toArray(),
                 QJsonArray({"install", "-r", QDir(a).filePath("requirements.txt")}));
        QCOMPARE(QDir::cleanPath(calls.at(1).value("cwd").toString()), QDir::cleanPath(b));
        QCOMPARE(results.count(), 2);
        for (const QList<QVariant> &result : results)
            QVERIFY2(result.at(0).toBool(), qPrintable(result.at(1).toString()));
    }
    void dependencyStartFailureCompletesEachProjectOnce() {
        QTemporaryDir temp;
        const QString a = temp.filePath("A"), b = temp.filePath("B");
        for (const QString &dir : {a, b}) {
            QVERIFY(QDir().mkpath(dir));
            QVERIFY(write(dir + "/requirements.txt", "anything==1\n"));
        }
        // Exists but cannot be executed: every run ends through FailedToStart.
        const QString notPython = temp.filePath("python.exe");
        QVERIFY(write(notPython, "not an executable"));
        ConfigurationManager config(temp.filePath("profiles.json"));
        ApplicationSettings settings(temp.filePath("settings.json"));
        VersionManager manager(&config, &settings);
        manager.m_operationPython = notPython;
        manager.m_operationGit.environment = QProcessEnvironment::systemEnvironment();
        QSignalSpy results(&manager, &VersionManager::dependencyInstallCompleted);
        manager.queueDependencyCheck(a);
        manager.queueDependencyCheck(b);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.installingDependencies(), 15000);
        QTest::qWait(200);
        QCOMPARE(results.count(), 2);
        QVERIFY(results.at(0).at(1).toString().startsWith("A"));
        QVERIFY(results.at(1).at(1).toString().startsWith("B"));
    }
    void dependencyInstallCanBeCancelled() {
        const QString python = QStandardPaths::findExecutable("python");
        if (python.isEmpty()) QSKIP("python is not available on PATH");
        QTemporaryDir temp;
        const QString fakePip = temp.filePath("fake-pip"), log = temp.filePath("pip.log");
        QVERIFY(writeFakePip(fakePip));
        const QString a = temp.filePath("A"), b = temp.filePath("B");
        for (const QString &dir : {a, b}) {
            QVERIFY(QDir().mkpath(dir));
            QVERIFY(write(dir + "/requirements.txt", "minifox-definitely-missing-x==1.0\n"));
        }
        ConfigurationManager config(temp.filePath("profiles.json"));
        ApplicationSettings settings(temp.filePath("settings.json"));
        VersionManager manager(&config, &settings);
        manager.m_operationPython = python;
        manager.m_operationGit.environment = fakePipEnvironment(fakePip, log);
        manager.m_operationGit.environment.insert("MINIFOX_TEST_PIP_SLEEP", "60");
        QSignalSpy results(&manager, &VersionManager::dependencyInstallCompleted);
        manager.queueDependencyCheck(a);
        manager.queueDependencyCheck(b);
        QTRY_COMPARE_WITH_TIMEOUT(pipCalls(log).size(), 1, 30000);
        manager.cancelDependencyInstall();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.installingDependencies(), 10000);
        QCOMPARE(results.count(), 1);
        QVERIFY(!results.first().at(0).toBool());
        QCOMPARE(pipCalls(log).size(), 1);
        QCOMPARE(manager.m_dependencyProcess.state(), QProcess::NotRunning);
    }
    void extensionModelsFilterAndKeepRows() {
        ExtensionListModel source(QStringLiteral("path"));
        ExtensionFilterModel filter(&source, {QStringLiteral("name"), QStringLiteral("remote")});
        QVariantList entries;
        for (int i = 0; i < 6000; ++i)
            entries.append(QVariantMap{{"path", QString("C:/n/%1").arg(i)}, {"name", QString("node-%1").arg(i)},
                                       {"remote", i == 4242 ? "https://example.invalid/Upscaler" : "https://example.invalid/x"},
                                       {"status", "checking"}});
        source.setEntries(entries);
        QCOMPARE(filter.count(), 6000);
        filter.setFilterText(QStringLiteral("  upscaler "));
        QCOMPARE(filter.count(), 1);
        QCOMPARE(filter.data(filter.index(0, 0), ExtensionListModel::EntryRole).toMap().value("name").toString(),
                 QStringLiteral("node-4242"));
        filter.setFilterText(QStringLiteral("no-such-extension"));
        QCOMPARE(filter.count(), 0);
        filter.setFilterText({});

        // A status change keeps the rows; only the changed row is reported.
        QSignalSpy resets(&source, &QAbstractItemModel::modelReset);
        QSignalSpy changes(&source, &QAbstractItemModel::dataChanged);
        source.setEntries(entries);
        QCOMPARE(changes.size(), 0);
        QVariantMap updated = entries.at(7).toMap();
        updated.insert("status", "latest");
        entries[7] = updated;
        source.setEntries(entries);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes.first().at(0).value<QModelIndex>().row(), 7);
        entries.removeLast();
        source.setEntries(entries);
        QCOMPARE(resets.size(), 1);
        QCOMPARE(filter.count(), 5999);
    }

    void refusedExtensionHistoryClearsPreviousList() {
        QTemporaryDir temp;
        const QString plain = temp.filePath("plain"), repository = temp.filePath("repository");
        QVERIFY(QDir().mkpath(plain)); QVERIFY(QDir().mkpath(repository + "/.git"));
        ConfigurationManager config(temp.filePath("profiles.json"));
        ApplicationSettings settings(temp.filePath("settings.json"));
        VersionManager manager(&config, &settings);
        QCoreApplication::processEvents();
        QSignalSpy loaded(&manager, &VersionManager::extensionVersionsLoaded);
        // A list left over from another extension must not be shown for this one.
        manager.m_extensionVersions = {QVariantMap{{"commit", "previous"}}};
        QVERIFY(!manager.loadExtensionVersions(plain, {}));
        QVERIFY(manager.extensionVersions().isEmpty());
        QCOMPARE(loaded.size(), 1);
        QVERIFY(!loaded.last().at(0).toBool());
        QVERIFY(!manager.lastError().isEmpty());

        manager.m_extensionVersions = {QVariantMap{{"commit", "previous"}}};
        manager.m_lastError = "running operation error";
        manager.m_busy = true;
        QVERIFY(!manager.loadExtensionVersions(repository, {}));
        manager.m_busy = false;
        QVERIFY(manager.extensionVersions().isEmpty());
        QCOMPARE(loaded.size(), 2);
        // The running operation still owns lastError.
        QCOMPARE(manager.lastError(), QStringLiteral("running operation error"));
    }
    void changedProfileCannotMixRepositoryAndPython() {
        QTemporaryDir temp;
        const QString a = temp.filePath("A"), b = temp.filePath("B");
        QVERIFY(QDir().mkpath(a)); QVERIFY(QDir().mkpath(b));
        ConfigurationManager config(temp.filePath("profiles.json"));
        config.setComfyRoot(a); config.setPythonPath(temp.filePath("pythonA/python.exe"));
        ApplicationSettings settings(temp.filePath("settings.json"));
        VersionManager manager(&config, &settings);
        QCoreApplication::processEvents();
        manager.m_comfyRoot = a;
        QVERIFY(manager.acquireMutationLease());
        manager.m_busy = true;
        config.setComfyRoot(b); config.setPythonPath(temp.filePath("pythonB/python.exe"));
        QCOMPARE(manager.m_operationPython, temp.filePath("pythonA/python.exe"));
        manager.m_busy = false;
        QVERIFY(!manager.acquireMutationLease());
        QCoreApplication::processEvents();
        QCOMPARE(manager.m_comfyRoot, b);
        QVERIFY(manager.acquireMutationLease());
        QCOMPARE(manager.m_operationPython, temp.filePath("pythonB/python.exe"));
    }
    void initTestCase() {
        git = QStandardPaths::findExecutable("git");
        QVERIFY(!git.isEmpty());
    }
    void init() { QFile::remove(PortablePaths::versionOperationFile()); }

    void refreshNeverMovesBranches_data() {
        QTest::addColumn<QString>("tag");
        QTest::addColumn<bool>("network");
        for (const QString &tag : {QString("none"), QString("lightweight"), QString("annotated"), QString("packed")})
            for (bool network : {false, true})
                QTest::newRow(qPrintable(tag + (network ? "-fetch" : "-local"))) << tag << network;
    }
    void refreshNeverMovesBranches() {
        QFETCH(QString, tag); QFETCH(bool, network);
        QTemporaryDir temp;
        const QString root = temp.filePath("ComfyUI"), remote = temp.filePath("remote.git");
        QVERIFY(QDir().mkpath(root));
        QVERIFY(run(root, {"init", "-b", "master"}));
        QVERIFY(run(root, {"config", "user.name", "Test"}));
        QVERIFY(run(root, {"config", "user.email", "test@example.invalid"}));
        QVERIFY(write(root + "/main.py", "# base\n"));
        QVERIFY(run(root, {"add", "."})); QVERIFY(run(root, {"commit", "-m", "base"}));
        QVERIFY(run(root, {"checkout", "-b", "dev"}));
        QVERIFY(write(root + "/main.py", "# dev\n")); QVERIFY(run(root, {"commit", "-am", "dev"}));
        QVERIFY(run(root, {"checkout", "master"}));
        QVERIFY(write(root + "/main.py", "# master\n")); QVERIFY(run(root, {"commit", "-am", "master"}));
        if (tag != "none") {
            QVERIFY(run(root, tag == "lightweight" ? QStringList{"tag", "v1.0.0"}
                                                     : QStringList{"tag", "-a", "v1.0.0", "-m", "release"}));
            if (tag == "packed") QVERIFY(run(root, {"pack-refs", "--all"}));
        }
        QVERIFY(run(temp.path(), {"clone", "--bare", root, remote}));
        QVERIFY(run(root, {"remote", "add", "origin", remote}));
        QVERIFY(run(root, {"fetch", "origin"}));
        QVERIFY(write(root + "/main.py", "# local edit\n"));
        QVERIFY(run(root, {"for-each-ref", "--format=%(refname) %(objectname)"})); const QByteArray refs = output;
        QFile index(root + "/.git/index"); QVERIFY(index.open(QIODevice::ReadOnly)); const QByteArray beforeIndex = index.readAll(); index.close();
        QVERIFY(run(root, {"symbolic-ref", "HEAD"})); const QByteArray head = output;
        ConfigurationManager config(temp.filePath("profiles.json")); config.setComfyRoot(root);
        ApplicationSettings settings(temp.filePath("settings.json")); VersionManager manager(&config, &settings);
        QCoreApplication::processEvents(); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 30000);
        if (network) manager.refreshCore(); else manager.loadLocalState();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 30000);
        QVERIFY2(manager.lastError().isEmpty(), qPrintable(manager.lastError()));
        QVERIFY(run(root, {"for-each-ref", "--format=%(refname) %(objectname)"})); QCOMPARE(output, refs);
        QVERIFY(run(root, {"symbolic-ref", "HEAD"})); QCOMPARE(output, head);
        QVERIFY(index.open(QIODevice::ReadOnly)); QCOMPARE(index.readAll(), beforeIndex);
        QFile file(root + "/main.py"); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("# local edit\n"));
    }

    void gitOwnershipIsNotOverridden() {
        QTemporaryDir temp; const QString root = temp.filePath("repo"); QVERIFY(QDir().mkpath(root));
        QVERIFY(run(root, {"init", "-b", "main"}));
        QVERIFY(run(root, {"config", "user.name", "Test"})); QVERIFY(run(root, {"config", "user.email", "test@example.invalid"}));
        QVERIFY(write(root + "/main.py", "# test\n")); QVERIFY(run(root, {"add", "."})); QVERIFY(run(root, {"commit", "-m", "test"}));
        const QString marker = temp.filePath("executed");
        const QString hook = temp.filePath("fsmonitor.sh");
        QVERIFY(write(hook, ("#!/bin/sh\necho executed > '" + marker + "'\n").toUtf8()));
        QVERIFY(run(root, {"config", "core.fsmonitor", hook}));
        ConfigurationManager config(temp.filePath("profiles.json")); ApplicationSettings settings(temp.filePath("settings.json"));
        VersionManager manager(&config, &settings);
        QProcess process; auto env = QProcessEnvironment::systemEnvironment();
        env.insert("GIT_TEST_ASSUME_DIFFERENT_OWNER", "1");
        env.insert("GIT_CONFIG_NOSYSTEM", "1"); env.insert("GIT_CONFIG_GLOBAL", temp.filePath("empty.config"));
        process.setProcessEnvironment(env);
        process.start(git, manager.repositoryArguments(root, {"status", "--porcelain"}));
        QVERIFY(process.waitForFinished()); QVERIFY(process.exitCode() != 0);
        QVERIFY(process.readAllStandardError().contains("dubious ownership"));
        QVERIFY(!QFileInfo::exists(marker));
        // A user's protected trust decision continues to work.
        QVERIFY(write(temp.filePath("empty.config"), ("[safe]\n directory = " + root + "\n").toUtf8()));
        process.start(git, manager.repositoryArguments(root, {"status", "--porcelain"}));
        QVERIFY(process.waitForFinished()); QCOMPARE(process.exitCode(), 0);
        QVERIFY(QFileInfo::exists(marker));
        QFile::remove(marker);
        // The update job must stop at the same ownership boundary.
        env.insert("GIT_CONFIG_GLOBAL", temp.filePath("missing.config"));
        const auto originalEnvironment = QProcessEnvironment::systemEnvironment();
        const QStringList keys{"GIT_TEST_ASSUME_DIFFERENT_OWNER", "GIT_CONFIG_NOSYSTEM", "GIT_CONFIG_GLOBAL"};
        const auto restoreEnvironment = qScopeGuard([&] {
            for (const auto &key : keys) {
                if (originalEnvironment.contains(key)) qputenv(key.toLatin1(), originalEnvironment.value(key).toUtf8());
                else qunsetenv(key.toLatin1());
            }
        });
        for (const auto &key : keys) qputenv(key.toLatin1(), env.value(key).toUtf8());
        RepositoryUpdateSpec spec; spec.gitProgram = git; spec.root = root; spec.displayName = "Test";
        RepositoryUpdateJob job(spec); QSignalSpy done(&job, &RepositoryUpdateJob::finished);
        job.start(); QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(), 30000);
        QVERIFY(!QFileInfo::exists(marker));
        QVERIFY(!QFileInfo::exists(PortablePaths::versionOperationFile()));
        VersionTransactionEntry entry; entry.id = "ownership-recovery"; entry.action = "update";
        entry.root = root; entry.targetRef = "HEAD"; entry.phase = "preparing";
        QVERIFY(VersionTransactionStore::upsert(entry));
        QString error;
        QVERIFY(!RepositoryRecovery::prepare(git, entry, {}, &error));
        QVERIFY2(error.contains("dubious ownership"), qPrintable(error));
        QVERIFY(!QFileInfo::exists(marker));
        QCOMPARE(VersionTransactionStore::entries().size(), 1);
        QVERIFY(VersionTransactionStore::remove(entry.id));
    }

    void pendingCompletionTextSurvivesStateCleanup_data() {
        QTest::addColumn<QString>("message");
        QTest::newRow("update") << QStringLiteral("扩展更新完成。");
        QTest::newRow("switch") << QStringLiteral("扩展版本已切换。");
        QTest::newRow("batch") << QStringLiteral("全部扩展更新完成。");
    }
    void mergeCallbackRequiresReadableTransaction_data() {
        QTest::addColumn<QByteArray>("marker");
        QTest::newRow("corrupt") << QByteArray("{broken");
        QTest::newRow("missing") << QByteArray();
        QTest::newRow("missing-id") << QByteArray(R"({"version":2,"entries":[]})");
    }
    void mergeCallbackRequiresReadableTransaction() {
        QFETCH(QByteArray, marker);
        QTemporaryDir temp; const QString root = temp.filePath("repo"); QVERIFY(QDir().mkpath(root));
        QVERIFY(run(root, {"init", "-b", "main"}));
        QVERIFY(run(root, {"config", "user.name", "Test"})); QVERIFY(run(root, {"config", "user.email", "test@example.invalid"}));
        QVERIFY(write(root + "/file", "old")); QVERIFY(run(root, {"add", "."})); QVERIFY(run(root, {"commit", "-m", "old"}));
        QVERIFY(run(root, {"rev-parse", "HEAD"})); const QString target = QString::fromLatin1(output);
        QVERIFY(write(root + "/file", "new")); QVERIFY(run(root, {"commit", "-am", "new"}));
        QVERIFY(run(root, {"rev-parse", "HEAD"})); const QByteArray head = output;
        ConfigurationManager config(temp.filePath("config.json")); ApplicationSettings settings(temp.filePath("settings.json"));
        VersionManager manager(&config, &settings); QCoreApplication::processEvents();
        manager.m_gitProgram = git; manager.m_transactionId = "merge-callback";
        manager.m_transactionInProgress = true; manager.m_safeMergeRoot = root;
        manager.m_safeMergeTargetRef = target; manager.m_safeMergeTargetBranch = "main";
        if (!marker.isEmpty()) { QVERIFY(PortablePaths::ensureDataDirectory()); QVERIFY(write(PortablePaths::versionOperationFile(), marker)); }
        manager.handleSafeSnapshotMergeFinished(true, {});
        QVERIFY(!manager.lastError().isEmpty()); QCOMPARE(manager.m_process.state(), QProcess::NotRunning);
        QVERIFY(run(root, {"rev-parse", "HEAD"})); QCOMPARE(output, head);
        if (!marker.isEmpty()) { QFile f(PortablePaths::versionOperationFile()); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(), marker); }
    }
    void pendingCompletionTextSurvivesStateCleanup() {
        QFETCH(QString, message);
        QTemporaryDir temp;
        ConfigurationManager config(temp.filePath("profiles.json"));
        ApplicationSettings settings(temp.filePath("settings.json"));
        VersionManager manager(&config, &settings);
        manager.m_pendingCompletionMessage = message;
        manager.m_notifyOnFinish = true;
        QSignalSpy spy(&manager, &VersionManager::operationCompleted);
        manager.finish(manager.m_pendingCompletionMessage);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toBool(), true);
        QCOMPARE(spy.first().at(1).toString(), message);
        QVERIFY(manager.m_pendingCompletionMessage.isEmpty());
    }

    void extensionUpdateAndSwitchUseRealBranches_data() {
        QTest::addColumn<QString>("branch");
        QTest::addColumn<bool>("reset");
        QTest::newRow("main-safe") << QString("main") << false;
        QTest::newRow("dev-safe") << QString("dev") << false;
        QTest::newRow("nested-safe") << QString("feature/topic") << false;
        QTest::newRow("main-reset") << QString("main") << true;
        QTest::newRow("dev-reset") << QString("dev") << true;
    }
    void extensionUpdateAndSwitchUseRealBranches() {
        QFETCH(QString, branch);
        QFETCH(bool, reset);
        QTemporaryDir temp;
        const QString remote = temp.filePath("remote.git"), seed = temp.filePath("seed");
        const QString comfy = temp.filePath("ComfyUI"), plugin = comfy + "/custom_nodes/TestPlugin";
        QVERIFY(QDir().mkpath(comfy + "/custom_nodes"));
        QVERIFY(run(temp.path(), {"init", "--bare", "--initial-branch=main", remote}));
        QVERIFY(run(temp.path(), {"clone", remote, seed}));
        QVERIFY(run(seed, {"config", "user.name", "Minifox Test"}));
        QVERIFY(run(seed, {"config", "user.email", "test@example.invalid"}));
        QVERIFY(write(seed + "/sample.txt", "original\n"));
        QVERIFY(run(seed, {"add", "."}));
        QVERIFY(run(seed, {"commit", "-m", "initial"}));
        QVERIFY(run(seed, {"rev-parse", "HEAD"}));
        const QString original = QString::fromLatin1(output);
        QVERIFY(run(seed, {"push", "-u", "origin", "main"}));
        if (branch != "main") {
            QVERIFY(run(seed, {"checkout", "-b", branch}));
            QVERIFY(run(seed, {"push", "-u", "origin", branch}));
        }
        QVERIFY(run(temp.path(), {"clone", "-b", branch, remote, plugin}));
        QVERIFY(run(plugin, {"config", "user.name", "Minifox Test"}));
        QVERIFY(run(plugin, {"config", "user.email", "test@example.invalid"}));
        QVERIFY(write(seed + "/sample.txt", "updated\n"));
        QVERIFY(run(seed, {"commit", "-am", "update"}));
        QVERIFY(run(seed, {"push"}));
        ConfigurationManager config(temp.filePath("profiles.json"));
        config.setComfyRoot(comfy);
        ApplicationSettings settings(temp.filePath("settings.json"));
        settings.setResetTrackedFilesOnUpdate(reset);
        VersionManager manager(&config, &settings);
        // Drain startup initialization before exercising the public operations.
        QCoreApplication::processEvents();
        manager.m_comfyRoot = comfy;
        manager.m_gitProgram = git;
        QVERIFY(run(plugin, {"checkout", "-b", "minifox/version-extension"}));
        QVERIFY(run(plugin, {"branch", "--set-upstream-to=origin/" + branch}));
        manager.scanExtensions();
        QSignalSpy spy(&manager, &VersionManager::operationCompleted);
        manager.updateExtension(plugin);
        QTRY_VERIFY_WITH_TIMEOUT(!spy.isEmpty(), 60000);
        QVERIFY2(spy.last().at(0).toBool(), qPrintable(spy.last().at(1).toString()));
        QCOMPARE(spy.last().at(1).toString(), QStringLiteral("扩展更新完成。"));
        QCOMPARE(manager.installedExtensions().first().toMap().value("branch").toString(), branch);

        // Reproduce the previous launcher's internal branch with a real upstream.
        QVERIFY(run(plugin, {"checkout", "-B", "minifox/version-extension"}));
        QVERIFY(run(plugin, {"branch", "--set-upstream-to=origin/" + branch}));
        manager.scanExtensions();
        spy.clear();
        if (reset) QVERIFY(write(plugin + "/sample.txt", "local to back up\n"));
        manager.switchExtensionVersion(plugin, original);
        QTRY_VERIFY_WITH_TIMEOUT(!spy.isEmpty(), 60000);
        QVERIFY2(spy.last().at(0).toBool(), qPrintable(spy.last().at(1).toString()));
        QVERIFY(spy.last().at(1).toString().startsWith(QStringLiteral("扩展版本已切换。")));
        QVERIFY(run(plugin, {"branch", "--show-current"}));
        QCOMPARE(QString::fromUtf8(output), branch);
        const QVariantMap displayed = manager.installedExtensions().first().toMap();
        QCOMPARE(displayed.value("branch").toString(), branch);
        QCOMPARE(displayed.value("commit").toString(), original.left(7));
    }
};

QTEST_MAIN(VersionManagerTest)
#include "tst_VersionManager.moc"
