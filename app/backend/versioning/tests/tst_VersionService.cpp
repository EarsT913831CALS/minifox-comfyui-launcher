#include "ConfigurationManager.h"
#include "EntryModel.h"
#include "Git.h"
#include "PortablePaths.h"
#include "Repository.h"
#include "VersionService.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
}

}

// The service against local "remotes": a fetch shows up as a moved
// remote-tracking ref, so the tests can tell whether anything went online.
class VersionServiceTest final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void openingThePageStaysLocal();
    void refreshListFetchesTheCore();
    void refreshListChecksExtensions();

private:
    git::Result git(const QString &root, const QStringList &arguments);
    QString commit(const QString &root, const QString &message);
    QString resolve(const QString &root, const QString &ref);
    void makeRepository(const QString &upstream, const QString &clone);
    void startService();
    void waitIdle();

    std::unique_ptr<QTemporaryDir> m_temp;
    git::Tool m_tool;
    QString m_upstream;
    QString m_comfy;
    QString m_extensionUpstream;
    QString m_extension;
    std::unique_ptr<ConfigurationManager> m_configuration;
    std::unique_ptr<VersionService> m_service;
};

git::Result VersionServiceTest::git(const QString &root, const QStringList &arguments)
{
    return m_tool.run(root, arguments, {.identity = true});
}

QString VersionServiceTest::commit(const QString &root, const QString &message)
{
    git(root, {QStringLiteral("add"), QStringLiteral("-A")});
    git(root, {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), message});
    return resolve(root, QStringLiteral("HEAD"));
}

QString VersionServiceTest::resolve(const QString &root, const QString &ref)
{
    return repository::resolveCommit(m_tool, root, ref);
}

void VersionServiceTest::makeRepository(const QString &upstream, const QString &clone)
{
    QDir().mkpath(upstream);
    git(upstream, {QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("master")});
    git(upstream, {QStringLiteral("config"), QStringLiteral("core.autocrlf"), QStringLiteral("false")});
    writeFile(upstream + QStringLiteral("/main.py"), "print('base')\n");
    QVERIFY(!commit(upstream, QStringLiteral("base")).isEmpty());
    QVERIFY(git(m_temp->path(), {QStringLiteral("clone"), QStringLiteral("-q"), QStringLiteral("-c"),
                                 QStringLiteral("core.autocrlf=false"), upstream, clone})
                .ok());
}

void VersionServiceTest::init()
{
    m_temp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_temp->isValid());
    m_tool = git::Tool(git::Tool::locate({}), QProcessEnvironment::systemEnvironment());
    QVERIFY(m_tool.isValid());
    // Journals and the catalog cache live beside the executable.
    QDir(QDir(PortablePaths::dataDirectory()).filePath(QStringLiteral("versioning"))).removeRecursively();
    QFile::remove(QDir(PortablePaths::dataDirectory()).filePath(QStringLiteral("custom-node-list.json")));

    m_upstream = m_temp->filePath(QStringLiteral("upstream"));
    m_comfy = m_temp->filePath(QStringLiteral("ComfyUI"));
    makeRepository(m_upstream, m_comfy);
    m_extensionUpstream = m_temp->filePath(QStringLiteral("extension-upstream"));
    m_extension = m_comfy + QStringLiteral("/custom_nodes/demo-nodes");
    makeRepository(m_extensionUpstream, m_extension);
}

void VersionServiceTest::cleanup()
{
    m_service.reset();
    m_configuration.reset();
    m_temp.reset();
}

void VersionServiceTest::startService()
{
    m_configuration = std::make_unique<ConfigurationManager>(m_temp->filePath(QStringLiteral("launch-profiles.json")));
    m_configuration->setComfyRoot(m_comfy);
    m_service = std::make_unique<VersionService>(m_configuration.get(), nullptr);
    waitIdle();
}

void VersionServiceTest::waitIdle()
{
    QTRY_VERIFY_WITH_TIMEOUT(!m_service->busy(), 60000);
}

void VersionServiceTest::openingThePageStaysLocal()
{
    const QString known = resolve(m_comfy, QStringLiteral("origin/master"));
    const QString knownExtension = resolve(m_extension, QStringLiteral("origin/master"));
    writeFile(m_upstream + QStringLiteral("/main.py"), "print('newer')\n");
    commit(m_upstream, QStringLiteral("newer core"));
    writeFile(m_extensionUpstream + QStringLiteral("/main.py"), "print('newer')\n");
    commit(m_extensionUpstream, QStringLiteral("newer extension"));

    startService();
    QSignalSpy refreshed(m_service.get(), &VersionService::refreshCompleted);
    for (int tab : {0, 1, 2, 0}) {
        m_service->ensureLoaded(tab);
        QVERIFY(!m_service->catalogLoading());
        waitIdle();
    }

    QCOMPARE(m_service->commitFull(), resolve(m_comfy, QStringLiteral("HEAD")));
    QCOMPARE(m_service->installedExtensionModel()->rowCount(), 1);
    QCOMPARE(resolve(m_comfy, QStringLiteral("origin/master")), known);
    QCOMPARE(resolve(m_extension, QStringLiteral("origin/master")), knownExtension);
    QCOMPARE(refreshed.count(), 0);
}

void VersionServiceTest::refreshListFetchesTheCore()
{
    startService();
    writeFile(m_upstream + QStringLiteral("/main.py"), "print('newer')\n");
    const QString newer = commit(m_upstream, QStringLiteral("newer core"));

    QSignalSpy refreshed(m_service.get(), &VersionService::refreshCompleted);
    m_service->refreshCore();
    QTRY_COMPARE_WITH_TIMEOUT(refreshed.count(), 1, 60000);
    QVERIFY2(refreshed.at(0).at(0).toBool(), qPrintable(refreshed.at(0).at(1).toString()));
    QCOMPARE(resolve(m_comfy, QStringLiteral("origin/master")), newer);
    // Refreshing only fetches; the working tree stays where it was.
    QVERIFY(m_service->commitFull() != newer);
}

void VersionServiceTest::refreshListChecksExtensions()
{
    startService();
    m_service->ensureLoaded(1);
    waitIdle();
    writeFile(m_extensionUpstream + QStringLiteral("/main.py"), "print('newer')\n");
    const QString newer = commit(m_extensionUpstream, QStringLiteral("newer extension"));

    QSignalSpy refreshed(m_service.get(), &VersionService::refreshCompleted);
    m_service->refreshInstalledExtensions();
    QTRY_COMPARE_WITH_TIMEOUT(refreshed.count(), 1, 60000);
    QVERIFY2(refreshed.at(0).at(0).toBool(), qPrintable(refreshed.at(0).at(1).toString()));
    QCOMPARE(resolve(m_extension, QStringLiteral("origin/master")), newer);
    QCOMPARE(m_service->outdatedCount(), 1);
}

QTEST_GUILESS_MAIN(VersionServiceTest)
#include "tst_VersionService.moc"
