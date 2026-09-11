#include "RepositoryUpdateJob.h"
#include "RepositoryUpdateCoordinator.h"
#include "SafeMergeEngine.h"
#include "VersionTransactionStore.h"
#include "RepositoryRecovery.h"
#include "OperationLease.h"

#include "PortablePaths.h"

#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>
#include <vector>
#include <cstdlib>

namespace {

class GitTraceScope
{
public:
    explicit GitTraceScope(const QString &path)
        : wasSet(qEnvironmentVariableIsSet("GIT_TRACE2_EVENT")), previous(qgetenv("GIT_TRACE2_EVENT"))
    { qputenv("GIT_TRACE2_EVENT", path.toUtf8()); }
    ~GitTraceScope()
    {
        if (wasSet) qputenv("GIT_TRACE2_EVENT", previous);
        else qunsetenv("GIT_TRACE2_EVENT");
    }
private:
    bool wasSet;
    QByteArray previous;
};

int textDiffCount(const QByteArray &trace)
{
    int count = 0;
    for (const QByteArray &line : trace.split('\n')) {
        const QJsonObject event = QJsonDocument::fromJson(line).object();
        if (event.value("event") == "start"
            && event.value("argv").toArray().contains(QStringLiteral("--no-index"))) ++count;
    }
    return count;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(data) == data.size()
        && file.commit();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

QByteArray normalizedLineEndings(QByteArray data)
{
    data.replace("\r\n", "\n");
    return data;
}

struct GitResult {
    bool success = false;
    QByteArray output;
};

GitResult runGit(const QString &git, const QString &root,
                 const QStringList &arguments)
{
    QProcess process;
    process.setProgram(git);
    QStringList processArguments;
    if (!root.isEmpty()) processArguments.append({QStringLiteral("-C"), root});
    processArguments.append(arguments);
    process.setArguments(processArguments);
    process.start();
    if (!process.waitForStarted(5000) || !process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    const QByteArray output = process.readAllStandardOutput()
        + process.readAllStandardError();
    return {process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            output};
}

class RepositoryFixture
{
public:
    explicit RepositoryFixture(const QString &git)
        : git(git)
    {
    }

    bool create(const QByteArray &initialData)
    {
        if (!directory.isValid()) return false;
        remote = QDir(directory.path()).filePath(QStringLiteral("remote.git"));
        seed = QDir(directory.path()).filePath(QStringLiteral("seed"));
        working = QDir(directory.path()).filePath(QStringLiteral("working"));
        updater = QDir(directory.path()).filePath(QStringLiteral("updater"));
        if (!runGit(git, {}, {QStringLiteral("init"), QStringLiteral("--bare"),
                              QStringLiteral("--initial-branch=main"), remote}).success
            || !runGit(git, {}, {QStringLiteral("clone"), remote, seed}).success
            || !configure(seed)
            || !writeFile(QDir(seed).filePath(QStringLiteral("sample.txt")), initialData)
            || !runGit(git, seed, {QStringLiteral("add"), QStringLiteral("sample.txt")}).success
            || !runGit(git, seed, {QStringLiteral("commit"), QStringLiteral("-m"),
                                   QStringLiteral("initial")}).success
            || !runGit(git, seed, {QStringLiteral("push"), QStringLiteral("-u"),
                                   QStringLiteral("origin"), QStringLiteral("main")}).success
            || !runGit(git, {}, {QStringLiteral("clone"), remote, working}).success
            || !runGit(git, {}, {QStringLiteral("clone"), remote, updater}).success) {
            return false;
        }
        return configure(working) && configure(updater);
    }

    bool publish(const QByteArray &data)
    {
        return writeFile(QDir(updater).filePath(QStringLiteral("sample.txt")), data)
            && runGit(git, updater, {QStringLiteral("add"), QStringLiteral("sample.txt")}).success
            && runGit(git, updater, {QStringLiteral("commit"), QStringLiteral("-m"),
                                     QStringLiteral("remote update")}).success
            && runGit(git, updater, {QStringLiteral("push")}).success;
    }

    bool configure(const QString &root) const
    {
        return runGit(git, root, {QStringLiteral("config"), QStringLiteral("user.name"),
                                  QStringLiteral("Minifox Test")}).success
            && runGit(git, root, {QStringLiteral("config"), QStringLiteral("user.email"),
                                  QStringLiteral("minifox@example.invalid")}).success;
    }

    QString git;
    QTemporaryDir directory;
    QString remote;
    QString seed;
    QString working;
    QString updater;
};

struct JobRun {
    bool received = false;
    RepositoryUpdateResult result;
};

JobRun runJob(const RepositoryUpdateSpec &spec)
{
    RepositoryUpdateJob job(spec);
    QEventLoop loop;
    JobRun run;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&job, &RepositoryUpdateJob::finished, &loop,
                     [&](const RepositoryUpdateResult &result) {
        run.received = true;
        run.result = result;
        loop.quit();
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(30000);
    job.start();
    loop.exec();
    if (!run.received) job.cancel();
    return run;
}

RepositoryUpdateSpec updateSpec(const RepositoryFixture &fixture)
{
    return {RepositoryKind::Extension, RepositoryTarget::CurrentBranch,
            fixture.working, QStringLiteral("Test Extension"), QStringLiteral("main"),
            fixture.git, {}};
}

} // namespace

class RepositoryUpdateTest final : public QObject
{
    Q_OBJECT

private slots:
    void resourceLeaseBlocksAnotherProcess() {
        QTemporaryDir temp;
        OperationLease lease; QString error;
        QVERIFY(lease.acquire({temp.path()}, &error));
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--lease-probe", temp.path()});
        QVERIFY(child.waitForFinished(5000)); QCOMPARE(child.exitCode(), 31);
        lease.release();
        child.start(QCoreApplication::applicationFilePath(), {"--lease-probe", temp.path()});
        QVERIFY(child.waitForFinished(5000)); QCOMPARE(child.exitCode(), 0);
    }
    void sharedJunctionPluginConflictsAcrossRoots() {
#ifdef Q_OS_WIN
        QTemporaryDir temp;
        for (const QString &path : {"A/custom_nodes", "B/custom_nodes", "shared"})
            QVERIFY(QDir().mkpath(temp.filePath(path)));
        for (const QString &root : {QString("A"), QString("B")}) {
            QProcess child;
            child.start("cmd.exe", {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(temp.filePath(root + "/custom_nodes/shared")), QDir::toNativeSeparators(temp.filePath("shared"))});
            QVERIFY(child.waitForFinished(5000)); QCOMPARE(child.exitCode(), 0);
        }
        OperationLease running, updating; QString error;
        QCOMPARE(OperationLease::canonicalResource(temp.filePath("A/custom_nodes/shared")),
                 OperationLease::canonicalResource(temp.filePath("shared")));
        QVERIFY(running.acquire(OperationLease::repositoryResources(temp.filePath("A"), temp.filePath("pythonA/python.exe")), &error));
        QVERIFY(!updating.acquire(OperationLease::repositoryResources(temp.filePath("B"), temp.filePath("pythonB/python.exe")), &error));
        running.release();
        QVERIFY(updating.acquire(OperationLease::repositoryResources(temp.filePath("B"), temp.filePath("pythonB/python.exe")), &error));
        updating.release();
        QVERIFY(QDir().rmdir(temp.filePath("A/custom_nodes/shared")));
        QVERIFY(QDir().rmdir(temp.filePath("B/custom_nodes/shared")));
#endif
    }
    void mergeRejectsJunctionAncestor() {
#ifdef Q_OS_WIN
        QTemporaryDir temp;
        for (const QString &dir : {"base", "local", "target/nested", "working", "outside"})
            QVERIFY(QDir().mkpath(temp.filePath(dir)));
        QVERIFY(writeFile(temp.filePath("target/nested/new.txt"), "remote"));
        QProcess process;
        process.start("cmd.exe", {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(temp.filePath("working/nested")), QDir::toNativeSeparators(temp.filePath("outside"))});
        QVERIFY(process.waitForFinished(5000)); QCOMPARE(process.exitCode(), 0);
        const auto result = SafeMergeEngine::run({m_git, temp.filePath("working"), temp.filePath("base"), temp.filePath("local"), temp.filePath("target"), {}});
        QVERIFY(!result.success);
        QVERIFY(!QFileInfo::exists(temp.filePath("outside/new.txt")));
        QVERIFY(QDir().rmdir(temp.filePath("working/nested")));
        const auto valid = SafeMergeEngine::run({m_git, temp.filePath("working"), temp.filePath("base"), temp.filePath("local"), temp.filePath("target"), {}});
        QVERIFY(valid.success);
        QCOMPARE(readFile(temp.filePath("working/nested/new.txt")), QByteArray("remote"));
#endif
    }
    void recoveryInheritsGitContext() {
        RepositoryFixture fixture(m_git); QVERIFY(fixture.create("base"));
        const QString head = QString::fromLatin1(runGit(m_git, fixture.working, {"rev-parse", "HEAD"}).output.trimmed());
        VersionTransactionEntry entry{"context", "extension-update", fixture.working, "Test", head, {}, head, "preparing"};
        QVERIFY(VersionTransactionStore::upsert(entry));
        QTemporaryDir temp;
        RepositoryGitContext context;
        context.prefixArguments = {"-c", "minifox.context=passed"};
        context.environment.insert("GIT_TRACE2_EVENT", temp.filePath("trace.log"));
        context.environment.insert("GIT_TRACE2_CONFIG_PARAMS", "minifox.context");
        QString error;
        QVERIFY2(RepositoryRecovery::prepare(m_git, entry, {}, &error, context), qPrintable(error));
        entry = VersionTransactionStore::entries().constFirst();
        QVERIFY2(RepositoryRecovery::verify(m_git, entry, &error, context), qPrintable(error));
        QVERIFY2(RepositoryRecovery::rollback(m_git, entry, &error, context), qPrintable(error));
        QVERIFY(readFile(temp.filePath("trace.log")).contains("passed"));
    }
    void initTestCase()
    {
        m_git = QStandardPaths::findExecutable(QStringLiteral("git.exe"));
        if (m_git.isEmpty()) m_git = QStandardPaths::findExecutable(QStringLiteral("git"));
        QVERIFY2(!m_git.isEmpty(), "Git is required for repository update tests");
    }

    void init()
    {
        QFile::remove(PortablePaths::versionOperationFile());
    }

    void sparseSnapshotPlanHandlesAddedAndDeletedPaths()
    {
        const auto nulSeparated = [](const QList<QByteArray> &fields) {
            QByteArray result;
            for (const QByteArray &field : fields) {
                result.append(field);
                result.append('\0');
            }
            return result;
        };
        const QByteArray status = nulSeparated({
            " M local-and-remote.txt", " D local-deleted.txt", "A  local-added.txt"});
        const QByteArray remote = nulSeparated({
            "M", "local-and-remote.txt", "A", "remote-added.txt",
            "D", "remote-deleted.txt"});

        const QList<LocalPathEntry> localEntries =
            SafeMergeEngine::parseStatusEntries(status);
        const QList<RemotePathEntry> remoteEntries =
            SafeMergeEngine::parseNameStatusEntries(remote);
        QCOMPARE(localEntries.size(), 3);
        QCOMPARE(remoteEntries.size(), 3);

        const SparseSnapshotPaths plan =
            SafeMergeEngine::planSnapshots(localEntries, remoteEntries);
        QCOMPARE(plan.basePaths.size(), 2);
        QCOMPARE(plan.localPaths.size(), 2);
        QCOMPARE(plan.targetPaths.size(), 2);

        QVERIFY(plan.basePaths.contains(QStringLiteral("local-and-remote.txt")));
        QVERIFY(plan.localPaths.contains(QStringLiteral("local-and-remote.txt")));
        QVERIFY(plan.targetPaths.contains(QStringLiteral("local-and-remote.txt")));

        QVERIFY(!plan.basePaths.contains(QStringLiteral("local-deleted.txt")));
        QVERIFY(!plan.localPaths.contains(QStringLiteral("local-deleted.txt")));
        QVERIFY(!plan.targetPaths.contains(QStringLiteral("local-deleted.txt")));

        QVERIFY(!plan.basePaths.contains(QStringLiteral("local-added.txt")));
        QVERIFY(!plan.localPaths.contains(QStringLiteral("local-added.txt")));
        QVERIFY(!plan.targetPaths.contains(QStringLiteral("local-added.txt")));

        QVERIFY(!plan.basePaths.contains(QStringLiteral("remote-added.txt")));
        QVERIFY(!plan.localPaths.contains(QStringLiteral("remote-added.txt")));
        QVERIFY(plan.targetPaths.contains(QStringLiteral("remote-added.txt")));

        QVERIFY(plan.basePaths.contains(QStringLiteral("remote-deleted.txt")));
        QVERIFY(plan.localPaths.contains(QStringLiteral("remote-deleted.txt")));
        QVERIFY(!plan.targetPaths.contains(QStringLiteral("remote-deleted.txt")));

        QByteArray expected("a.txt");
        expected += '\0';
        expected += QStringLiteral("目录/b.txt").toUtf8();
        expected += '\0';
        QCOMPARE(SafeMergeEngine::encodePathList(
                     {QStringLiteral("a.txt"), QStringLiteral("目录/b.txt")}),
                 expected);
    }

    void cleanRepositoryUsesDurableSparseUpdate()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("alpha\nbeta\n"));
        QVERIFY(fixture.publish("alpha\nbeta\nremote\n"));

        const JobRun run = runJob(updateSpec(fixture));
        QVERIFY(run.received);
        QVERIFY2(run.result.success, qPrintable(run.result.error));
        QCOMPARE(normalizedLineEndings(readFile(
                     QDir(fixture.working).filePath(QStringLiteral("sample.txt")))),
                 QByteArray("alpha\nbeta\nremote\n"));
        QVERIFY(VersionTransactionStore::entries().isEmpty());
    }

    void coreDevelopmentUpdateUsesTheSameRepositoryJob()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("core-base\n"));
        QVERIFY(fixture.publish("core-remote\n"));
        RepositoryUpdateSpec spec {
            RepositoryKind::Core, RepositoryTarget::Development,
            fixture.working, QStringLiteral("ComfyUI Core"), QStringLiteral("main"),
            fixture.git, {}
        };

        const JobRun run = runJob(spec);
        QVERIFY(run.received);
        QVERIFY2(run.result.success, qPrintable(run.result.error));
        const GitResult branch = runGit(m_git, fixture.working,
                                        {QStringLiteral("branch"), QStringLiteral("--show-current")});
        QVERIFY(branch.success);
        QCOMPARE(branch.output.trimmed(), QByteArray("dev"));
        QCOMPARE(normalizedLineEndings(readFile(
                     QDir(fixture.working).filePath(QStringLiteral("sample.txt")))),
                 QByteArray("core-remote\n"));
        QVERIFY(VersionTransactionStore::entries().isEmpty());
    }

    void dirtyRepositoryPreservesLocalLinesAndAppliesRemoteLines()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("alpha\nbeta\ngamma\n"));
        QVERIFY(writeFile(QDir(fixture.working).filePath(QStringLiteral("sample.txt")),
                          "alpha\nlocal-beta\ngamma\n"));
        QVERIFY(fixture.publish("alpha\nbeta\nremote-gamma\n"));

        const JobRun run = runJob(updateSpec(fixture));
        QVERIFY(run.received);
        QVERIFY2(run.result.success, qPrintable(run.result.error));
        QCOMPARE(readFile(QDir(fixture.working).filePath(QStringLiteral("sample.txt"))),
                 QByteArray("alpha\nlocal-beta\nremote-gamma\n"));
        QVERIFY(VersionTransactionStore::entries().isEmpty());
    }

    void localDeletionAndReplacementWinsOnlyInsideItsChangedRange()
    {
        RepositoryFixture fixture(m_git);
        const QByteArray base(
            "line-0\nline-1\nline-2\nline-3\nline-4\nline-5\n"
            "line-6\nline-7\nline-8\nline-9\n");
        QVERIFY(fixture.create(base));
        QVERIFY(writeFile(QDir(fixture.working).filePath(QStringLiteral("sample.txt")),
                          "line-0\nlocal-block\nline-6\nline-7\nline-8\nline-9\n"));
        QVERIFY(writeFile(QDir(fixture.working).filePath(QStringLiteral("private.txt")),
                          "untracked-private-data\n"));
        QVERIFY(fixture.publish(
            "remote-0\nline-1\nline-2\nremote-inside-local-range\nline-4\nline-5\n"
            "line-6\nline-7\nremote-8\nline-9\n"));

        const JobRun run = runJob(updateSpec(fixture));
        QVERIFY(run.received);
        QVERIFY2(run.result.success, qPrintable(run.result.error));
        QCOMPARE(readFile(QDir(fixture.working).filePath(QStringLiteral("sample.txt"))),
                 QByteArray("remote-0\nlocal-block\nline-6\nline-7\nremote-8\nline-9\n"));
        QCOMPARE(readFile(QDir(fixture.working).filePath(QStringLiteral("private.txt"))),
                 QByteArray("untracked-private-data\n"));
        QVERIFY(VersionTransactionStore::entries().isEmpty());
    }


    void batchDiffPreservesMergeSemanticsAndHandlesPatchLikeContent()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QDir root(directory.path());
        for (const QString &name : {"base", "local", "target", "working"}) {
            QVERIFY(root.mkpath(name + "/目录"));
        }
        struct Case { QByteArray base, local, remote, expected; };
        const QList<Case> cases {
            {"a\nb\nc\n", "a\nlocal\nc\n", "a\nb\nremote\n", "a\nlocal\nremote\n"},
            {"a\nb\nc\n", "a\nlocal\nc\n", "a\nremote\nc\n", "a\nlocal\nc\n"},
            {"a\nb\n", "a\nlocal\nb\n", "a\nremote\nb\n", "a\nlocal\nb\n"},
            {"a\nb\nc\n", "a\nlocal\nc\n", "a\ninsert\nb\nc\n", "a\ninsert\nlocal\nc\n"},
            {"a\nb\nc\n", "a\nlocal\nc\n", "a\nb\ninsert\nc\n", "a\nlocal\ninsert\nc\n"},
            {"a\nb\nc\nd\n", "a\nb\nlocal\nc\nd\n", "a\nremote\nd\n", "a\nb\nlocal\nc\nd\n"},
            {"a\nb\nc\nd\n", "a\nd\n", "remote\nb\nc\nd\n", "remote\nd\n"},
            {"a\r\nb\r\nc\r\n", "a\r\nlocal\r\nc\r\n", "a\r\nb\r\nremote\r\n", "a\r\nlocal\r\nremote\r\n"},
            {"a\nb\nc", "a\nlocal\nc", "a\nb\nremote", "a\nlocal\nremote"},
            {"a\nb\nc\nd\ne\nf\n", "a\n++ local/999\nc\nd\ne\nlocal-end\n",
             "a\nb\nc\nremote\ne\nf\n", "a\n++ local/999\nc\nremote\ne\nlocal-end\n"},
            {"", "local\n", "remote\n", "local\n"}
        };
        for (qsizetype index = 0; index < cases.size(); ++index) {
            const QString name = QStringLiteral("目录/[file %1] +++.txt").arg(index);
            const Case &c = cases[index];
            QVERIFY(writeFile(root.filePath("base/" + name), c.base));
            QVERIFY(writeFile(root.filePath("local/" + name), c.local));
            QVERIFY(writeFile(root.filePath("target/" + name), c.remote));
            QVERIFY(writeFile(root.filePath("working/" + name), c.local));
        }
        const QString tracePath = root.filePath("trace.json");
        GitTraceScope trace(tracePath);
        const SafeMergeResult result = SafeMergeEngine::run({m_git, root.filePath("working"),
            root.filePath("base"), root.filePath("local"), root.filePath("target")});
        QVERIFY2(result.success, qPrintable(result.detail));
        for (qsizetype index = 0; index < cases.size(); ++index) {
            const QString name = QStringLiteral("working/目录/[file %1] +++.txt").arg(index);
            QCOMPARE(readFile(root.filePath(name)), cases[index].expected);
        }
        QCOMPARE(textDiffCount(readFile(tracePath)), 2);
    }

    void deletedAndLocalOnlyFilesSurviveSparseUpdate()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("alpha\nbeta\n"));
        QVERIFY(writeFile(QDir(fixture.working).filePath("local-only.txt"), "original\n"));
        QVERIFY(runGit(m_git, fixture.working, {"add", "."}).success);
        QVERIFY(runGit(m_git, fixture.working, {"commit", "-m", "local-only base"}).success);
        QVERIFY(runGit(m_git, fixture.working, {"push"}).success);
        QVERIFY(runGit(m_git, fixture.updater, {"pull", "--ff-only"}).success);
        QVERIFY(writeFile(QDir(fixture.working).filePath("local-only.txt"), "private local edit\n"));
        QVERIFY(QFile::remove(QDir(fixture.working).filePath("sample.txt")));
        QVERIFY(fixture.publish("remote\nbeta\n"));
        const JobRun run = runJob(updateSpec(fixture));
        QVERIFY(run.received);
        QVERIFY2(run.result.success, qPrintable(run.result.error));
        QVERIFY(!QFile::exists(QDir(fixture.working).filePath("sample.txt")));
        QCOMPARE(readFile(QDir(fixture.working).filePath("local-only.txt")), QByteArray("private local edit\n"));
    }

    void currentTargetSkipsSnapshotsAndKeepsStagedChanges()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("alpha\n"));
        QVERIFY(writeFile(QDir(fixture.working).filePath("sample.txt"), "local\n"));
        QVERIFY(runGit(m_git, fixture.working, {"add", "sample.txt"}).success);
        const QByteArray before = runGit(m_git, fixture.working, {"write-tree"}).output;
        const JobRun run = runJob(updateSpec(fixture));
        QVERIFY(run.received);
        QVERIFY2(run.result.success, qPrintable(run.result.error));
        QCOMPARE(runGit(m_git, fixture.working, {"write-tree"}).output, before);
        QCOMPARE(readFile(QDir(fixture.working).filePath("sample.txt")), QByteArray("local\n"));
    }

    void bulkPluginUpdatesKeepEventLoopResponsive()
    {
        std::vector<std::unique_ptr<RepositoryFixture>> fixtures;
        QList<RepositoryUpdateSpec> specs;
        constexpr int repositoryCount = 3;
        constexpr int filesPerRepository = 24;
        for (int index = 0; index < repositoryCount; ++index) {
            auto fixture = std::make_unique<RepositoryFixture>(m_git);
            QVERIFY(fixture->create("alpha\nbeta\ngamma\n"));
            for (int file = 0; file < filesPerRepository; ++file) {
                const QString name = QStringLiteral("file %1.txt").arg(file);
                QVERIFY(writeFile(QDir(fixture->working).filePath(name), "alpha\nbeta\ngamma\n"));
            }
            QVERIFY(runGit(m_git, fixture->working, {"add", "."}).success);
            QVERIFY(runGit(m_git, fixture->working, {"commit", "-m", "files"}).success);
            QVERIFY(runGit(m_git, fixture->working, {"push"}).success);
            QVERIFY(runGit(m_git, fixture->updater, {"pull", "--ff-only"}).success);
            for (int file = 0; file < filesPerRepository; ++file) {
                const QString name = QStringLiteral("file %1.txt").arg(file);
                QVERIFY(writeFile(QDir(fixture->working).filePath(name), "alpha\nlocal\ngamma\n"));
                QVERIFY(writeFile(QDir(fixture->updater).filePath(name), "alpha\nbeta\nremote\n"));
            }
            QVERIFY(runGit(m_git, fixture->updater, {"add", "."}).success);
            QVERIFY(fixture->publish("alpha\nbeta\nremote\n"));
            specs.append(updateSpec(*fixture));
            fixtures.push_back(std::move(fixture));
        }
        QTemporaryDir traceDirectory;
        const QString tracePath = QDir(traceDirectory.path()).filePath("trace.json");
        GitTraceScope trace(tracePath);
        RepositoryUpdateCoordinator coordinator;
        QEventLoop loop;
        QTimer heartbeat, timeout;
        timeout.setSingleShot(true);
        QElapsedTimer elapsed;
        elapsed.start();
        qint64 previous = 0, largestGap = 0;
        int ticks = 0, succeeded = -1, failed = -1;
        connect(&heartbeat, &QTimer::timeout, &loop, [&] {
            const qint64 now = elapsed.elapsed();
            largestGap = qMax(largestGap, now - previous);
            previous = now;
            ++ticks;
        });
        connect(&coordinator, &RepositoryUpdateCoordinator::finished, &loop,
                [&](int ok, int failures, const QStringList &, const QStringList &) {
            succeeded = ok;
            failed = failures;
            loop.quit();
        });
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        heartbeat.start(10);
        timeout.start(60000);
        coordinator.start(specs, 2);
        loop.exec();
        QCOMPARE(succeeded, repositoryCount);
        QCOMPARE(failed, 0);
        QVERIFY(ticks > 10);
        QVERIFY2(largestGap < 1000, qPrintable(QStringLiteral("Event loop stalled for %1 ms").arg(largestGap)));
        QCOMPARE(textDiffCount(readFile(tracePath)), repositoryCount * 2);
        for (const auto &fixture : fixtures) {
            for (int file = 0; file < filesPerRepository; ++file) {
                QCOMPARE(readFile(QDir(fixture->working).filePath(QStringLiteral("file %1.txt").arg(file))),
                         QByteArray("alpha\nlocal\nremote\n"));
            }
        }
        qInfo("Bulk update: %lld ms, %d timer ticks, largest event-loop gap %lld ms",
              elapsed.elapsed(), ticks, largestGap);
    }

    void fetchFailureDoesNotCreateInterruptedOperation()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("alpha\n"));
        QVERIFY(runGit(m_git, fixture.working,
                       {QStringLiteral("remote"), QStringLiteral("set-url"),
                        QStringLiteral("origin"), QStringLiteral("Z:/missing/minifox.git")}).success);

        const JobRun run = runJob(updateSpec(fixture));
        QVERIFY(run.received);
        QVERIFY(!run.result.success);
        QVERIFY(!run.result.incomplete);
        QVERIFY(VersionTransactionStore::entries().isEmpty());
    }

    void startupRecoveryKeepsLegacyTransactionsWithoutProof()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("alpha\n"));
        const GitResult head = runGit(m_git, fixture.working,
                                      {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
        QVERIFY(head.success);
        const QString commit = QString::fromLatin1(head.output.trimmed());
        VersionTransactionEntry entry {
            QStringLiteral("completed"), QStringLiteral("extension-update"),
            fixture.working, QStringLiteral("Test Extension"), commit, {}, commit,
            QStringLiteral("fast-forward"), {}
        };
        QVERIFY(VersionTransactionStore::upsert(entry));
        QVERIFY(VersionTransactionStore::reconcileCompleted(m_git));
        QCOMPARE(VersionTransactionStore::entries().size(), 1);
        QVERIFY(VersionTransactionStore::remove(entry.id));

        QVERIFY(writeFile(QDir(fixture.working).filePath(QStringLiteral("sample.txt")),
                          "staged-but-incomplete\n"));
        QVERIFY(runGit(m_git, fixture.working,
                       {QStringLiteral("add"), QStringLiteral("sample.txt")}).success);
        entry.id = QStringLiteral("incomplete-index");
        QVERIFY(VersionTransactionStore::upsert(entry));
        QVERIFY(VersionTransactionStore::reconcileCompleted(m_git));
        QCOMPARE(VersionTransactionStore::entries().size(), 1);
    }

    void corruptStoreCannotBeSilentlyOverwritten()
    {
        QVERIFY(PortablePaths::ensureDataDirectory());
        const QByteArray corrupt("{\"version\":2,\"entries\":[");
        QVERIFY(writeFile(PortablePaths::versionOperationFile(), corrupt));
        QString error;
        QVERIFY(VersionTransactionStore::entries(&error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(!VersionTransactionStore::upsert({"new", "core-update", QDir::tempPath()}));
        QVERIFY(!VersionTransactionStore::remove("old"));
        QCOMPARE(readFile(PortablePaths::versionOperationFile()), corrupt);
        QVERIFY(!VersionTransactionStore::reconcileCompleted(m_git));
        QVERIFY(writeFile(PortablePaths::versionOperationFile(), "{\"version\":2,\"entries\":[{}]}"));
        VersionTransactionStore::entries(&error);
        QVERIFY(!error.isEmpty());
    }

    void retryCannotReplaceAnotherRecoveryRecord()
    {
        VersionTransactionEntry entry{"first", "extension-update", QDir::tempPath()};
        QVERIFY(VersionTransactionStore::upsert(entry));
        entry.id = "second";
        QVERIFY(!VersionTransactionStore::upsert(entry));
        QCOMPARE(VersionTransactionStore::entries().constFirst().id, QString("first"));
    }

    void rollbackRestoresStagingDeletionsAndPartialWrites()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("base\n"));
        const QDir root(fixture.working);
        QVERIFY(writeFile(root.filePath("sample.txt"), "staged-only\n"));
        QVERIFY(runGit(m_git, fixture.working, {"add", "sample.txt"}).success);
        QVERIFY(writeFile(root.filePath("sample.txt"), "working-only\n"));
        QVERIFY(writeFile(root.filePath("deleted.txt"), "keep deletion backup\n"));
        const QString head = QString::fromLatin1(runGit(m_git, fixture.working, {"rev-parse", "HEAD"}).output.trimmed());
        VersionTransactionEntry entry{"rollback", "extension-update", fixture.working,
                                      "Test", head, {}, head, "safe-merge"};
        QVERIFY(VersionTransactionStore::upsert(entry));
        const QList<RecoveryChange> changes{
            {root.filePath("sample.txt"), "working-only\n", "merged\n", true, false},
            {root.filePath("deleted.txt"), "keep deletion backup\n", {}, true, true},
            {root.filePath("added.txt"), {}, "new\n", false, false}};
        QString error;
        QVERIFY2(RepositoryRecovery::prepare(m_git, entry, changes, &error), qPrintable(error));
        entry = VersionTransactionStore::entries().constFirst();
        QVERIFY(writeFile(root.filePath("sample.txt"), "merged\n"));
        QVERIFY(QFile::remove(root.filePath("deleted.txt")));
        QVERIFY(runGit(m_git, fixture.working, {"reset", "--mixed", head}).success);
        QVERIFY(runGit(m_git, fixture.working, {"gc", "--prune=now"}).success);
        QVERIFY2(RepositoryRecovery::rollback(m_git, entry, &error), qPrintable(error));
        QCOMPARE(readFile(root.filePath("sample.txt")), QByteArray("working-only\n"));
        QCOMPARE(readFile(root.filePath("deleted.txt")), QByteArray("keep deletion backup\n"));
        QVERIFY(!QFileInfo::exists(root.filePath("added.txt")));
        QCOMPARE(runGit(m_git, fixture.working, {"show", ":sample.txt"}).output, QByteArray("staged-only\n"));
        QVERIFY(VersionTransactionStore::entries().isEmpty());
        // A second recovery, as after another interruption, is idempotent.
        QVERIFY(VersionTransactionStore::upsert(entry));
        QVERIFY2(RepositoryRecovery::rollback(m_git, entry, &error), qPrintable(error));
    }

    void rollbackRefusesNewEditsAndDamagedBackup()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("base\n"));
        const QString path = QDir(fixture.working).filePath("sample.txt");
        const QByteArray original = readFile(path);
        const QString head = QString::fromLatin1(runGit(m_git, fixture.working, {"rev-parse", "HEAD"}).output.trimmed());
        VersionTransactionEntry entry{"edited", "extension-update", fixture.working,
                                      "Test", head, {}, head, "safe-merge"};
        QVERIFY(VersionTransactionStore::upsert(entry));
        QString error;
        QVERIFY2(RepositoryRecovery::prepare(m_git, entry,
            {{path, original, "merged\n", true, false}}, &error), qPrintable(error));
        entry = VersionTransactionStore::entries().constFirst();
        QVERIFY(writeFile(path, "new user edit\n"));
        QVERIFY(!RepositoryRecovery::rollback(m_git, entry, &error));
        QCOMPARE(readFile(path), QByteArray("new user edit\n"));
        QCOMPARE(VersionTransactionStore::entries().size(), 1);
        QVERIFY(writeFile(path, original));
        QVERIFY(writeFile(entry.recoveryPath, "damaged"));
        QVERIFY(!RepositoryRecovery::rollback(m_git, entry, &error));
        QCOMPARE(readFile(path), original);
        QCOMPARE(VersionTransactionStore::entries().size(), 1);
    }

    void completionRequiresFilesBranchAndCommittedJournal()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("base\n"));
        const QString path = QDir(fixture.working).filePath("sample.txt");
        const QString head = QString::fromLatin1(runGit(m_git, fixture.working, {"rev-parse", "HEAD"}).output.trimmed());
        VersionTransactionEntry entry{"verification", "extension-update", fixture.working,
                                      "Test", head, "wanted", head, "safe-merge"};
        QVERIFY(VersionTransactionStore::upsert(entry));
        QString error;
        QVERIFY2(RepositoryRecovery::prepare(m_git, entry,
            {{path, readFile(path), "preserved-local\n", true, false}}, &error), qPrintable(error));
        entry = VersionTransactionStore::entries().constFirst();
        QVERIFY(!RepositoryRecovery::verify(m_git, entry, &error));
        QVERIFY(writeFile(path, "preserved-local\n"));
        QVERIFY(!RepositoryRecovery::verify(m_git, entry, &error));
        QVERIFY(runGit(m_git, fixture.working, {"checkout", "-B", "wanted", head}).success);
        QVERIFY2(RepositoryRecovery::verify(m_git, entry, &error), qPrintable(error));
        QVERIFY(VersionTransactionStore::reconcileCompleted(m_git));
        QCOMPARE(VersionTransactionStore::entries().size(), 1);
        entry.phase = "committed";
        QVERIFY(VersionTransactionStore::upsert(entry));
        QVERIFY(writeFile(path, "incomplete-on-disk\n"));
        QVERIFY(VersionTransactionStore::reconcileCompleted(m_git));
        QCOMPARE(VersionTransactionStore::entries().size(), 1);
        QVERIFY(writeFile(path, "preserved-local\n"));
        QVERIFY(VersionTransactionStore::reconcileCompleted(m_git));
        QVERIFY(VersionTransactionStore::entries().isEmpty());
    }

    void processCrashIsRecoveredBeforeRetryingUpdate()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("base\n"));
        QVERIFY(fixture.publish("remote\n"));
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(),
                    {"--recovery-crash-child", m_git, fixture.working});
        QVERIFY(child.waitForStarted(5000));
        QVERIFY(child.waitForFinished(30000));
        QCOMPARE(child.exitCode(), 77);
        QCOMPARE(readFile(QDir(fixture.working).filePath("sample.txt")), QByteArray("partial\n"));
        QCOMPARE(VersionTransactionStore::entries().size(), 1);
        const JobRun run = runJob(updateSpec(fixture));
        QVERIFY(run.received);
        QVERIFY2(run.result.success, qPrintable(run.result.error));
        QCOMPARE(normalizedLineEndings(readFile(QDir(fixture.working).filePath("sample.txt"))), QByteArray("remote\n"));
        QVERIFY(VersionTransactionStore::entries().isEmpty());
    }

    void destructiveResetBackupPreservesIndexAndDeletion()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("base\n"));
        const QDir root(fixture.working);
        QVERIFY(writeFile(root.filePath("deleted.txt"), "original\n"));
        QVERIFY(runGit(m_git, fixture.working, {"add", "deleted.txt"}).success);
        QVERIFY(runGit(m_git, fixture.working, {"commit", "-m", "deletion fixture"}).success);
        QVERIFY(writeFile(root.filePath("sample.txt"), "staged-private\n"));
        QVERIFY(runGit(m_git, fixture.working, {"add", "sample.txt"}).success);
        QVERIFY(writeFile(root.filePath("sample.txt"), "base\n"));
        QVERIFY(QFile::remove(root.filePath("deleted.txt")));
        VersionTransactionEntry entry{"reset-backup", "core-full-clean", fixture.working};
        QVERIFY(VersionTransactionStore::upsert(entry));
        QString error;
        QVERIFY2(RepositoryRecovery::backupBeforeReset(m_git, entry, &error), qPrintable(error));
        entry = VersionTransactionStore::entries().constFirst();
        QVERIFY(!entry.trackedBackupRef.isEmpty());
        const QString ref = entry.trackedBackupRef;
        QCOMPARE(normalizedLineEndings(runGit(m_git, fixture.working, {"show", ref + "^2:sample.txt"}).output),
                 QByteArray("staged-private\n"));
        QVERIFY(!runGit(m_git, fixture.working, {"cat-file", "-e", ref + ":deleted.txt"}).success);
        QVERIFY(runGit(m_git, fixture.working, {"reset", "--hard", "HEAD"}).success);
        QVERIFY(runGit(m_git, fixture.working, {"gc", "--prune=now"}).success);
        QVERIFY2(runGit(m_git, fixture.working, {"stash", "apply", "--index", ref}).success, "pinned reset backup must be restorable");
        QCOMPARE(normalizedLineEndings(readFile(root.filePath("sample.txt"))), QByteArray("base\n"));
        QCOMPARE(normalizedLineEndings(runGit(m_git, fixture.working, {"show", ":sample.txt"}).output), QByteArray("staged-private\n"));
        QVERIFY(!QFileInfo::exists(root.filePath("deleted.txt")));
    }

    void destructiveResetVerifiesArchivesBeforeBackupReady()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("base\n"));
        const QString archive = fixture.directory.filePath("backup.zip");
        QProcess tar;
        tar.start(QStandardPaths::findExecutable("tar"),
                  {"-a", "-cf", archive, "-C", fixture.working, "sample.txt"});
        QVERIFY(tar.waitForFinished(15000));
        QCOMPARE(tar.exitCode(), 0);
        VersionTransactionEntry entry{"archive", "core-full-clean", fixture.working};
        entry.backupArchives = {archive};
        QVERIFY(VersionTransactionStore::upsert(entry));
        QString error;
        QVERIFY2(RepositoryRecovery::backupBeforeReset(m_git, entry, &error), qPrintable(error));
        QCOMPARE(VersionTransactionStore::entries().constFirst().phase, QString("reset-backup-ready"));
        QVERIFY(writeFile(archive, "corrupt archive"));
        entry.phase = "reset-preparing";
        QVERIFY(VersionTransactionStore::upsert(entry));
        QVERIFY(!RepositoryRecovery::backupBeforeReset(m_git, entry, &error));
        QCOMPARE(VersionTransactionStore::entries().constFirst().phase, QString("reset-preparing"));
    }

    void historicalVersionSwitchAlsoHasRecoverableBackup()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("old\n"));
        const QString original = QString::fromLatin1(runGit(m_git, fixture.working, {"rev-parse", "HEAD"}).output.trimmed());
        QVERIFY(fixture.publish("new\n"));
        QVERIFY(runJob(updateSpec(fixture)).result.success);
        auto spec = updateSpec(fixture);
        spec.target = RepositoryTarget::Explicit;
        spec.explicitRef = original;
        spec.explicitBranch = "history";
        const auto result = runJob(spec);
        QVERIFY(result.received);
        QVERIFY2(result.result.success, qPrintable(result.result.error));
        QCOMPARE(normalizedLineEndings(readFile(QDir(fixture.working).filePath("sample.txt"))), QByteArray("old\n"));
        QCOMPARE(runGit(m_git, fixture.working, {"branch", "--show-current"}).output.trimmed(), QByteArray("history"));
        QVERIFY(VersionTransactionStore::entries().isEmpty());
    }

    void failedJournalWritePreventsAllWorkingTreeWrites()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("base\n"));
        const QString path = QDir(fixture.working).filePath("sample.txt");
        const QByteArray base = readFile(path);
        const QString head = QString::fromLatin1(runGit(m_git, fixture.working, {"rev-parse", "HEAD"}).output.trimmed());
        VersionTransactionEntry entry{"blocked-pack", "extension-update", fixture.working,
                                      "Test", head, {}, head, "preparing"};
        QVERIFY(VersionTransactionStore::upsert(entry));
        const QString blocked = QDir(PortablePaths::dataDirectory()).filePath("recovery/blocked-pack.pack");
        QVERIFY(QDir().mkpath(blocked));
        QTemporaryDir snapshots;
        for (const QString &name : {"base", "local", "target"}) {
            QVERIFY(QDir(snapshots.path()).mkpath(name));
            QVERIFY(writeFile(snapshots.filePath(name + "/sample.txt"), name == "target" ? QByteArray("remote\n") : base));
        }
        const auto result = SafeMergeEngine::run({m_git, fixture.working,
            snapshots.filePath("base"), snapshots.filePath("local"), snapshots.filePath("target"), entry.id});
        QCOMPARE(result.error, SafeMergeError::RecoveryBackup);
        QCOMPARE(readFile(path), base);
        QCOMPARE(VersionTransactionStore::entries().constFirst().phase, QString("preparing"));
        QVERIFY(QDir().rmdir(blocked));
    }

    void unadvertisedPluginCommitDetachesWithoutMovingMain()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("main\n"));
        const QString original = QString::fromLatin1(runGit(m_git, fixture.working, {"rev-parse", "HEAD"}).output.trimmed());
        QVERIFY(runGit(m_git, fixture.working, {"checkout", "-b", "private"}).success);
        QVERIFY(writeFile(QDir(fixture.working).filePath("sample.txt"), "private\n"));
        QVERIFY(runGit(m_git, fixture.working, {"commit", "-am", "private commit"}).success);
        const QString selected = QString::fromLatin1(runGit(m_git, fixture.working, {"rev-parse", "HEAD"}).output.trimmed());
        QVERIFY(runGit(m_git, fixture.working, {"checkout", "main"}).success);
        auto spec = updateSpec(fixture);
        spec.target = RepositoryTarget::Explicit;
        spec.explicitRef = selected;
        const auto result = runJob(spec);
        QVERIFY(result.received);
        QVERIFY2(result.result.success, qPrintable(result.result.error));
        QCOMPARE(runGit(m_git, fixture.working, {"rev-parse", "HEAD"}).output.trimmed(), selected.toLatin1());
        QCOMPARE(runGit(m_git, fixture.working, {"rev-parse", "main"}).output.trimmed(), original.toLatin1());
        QVERIFY(runGit(m_git, fixture.working, {"branch", "--show-current"}).output.trimmed().isEmpty());
        QVERIFY(VersionTransactionStore::entries().isEmpty());
    }

    void detachedRecoveryRestoresOriginalBranch()
    {
        RepositoryFixture fixture(m_git);
        QVERIFY(fixture.create("main\n"));
        const QString head = QString::fromLatin1(runGit(m_git, fixture.working, {"rev-parse", "HEAD"}).output.trimmed());
        VersionTransactionEntry entry{"detach-recovery", "extension-update", fixture.working,
                                      "Test", head, {}, head, "preparing"};
        entry.detachedTarget = true;
        QVERIFY(VersionTransactionStore::upsert(entry));
        QString error;
        QVERIFY2(RepositoryRecovery::prepare(m_git, entry, {}, &error), qPrintable(error));
        entry = VersionTransactionStore::entries().constFirst();
        QVERIFY(entry.detachedTarget);
        QVERIFY(runGit(m_git, fixture.working, {"update-ref", "--no-deref", "HEAD", head}).success);
        QVERIFY2(RepositoryRecovery::verify(m_git, entry, &error), qPrintable(error));
        QVERIFY2(RepositoryRecovery::rollback(m_git, entry, &error), qPrintable(error));
        QCOMPARE(runGit(m_git, fixture.working, {"branch", "--show-current"}).output.trimmed(), QByteArray("main"));
    }

private:
    QString m_git;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.value(1) == "--lease-probe") {
        OperationLease lease; QString error;
        return lease.acquire({args.value(2)}, &error) ? 0 : 31;
    }
    if (args.value(1) == "--recovery-crash-child") {
        const QString git = args.value(2), root = args.value(3);
        const QString head = QString::fromLatin1(runGit(git, root, {"rev-parse", "HEAD"}).output.trimmed());
        const QString path = QDir(root).filePath("sample.txt");
        VersionTransactionEntry entry{"crash-child", "extension-update", root,
                                      "Test", head, {}, head, "safe-merge"};
        QString error;
        if (!VersionTransactionStore::upsert(entry, &error)
            || !RepositoryRecovery::prepare(git, entry,
                    {{path, readFile(path), "partial\n", true, false}}, &error)
            || !writeFile(path, "partial\n")) return 76;
        std::_Exit(77);
    }
    RepositoryUpdateTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_RepositoryUpdate.moc"
