#include "RepositoryUpdateJob.h"
#include "VersionTransactionStore.h"

#include "PortablePaths.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

namespace {

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

    void cleanRepositoryUsesFastForwardPath()
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

    void startupRecoveryClearsOnlyFullyCompletedTransactions()
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
        QVERIFY(VersionTransactionStore::entries().isEmpty());

        QVERIFY(writeFile(QDir(fixture.working).filePath(QStringLiteral("sample.txt")),
                          "staged-but-incomplete\n"));
        QVERIFY(runGit(m_git, fixture.working,
                       {QStringLiteral("add"), QStringLiteral("sample.txt")}).success);
        entry.id = QStringLiteral("incomplete-index");
        QVERIFY(VersionTransactionStore::upsert(entry));
        QVERIFY(VersionTransactionStore::reconcileCompleted(m_git));
        QCOMPARE(VersionTransactionStore::entries().size(), 1);
    }

private:
    QString m_git;
};

QTEST_GUILESS_MAIN(RepositoryUpdateTest)

#include "tst_RepositoryUpdate.moc"
