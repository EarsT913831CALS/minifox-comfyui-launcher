#include <QCoreApplication>
#include "RepositoryUpdateJob.h"

#include "ProcessTextDecoder.h"
#include "VersionTransactionStore.h"
#include "RepositoryRecovery.h"

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
    QString storeError;
    const auto entries = VersionTransactionStore::entries(&storeError);
    if (!storeError.isEmpty()) {
        fail(tr("无法读取中断记录，已停止更新：%1").arg(storeError));
        return;
    }
    for (const VersionTransactionEntry &entry : entries) {
        if (QDir::cleanPath(entry.root).compare(QDir::cleanPath(m_spec.root),
                                                Qt::CaseInsensitive) == 0) {
            m_hadInterruptedEntry = true;
            emit statusChanged(tr("正在恢复 %1 上次未完成的更新…").arg(m_spec.displayName));
            auto *watcher = new QFutureWatcher<QString>(this);
            connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
                const QString error = watcher->result();
                watcher->deleteLater();
                if (!error.isEmpty()) { fail(error); return; }
                m_hadInterruptedEntry = false;
                m_started = false;
                start();
            });
            const QString git = m_spec.gitProgram;
            const RepositoryGitContext context{m_spec.gitPrefixArguments, m_spec.gitEnvironment};
            watcher->setFuture(QtConcurrent::run(SafeMergeEngine::workerPool(), [git, entry, context] {
                QString error;
                if (!RepositoryRecovery::rollback(git, entry, &error, context)) return error;
                return QString();
            }));
            return;
        }
    }
    emit statusChanged(tr("正在检查 %1 的仓库状态…").arg(m_spec.displayName));
    runGit(Stage::Status, {QStringLiteral("status"), QStringLiteral("--porcelain"),
                           QStringLiteral("--untracked-files=no"),
                           QStringLiteral("--no-renames"), QStringLiteral("-z")});
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

QString RepositoryUpdateJob::selectExtensionRemoteBranch(const QString &output,
                                                         const QString &localBranch)
{
    QStringList candidates;
    QString upstream;
    for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
        const QString ref = line.section('\t', 0, 0).trimmed();
        if (ref == localBranch) upstream = line.section('\t', 1, 1).trimmed();
        if (ref.startsWith("origin/") && ref != "origin/HEAD") candidates.append(ref);
    }
    if (!branchNeedsRecovery(localBranch) && candidates.contains("origin/" + localBranch))
        return "origin/" + localBranch;
    if (candidates.contains(upstream)) return upstream;
    for (const QString &preferred : {QStringLiteral("origin/main"), QStringLiteral("origin/master"), QStringLiteral("origin/dev")})
        if (candidates.contains(preferred)) return preferred;
    return candidates.value(0);
}

bool RepositoryUpdateJob::branchNeedsRecovery(const QString &branch)
{
    return branch.isEmpty()
        || branch.startsWith(QStringLiteral("HEAD"), Qt::CaseInsensitive)
        || branch.startsWith(QStringLiteral("minifox/"), Qt::CaseInsensitive);
}

void RepositoryUpdateJob::runGit(Stage stage, const QStringList &arguments,
                                 const QString &indexFile, int inactivityTimeoutMs,
                                 const QByteArray &standardInput)
{
    m_stage = stage;
    QProcessEnvironment environment = m_spec.gitEnvironment;
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GCM_INTERACTIVE"), QStringLiteral("Never"));
    if (!indexFile.isEmpty()) environment.insert(QStringLiteral("GIT_INDEX_FILE"), indexFile);
    QStringList processArguments = repositoryArguments(arguments);
    if (stage == Stage::UpdateSafeRef || stage == Stage::SwitchSafeHead
        || stage == Stage::ResetSafeIndex || stage == Stage::SetUpstream)
        processArguments = QStringList{"-c", "core.fsync=all"} + processArguments;
    m_runner.start(m_spec.gitProgram, processArguments,
                   environment, inactivityTimeoutMs, standardInput);
}

void RepositoryUpdateJob::runCheckoutIndex(Stage stage, const QString &snapshotPath,
                                           const QStringList &paths,
                                           const QString &indexFile)
{
    if (paths.isEmpty()) {
        // Advance without spawning Git.
        m_stage = stage;
        handleProcessFinished(ProcessResult{0, QProcess::NormalExit,
                                            QByteArray(), QByteArray(), false, false});
        return;
    }
    runGit(stage, {QStringLiteral("checkout-index"),
                   QStringLiteral("--prefix=") + snapshotPath + QStringLiteral("/"),
                   QStringLiteral("--stdin"), QStringLiteral("-z")},
           indexFile, 120000, SafeMergeEngine::encodePathList(paths));
}

QStringList RepositoryUpdateJob::repositoryArguments(const QStringList &arguments) const
{
    QStringList result = m_spec.gitPrefixArguments;
    result.append({QStringLiteral("-C"), m_spec.root});
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
        m_localEntries = SafeMergeEngine::parseStatusEntries(result.standardOutput);
        emit statusChanged(tr("正在读取 %1 的当前版本…").arg(m_spec.displayName));
        runGit(Stage::ReadHead, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")});
        break;
    case Stage::ReadHead:
        m_originalHead = output.section(QLatin1Char('\n'), 0, 0).trimmed();
        if (m_spec.target == RepositoryTarget::Explicit) {
            m_targetBranch = m_spec.explicitBranch;
            m_upstreamRef = m_spec.explicitUpstream;
            m_trackBranch = !m_upstreamRef.isEmpty();
            runGit(Stage::ResolveTargetCommit,
                   {"rev-parse", "--verify", m_spec.explicitRef + "^{commit}"});
            break;
        }
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
            QStringList args = m_spec.target == RepositoryTarget::CurrentBranch
                ? QStringList {QStringLiteral("for-each-ref"), QStringLiteral("--contains=HEAD"),
                               QStringLiteral("--format=%(refname:short)"),
                               QStringLiteral("refs/remotes/origin/")}
                : QStringList {QStringLiteral("for-each-ref"),
                               QStringLiteral("--format=%(refname:short)"),
                               QStringLiteral("refs/remotes/origin/")};
            if (m_spec.kind == RepositoryKind::Extension) {
                args = {"for-each-ref", "--contains=HEAD",
                        "--format=%(refname:short)%09%(upstream:short)",
                        "refs/remotes/origin/", "refs/heads/"};
            }
            runGit(stage, args);
        }
        break;
    case Stage::ResolveStable:
    case Stage::ResolveDevelopment:
    case Stage::ResolveCurrent:
        resolveTarget(output);
        break;
    case Stage::ResolveTargetCommit:
        m_targetRef = output;
        if (m_spec.kind == RepositoryKind::Extension && m_spec.target == RepositoryTarget::Explicit
            && m_spec.explicitBranch.isEmpty()) {
            runGit(Stage::ResolveExplicitBranch,
                   {"for-each-ref", "--contains=" + m_targetRef,
                    "--format=%(refname:short)%09%(upstream:short)",
                    "refs/remotes/origin/", "refs/heads/"});
            break;
        }
        if (m_targetRef == m_originalHead && m_targetBranch.isEmpty()
            && !m_hadInterruptedEntry) {
            complete();
        } else if (m_spec.target == RepositoryTarget::CurrentBranch && m_targetBranch.isEmpty()) {
            runGit(Stage::VerifyFastForward, {"merge-base", "--is-ancestor", m_originalHead, m_targetRef});
        } else beginSafeMerge();
        break;
    case Stage::VerifyFastForward:
        beginSafeMerge();
        break;
    case Stage::ResolveExplicitBranch:
        m_upstreamRef = selectExtensionRemoteBranch(output, m_spec.localBranch);
        m_targetBranch = m_upstreamRef.isEmpty() ? QString() : m_upstreamRef.mid(7);
        m_trackBranch = !m_upstreamRef.isEmpty();
        m_detachTarget = m_upstreamRef.isEmpty();
        beginSafeMerge();
        break;
    case Stage::PrepareWorkingIndex:
        if (m_plan.basePaths.isEmpty()) {
            runGit(Stage::WriteLocalTree, {QStringLiteral("write-tree")}, m_workingIndexPath);
        } else {
            runGit(Stage::StageLocalChanges,
                   {QStringLiteral("--literal-pathspecs"), QStringLiteral("add"),
                    QStringLiteral("-u"), QStringLiteral("--pathspec-from-file=-"),
                    QStringLiteral("--pathspec-file-nul")},
                   m_workingIndexPath, 120000, SafeMergeEngine::encodePathList(m_plan.basePaths));
        }
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
        runGit(Stage::PrepareBaseIndex, {QStringLiteral("read-tree"), m_originalHead},
               m_baseIndexPath);
        break;
    case Stage::ResolveRemotePaths:
        m_plan = SafeMergeEngine::planSnapshots(
            m_localEntries,
            SafeMergeEngine::parseNameStatusEntries(result.standardOutput));
        runGit(Stage::PrepareWorkingIndex,
               {QStringLiteral("read-tree"), m_originalHead}, m_workingIndexPath);
        break;
    case Stage::PrepareBaseIndex:
        runCheckoutIndex(Stage::CheckoutBaseSnapshot, m_baseSnapshotPath,
                         m_plan.basePaths, m_baseIndexPath);
        break;
    case Stage::CheckoutBaseSnapshot:
        runCheckoutIndex(Stage::CheckoutLocalSnapshot, m_localSnapshotPath,
                         m_plan.localPaths, m_workingIndexPath);
        break;
    case Stage::CheckoutLocalSnapshot:
        runGit(Stage::PrepareTargetIndex,
               {QStringLiteral("read-tree"), m_targetRef}, m_targetIndexPath);
        break;
    case Stage::PrepareTargetIndex:
        runCheckoutIndex(Stage::CheckoutTargetSnapshot, m_targetSnapshotPath,
                         m_plan.targetPaths, m_targetIndexPath);
        break;
    case Stage::CheckoutTargetSnapshot:
        beginMutation(QStringLiteral("preparing"));
        break;
    case Stage::UpdateSafeRef:
        if (m_detachTarget) {
            runGit(Stage::ResetSafeIndex, {"reset", "--mixed", m_targetRef});
        } else if (!m_targetBranch.isEmpty()) {
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
        const QString remote = m_spec.kind == RepositoryKind::Extension
            ? selectExtensionRemoteBranch(output, m_spec.localBranch)
            : selectRemoteBranch(output, m_spec.localBranch);
        if (branchNeedsRecovery(m_spec.localBranch)) {
            m_targetRef = remote;
            m_targetBranch = remote.startsWith("origin/") ? remote.mid(7) : QString();
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
    // Resolve the moving remote ref once. Preserve its name separately for upstream.
    runGit(Stage::ResolveTargetCommit,
           {QStringLiteral("rev-parse"), QStringLiteral("--verify"),
            m_targetRef + QStringLiteral("^{commit}")});
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
    entry.upstreamRef = m_trackBranch ? m_upstreamRef : QString();
    entry.detachedTarget = m_detachTarget;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const QString error = watcher->result();
        watcher->deleteLater();
        if (!error.isEmpty()) {
            fail(tr("无法记录 %1 的版本操作状态，仓库未被修改：%2")
                     .arg(m_spec.displayName, error), false, true);
            return;
        }
        m_transactionActive = true;
        runSafeMerge();
    });
    watcher->setFuture(QtConcurrent::run(SafeMergeEngine::workerPool(), [entry] {
        QString error;
        if (!VersionTransactionStore::upsert(entry, &error)) return error;
        return QString();
    }));
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
    // Local-only changes stay in place; only target changes need snapshots.
    runGit(Stage::ResolveRemotePaths,
           {QStringLiteral("diff"), QStringLiteral("--no-renames"),
            QStringLiteral("--name-status"), QStringLiteral("-z"),
            m_originalHead, m_targetRef});
}

void RepositoryUpdateJob::runSafeMerge()
{
    const SafeMergeRequest request {m_spec.gitProgram, m_spec.root,
                                    m_baseSnapshotPath, m_localSnapshotPath,
                                    m_targetSnapshotPath, m_transactionId, {m_spec.gitPrefixArguments, m_spec.gitEnvironment}};
    auto *watcher = new QFutureWatcher<SafeMergeResult>(this);
    connect(watcher, &QFutureWatcher<SafeMergeResult>::finished, this,
            [this, watcher] {
        const SafeMergeResult result = watcher->result();
        watcher->deleteLater();
        handleSafeMergeFinished(result);
    });
    watcher->setFuture(QtConcurrent::run(SafeMergeEngine::workerPool(),
                                          SafeMergeEngine::run, request));
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
        case SafeMergeError::RecoveryBackup: detail = tr("无法保存恢复资料：%1").arg(result.detail); break;
        case SafeMergeError::None: break;
        }
        const bool unmodified = result.error != SafeMergeError::FileWrite;
        fail(tr("%1 的安全更新未完成。%2").arg(m_spec.displayName, detail),
             false, unmodified);
        return;
    }
    if (m_detachTarget) {
        runGit(Stage::UpdateSafeRef, {"update-ref", "--no-deref", "HEAD", m_targetRef});
    } else if (m_targetBranch.isEmpty()) {
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
    if (m_transactionActive && !m_completionVerified) {
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
            const QString error = watcher->result();
            watcher->deleteLater();
            if (!error.isEmpty()) { fail(error); return; }
            m_completionVerified = true;
            complete();
        });
        const QString id = m_transactionId, git = m_spec.gitProgram;
        const RepositoryGitContext context{m_spec.gitPrefixArguments, m_spec.gitEnvironment};
        watcher->setFuture(QtConcurrent::run(SafeMergeEngine::workerPool(), [id, git, context] {
            QString error;
            const auto entries = VersionTransactionStore::entries(&error);
            for (auto entry : entries) {
                if (entry.id != id) continue;
                if (!RepositoryRecovery::verify(git, entry, &error, context)) return error;
                entry.phase = "committed";
                if (!VersionTransactionStore::upsert(entry, &error)) return error;
                return QString();
            }
            return error.isEmpty() ? QCoreApplication::translate("RepositoryUpdateJob", "恢复记录丢失，已停止完成操作。") : error;
        }));
        return;
    }
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
                   m_transactionActive || m_hadInterruptedEntry, timedOut, message});
}

void RepositoryUpdateJob::clearTemporaryFiles()
{
    SafeMergeEngine::discardSnapshots({m_workingIndexPath, m_baseIndexPath, m_targetIndexPath,
                                       m_baseSnapshotPath, m_localSnapshotPath, m_targetSnapshotPath});
    m_workingIndexPath.clear();
    m_baseIndexPath.clear();
    m_targetIndexPath.clear();
    m_baseSnapshotPath.clear();
    m_localSnapshotPath.clear();
    m_targetSnapshotPath.clear();
    m_localEntries.clear();
    m_plan = SparseSnapshotPaths{};
}

QString RepositoryUpdateJob::actionKey() const
{
    return m_spec.kind == RepositoryKind::Core
        ? QStringLiteral("core-update") : QStringLiteral("extension-update");
}
