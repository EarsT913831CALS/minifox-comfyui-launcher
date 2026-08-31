#include "RepositoryUpdateJob.h"

#include "ProcessTextDecoder.h"
#include "VersionTransactionStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QUuid>
#include <QtConcurrentRun>

#include <utility>

RepositoryUpdateJob::RepositoryUpdateJob(const RepositoryUpdateSpec &spec,
                                         QObject *parent)
    : QObject(parent), m_spec(spec), m_transactionId(QUuid::createUuid().toString(QUuid::Id128))
{
    connect(&m_runner, &GitProcessRunner::finished,
            this, &RepositoryUpdateJob::handleProcessFinished);
}

RepositoryUpdateJob::~RepositoryUpdateJob()
{
    clearTemporaryFiles();
}

RepositoryUpdateSpec RepositoryUpdateJob::spec() const { return m_spec; }

void RepositoryUpdateJob::start()
{
    if (m_started) return;
    m_started = true;
    for (const VersionTransactionEntry &entry : VersionTransactionStore::entries()) {
        if (QDir::cleanPath(entry.root).compare(QDir::cleanPath(m_spec.root),
                                                Qt::CaseInsensitive) == 0) {
            m_hadInterruptedEntry = true;
            break;
        }
    }
    emit statusChanged(tr("正在检查 %1 的仓库状态…").arg(m_spec.displayName));
    runGit(Stage::Status, {QStringLiteral("status"), QStringLiteral("--porcelain"),
                           QStringLiteral("--untracked-files=no")});
}

void RepositoryUpdateJob::cancel()
{
    m_runner.cancel();
}

QString RepositoryUpdateJob::selectRemoteBranch(const QString &output,
                                                const QString &localBranch)
{
    QStringList candidates;
    for (QString branch : output.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                       Qt::SkipEmptyParts)) {
        branch = branch.trimmed();
        if (branch.isEmpty() || branch.endsWith(QStringLiteral("/HEAD"))) continue;
        if (!candidates.contains(branch)) candidates.append(branch);
    }
    if (candidates.isEmpty()) return {};
    if (localBranch.compare(QStringLiteral("minifox/development"), Qt::CaseInsensitive) == 0
        || localBranch.compare(QStringLiteral("dev"), Qt::CaseInsensitive) == 0) {
        return selectDevelopmentBranch(output);
    }
    if (!localBranch.isEmpty() && !branchNeedsRecovery(localBranch)) {
        const QString matching = QStringLiteral("origin/") + localBranch;
        if (candidates.contains(matching)) return matching;
    }
    for (const QString &candidate : std::as_const(candidates)) {
        if (candidate.startsWith(QStringLiteral("origin/release/"))) return candidate;
    }
    if (candidates.contains(QStringLiteral("origin/main"))) return QStringLiteral("origin/main");
    if (candidates.contains(QStringLiteral("origin/master"))) return QStringLiteral("origin/master");
    return candidates.constFirst();
}

QString RepositoryUpdateJob::selectDevelopmentBranch(const QString &output)
{
    QStringList candidates;
    for (QString branch : output.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                       Qt::SkipEmptyParts)) {
        branch = branch.trimmed();
        if (branch.isEmpty() || branch.endsWith(QStringLiteral("/HEAD"))) continue;
        if (!candidates.contains(branch)) candidates.append(branch);
    }
    if (candidates.contains(QStringLiteral("origin/main"))) return QStringLiteral("origin/main");
    if (candidates.contains(QStringLiteral("origin/master"))) return QStringLiteral("origin/master");
    for (const QString &candidate : std::as_const(candidates)) {
        if (!candidate.startsWith(QStringLiteral("origin/release/"))) return candidate;
    }
    return {};
}

bool RepositoryUpdateJob::branchNeedsRecovery(const QString &branch)
{
    return branch.isEmpty()
        || branch.startsWith(QStringLiteral("HEAD"), Qt::CaseInsensitive)
        || branch.startsWith(QStringLiteral("minifox/"), Qt::CaseInsensitive);
}

void RepositoryUpdateJob::runGit(Stage stage, const QStringList &arguments,
                                 const QString &indexFile, int inactivityTimeoutMs)
{
    m_stage = stage;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GCM_INTERACTIVE"), QStringLiteral("Never"));
    if (!indexFile.isEmpty()) environment.insert(QStringLiteral("GIT_INDEX_FILE"), indexFile);
    m_runner.start(m_spec.gitProgram, repositoryArguments(arguments),
                   environment, inactivityTimeoutMs);
}

QStringList RepositoryUpdateJob::repositoryArguments(const QStringList &arguments) const
{
    QStringList result = m_spec.gitPrefixArguments;
    result.append({QStringLiteral("-c"),
                   QStringLiteral("safe.directory=%1").arg(QDir::cleanPath(m_spec.root)),
                   QStringLiteral("-C"), m_spec.root});
    result.append(arguments);
    return result;
}

QString RepositoryUpdateJob::processError(const ProcessResult &result) const
{
    if (result.timedOut) {
        return tr("%1 的 Git 操作长时间无响应，已跳过。").arg(m_spec.displayName);
    }
    if (result.failedToStart) {
        return tr("无法启动 Git。请安装 Git for Windows 后重试。");
    }
    QString detail = ProcessTextDecoder::decode(result.standardError).trimmed();
    if (detail.isEmpty()) detail = ProcessTextDecoder::decode(result.standardOutput).trimmed();
    return detail.isEmpty()
        ? tr("%1 的 Git 操作失败。").arg(m_spec.displayName)
        : tr("%1 的 Git 操作失败：%2").arg(m_spec.displayName, detail);
}

void RepositoryUpdateJob::handleProcessFinished(const ProcessResult &result)
{
    if (result.exitStatus != QProcess::NormalExit || result.exitCode != 0) {
        fail(processError(result), result.timedOut, !m_transactionActive);
        return;
    }
    const QString output = ProcessTextDecoder::decode(result.standardOutput).trimmed();
    switch (m_stage) {
    case Stage::Status:
        m_dirty = !output.isEmpty();
        emit statusChanged(tr("正在读取 %1 的当前版本…").arg(m_spec.displayName));
        runGit(Stage::ReadHead, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
        break;
    case Stage::ReadHead:
        m_originalHead = output.section(QLatin1Char('\n'), 0, 0).trimmed();
        emit statusChanged(tr("正在获取 %1 的远端更新…").arg(m_spec.displayName));
        runGit(Stage::Fetch, {QStringLiteral("fetch"), QStringLiteral("--tags"),
                              QStringLiteral("--progress")}, {}, 60000);
        break;
    case Stage::Fetch:
        if (m_spec.target == RepositoryTarget::Stable) {
            runGit(Stage::ResolveStable,
                   {QStringLiteral("log"), QStringLiteral("--all"),
                    QStringLiteral("--regexp-ignore-case"), QStringLiteral("--grep=^ComfyUI v"),
                    QStringLiteral("-1"), QStringLiteral("--format=%H")});
        } else {
            const Stage stage = m_spec.target == RepositoryTarget::Development
                ? Stage::ResolveDevelopment : Stage::ResolveCurrent;
            const QStringList args = m_spec.target == RepositoryTarget::CurrentBranch
                ? QStringList {QStringLiteral("for-each-ref"), QStringLiteral("--contains=HEAD"),
                               QStringLiteral("--format=%(refname:short)"),
                               QStringLiteral("refs/remotes/origin/")}
                : QStringList {QStringLiteral("for-each-ref"),
                               QStringLiteral("--format=%(refname:short)"),
                               QStringLiteral("refs/remotes/origin/")};
            runGit(stage, args);
        }
        break;
    case Stage::ResolveStable:
    case Stage::ResolveDevelopment:
    case Stage::ResolveCurrent:
        resolveTarget(output);
        break;
    case Stage::CleanCheckout:
    case Stage::CleanFastForward:
        if (m_trackBranch) {
            runGit(Stage::SetUpstream,
                   {QStringLiteral("branch"),
                    QStringLiteral("--set-upstream-to=") + m_upstreamRef,
                    m_targetBranch});
        } else {
            complete();
        }
        break;
    case Stage::PrepareWorkingIndex:
        runGit(Stage::StageLocalChanges,
               {QStringLiteral("add"), QStringLiteral("-u"), QStringLiteral("--")},
               m_workingIndexPath);
        break;
    case Stage::StageLocalChanges:
        runGit(Stage::WriteLocalTree, {QStringLiteral("write-tree")}, m_workingIndexPath);
        break;
    case Stage::WriteLocalTree:
        if (!QRegularExpression(QStringLiteral("^[0-9a-fA-F]{40}$")).match(output).hasMatch()) {
            fail(tr("无法记录 %1 的本地修改，仓库未被修改。").arg(m_spec.displayName),
                 false, true);
            break;
        }
        runGit(Stage::PrepareBaseIndex, {QStringLiteral("read-tree"), QStringLiteral("HEAD")},
               m_baseIndexPath);
        break;
    case Stage::PrepareBaseIndex:
        runGit(Stage::CheckoutBaseSnapshot,
               {QStringLiteral("checkout-index"), QStringLiteral("-a"),
                QStringLiteral("--prefix=") + m_baseSnapshotPath + QStringLiteral("/")},
               m_baseIndexPath);
        break;
    case Stage::CheckoutBaseSnapshot:
        runGit(Stage::CheckoutLocalSnapshot,
               {QStringLiteral("checkout-index"), QStringLiteral("-a"),
                QStringLiteral("--prefix=") + m_localSnapshotPath + QStringLiteral("/")},
               m_workingIndexPath);
        break;
    case Stage::CheckoutLocalSnapshot:
        runGit(Stage::PrepareTargetIndex,
               {QStringLiteral("read-tree"), m_targetRef}, m_targetIndexPath);
        break;
    case Stage::PrepareTargetIndex:
        runGit(Stage::CheckoutTargetSnapshot,
               {QStringLiteral("checkout-index"), QStringLiteral("-a"),
                QStringLiteral("--prefix=") + m_targetSnapshotPath + QStringLiteral("/")},
               m_targetIndexPath);
        break;
    case Stage::CheckoutTargetSnapshot:
        beginMutation(QStringLiteral("safe-merge"));
        if (m_transactionActive) runSafeMerge();
        break;
    case Stage::UpdateSafeRef:
        if (!m_targetBranch.isEmpty()) {
            runGit(Stage::SwitchSafeHead,
                   {QStringLiteral("symbolic-ref"), QStringLiteral("HEAD"),
                    QStringLiteral("refs/heads/") + m_targetBranch});
        } else {
            complete();
        }
        break;
    case Stage::SwitchSafeHead:
        runGit(Stage::ResetSafeIndex,
               {QStringLiteral("reset"), QStringLiteral("--mixed"), m_targetRef});
        break;
    case Stage::ResetSafeIndex:
        if (m_trackBranch) {
            runGit(Stage::SetUpstream,
                   {QStringLiteral("branch"),
                    QStringLiteral("--set-upstream-to=") + m_upstreamRef,
                    m_targetBranch});
        } else {
            complete();
        }
        break;
    case Stage::SetUpstream:
        complete();
        break;
    case Stage::Idle:
        break;
    }
}

void RepositoryUpdateJob::resolveTarget(const QString &output)
{
    if (m_stage == Stage::ResolveStable) {
        m_targetRef = output.section(QLatin1Char('\n'), 0, 0).trimmed();
        m_targetBranch = QStringLiteral("master");
    } else if (m_stage == Stage::ResolveDevelopment) {
        m_targetRef = selectDevelopmentBranch(output);
        m_targetBranch = QStringLiteral("dev");
        m_trackBranch = true;
        m_upstreamRef = m_targetRef;
    } else {
        const QString remote = selectRemoteBranch(output, m_spec.localBranch);
        if (branchNeedsRecovery(m_spec.localBranch)) {
            m_targetRef = remote;
            m_targetBranch = m_spec.kind == RepositoryKind::Core
                ? QStringLiteral("minifox/version-core")
                : QStringLiteral("minifox/version-extension");
            m_trackBranch = true;
            m_upstreamRef = remote;
        } else {
            m_targetRef = remote.isEmpty() ? QStringLiteral("@{upstream}") : remote;
        }
    }
    if (m_targetRef.isEmpty()) {
        fail(tr("无法确定 %1 对应的远端版本，已跳过且未修改仓库。")
                 .arg(m_spec.displayName), false, true);
        return;
    }
    if (m_dirty) beginSafeMerge();
    else beginCleanMutation();
}

void RepositoryUpdateJob::beginMutation(const QString &phase)
{
    VersionTransactionEntry entry;
    entry.id = m_transactionId;
    entry.action = actionKey();
    entry.root = QDir::cleanPath(m_spec.root);
    entry.displayName = m_spec.displayName;
    entry.targetRef = m_targetRef;
    entry.targetBranch = m_targetBranch;
    entry.originalHead = m_originalHead;
    entry.phase = phase;
    entry.startedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QString error;
    if (!VersionTransactionStore::upsert(entry, &error)) {
        fail(tr("无法记录 %1 的版本操作状态，仓库未被修改：%2")
                 .arg(m_spec.displayName, error), false, true);
        return;
    }
    m_transactionActive = true;
}

void RepositoryUpdateJob::beginCleanMutation()
{
    emit statusChanged(tr("正在同步 %1…").arg(m_spec.displayName));
    beginMutation(QStringLiteral("fast-forward"));
    if (!m_transactionActive) return;
    if (!m_targetBranch.isEmpty()) {
        runGit(Stage::CleanCheckout,
               {QStringLiteral("checkout"), QStringLiteral("-B"),
                m_targetBranch, m_targetRef});
    } else {
        runGit(Stage::CleanFastForward,
               {QStringLiteral("merge"), QStringLiteral("--ff-only"), m_targetRef});
    }
}

void RepositoryUpdateJob::beginSafeMerge()
{
    emit statusChanged(tr("正在合并 %1 的本地修改与远端更新…").arg(m_spec.displayName));
    const QString id = QUuid::createUuid().toString(QUuid::Id128);
    const QString root = QDir(QDir::tempPath()).filePath(
        QStringLiteral("minifox-safe-merge-%1").arg(id));
    m_workingIndexPath = root + QStringLiteral(".index");
    m_baseIndexPath = root + QStringLiteral("-base.index");
    m_targetIndexPath = root + QStringLiteral("-target.index");
    m_baseSnapshotPath = root + QStringLiteral("-base");
    m_localSnapshotPath = root + QStringLiteral("-local");
    m_targetSnapshotPath = root + QStringLiteral("-target");
    if (!QDir().mkpath(m_baseSnapshotPath)
        || !QDir().mkpath(m_localSnapshotPath)
        || !QDir().mkpath(m_targetSnapshotPath)) {
        fail(tr("无法准备 %1 的安全更新快照，仓库未被修改。")
                 .arg(m_spec.displayName), false, true);
        return;
    }
    runGit(Stage::PrepareWorkingIndex,
           {QStringLiteral("read-tree"), QStringLiteral("HEAD")}, m_workingIndexPath);
}

void RepositoryUpdateJob::runSafeMerge()
{
    const SafeMergeRequest request {m_spec.gitProgram, m_spec.root,
                                    m_baseSnapshotPath, m_localSnapshotPath,
                                    m_targetSnapshotPath};
    auto *watcher = new QFutureWatcher<SafeMergeResult>(this);
    connect(watcher, &QFutureWatcher<SafeMergeResult>::finished, this,
            [this, watcher] {
        const SafeMergeResult result = watcher->result();
        watcher->deleteLater();
        handleSafeMergeFinished(result);
    });
    watcher->setFuture(QtConcurrent::run(SafeMergeEngine::run, request));
}

void RepositoryUpdateJob::handleSafeMergeFinished(const SafeMergeResult &result)
{
    if (!result.success) {
        QString detail;
        switch (result.error) {
        case SafeMergeError::SnapshotRead: detail = tr("无法读取安全更新快照：%1").arg(result.detail); break;
        case SafeMergeError::MergeProcess: detail = tr("无法合并本地文件：%1").arg(result.detail); break;
        case SafeMergeError::WorkingTreeChanged: detail = tr("安全更新期间文件再次发生变化：%1").arg(result.detail); break;
        case SafeMergeError::FileWrite: detail = tr("无法写入安全更新结果：%1").arg(result.detail); break;
        case SafeMergeError::None: break;
        }
        const bool unmodified = result.error != SafeMergeError::FileWrite;
        fail(tr("%1 的安全更新未完成。%2").arg(m_spec.displayName, detail),
             false, unmodified);
        return;
    }
    if (m_targetBranch.isEmpty()) {
        runGit(Stage::UpdateSafeRef,
               {QStringLiteral("reset"), QStringLiteral("--mixed"), m_targetRef});
    } else {
        runGit(Stage::UpdateSafeRef,
               {QStringLiteral("update-ref"),
                QStringLiteral("refs/heads/") + m_targetBranch, m_targetRef});
    }
}

void RepositoryUpdateJob::complete()
{
    QString error;
    if (m_transactionActive && !VersionTransactionStore::remove(m_transactionId, &error)) {
        fail(tr("%1 已更新，但无法清除中断标记：%2").arg(m_spec.displayName, error));
        return;
    }
    m_transactionActive = false;
    clearTemporaryFiles();
    emit finished({m_spec.root, m_spec.displayName, true, false, false, {}});
}

void RepositoryUpdateJob::fail(const QString &message, bool timedOut,
                               bool definitelyUnmodified)
{
    if (m_transactionActive && definitelyUnmodified && !m_hadInterruptedEntry) {
        QString ignored;
        if (VersionTransactionStore::remove(m_transactionId, &ignored)) {
            m_transactionActive = false;
        }
    }
    clearTemporaryFiles();
    emit finished({m_spec.root, m_spec.displayName, false,
                   m_transactionActive, timedOut, message});
}

void RepositoryUpdateJob::clearTemporaryFiles()
{
    for (const QString &path : {m_workingIndexPath, m_baseIndexPath, m_targetIndexPath}) {
        if (!path.isEmpty()) QFile::remove(path);
    }
    for (const QString &path : {m_baseSnapshotPath, m_localSnapshotPath, m_targetSnapshotPath}) {
        if (!path.isEmpty()) QDir(path).removeRecursively();
    }
    m_workingIndexPath.clear();
    m_baseIndexPath.clear();
    m_targetIndexPath.clear();
    m_baseSnapshotPath.clear();
    m_localSnapshotPath.clear();
    m_targetSnapshotPath.clear();
}

QString RepositoryUpdateJob::actionKey() const
{
    return m_spec.kind == RepositoryKind::Core
        ? QStringLiteral("core-update") : QStringLiteral("extension-update");
}
