#include "VersionManager.h"
#include "ApplicationSettings.h"
#include "ConfigurationManager.h"
#include "PortablePaths.h"
#include <QDir>
#include <QFile>
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
private slots:
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
