// Exercise the actual parser and publication boundary, including old snapshots.
#include "../ConfigurationPackageManager.cpp"
#include "DependencyInstaller.h"
#include "ExternalContent.h"
#include "OperationLease.h"
#include <QTextDocument>
#include <QTemporaryDir>
#include <QTest>
#include <QStandardPaths>
#include <QElapsedTimer>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
bool save(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}
QByteArray read(const QString &path) {
    QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
}
// Deliberately accepts aliases and duplicates, unlike the production writer.
QByteArray hostileZip(const QList<QPair<QString, QByteArray>> &entries) {
    QByteArray data, central;
    QDataStream out(&data, QIODevice::WriteOnly), dir(&central, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian); dir.setByteOrder(QDataStream::LittleEndian);
    for (const auto &[path, bytes] : entries) {
        const QByteArray name = path.toUtf8();
        const quint32 offset = data.size(), size = bytes.size(), crc = crc32Bytes(bytes);
        out << quint32(0x04034b50) << quint16(20) << quint16(0x800) << quint16(0)
            << quint16(0) << quint16(0) << crc << size << size << quint16(name.size()) << quint16(0);
        out.writeRawData(name.constData(), name.size()); out.writeRawData(bytes.constData(), bytes.size());
        dir << quint32(0x02014b50) << quint16(20) << quint16(20) << quint16(0x800)
            << quint16(0) << quint16(0) << quint16(0) << crc << size << size
            << quint16(name.size()) << quint16(0) << quint16(0) << quint16(0) << quint16(0)
            << quint32(0) << offset;
        dir.writeRawData(name.constData(), name.size());
    }
    const quint32 offset = data.size();
    out.writeRawData(central.constData(), central.size());
    out << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(entries.size())
        << quint16(entries.size()) << quint32(central.size()) << offset << quint16(0);
    return data;
}
}

class ConfigurationPackageTest : public QObject {
    Q_OBJECT
    QString root() const { return PortablePaths::dataDirectory(); }
private slots:
    void resourceLeaseAliasesAndRelease() {
        QTemporaryDir temporary;
        OperationLease first, second;
        QString error;
        QVERIFY(first.acquire({temporary.path()}, &error));
        QVERIFY(!second.acquire({temporary.path() + "/./"}, &error));
        QVERIFY(!error.isEmpty());
        first.release();
        QVERIFY(second.acquire({temporary.path()}, &error));
    }
    void externalContentBoundary() {
        for (const QString &url : {"file:///C:/probe.exe", "javascript:alert(1)", "ms-settings:privacy", "https://user:password@example.com", "https://example.com/%0aevil", "https://example.com/%5cevil"})
            QVERIFY2(ExternalContent::repositoryUrl(url).isEmpty(), qPrintable(url));
        QCOMPARE(ExternalContent::repositoryUrl("git@github.com:owner/repo.git").toString(), QString("https://github.com/owner/repo"));
        QCOMPARE(ExternalContent::repositoryUrl("ssh://git@github.com/owner/repo.git").toString(), QString("https://github.com/owner/repo"));
        const QString html = ExternalContent::markdown("**bold** [safe](https://example.com) ![remote](https://example.com/leak) ![local](file:///C:/secret) [bad](file:///C:/run.exe) <img src='https://example.com/raw'>");
        QVERIFY(!html.contains("<img", Qt::CaseInsensitive));
        QVERIFY(!html.contains("file:///"));
        QVERIFY(html.contains("https://example.com"));
        QTextDocument document; document.setHtml(html);
        QVERIFY(document.toPlainText().contains("bold"));
    }
    void sharingStripsFreeformData() {
        ConfigurationManager configuration;
        configuration.setCustomArguments("--token SENTINEL_ARGS");
        configuration.setParameterValue("databaseUrl", "postgres://user:SENTINEL_DB@host/db");
        const int row = configuration.addEnvironmentEntry();
        configuration.updateEnvironmentEntry(row, "ORDINARY", "SENTINEL_ENV", true);
        configuration.setParameterValue("port", 9000);
        ConfigurationPackageManager packages(&configuration);
        const QByteArray settings = R"({"proxyUrl":"https://user:SENTINEL_PROXY@host"})";
        QVERIFY(save(QDir(root()).filePath("application-settings.json"), settings));
        QTemporaryDir temporary;
        const QString zip = temporary.filePath("share.zip");
        QVERIFY2(packages.exportPackage(QUrl::fromLocalFile(zip), false), qPrintable(packages.lastError()));
        QHash<QString, QByteArray> files; QString error;
        QVERIFY(readStoreZip(zip, &files, &error));
        QCOMPARE(files.size(), 2);
        QVERIFY(!files.value("profile.json").contains("SENTINEL"));
        const auto profile = QJsonDocument::fromJson(files.value("profile.json")).object();
        QCOMPARE(profile.value("parameters").toObject().value("port").toInt(), 9000);
        QVERIFY(packages.exportPackage(QUrl::fromLocalFile(temporary.filePath("full.zip")), true));
        QVERIFY(readStoreZip(temporary.filePath("full.zip"), &files, &error));
        QVERIFY(files.value("profile.json").contains("SENTINEL_ENV"));
        QVERIFY(files.value(".minifox/application-settings.json").contains("SENTINEL_PROXY"));
        QVERIFY(packages.importPackage(QUrl::fromLocalFile(zip)));
        QVERIFY(packages.switchProfile(configuration.profileNames().size() - 1));
        QCOMPARE(read(QDir(root()).filePath("application-settings.json")), settings);
    }
    void init() {
        // This executable has its own test build output directory.
        QVERIFY(QDir(root()).removeRecursively());
        QVERIFY(PortablePaths::ensureDataDirectory());
    }
    void cleanup() { QVERIFY(QDir(root()).removeRecursively()); }
    void rejectsPaths_data() {
        QTest::addColumn<QString>("name");
        const QStringList paths{
            ".minifox/runtime/probe.py", ".minifox/./runtime/probe.py", ".minifox//runtime/probe.py",
            ".MINIFOX/RUNTIME/probe.py", ".minifox/runtime./probe.py", ".minifox/runtime /probe.py",
            ".minifox/skins/a/assets/NUL.png", ".minifox/skins/a/assets/COM¹.png",
            ".minifox/skins/a/assets/x.png:payload", ".minifox/skins/a/assets/../x.png",
            ".minifox\\runtime\\probe.py", ".minifox/skins/a/assets/X~1.png",
            ".minifox/packages/x.dll", ".minifox/launch-profiles.json",
            ".minifox/version-operation.json", ".minifox/recovery/x.pack",
            ".minifox/configuration-state.pending.zip", ".minifox/skins/a/assets/script.py",
            ".minifox/application-settings.json ", "C:/payload.json", "/payload.json"};
        for (const QString &name : paths) QTest::newRow(qPrintable(name)) << name;
    }
    void rejectsPaths() {
        QFETCH(QString, name);
        QTemporaryDir temp;
        const QString zip = temp.filePath("hostile.zip");
        const QList<QPair<QString, QByteArray>> entries{
            {"manifest.json", R"({"format":"minifox-configuration","schemaVersion":1})"},
            {"profile.json", R"({"name":"test"})"}, {name, "payload"}};
        QVERIFY(save(zip, hostileZip(entries)));
        QVERIFY(save(PortablePaths::versionOperationFile(), "interrupted"));
        QVERIFY(save(QDir(root()).filePath("runtime/probe.py"), "original"));
        ConfigurationManager config;
        ConfigurationPackageManager manager(&config);
        QVERIFY(!manager.importPackage(QUrl::fromLocalFile(zip)));
        QCOMPARE(read(PortablePaths::versionOperationFile()), QByteArray("interrupted"));
        QCOMPARE(read(QDir(root()).filePath("runtime/probe.py")), QByteArray("original"));
    }
    void duplicateAndCaseAliasesRejected() {
        QTemporaryDir temp;
        for (const QString &second : {QString("profile.json"), QString("PROFILE.JSON")}) {
            const QString zip = temp.filePath("duplicate.zip");
            QVERIFY(save(zip, hostileZip({{"profile.json", "one"}, {second, "two"}})));
            QHash<QString, QByteArray> files; QString error;
            QVERIFY(!readStoreZip(zip, &files, &error)); QVERIFY(!error.isEmpty());
        }
    }
    void portableStatePreservesOperationalFiles() {
        const QHash<QString, QByteArray> operational{{"version-operation.json", "interrupted"},
            {"recovery/backup.pack", "private backup"}, {"runtime/bridge.py", "code"},
            {"dep-old.bat", "old installer"}, {"packages/archive.extpack", "package"}};
        for (auto it = operational.begin(); it != operational.end(); ++it)
            QVERIFY(save(QDir(root()).filePath(it.key()), it.value()));
        QVERIFY(save(QDir(root()).filePath("application-settings.json"), "old"));
        QHash<QString, QByteArray> collected; QString error;
        QVERIFY(collectPortableState(&collected, &error)); QCOMPARE(collected.size(), 1);
        const QHash<QString, QByteArray> target{{"application-settings.json", "new"},
            {"skins/test/skin.json", "{}"}, {"skins/test/assets/photo.png", "image"},
            {"skins/test/assets/backgrounds/PHOTO.PNG", "nested image"}};
        QVERIFY2(writePortableState(target, &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(pendingStatePath()));
        collected.clear(); QVERIFY(collectPortableState(&collected, &error)); QCOMPARE(collected, target);
        QVERIFY(writePortableState({}, &error));
        for (auto it = operational.begin(); it != operational.end(); ++it)
            QCOMPARE(read(QDir(root()).filePath(it.key())), it.value());
    }
    void recoveryAfterEachPartialStage_data() {
        QTest::addColumn<int>("stage");
        for (int i = 0; i != 4; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void recoveryAfterEachPartialStage() {
        QFETCH(int, stage);
        const QString settings = QDir(root()).filePath("application-settings.json");
        const QString image = QDir(root()).filePath("icons/custom.png");
        QVERIFY(save(settings, "before")); QVERIFY(save(image, "old image"));
        const QByteArray originalProfiles = R"({"currentProfileId":"old","profiles":[]})";
        QVERIFY(save(PortablePaths::configurationFile(), originalProfiles));
        const QJsonObject meta{{"format", "minifox-state-rollback-v1"},
            {"restoreProfiles", true},
            {"remove", QJsonArray{"application-settings.json", "skins/new/skin.json"}}};
        QString error;
        QVERIFY(writeStoreZip(pendingStatePath(), {{"manifest.json", QJsonDocument(meta).toJson()},
            {"profiles-before.json", originalProfiles},
            {".minifox/application-settings.json", "before"}, {".minifox/icons/custom.png", "old image"}}, &error));
        if (stage >= 1) QVERIFY(save(settings, "after"));
        if (stage >= 2) QVERIFY(save(QDir(root()).filePath("skins/new/skin.json"), "new"));
        if (stage >= 3) QVERIFY(QFile::remove(image));
        if (stage >= 3) QVERIFY(save(PortablePaths::configurationFile(), R"({"currentProfileId":"new","profiles":[]})"));
        QVERIFY2(ConfigurationPackageManager::recoverPendingState(&error), qPrintable(error));
        QCOMPARE(read(settings), QByteArray("before")); QCOMPARE(read(image), QByteArray("old image"));
        QCOMPARE(read(PortablePaths::configurationFile()), originalProfiles);
        QVERIFY(!QFileInfo::exists(QDir(root()).filePath("skins/new/skin.json")));
        QVERIFY(ConfigurationPackageManager::recoverPendingState(&error));
    }
    void corruptRecoveryBlocksChanges() {
        QVERIFY(save(pendingStatePath(), "broken journal"));
        QString error;
        QVERIFY(!writePortableState({{"application-settings.json", "new"}}, &error));
        QCOMPARE(read(pendingStatePath()), QByteArray("broken journal"));
        QVERIFY(!QFileInfo::exists(QDir(root()).filePath("application-settings.json")));
    }
    void replacementDeniedKeepsRecoverablePreimage() {
#ifdef Q_OS_WIN
        const QString settings = QDir(root()).filePath("application-settings.json");
        QVERIFY(save(settings, "before"));
        const HANDLE held = CreateFileW(reinterpret_cast<LPCWSTR>(settings.utf16()), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(held != INVALID_HANDLE_VALUE);
        QString error;
        const bool changed = writePortableState({{"application-settings.json", "after"}}, &error);
        CloseHandle(held);
        QVERIFY(!changed); QVERIFY(!error.isEmpty());
        QCOMPARE(read(settings), QByteArray("before"));
        QVERIFY(QFileInfo::exists(pendingStatePath()));
        QVERIFY2(ConfigurationPackageManager::recoverPendingState(&error), qPrintable(error));
        QCOMPARE(read(settings), QByteArray("before"));
#endif
    }
    void actualProcessKillRecoversPublishedFiles() {
        const QString settings = QDir(root()).filePath("application-settings.json");
        QVERIFY(save(settings, "original"));
        QVERIFY(save(PortablePaths::versionOperationFile(), "version operation"));
        QProcess child; child.start(QCoreApplication::applicationFilePath(), {"--apply-state-child"});
        QVERIFY(child.waitForStarted());
        QElapsedTimer timer; timer.start(); bool observedWrite = false;
        const QString assets = QDir(root()).filePath("skins/stress/assets");
        while (timer.elapsed() < 15000 && child.state() != QProcess::NotRunning) {
            if (QFileInfo::exists(pendingStatePath()) && !QDir(assets).entryList({"*.png"}, QDir::Files).isEmpty()) {
                observedWrite = true; child.kill(); break;
            }
            child.waitForFinished(1);
        }
        if (child.state() != QProcess::NotRunning) { child.kill(); child.waitForFinished(); }
        QVERIFY2(observedWrite, "Must kill the real publisher after a destination file becomes visible");
        QVERIFY(QFileInfo::exists(pendingStatePath()));
        QString error; QVERIFY2(ConfigurationPackageManager::recoverPendingState(&error), qPrintable(error));
        QCOMPARE(read(settings), QByteArray("original"));
        QCOMPARE(read(PortablePaths::versionOperationFile()), QByteArray("version operation"));
        QVERIFY(QDir(assets).entryList({"*.png"}, QDir::Files).isEmpty());
    }
    void validPackageRoundTripAndSwitch() {
        ConfigurationManager config; ConfigurationPackageManager manager(&config);
        QVERIFY(save(QDir(root()).filePath("application-settings.json"), R"({"language":"zh_CN"})"));
        QVERIFY(save(PortablePaths::versionOperationFile(), "keep transaction"));
        QTemporaryDir temp;
        QVERIFY2(manager.exportPackage(QUrl::fromLocalFile(temp.filePath("valid.zip")), true), qPrintable(manager.lastError()));
        QVERIFY2(manager.importPackage(QUrl::fromLocalFile(temp.filePath("valid.zip"))), qPrintable(manager.lastError()));
        QVERIFY(save(QDir(root()).filePath("application-settings.json"), R"({"language":"en"})"));
        QVERIFY2(manager.switchProfile(1), qPrintable(manager.lastError()));
        QCOMPARE(config.currentProfileIndex(), 1);
        QCOMPARE(read(QDir(root()).filePath("application-settings.json")), QByteArray(R"({"language":"zh_CN"})"));
        QCOMPARE(read(PortablePaths::versionOperationFile()), QByteArray("keep transaction"));
        QVERIFY2(manager.switchProfile(0), qPrintable(manager.lastError()));
        QCOMPARE(read(QDir(root()).filePath("application-settings.json")), QByteArray(R"({"language":"en"})"));
    }
    void junctionAndReplacementRejected() {
#ifdef Q_OS_WIN
        QTemporaryDir outside;
        const QString junction = QDir(root()).filePath("skins");
        QProcess p; p.start("cmd.exe", {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(junction),
                                       QDir::toNativeSeparators(outside.path())});
        QVERIFY(p.waitForFinished()); QCOMPARE(p.exitCode(), 0);
        QString error;
        QVERIFY(!writePortableState({{"skins/a/skin.json", "payload"}}, &error));
        QVERIFY(!QFileInfo::exists(outside.filePath("a/skin.json")));
        QVERIFY(QDir().rmdir(junction));
        QVERIFY(QDir().mkpath(junction));
        {
            SafeDataPath guard;
            QVERIFY(guard.lock(junction + "/a/skin.json", &error));
            QVERIFY(!QDir().rename(junction, junction + "-moved"));
        }
        QVERIFY(QDir().rename(junction, junction + "-moved"));
#endif
    }
    void dependencyArgumentsAreLiteral() {
        QTemporaryDir temp;
        const QString directory = temp.filePath("插件 & echo INJECTED %PATH% !x! ^ (test)");
        QVERIFY(QDir().mkpath(directory + "/pip"));
        const QString result = temp.filePath("arguments.json");
        QVERIFY(save(directory + "/pip/__init__.py", ""));
        QVERIFY(save(directory + "/pip/__main__.py",
            "import json,os,sys\nopen(os.environ['MINIFOX_TEST_RESULT'],'w',encoding='utf-8').write(json.dumps(sys.argv[1:]))\n"));
        const QString requirements = directory + "/requirements %PATH% !x! & ^.txt";
        QVERIFY(save(requirements, ""));
        QProcess process;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("PYTHONPATH", directory); env.insert("MINIFOX_TEST_RESULT", result);
        process.setProcessEnvironment(env); process.setWorkingDirectory(directory);
        const QString python = QStandardPaths::findExecutable("python"); QVERIFY(!python.isEmpty());
        DependencyInstaller::configure(process, python, requirements);
        process.start(); QVERIFY(process.waitForFinished(15000)); QCOMPARE(process.exitCode(), 0);
        const QJsonArray args = QJsonDocument::fromJson(read(result)).array();
        QCOMPARE(args, QJsonArray({"install", "-r", requirements}));
        QVERIFY(!QFileInfo::exists(directory + "/INJECTED"));
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--apply-state-child")) {
        QHash<QString, QByteArray> target{{"application-settings.json", "after"}};
        for (int i = 0; i < 512; ++i)
            target.insert(QString("skins/stress/assets/%1.png").arg(i), QByteArray(4096, 'x'));
        QString error; return writePortableState(target, &error) ? 0 : 1;
    }
    ConfigurationPackageTest test; return QTest::qExec(&test, argc, argv);
}
#include "tst_ConfigurationPackageManager.moc"
