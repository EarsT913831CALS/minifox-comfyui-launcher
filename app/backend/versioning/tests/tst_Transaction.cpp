#include "Git.h"
#include "Repository.h"
#include "Transaction.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeFile(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content);
}

}

class TransactionTest final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanUpdateMovesBranch();
    void keepsNonConflictingEdits();
    void remoteWinsConflictsAndBacksUp();
    void modifyDeleteTakesTarget();
    void localDeletionOfChangedFileTakesTarget();
    void movesCollidingUntrackedFiles();
    void downgradeKeepsEdits();
    void discardRestoresCommittedState();
    void switchesToTrackingBranch();
    void rollbackRestoresEveryPhase_data();
    void rollbackRestoresEveryPhase();

private:
    git::Result git(const QStringList &arguments, const QString &root = {});
    QString commit(const QString &root, const QString &message);
    QString head(const QString &root) { return repository::resolveCommit(m_tool, root, QStringLiteral("HEAD")); }
    QString status(const QString &root);
    transaction::Request request(const QString &target);

    std::unique_ptr<QTemporaryDir> m_temp;
    git::Tool m_tool;
    QString m_upstream;
    QString m_clone;
    QString m_base;
};

git::Result TransactionTest::git(const QStringList &arguments, const QString &root)
{
    return m_tool.run(root.isEmpty() ? m_clone : root, arguments, {.identity = true});
}

QString TransactionTest::commit(const QString &root, const QString &message)
{
    git({QStringLiteral("add"), QStringLiteral("-A")}, root);
    git({QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), message}, root);
    return head(root);
}

QString TransactionTest::status(const QString &root)
{
    return git({QStringLiteral("status"), QStringLiteral("--porcelain"), QStringLiteral("--untracked-files=all")}, root)
        .text()
        .trimmed();
}

transaction::Request TransactionTest::request(const QString &target)
{
    transaction::Request request;
    request.root = m_clone;
    request.name = QStringLiteral("demo");
    request.target = target;
    request.branch = QStringLiteral("master");
    request.backupRoot = m_temp->filePath(QStringLiteral("backup"));
    request.journalDirectory = m_temp->filePath(QStringLiteral("journal"));
    return request;
}

void TransactionTest::init()
{
    m_temp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_temp->isValid());
    m_tool = git::Tool(git::Tool::locate({}), QProcessEnvironment::systemEnvironment());
    QVERIFY(m_tool.isValid());
    m_upstream = m_temp->filePath(QStringLiteral("upstream"));
    m_clone = m_temp->filePath(QStringLiteral("clone"));

    QDir().mkpath(m_upstream);
    git({QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("master")}, m_upstream);
    git({QStringLiteral("config"), QStringLiteral("core.autocrlf"), QStringLiteral("false")}, m_upstream);
    writeFile(m_upstream + QStringLiteral("/a.txt"), "1\n2\n3\n4\n5\n6\n7\n8\n");
    writeFile(m_upstream + QStringLiteral("/b.txt"), "b\n");
    writeFile(m_upstream + QStringLiteral("/requirements.txt"), "torch\n");
    m_base = commit(m_upstream, QStringLiteral("base"));
    QVERIFY(!m_base.isEmpty());
    QVERIFY(git({QStringLiteral("clone"), QStringLiteral("-q"), QStringLiteral("-c"), QStringLiteral("core.autocrlf=false"),
                 m_upstream, m_clone},
                m_temp->path())
                .ok());
}

void TransactionTest::cleanUpdateMovesBranch()
{
    writeFile(m_upstream + QStringLiteral("/c.txt"), "new\n");
    const QString target = commit(m_upstream, QStringLiteral("add c"));
    QVERIFY(repository::fetch(m_tool, m_clone).ok());

    const transaction::Outcome outcome = transaction::apply(m_tool, request(target));
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(head(m_clone), target);
    QCOMPARE(readFile(m_clone + QStringLiteral("/c.txt")), QByteArray("new\n"));
    QCOMPARE(status(m_clone), QString());
    QVERIFY(!outcome.hadLocalChanges);
    QVERIFY(outcome.backupDirectory.isEmpty());
    QVERIFY(transaction::pending(m_temp->filePath(QStringLiteral("journal"))).isEmpty());
    QVERIFY(transaction::backupRefs(m_tool, m_clone).isEmpty());
}

void TransactionTest::keepsNonConflictingEdits()
{
    writeFile(m_upstream + QStringLiteral("/a.txt"), "1 remote\n2\n3\n4\n5\n6\n7\n8\n");
    const QString target = commit(m_upstream, QStringLiteral("edit top"));
    repository::fetch(m_tool, m_clone);
    writeFile(m_clone + QStringLiteral("/a.txt"), "1\n2\n3\n4\n5\n6\n7\n8 local\n");

    const transaction::Outcome outcome = transaction::apply(m_tool, request(target));
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(head(m_clone), target);
    QCOMPARE(readFile(m_clone + QStringLiteral("/a.txt")), QByteArray("1 remote\n2\n3\n4\n5\n6\n7\n8 local\n"));
    QVERIFY(outcome.conflicts.isEmpty());
    QCOMPARE(status(m_clone), QStringLiteral("M a.txt"));
    QCOMPARE(transaction::backupRefs(m_tool, m_clone).size(), 1);
}

void TransactionTest::remoteWinsConflictsAndBacksUp()
{
    writeFile(m_upstream + QStringLiteral("/a.txt"), "1 remote\n2\n3\n4\n5\n6\n7\n8\n");
    const QString target = commit(m_upstream, QStringLiteral("edit top"));
    repository::fetch(m_tool, m_clone);
    writeFile(m_clone + QStringLiteral("/a.txt"), "1 local\n2\n3\n4\n5\n6\n7\n8 local\n");

    const transaction::Outcome outcome = transaction::apply(m_tool, request(target));
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    // The conflicting hunk takes the remote; the other local hunk survives.
    QCOMPARE(readFile(m_clone + QStringLiteral("/a.txt")), QByteArray("1 remote\n2\n3\n4\n5\n6\n7\n8 local\n"));
    QCOMPARE(outcome.conflicts, QStringList{QStringLiteral("a.txt")});
    QVERIFY(!outcome.backupDirectory.isEmpty());
    QCOMPARE(readFile(outcome.backupDirectory + QStringLiteral("/files/a.txt")),
             QByteArray("1 local\n2\n3\n4\n5\n6\n7\n8 local\n"));
    QVERIFY(readFile(outcome.backupDirectory + QStringLiteral("/conflicts.log")).contains("a.txt"));
    QVERIFY(readFile(outcome.backupDirectory + QStringLiteral("/conflicts.patch")).contains("+1 local"));
}

void TransactionTest::modifyDeleteTakesTarget()
{
    QFile::remove(m_upstream + QStringLiteral("/b.txt"));
    const QString target = commit(m_upstream, QStringLiteral("drop b"));
    repository::fetch(m_tool, m_clone);
    writeFile(m_clone + QStringLiteral("/b.txt"), "b local\n");

    const transaction::Outcome outcome = transaction::apply(m_tool, request(target));
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QVERIFY(!QFileInfo::exists(m_clone + QStringLiteral("/b.txt")));
    QCOMPARE(outcome.conflicts, QStringList{QStringLiteral("b.txt")});
    QCOMPARE(readFile(outcome.backupDirectory + QStringLiteral("/files/b.txt")), QByteArray("b local\n"));
    QCOMPARE(status(m_clone), QString());
}

void TransactionTest::localDeletionOfChangedFileTakesTarget()
{
    writeFile(m_upstream + QStringLiteral("/b.txt"), "b remote\n");
    const QString target = commit(m_upstream, QStringLiteral("edit b"));
    repository::fetch(m_tool, m_clone);
    QFile::remove(m_clone + QStringLiteral("/b.txt"));

    const transaction::Outcome outcome = transaction::apply(m_tool, request(target));
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(readFile(m_clone + QStringLiteral("/b.txt")), QByteArray("b remote\n"));
    QCOMPARE(status(m_clone), QString());
}

void TransactionTest::movesCollidingUntrackedFiles()
{
    writeFile(m_upstream + QStringLiteral("/models/config.json"), "remote\n");
    writeFile(m_upstream + QStringLiteral("/tool/run.py"), "remote\n");
    const QString target = commit(m_upstream, QStringLiteral("add files"));
    repository::fetch(m_tool, m_clone);
    writeFile(m_clone + QStringLiteral("/models/config.json"), "mine\n");
    writeFile(m_clone + QStringLiteral("/models/other.bin"), "keep\n");
    // A file where the target needs a folder.
    writeFile(m_clone + QStringLiteral("/tool"), "file\n");

    const transaction::Outcome outcome = transaction::apply(m_tool, request(target));
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(readFile(m_clone + QStringLiteral("/models/config.json")), QByteArray("remote\n"));
    QCOMPARE(readFile(m_clone + QStringLiteral("/tool/run.py")), QByteArray("remote\n"));
    QCOMPARE(readFile(m_clone + QStringLiteral("/models/other.bin")), QByteArray("keep\n"));
    QCOMPARE(readFile(outcome.backupDirectory + QStringLiteral("/untracked/models/config.json")), QByteArray("mine\n"));
    QCOMPARE(readFile(outcome.backupDirectory + QStringLiteral("/untracked/tool")), QByteArray("file\n"));
    QCOMPARE(outcome.moved.size(), 2);
}

void TransactionTest::downgradeKeepsEdits()
{
    writeFile(m_clone + QStringLiteral("/a.txt"), "1 newer\n2\n3\n4\n5\n6\n7\n8\n");
    const QString newer = commit(m_clone, QStringLiteral("newer"));
    writeFile(m_clone + QStringLiteral("/b.txt"), "b local\n");

    const transaction::Outcome outcome = transaction::apply(m_tool, request(m_base));
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QVERIFY(newer != m_base);
    QCOMPARE(head(m_clone), m_base);
    QCOMPARE(readFile(m_clone + QStringLiteral("/a.txt")), QByteArray("1\n2\n3\n4\n5\n6\n7\n8\n"));
    QCOMPARE(readFile(m_clone + QStringLiteral("/b.txt")), QByteArray("b local\n"));
}

void TransactionTest::discardRestoresCommittedState()
{
    writeFile(m_clone + QStringLiteral("/a.txt"), "changed\n");
    QFile::remove(m_clone + QStringLiteral("/b.txt"));
    writeFile(m_clone + QStringLiteral("/untracked.txt"), "stays\n");

    transaction::Request discard = request(m_base);
    discard.keepLocal = false;
    const transaction::Outcome outcome = transaction::apply(m_tool, discard);
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(status(m_clone), QStringLiteral("?? untracked.txt"));
    QCOMPARE(readFile(m_clone + QStringLiteral("/b.txt")), QByteArray("b\n"));
    QCOMPARE(outcome.conflicts.size(), 2);
    QCOMPARE(readFile(outcome.backupDirectory + QStringLiteral("/files/a.txt")), QByteArray("changed\n"));
}

void TransactionTest::switchesToTrackingBranch()
{
    git({QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("dev")}, m_upstream);
    writeFile(m_upstream + QStringLiteral("/dev.txt"), "dev\n");
    const QString target = commit(m_upstream, QStringLiteral("dev work"));
    repository::fetch(m_tool, m_clone);
    git({QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("--detach")});

    transaction::Request change = request(target);
    change.branch = QStringLiteral("dev");
    change.upstream = QStringLiteral("origin/dev");
    const transaction::Outcome outcome = transaction::apply(m_tool, change);
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    const RepositoryInfo info = repository::inspect(m_tool, m_clone);
    QCOMPARE(info.branch, QStringLiteral("dev"));
    QCOMPARE(info.upstream, QStringLiteral("origin/dev"));
    QCOMPARE(info.head, target);
    QCOMPARE(repository::updateTarget(m_tool, m_clone, info), QStringLiteral("origin/dev"));
}

void TransactionTest::rollbackRestoresEveryPhase_data()
{
    QTest::addColumn<int>("phase");
    QTest::newRow("moving") << 1;
    QTest::newRow("applying") << 2;
    QTest::newRow("refs") << 3;
}

void TransactionTest::rollbackRestoresEveryPhase()
{
    QFETCH(int, phase);
    writeFile(m_upstream + QStringLiteral("/a.txt"), "1 remote\n2\n3\n4\n5\n6\n7\n8\n");
    writeFile(m_upstream + QStringLiteral("/new.txt"), "remote\n");
    const QString target = commit(m_upstream, QStringLiteral("remote"));
    repository::fetch(m_tool, m_clone);
    writeFile(m_clone + QStringLiteral("/a.txt"), "1 local\n2\n3\n4\n5\n6\n7\n8\n");
    writeFile(m_clone + QStringLiteral("/new.txt"), "untracked\n");
    const QString before = status(m_clone);

    transaction::Request interrupted = request(target);
    interrupted.interruptAfter = phase;
    QVERIFY(!transaction::apply(m_tool, interrupted).ok);

    const QList<transaction::Journal> journals = transaction::pending(interrupted.journalDirectory);
    QCOMPARE(journals.size(), 1);
    QString error;
    QVERIFY2(transaction::rollback(m_tool, journals.constFirst(), &error), qPrintable(error));
    QCOMPARE(head(m_clone), m_base);
    QCOMPARE(repository::inspect(m_tool, m_clone).branch, QStringLiteral("master"));
    QCOMPARE(readFile(m_clone + QStringLiteral("/a.txt")), QByteArray("1 local\n2\n3\n4\n5\n6\n7\n8\n"));
    QCOMPARE(readFile(m_clone + QStringLiteral("/new.txt")), QByteArray("untracked\n"));
    QCOMPARE(status(m_clone), before);
    QVERIFY(transaction::pending(interrupted.journalDirectory).isEmpty());

    // The same update succeeds afterwards.
    const transaction::Outcome outcome = transaction::apply(m_tool, request(target));
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(head(m_clone), target);
}

QTEST_GUILESS_MAIN(TransactionTest)
#include "tst_Transaction.moc"
