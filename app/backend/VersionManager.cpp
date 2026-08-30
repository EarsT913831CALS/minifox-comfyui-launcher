#include "VersionManager.h"

#include "ApplicationSettings.h"
#include "ConfigurationManager.h"
#include "PortablePaths.h"
#include "ProcessTextDecoder.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QHash>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace {

constexpr auto kOfficialComfyUiRemote = "https://github.com/Comfy-Org/ComfyUI.git";
constexpr auto kCnbComfyUiRemote = "https://cnb.cool/IndexMirror/ComfyUI.git";
constexpr qsizetype kMaximumCatalogBytes = 16 * 1024 * 1024;
constexpr qsizetype kMaximumCatalogEntries = 20000;

Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

bool isManagedExtensionDirectory(const QString &customNodesPath, const QString &targetPath)
{
    const QFileInfo customNodes(customNodesPath);
    const QFileInfo target(targetPath);
    if (!customNodes.isDir() || !target.isDir() || target.isSymLink()) {
        return false;
    }

    const QString rootAbsolute = QDir::cleanPath(customNodes.absoluteFilePath());
    const QString rootCanonical = QDir::cleanPath(customNodes.canonicalFilePath());
    const QString targetParent = QDir::cleanPath(target.absolutePath());
    const QString targetCanonical = target.canonicalFilePath();
    if (rootCanonical.isEmpty() || targetCanonical.isEmpty()) {
        return false;
    }
    const QString targetCanonicalParent = QDir::cleanPath(
        QFileInfo(targetCanonical).absolutePath());
    return targetParent.compare(rootAbsolute, pathCaseSensitivity()) == 0
        && targetCanonicalParent.compare(rootCanonical, pathCaseSensitivity()) == 0;
}

bool isIgnoredCoreWorkingTreeEntry(const QString &statusLine)
{
    if (statusLine.size() < 3) {
        return false;
    }
    // QProcess output is trimmed before it reaches this parser. When the first
    // porcelain entry is an unstaged change (for example " D path"), that
    // removes its leading index-column space and leaves "D path".
    const int pathOffset = statusLine.size() >= 3 && statusLine.at(2) == QLatin1Char(' ')
        ? 3
        : (statusLine.at(1) == QLatin1Char(' ') ? 2 : -1);
    if (pathOffset < 0) {
        return false;
    }
    QString path = statusLine.mid(pathOffset).trimmed();
    const int renameSeparator = path.indexOf(QStringLiteral(" -> "));
    if (renameSeparator >= 0) {
        path = path.mid(renameSeparator + 4).trimmed();
    }
    if (path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"'))) {
        path = path.mid(1, path.size() - 2);
    }
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return path == QStringLiteral("output/_output_images_will_be_put_here");
}

bool hasMeaningfulCoreWorkingTreeChanges(const QString &output)
{
    const QStringList lines = output.split(
        QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (!line.startsWith(QStringLiteral("##"))
            && !isIgnoredCoreWorkingTreeEntry(line)) {
            return true;
        }
    }
    return false;
}

} // namespace

VersionManager::VersionManager(ConfigurationManager *configuration,
                               ApplicationSettings *settings,
                               QObject *parent)
    : QObject(parent),
      m_configuration(configuration),
      m_settings(settings)
{
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_gitTimeout.setSingleShot(true);
    connect(&m_gitTimeout, &QTimer::timeout, this, [this] {
        if (m_process.state() != QProcess::NotRunning) m_process.kill();
    });
    connect(&m_process, &QProcess::finished,
            this, &VersionManager::handleProcessFinished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            if (m_operation == Operation::CreateBackupArchive) {
                const QString archivePath = m_backupArchivePath;
                clearBackupState();
                QFile::remove(archivePath);
                setFailure(tr("无法启动 Windows 归档工具，已取消操作且未修改仓库。"));
            } else {
                setFailure(tr("无法启动 Git。请安装 Git for Windows 后重试。"));
            }
        }
    });
    m_dependencyProcess.setProcessChannelMode(QProcess::MergedChannels);
    m_dependencyTimeout.setSingleShot(true);
    connect(&m_dependencyTimeout, &QTimer::timeout, this, [this] {
        m_dependencyTimedOut = true;
        if (m_dependencyProcess.state() != QProcess::NotRunning) m_dependencyProcess.kill();
    });
    connect(&m_dependencyProcess, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_dependencyInstallPhase) {
            handleDependencyInstallFinished(exitCode, exitStatus);
        } else {
            handleDependencyCheckFinished(exitCode, exitStatus);
        }
    });
    connect(&m_dependencyProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            if (m_dependencyInstallPhase) {
                handleDependencyInstallFinished(-1, QProcess::CrashExit);
            } else {
                handleDependencyCheckFinished(-1, QProcess::CrashExit);
            }
        }
    });
    connect(m_configuration, &ConfigurationManager::currentProfileChanged, this, [this] {
        if (!m_busy && m_comfyRoot != m_configuration->comfyRoot()) {
            QTimer::singleShot(150, this, &VersionManager::loadLocalState);
        }
    });
    connect(m_settings, &ApplicationSettings::versionControlChanged,
            this, &VersionManager::stateChanged);
    QTimer::singleShot(0, this, &VersionManager::loadLocalState);
}

bool VersionManager::busy() const { return m_busy || m_catalogLoading; }
bool VersionManager::installingDependencies() const { return m_installingDependencies; }
bool VersionManager::updating() const { return m_notifyOnFinish && m_busy; }
bool VersionManager::catalogLoading() const { return m_catalogLoading; }
bool VersionManager::repository() const { return m_repository; }
bool VersionManager::dirty() const { return m_dirty; }
bool VersionManager::canCheck() const { return m_repository && !m_busy && !m_gitProgram.isEmpty(); }
bool VersionManager::canUpdate() const
{
    return canCheck() && (m_settings->resetTrackedFilesOnUpdate() || !m_dirty);
}
QString VersionManager::launcherVersion() const { return QCoreApplication::applicationVersion(); }
QString VersionManager::qtVersion() const { return QString::fromLatin1(qVersion()); }
QString VersionManager::platformText() const
{
    return QStringLiteral("%1 %2 · %3")
        .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(),
             QSysInfo::buildCpuArchitecture());
}
QString VersionManager::comfyRoot() const { return m_comfyRoot; }
QString VersionManager::comfyVersion() const { return m_comfyVersion; }
QString VersionManager::branch() const { return m_branch; }
QString VersionManager::commit() const { return m_commit; }
QString VersionManager::commitFull() const { return m_commitFull; }
int VersionManager::networkRoute() const { return m_networkRoute; }
void VersionManager::setNetworkRoute(int route)
{
    const int normalized = route == 1 ? 1 : 0;
    if (m_networkRoute == normalized) return;
    m_networkRoute = normalized;
    emit stateChanged();
}
QString VersionManager::commitDate() const { return m_commitDate; }
QString VersionManager::commitSubject() const { return m_commitSubject; }
QString VersionManager::remoteUrl() const { return m_remoteUrl; }
QVariantList VersionManager::coreVersions() const { return m_coreVersions; }
QVariantList VersionManager::stableVersions() const { return m_stableVersions; }
QVariantList VersionManager::installedExtensions() const { return m_installedExtensions; }
QVariantList VersionManager::availableExtensions() const { return m_availableExtensions; }
QVariantList VersionManager::extensionVersions() const { return m_extensionVersions; }
QString VersionManager::statusMessage() const { return m_statusMessage; }
QString VersionManager::lastError() const { return m_lastError; }
int VersionManager::aheadCount() const { return m_aheadCount; }
int VersionManager::behindCount() const { return m_behindCount; }

QString VersionManager::repositoryState() const
{
    if (!m_repository) {
        return tr("未检测到 Git 仓库");
    }
    return m_dirty ? tr("有未提交更改") : tr("工作区干净");
}

QString VersionManager::updateState() const
{
    if (m_behindCount > 0 && m_aheadCount > 0) {
        return tr("领先 %1 个提交，落后 %2 个提交").arg(m_aheadCount).arg(m_behindCount);
    }
    if (m_behindCount > 0) {
        return tr("有 %1 个提交可更新").arg(m_behindCount);
    }
    if (m_aheadCount > 0) {
        return tr("本地领先 %1 个提交").arg(m_aheadCount);
    }
    return tr("已是最新版本");
}

void VersionManager::refresh()
{
    refreshCore();
}

void VersionManager::refreshCore()
{
    if (busy()) return;

    m_refreshScope = RefreshScope::Core;
    m_refreshErrors.clear();
    m_comfyRoot = m_configuration->comfyRoot();
    m_comfyVersion = readComfyVersion(m_comfyRoot);
    m_branch.clear();
    m_commit.clear();
    m_commitFull.clear();
    m_commitDate.clear();
    m_commitSubject.clear();
    m_remoteUrl.clear();
    m_fullRefresh = true;
    m_remoteUnavailable = false;
    m_lastError.clear();
    m_repository = false;
    m_dirty = false;
    m_aheadCount = 0;
    m_behindCount = 0;
    m_gitProgram = findGit();

    if (!QFileInfo::exists(m_comfyRoot + QStringLiteral("/main.py"))) {
        setFailure(tr("请先在高级选项中选择 ComfyUI 根目录。"));
        return;
    }
    if (m_gitProgram.isEmpty()) {
        setFailure(tr("未找到 Git for Windows，无法刷新核心版本列表。"));
        return;
    }
    startGit(Operation::RefreshStatus,
             repositoryArguments(m_comfyRoot,
                 {QStringLiteral("status"), QStringLiteral("--short"), QStringLiteral("--branch")}));
}

void VersionManager::refreshInstalledExtensions()
{
    if (busy()) return;

    m_refreshScope = RefreshScope::InstalledExtensions;
    m_refreshErrors.clear();
    m_lastError.clear();
    m_remoteUnavailable = false;
    m_comfyRoot = m_configuration->comfyRoot();
    m_gitProgram = findGit();
    if (!QFileInfo::exists(m_comfyRoot + QStringLiteral("/custom_nodes"))) {
        setFailure(tr("未找到 ComfyUI 的 custom_nodes 目录。"));
        return;
    }
    if (m_gitProgram.isEmpty()) {
        setFailure(tr("未找到 Git for Windows，无法检测扩展更新。"));
        return;
    }

    m_busy = true;
    m_statusMessage = tr("正在检测已安装扩展…");
    scanExtensions(true);
    beginExtensionChecks();
}

void VersionManager::refreshAvailableExtensions()
{
    if (busy()) return;

    m_refreshScope = RefreshScope::AvailableExtensions;
    m_refreshErrors.clear();
    m_lastError.clear();
    downloadCatalog();
}

void VersionManager::loadLocalState()
{
    if (m_busy) return;
    m_comfyRoot = m_configuration->comfyRoot();
    m_comfyVersion = readComfyVersion(m_comfyRoot);
    m_branch.clear();
    m_commit.clear();
    m_commitFull.clear();
    m_commitDate.clear();
    m_commitSubject.clear();
    m_remoteUrl.clear();
    m_lastError.clear();
    m_repository = false;
    m_dirty = false;
    m_aheadCount = 0;
    m_behindCount = 0;
    m_fullRefresh = false;
    m_remoteUnavailable = false;
    m_gitProgram = findGit();
    scanExtensions(false);

    QFile cached(catalogCachePath());
    if (m_catalogExtensions.isEmpty() && cached.open(QIODevice::ReadOnly)) {
        applyCatalogData(cached.readAll());
    }

    if (!QFileInfo::exists(m_comfyRoot + QStringLiteral("/main.py"))) {
        finish(tr("请先在高级选项中选择 ComfyUI 根目录。"));
        return;
    }
    if (m_gitProgram.isEmpty()) {
        finish(tr("已读取本地版本；安装 Git for Windows 后可检查和更新版本。"));
        return;
    }
    startGit(Operation::RefreshStatus,
             repositoryArguments(m_comfyRoot,
                 {QStringLiteral("status"), QStringLiteral("--short"), QStringLiteral("--branch")}));
}

void VersionManager::checkForUpdates()
{
    if (!canCheck()) {
        return;
    }
    m_lastError.clear();
    startGit(Operation::Fetch,
             repositoryArguments(m_comfyRoot,
                 {QStringLiteral("fetch"), QStringLiteral("--quiet")}));
}

void VersionManager::updateComfyUi(int channel)
{
    resetOperationBackupSummary();
    m_notifyOnFinish = true;
    m_requestedCoreChannel = channel == 1 ? 1 : 0;
    m_pendingCompletionMessage = m_requestedCoreChannel == 0
        ? tr("ComfyUI 稳定版已更新到最新版本。")
        : tr("ComfyUI 开发版已更新到最新版本。");
    if (!canUpdate()) {
        setFailure(m_dirty
            ? tr("安全更新不会覆盖未提交更改。请先提交或移走更改，或启用“重置已跟踪文件”。")
            : tr("当前 ComfyUI 状态不允许更新，请先刷新内核列表。"));
        return;
    }
    m_lastError.clear();
    if (m_settings->resetTrackedFilesOnUpdate()) {
        beginResetAction(PendingCoreAction::Update, m_comfyRoot);
    } else {
        startGit(Operation::PrepareCoreUpdateFetch,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("fetch"), QStringLiteral("--tags"), QStringLiteral("--quiet")}));
    }
}

void VersionManager::switchCoreVersion(const QString &commit, int channel)
{
    if (commit.trimmed().isEmpty()) return;
    resetOperationBackupSummary();
    if (!canUpdate()) {
        setFailure(m_dirty
            ? tr("安全更新不会覆盖未提交更改。请先提交或移走更改，或启用“重置已跟踪文件”。")
            : tr("当前 ComfyUI 状态不允许切换版本，请先刷新内核列表。"));
        return;
    }
    m_notifyOnFinish = true;
    m_pendingCompletionMessage = tr("核心版本已切换。");
    m_requestedCoreChannel = channel == 1 ? 1 : 0;
    m_pendingCommit = commit.trimmed();
    if (m_settings->resetTrackedFilesOnUpdate()) {
        beginResetAction(PendingCoreAction::SwitchVersion, m_comfyRoot);
    } else {
        startGit(Operation::CheckoutCore,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("checkout"), QStringLiteral("-B"),
                      m_requestedCoreChannel == 0
                          ? QStringLiteral("master")
                          : QStringLiteral("dev"),
                      m_pendingCommit}));
    }
}

void VersionManager::switchBranch(const QString &branch, int repositorySource)
{
    resetOperationBackupSummary();
    m_notifyOnFinish = true;
    m_pendingCompletionMessage = tr("分支已切换。");
    if (branch.trimmed().isEmpty()) {
        setFailure(tr("请输入有效的分支名称。"));
        return;
    }
    if (!canUpdate()) {
        setFailure(m_dirty
            ? tr("安全更新不会覆盖未提交更改。请先提交或移走更改，或启用“重置已跟踪文件”。")
            : tr("当前 ComfyUI 状态不允许切换分支，请先刷新内核列表。"));
        return;
    }
    m_pendingBranch = branch.trimmed();
    m_pendingBranchRemoteUrl = repositorySource == 1
        ? QString::fromLatin1(kCnbComfyUiRemote)
        : QString::fromLatin1(kOfficialComfyUiRemote);
    if (m_settings->resetTrackedFilesOnUpdate()) {
        beginResetAction(PendingCoreAction::SwitchBranch, m_comfyRoot);
    } else {
        startGit(Operation::SetCoreBranchRemote,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("remote"), QStringLiteral("set-url"),
                      QStringLiteral("origin"), m_pendingBranchRemoteUrl}));
    }
}

void VersionManager::cleanComfyUiRepository()
{
    resetOperationBackupSummary();
    m_notifyOnFinish = true;
    if (!canCheck()) {
        setFailure(tr("当前 ComfyUI 状态不允许完全清理，请先刷新内核列表。"));
        return;
    }
    m_lastError.clear();
    beginResetAction(PendingCoreAction::FullClean, m_comfyRoot);
}

void VersionManager::updateExtension(const QString &path)
{
    if (m_busy || !QFileInfo::exists(QDir(path).filePath(QStringLiteral(".git")))) return;
    resetOperationBackupSummary();
    m_notifyOnFinish = true;
    m_pendingCompletionMessage = tr("扩展更新完成。");
    m_updatingAllExtensions = false;
    m_extensionUpdateQueue.clear();
    startExtensionUpdate(path);
}

void VersionManager::updateAllExtensions()
{
    if (m_busy) return;
    resetOperationBackupSummary();
    m_notifyOnFinish = true;
    m_pendingCompletionMessage = tr("全部扩展更新完成。");
    if (m_gitProgram.isEmpty()) {
        setFailure(tr("未找到 Git for Windows，无法更新扩展。"));
        return;
    }
    m_extensionUpdateQueue.clear();
    for (const QVariant &entry : std::as_const(m_installedExtensions)) {
        const QVariantMap extension = entry.toMap();
        const QString status = extension.value(QStringLiteral("status")).toString();
        const QString branch = extension.value(QStringLiteral("branch")).toString();
        if (extension.value(QStringLiteral("repository")).toBool()
            && (status == QStringLiteral("outdated") || branchNeedsRecovery(branch))) {
            m_extensionUpdateQueue.append(extension.value(QStringLiteral("path")).toString());
        }
    }
    if (m_extensionUpdateQueue.isEmpty()) {
        finish(tr("没有可更新的扩展。"));
        return;
    }
    m_updatingAllExtensions = true;
    m_operationPath = m_extensionUpdateQueue.takeFirst();
    startExtensionUpdate(m_operationPath);
}

void VersionManager::startExtensionUpdate(const QString &path)
{
    m_operationPath = path;
    m_targetRemoteBranch.clear();
    startGit(Operation::ValidateExtensionUpdate,
             repositoryArguments(path,
                 {QStringLiteral("status"), QStringLiteral("--porcelain")}));
}

void VersionManager::loadExtensionVersions(const QString &path, const QString &currentCommit)
{
    if (m_busy || !QFileInfo::exists(QDir(path).filePath(QStringLiteral(".git")))) {
        return;
    }
    m_operationPath = path;
    m_extensionHistoryCurrentCommit = currentCommit.trimmed();
    m_extensionVersions.clear();
    m_lastError.clear();
    startGit(Operation::LoadExtensionHistory,
             repositoryArguments(path,
                 {QStringLiteral("log"), QStringLiteral("--all"), QStringLiteral("-100"),
                  QStringLiteral("--date=format-local:%Y-%m-%d %H:%M:%S"),
                  QStringLiteral("--format=%h%x1f%H%x1f%ad%x1f%s%x1e")}));
}

void VersionManager::switchExtensionVersion(const QString &path, const QString &commit)
{
    if (m_busy || commit.trimmed().isEmpty()
        || !QFileInfo::exists(QDir(path).filePath(QStringLiteral(".git")))) return;
    resetOperationBackupSummary();
    m_notifyOnFinish = true;
    m_pendingCompletionMessage = tr("扩展版本已切换。");
    m_operationPath = path;
    m_pendingCommit = commit.trimmed();
    startGit(Operation::ValidateExtensionCheckout,
             repositoryArguments(path,
                 {QStringLiteral("status"), QStringLiteral("--porcelain")}));
}

void VersionManager::installExtension(const QString &url)
{
    if (m_busy) return;
    m_notifyOnFinish = true;
    m_pendingCompletionMessage = tr("扩展安装完成。");
    if (url.trimmed().isEmpty()) {
        setFailure(tr("请输入有效的扩展 URL。"));
        return;
    }
    if (m_gitProgram.isEmpty()) {
        setFailure(tr("未找到 Git for Windows，无法安装扩展。"));
        return;
    }
    QString name = QUrl(url.trimmed()).path().section(QLatin1Char('/'), -1);
    if (name.endsWith(QStringLiteral(".git"), Qt::CaseInsensitive)) name.chop(4);
    if (name.isEmpty()) {
        setFailure(tr("扩展 URL 无效。"));
        return;
    }
    m_operationPath = QDir(m_comfyRoot).filePath(QStringLiteral("custom_nodes/") + name);
    QStringList arguments = networkRouteArguments();
    arguments.append({QStringLiteral("clone"), QStringLiteral("--depth=1"),
                      url.trimmed(), m_operationPath});
    startGit(Operation::InstallExtension, arguments);
}

void VersionManager::removeExtension(const QString &path)
{
    const QString customNodesPath =
        QDir(m_comfyRoot).filePath(QStringLiteral("custom_nodes"));
    const QFileInfo target(path);
    if (m_busy || !isManagedExtensionDirectory(customNodesPath, path)) {
        return;
    }
    QDir directory(target.absoluteFilePath());
    if (!directory.removeRecursively()) {
        setFailure(tr("无法卸载扩展：%1").arg(target.fileName()));
        return;
    }
    scanExtensions();
    finish(tr("扩展已卸载：%1").arg(target.fileName()));
}

void VersionManager::setExtensionEnabled(const QString &path, bool enabled)
{
    if (m_busy) return;
    const QString customNodesPath =
        QDir(m_comfyRoot).filePath(QStringLiteral("custom_nodes"));
    const QFileInfo source(path);
    if (!isManagedExtensionDirectory(customNodesPath, path)) return;
    QString targetName = source.fileName();
    if (enabled && targetName.endsWith(QStringLiteral(".disabled"))) {
        targetName.chop(9);
    } else if (!enabled && !targetName.endsWith(QStringLiteral(".disabled"))) {
        targetName.append(QStringLiteral(".disabled"));
    } else {
        return;
    }
    QDir parent(source.absolutePath());
    if (!parent.rename(source.fileName(), targetName)) {
        setFailure(tr("无法更改扩展启用状态。"));
        return;
    }
    scanExtensions();
    emit stateChanged();
}

void VersionManager::retranslate()
{
    emit stateChanged();
}

void VersionManager::startGit(Operation operation, const QStringList &arguments)
{
    startProcess(operation, m_gitProgram, arguments);
}

void VersionManager::startProcess(Operation operation, const QString &program,
                                  const QStringList &arguments)
{
    m_operation = operation;
    m_busy = true;
    switch (operation) {
    case Operation::RefreshStatus: m_statusMessage = tr("正在读取 ComfyUI 版本…"); break;
    case Operation::RefreshLog: m_statusMessage = tr("正在读取提交信息…"); break;
    case Operation::Fetch: m_statusMessage = tr("正在检查远程更新…"); break;
    case Operation::ResolveCoreCompareBranch: m_statusMessage = tr("正在匹配远端分支…"); break;
    case Operation::Compare: m_statusMessage = tr("正在比较版本…"); break;
    case Operation::CollectTrackedBackup:
    case Operation::CollectUntrackedBackup:
    case Operation::CreateBackupArchive: m_statusMessage = tr("正在备份将受影响的文件…"); break;
    case Operation::ResetCoreForUpdate:
    case Operation::PrepareCoreUpdateFetch:
    case Operation::ResolveStableUpdateCommit:
    case Operation::ResolveDevelopmentUpdateBranch:
    case Operation::ResolveCoreUpdateBranch:
    case Operation::AttachCoreUpdateBranch:
    case Operation::SetCoreUpdateUpstream:
    case Operation::Pull: m_statusMessage = tr("正在更新 ComfyUI…"); break;
    case Operation::ResolveDevelopmentHistoryBranch:
    case Operation::LoadCoreHistory: m_statusMessage = tr("正在读取版本列表…"); break;
    case Operation::LoadStableHistory: m_statusMessage = tr("正在读取稳定版本…"); break;
    case Operation::ResetCoreForVersion:
    case Operation::CheckoutCore: m_statusMessage = tr("正在切换核心版本…"); break;
    case Operation::ResetCoreForBranch: m_statusMessage = tr("正在重置核心目录…"); break;
    case Operation::ResetCoreForCleanup:
    case Operation::CleanCore: m_statusMessage = tr("正在完全清理核心目录…"); break;
    case Operation::SetCoreBranchRemote:
    case Operation::FetchCoreBranchRemote:
    case Operation::CheckoutBranch: m_statusMessage = tr("正在切换分支…"); break;
    case Operation::NormalizeBranch: m_statusMessage = tr("正在校正分支…"); break;
    case Operation::ValidateExtensionUpdate:
    case Operation::ResetExtensionForUpdate:
    case Operation::PrepareExtensionUpdateFetch:
    case Operation::ResolveExtensionUpdateBranch:
    case Operation::AttachExtensionUpdateBranch:
    case Operation::SetExtensionUpdateUpstream:
    case Operation::UpdateExtension: m_statusMessage = tr("正在更新扩展…"); break;
    case Operation::LoadExtensionHistory: m_statusMessage = tr("正在读取扩展版本列表…"); break;
    case Operation::ValidateExtensionCheckout:
    case Operation::ResetExtensionForCheckout:
    case Operation::CheckoutExtension: m_statusMessage = tr("正在切换扩展版本…"); break;
    case Operation::InstallExtension: m_statusMessage = tr("正在安装扩展…"); break;
    default: break;
    }
    emit stateChanged();
    if (operation == Operation::Fetch
        || operation == Operation::PrepareCoreUpdateFetch
        || operation == Operation::FetchCoreBranchRemote
        || operation == Operation::PrepareExtensionUpdateFetch) {
        m_gitTimeout.start(30000);
    } else {
        m_gitTimeout.stop();
    }
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GCM_INTERACTIVE"), QStringLiteral("Never"));
    m_process.setProcessEnvironment(environment);
    m_process.start();
}

void VersionManager::beginResetAction(PendingCoreAction action,
                                      const QString &repositoryRoot)
{
    clearBackupState();
    m_pendingCoreAction = action;
    m_backupSourceRoot = repositoryRoot;
    startGit(Operation::CollectTrackedBackup,
             repositoryArguments(repositoryRoot,
                 {QStringLiteral("diff"), QStringLiteral("--name-only"),
                  QStringLiteral("--diff-filter=ACMRTUXB"), QStringLiteral("-z"),
                  QStringLiteral("HEAD"), QStringLiteral("--")}));
}

void VersionManager::collectBackupPaths(const QByteArray &output)
{
    if (output.isEmpty()) {
        return;
    }
    m_backupPaths.append(output);
    if (!m_backupPaths.endsWith('\0')) {
        m_backupPaths.append('\0');
    }
}

void VersionManager::createBackupArchivesOrContinue()
{
    QSet<QString> seen;
    QByteArray coreFiles;
    QByteArray extensionFiles;
    const QList<QByteArray> paths = m_backupPaths.split('\0');
    const QDir root(m_backupSourceRoot);
    for (const QByteArray &encodedPath : paths) {
        if (encodedPath.isEmpty()) {
            continue;
        }
        const QString relativePath = QDir::cleanPath(
            QDir::fromNativeSeparators(QString::fromUtf8(encodedPath)));
        if (relativePath.isEmpty() || relativePath == QStringLiteral(".")
            || relativePath == QStringLiteral("..")
            || relativePath.startsWith(QStringLiteral("../"))
            || QDir::isAbsolutePath(relativePath)
            || !QFileInfo::exists(root.filePath(relativePath))) {
            continue;
        }
        if (seen.contains(relativePath)) {
            continue;
        }
        const QByteArray archivePath = relativePath.toLocal8Bit();
        if (QString::fromLocal8Bit(archivePath) != relativePath) {
            clearBackupState();
            setFailure(tr("文件名无法用当前 Windows 系统编码保存到备份包，已取消操作且未修改仓库：%1")
                       .arg(relativePath));
            return;
        }
        seen.insert(relativePath);
        QByteArray &target = m_pendingCoreAction == PendingCoreAction::FullClean
                && (relativePath == QStringLiteral("custom_nodes")
                    || relativePath.startsWith(QStringLiteral("custom_nodes/")))
            ? extensionFiles : coreFiles;
        target.append(archivePath);
        target.append('\0');
    }

    if (!coreFiles.isEmpty()) {
        const bool extensionAction = m_pendingCoreAction == PendingCoreAction::UpdateExtension
            || m_pendingCoreAction == PendingCoreAction::SwitchExtensionVersion;
        const QString label = m_pendingCoreAction == PendingCoreAction::FullClean
            ? QStringLiteral("core_full-clean")
            : QFileInfo(m_backupSourceRoot).fileName() + QStringLiteral("_reset-tracked");
        m_backupArchiveQueue.append({m_backupSourceRoot,
                                     extensionAction ? QStringLiteral("extensions")
                                                     : QStringLiteral("core"),
                                     label, coreFiles});
    }
    if (!extensionFiles.isEmpty()) {
        m_backupArchiveQueue.append({m_backupSourceRoot, QStringLiteral("extensions"),
                                     QStringLiteral("extensions_full-clean"), extensionFiles});
    }

    if (m_backupArchiveQueue.isEmpty()) {
        continuePendingCoreAction();
        return;
    }

    startNextBackupArchive();
}

void VersionManager::startNextBackupArchive()
{
    if (m_backupArchiveQueue.isEmpty()) {
        QString pruneError;
        if (!pruneBackupDays(m_backupRootPath, &pruneError)) {
            setFailure(pruneError);
            return;
        }
        continuePendingCoreAction();
        return;
    }

    const BackupArchiveRequest request = m_backupArchiveQueue.takeFirst();
    const QDir rootParent(QFileInfo(m_comfyRoot).absolutePath());
    m_backupRootPath = rootParent.filePath(QStringLiteral("backup"));
    const QString dayDirectory = QDir(m_backupRootPath).filePath(
        QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
    m_currentBackupCategoryDirectory = QDir(dayDirectory).filePath(request.category);
    if (!QDir().mkpath(m_currentBackupCategoryDirectory)) {
        setFailure(tr("无法创建备份目录：%1").arg(m_currentBackupCategoryDirectory));
        return;
    }

    int nextSequence = 1;
    const QFileInfoList existingArchives = QDir(m_currentBackupCategoryDirectory).entryInfoList(
        {QStringLiteral("*.zip")}, QDir::Files, QDir::Name);
    const QRegularExpression sequenceExpression(QStringLiteral("^(\\d+)_"));
    for (const QFileInfo &archive : existingArchives) {
        const QRegularExpressionMatch match = sequenceExpression.match(archive.fileName());
        if (match.hasMatch()) {
            nextSequence = qMax(nextSequence, match.captured(1).toInt() + 1);
        }
    }

    QString label = request.label;
    label.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")),
                  QStringLiteral("_"));
    if (label.isEmpty()) {
        label = request.category;
    }
    m_backupArchivePath = QDir(m_currentBackupCategoryDirectory).filePath(
        QStringLiteral("%1_%2.zip").arg(nextSequence, 4, 10, QLatin1Char('0')).arg(label));
    while (QFileInfo::exists(m_backupArchivePath)) {
        ++nextSequence;
        m_backupArchivePath = QDir(m_currentBackupCategoryDirectory).filePath(
            QStringLiteral("%1_%2.zip").arg(nextSequence, 4, 10, QLatin1Char('0')).arg(label));
    }

    m_backupListPath = QDir(QDir::tempPath()).filePath(
        QStringLiteral("minifox-backup-%1.lst").arg(QUuid::createUuid().toString(QUuid::Id128)));

    QSaveFile listFile(m_backupListPath);
    if (!listFile.open(QIODevice::WriteOnly)
        || listFile.write(request.fileList) != request.fileList.size()
        || !listFile.commit()) {
        const QString error = listFile.errorString();
        setFailure(tr("无法准备备份文件列表：%1").arg(error));
        return;
    }

    QString tarProgram = QStandardPaths::findExecutable(QStringLiteral("tar.exe"));
    if (tarProgram.isEmpty()) {
        tarProgram = QStandardPaths::findExecutable(QStringLiteral("tar"));
    }
    if (tarProgram.isEmpty()) {
        setFailure(tr("未找到 Windows 归档工具 tar.exe，已取消操作且未修改仓库。"));
        return;
    }
    startProcess(Operation::CreateBackupArchive, tarProgram,
                 {QStringLiteral("-a"), QStringLiteral("-c"), QStringLiteral("-f"),
                  m_backupArchivePath, QStringLiteral("-C"), request.sourceRoot,
                  QStringLiteral("--null"), QStringLiteral("-T"), m_backupListPath});
}

bool VersionManager::pruneBackupArchives(const QString &categoryDirectory,
                                         int maximum, QString *error)
{
    const QFileInfoList backups = QDir(categoryDirectory).entryInfoList(
        {QStringLiteral("*.zip")}, QDir::Files,
        QDir::Time | QDir::Reversed);
    const int removeCount = qMax(0, backups.size() - maximum);
    for (int index = 0; index < removeCount; ++index) {
        if (!QFile::remove(backups.at(index).absoluteFilePath())) {
            if (error) {
                *error = tr("无法删除旧备份：%1").arg(backups.at(index).absoluteFilePath());
            }
            return false;
        }
    }
    return true;
}

bool VersionManager::pruneBackupDays(const QString &backupRoot, QString *error)
{
    if (backupRoot.isEmpty()) {
        return true;
    }
    const QRegularExpression dayPattern(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
    QFileInfoList dayDirectories;
    for (const QFileInfo &entry : QDir(backupRoot).entryInfoList(
             QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (dayPattern.match(entry.fileName()).hasMatch()) {
            dayDirectories.append(entry);
        }
    }
    const int removeCount = qMax(0, dayDirectories.size() - 5);
    for (int index = 0; index < removeCount; ++index) {
        const QString path = dayDirectories.at(index).absoluteFilePath();
        if (!QDir(path).removeRecursively()) {
            if (error) {
                *error = tr("无法删除旧备份日期目录：%1").arg(path);
            }
            return false;
        }
    }
    return true;
}

QString VersionManager::backupLocationSummary() const
{
    QStringList directories;
    for (const QString &archive : m_operationBackupArchives) {
        const QString directory = QFileInfo(archive).absolutePath();
        if (!directories.contains(directory, Qt::CaseInsensitive)) {
            directories.append(directory);
        }
    }
    return tr("%1 个备份包：%2")
        .arg(m_operationBackupArchives.size())
        .arg(directories.join(QStringLiteral("；")));
}

void VersionManager::resetOperationBackupSummary()
{
    m_operationBackupArchives.clear();
}

void VersionManager::appendBackupSummaryToPendingCompletion()
{
    if (!m_operationBackupArchives.isEmpty()) {
        m_pendingCompletionMessage += tr(" 受影响文件已备份到：%1")
            .arg(backupLocationSummary());
    }
}

void VersionManager::continuePendingCoreAction()
{
    const PendingCoreAction action = m_pendingCoreAction;
    m_pendingCoreAction = PendingCoreAction::None;
    switch (action) {
    case PendingCoreAction::Update:
        startGit(Operation::ResetCoreForUpdate,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("reset"), QStringLiteral("--hard"), QStringLiteral("HEAD")}));
        break;
    case PendingCoreAction::SwitchVersion:
        startGit(Operation::ResetCoreForVersion,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("reset"), QStringLiteral("--hard"), QStringLiteral("HEAD")}));
        break;
    case PendingCoreAction::SwitchBranch:
        startGit(Operation::ResetCoreForBranch,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("reset"), QStringLiteral("--hard"), QStringLiteral("HEAD")}));
        break;
    case PendingCoreAction::FullClean:
        startGit(Operation::ResetCoreForCleanup,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("reset"), QStringLiteral("--hard"), QStringLiteral("HEAD")}));
        break;
    case PendingCoreAction::UpdateExtension:
        startGit(Operation::ResetExtensionForUpdate,
                 repositoryArguments(m_operationPath,
                     {QStringLiteral("reset"), QStringLiteral("--hard"), QStringLiteral("HEAD")}));
        break;
    case PendingCoreAction::SwitchExtensionVersion:
        startGit(Operation::ResetExtensionForCheckout,
                 repositoryArguments(m_operationPath,
                     {QStringLiteral("reset"), QStringLiteral("--hard"), QStringLiteral("HEAD")}));
        break;
    case PendingCoreAction::None:
        setFailure(tr("内部状态无效，已取消 Git 操作。"));
        break;
    }
}

void VersionManager::clearBackupState()
{
    if (!m_backupListPath.isEmpty()) {
        QFile::remove(m_backupListPath);
    }
    m_backupPaths.clear();
    m_backupSourceRoot.clear();
    m_backupArchiveQueue.clear();
    m_createdBackupArchives.clear();
    m_backupListPath.clear();
    m_backupArchivePath.clear();
    m_backupRootPath.clear();
    m_currentBackupCategoryDirectory.clear();
    m_pendingCoreAction = PendingCoreAction::None;
}

void VersionManager::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_operation == Operation::None) {
        return;
    }
    m_gitTimeout.stop();
    const Operation completed = m_operation;
    m_operation = Operation::None;
    const QByteArray rawOutput = m_process.readAllStandardOutput();
    const QString output = ProcessTextDecoder::decode(rawOutput).trimmed();
    const QString error = ProcessTextDecoder::decode(m_process.readAllStandardError()).trimmed();

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        if (completed == Operation::CreateBackupArchive) {
            const QString archivePath = m_backupArchivePath;
            const QString listPath = m_backupListPath;
            clearBackupState();
            QFile::remove(archivePath);
            QFile::remove(listPath);
            setFailure(error.isEmpty()
                ? tr("备份压缩包创建失败，已取消操作且未修改仓库。")
                : tr("备份压缩包创建失败：%1").arg(error));
        } else if (completed == Operation::CollectTrackedBackup
                   || completed == Operation::CollectUntrackedBackup) {
            clearBackupState();
            setFailure(error.isEmpty()
                ? tr("无法确定需要备份的文件，已取消操作且未修改仓库。")
                : error);
        } else if (completed == Operation::LoadExtensionHistory) {
            const QString message =
                error.isEmpty() ? tr("无法读取扩展版本历史。") : error;
            setFailure(message);
            emit extensionVersionsLoaded(false, message);
        } else if (completed == Operation::RefreshStatus) {
            setFailure(tr("核心版本列表刷新失败：ComfyUI 目录不是 Git 仓库。"));
        } else if (completed == Operation::Fetch && m_fullRefresh) {
            m_lastError = error.isEmpty() ? tr("无法访问远程 Git 仓库。") : error;
            m_remoteUnavailable = true;
            startGit(Operation::ResolveDevelopmentHistoryBranch,
                     repositoryArguments(m_comfyRoot,
                         {QStringLiteral("for-each-ref"), QStringLiteral("--format=%(refname:short)"),
                          QStringLiteral("refs/remotes/origin/")}));
        } else {
            setFailure(error.isEmpty() ? tr("Git 操作失败。") : error);
        }
        return;
    }

    switch (completed) {
    case Operation::RefreshStatus:
        parseStatus(output);
        m_repository = true;
        startGit(Operation::RefreshLog,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--date=format-local:%Y-%m-%d %H:%M:%S"),
                      QStringLiteral("--format=%h%x1f%H%x1f%ad%x1f%s")}));
        break;
    case Operation::CollectTrackedBackup:
        collectBackupPaths(rawOutput);
        if (m_pendingCoreAction == PendingCoreAction::FullClean) {
            startGit(Operation::CollectUntrackedBackup,
                     repositoryArguments(m_backupSourceRoot,
                         {QStringLiteral("ls-files"), QStringLiteral("--others"),
                          QStringLiteral("--exclude-standard"), QStringLiteral("-z"),
                          QStringLiteral("--")}));
        } else {
            createBackupArchivesOrContinue();
        }
        break;
    case Operation::CollectUntrackedBackup:
        collectBackupPaths(rawOutput);
        createBackupArchivesOrContinue();
        break;
    case Operation::CreateBackupArchive: {
        QFile::remove(m_backupListPath);
        m_backupListPath.clear();
        QString pruneError;
        const int maximum = QFileInfo(m_currentBackupCategoryDirectory).fileName()
                    == QStringLiteral("core") ? 3 : 60;
        if (!pruneBackupArchives(m_currentBackupCategoryDirectory, maximum, &pruneError)) {
            const QString archivePath = m_backupArchivePath;
            clearBackupState();
            QFile::remove(archivePath);
            setFailure(pruneError);
            break;
        }
        m_createdBackupArchives.append(m_backupArchivePath);
        m_operationBackupArchives.append(m_backupArchivePath);
        m_backupArchivePath.clear();
        m_currentBackupCategoryDirectory.clear();
        startNextBackupArchive();
        break;
    }
    case Operation::ResetCoreForVersion:
        startGit(Operation::CheckoutCore,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("checkout"), QStringLiteral("-B"),
                      m_requestedCoreChannel == 0
                          ? QStringLiteral("master")
                          : QStringLiteral("dev"),
                      m_pendingCommit}));
        break;
    case Operation::ResetCoreForBranch:
        startGit(Operation::SetCoreBranchRemote,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("remote"), QStringLiteral("set-url"),
                      QStringLiteral("origin"), m_pendingBranchRemoteUrl}));
        break;
    case Operation::ResetCoreForCleanup:
        startGit(Operation::CleanCore,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("clean"), QStringLiteral("-ffd")}));
        break;
    case Operation::CleanCore: {
        const QString message = m_createdBackupArchives.isEmpty()
            ? tr("完全清理完成；没有需要备份的受影响文件。")
            : tr("完全清理完成。受影响文件已备份到：%1")
                  .arg(backupLocationSummary());
        finish(message);
        QTimer::singleShot(0, this, &VersionManager::refreshCore);
        break;
    }
    case Operation::SetCoreBranchRemote:
        startGit(Operation::FetchCoreBranchRemote,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("fetch"), QStringLiteral("--tags"),
                      QStringLiteral("--prune"), QStringLiteral("--quiet"),
                      QStringLiteral("origin")}));
        break;
    case Operation::FetchCoreBranchRemote:
        startGit(Operation::CheckoutBranch,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("checkout"), QStringLiteral("--track"),
                      QStringLiteral("-B"), m_pendingBranch,
                      QStringLiteral("origin/") + m_pendingBranch}));
        break;
    case Operation::RefreshLog: {
        const QStringList parts = output.split(QChar(0x1f));
        if (!parts.isEmpty()) m_commit = parts.at(0);
        if (parts.size() > 1) m_commitFull = parts.at(1);
        if (parts.size() > 2) m_commitDate = parts.at(2);
        if (parts.size() > 3) m_commitSubject = parts.mid(3).join(QStringLiteral(" "));
        if (m_comfyVersion.isEmpty()) m_comfyVersion = m_commit;
        m_remoteUrl = readGitValue(m_comfyRoot, QStringLiteral("remote"));
        const QString normalizeTarget = normalizedBranchName();
        if (!normalizeTarget.isEmpty()) {
            startGit(Operation::NormalizeBranch,
                     repositoryArguments(m_comfyRoot,
                         {QStringLiteral("checkout"), QStringLiteral("-B"), normalizeTarget}));
            break;
        }
        if (m_fullRefresh) {
            startGit(Operation::Fetch,
                     repositoryArguments(m_comfyRoot,
                         {QStringLiteral("fetch"), QStringLiteral("--tags"), QStringLiteral("--quiet")}));
        } else {
            startGit(Operation::ResolveDevelopmentHistoryBranch,
                     repositoryArguments(m_comfyRoot,
                         {QStringLiteral("for-each-ref"), QStringLiteral("--format=%(refname:short)"),
                          QStringLiteral("refs/remotes/origin/")}));
        }
        break;
    }
    case Operation::ResolveDevelopmentHistoryBranch:
        m_developmentRemoteBranch = selectDevelopmentBranch(output);
        if (m_developmentRemoteBranch.isEmpty()) {
            setFailure(tr("无法确定 ComfyUI 开发主线，版本列表未更新。"));
            break;
        }
        startGit(Operation::LoadCoreHistory,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("log"), m_developmentRemoteBranch, QStringLiteral("-100"),
                      QStringLiteral("--date=format-local:%Y-%m-%d %H:%M:%S"),
                      QStringLiteral("--format=%h%x1f%H%x1f%ad%x1f%s%x1e")}));
        break;
    case Operation::LoadCoreHistory:
        parseCoreHistory(output);
        startGit(Operation::LoadStableHistory,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("log"), QStringLiteral("--all"),
                      QStringLiteral("--regexp-ignore-case"), QStringLiteral("--grep=^ComfyUI v"),
                      QStringLiteral("-120"),
                      QStringLiteral("--date=format-local:%Y-%m-%d %H:%M:%S"),
                      QStringLiteral("--format=%h%x1f%H%x1f%ad%x1f%s%x1e")}));
        break;
    case Operation::LoadStableHistory:
        parseStableHistory(output);
        if (m_fullRefresh) {
            m_fullRefresh = false;
            if (m_remoteUnavailable) {
                completeRefresh(false, tr("核心版本列表刷新失败：%1").arg(m_lastError));
            } else {
                completeRefresh(true, tr("核心版本列表刷新成功。"));
            }
        } else {
            finish(tr("已读取本地版本信息。"));
        }
        break;
    case Operation::Fetch:
        startGit(Operation::ResolveCoreCompareBranch,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("for-each-ref"), QStringLiteral("--contains=HEAD"),
                      QStringLiteral("--format=%(refname:short)"),
                      QStringLiteral("refs/remotes/origin/")}));
        break;
    case Operation::ResolveCoreCompareBranch: {
        m_targetRemoteBranch = selectRemoteBranch(output, m_branch);
        const QString comparisonTarget = m_targetRemoteBranch.isEmpty()
            ? QStringLiteral("@{upstream}") : m_targetRemoteBranch;
        startGit(Operation::Compare,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("rev-list"), QStringLiteral("--left-right"), QStringLiteral("--count"),
                      QStringLiteral("HEAD...") + comparisonTarget}));
        break;
    }
    case Operation::Compare: {
        const QStringList counts = output.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (counts.size() >= 2) {
            m_aheadCount = counts.at(0).toInt();
            m_behindCount = counts.at(1).toInt();
        }
        if (m_fullRefresh) {
            startGit(Operation::ResolveDevelopmentHistoryBranch,
                     repositoryArguments(m_comfyRoot,
                         {QStringLiteral("for-each-ref"), QStringLiteral("--format=%(refname:short)"),
                          QStringLiteral("refs/remotes/origin/")}));
        } else {
            finish(updateState());
        }
        break;
    }
    case Operation::ResetCoreForUpdate:
        startGit(Operation::PrepareCoreUpdateFetch,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("fetch"), QStringLiteral("--tags"), QStringLiteral("--quiet")}));
        break;
    case Operation::PrepareCoreUpdateFetch:
        if (m_requestedCoreChannel == 0) {
            startGit(Operation::ResolveStableUpdateCommit,
                     repositoryArguments(m_comfyRoot,
                         {QStringLiteral("log"), QStringLiteral("--all"),
                          QStringLiteral("--regexp-ignore-case"), QStringLiteral("--grep=^ComfyUI v"),
                          QStringLiteral("-1"), QStringLiteral("--format=%H")}));
        } else {
            startGit(Operation::ResolveDevelopmentUpdateBranch,
                     repositoryArguments(m_comfyRoot,
                         {QStringLiteral("for-each-ref"), QStringLiteral("--format=%(refname:short)"),
                          QStringLiteral("refs/remotes/origin/")}));
        }
        break;
    case Operation::ResolveStableUpdateCommit:
        if (output.isEmpty()) {
            setFailure(tr("未找到可用的 ComfyUI 稳定版本，仓库未被修改。"));
            break;
        }
        startGit(Operation::CheckoutCore,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("checkout"), QStringLiteral("-B"),
                      QStringLiteral("master"), output.section(QLatin1Char('\n'), 0, 0).trimmed()}));
        break;
    case Operation::ResolveDevelopmentUpdateBranch:
        m_targetRemoteBranch = selectDevelopmentBranch(output);
        if (m_targetRemoteBranch.isEmpty()) {
            setFailure(tr("未找到可用的 ComfyUI 开发分支，仓库未被修改。"));
            break;
        }
        startGit(Operation::CheckoutCore,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("checkout"), QStringLiteral("--track"),
                      QStringLiteral("-B"),
                      QStringLiteral("dev"), m_targetRemoteBranch}));
        break;
    case Operation::ResolveCoreUpdateBranch:
        m_targetRemoteBranch = selectRemoteBranch(output, m_branch);
        if (branchNeedsRecovery(m_branch)) {
            if (m_targetRemoteBranch.isEmpty()) {
                setFailure(tr("无法安全确定当前版本对应的远端分支，已取消更新且未修改仓库。"));
                break;
            }
            startGit(Operation::AttachCoreUpdateBranch,
                     repositoryArguments(m_comfyRoot,
                         {QStringLiteral("checkout"), QStringLiteral("-B"),
                          QStringLiteral("minifox/version-core"), QStringLiteral("HEAD")}));
        } else {
            startGit(Operation::Pull,
                     repositoryArguments(m_comfyRoot,
                         {QStringLiteral("pull"), QStringLiteral("--ff-only")}));
        }
        break;
    case Operation::AttachCoreUpdateBranch:
        startGit(Operation::SetCoreUpdateUpstream,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("branch"), QStringLiteral("--set-upstream-to=") + m_targetRemoteBranch,
                      QStringLiteral("minifox/version-core")}));
        break;
    case Operation::SetCoreUpdateUpstream:
        startGit(Operation::Pull,
                 repositoryArguments(m_comfyRoot,
                     {QStringLiteral("pull"), QStringLiteral("--ff-only")}));
        break;
    case Operation::Pull:
    case Operation::CheckoutCore:
    case Operation::CheckoutBranch:
        appendBackupSummaryToPendingCompletion();
        queueDependencyCheck(m_comfyRoot);
        m_busy = false;
        emit stateChanged();
        refresh();
        break;
    case Operation::NormalizeBranch:
        m_busy = false;
        emit stateChanged();
        refresh();
        break;
    case Operation::UpdateExtension:
    case Operation::CheckoutExtension:
    case Operation::InstallExtension:
        queueDependencyCheck(m_operationPath);
        if (completed == Operation::UpdateExtension) {
            setExtensionStatus(m_operationPath, QStringLiteral("latest"));
        }
        if (completed == Operation::UpdateExtension && m_updatingAllExtensions
            && !m_extensionUpdateQueue.isEmpty()) {
            m_operationPath = m_extensionUpdateQueue.takeFirst();
            startExtensionUpdate(m_operationPath);
            break;
        }
        scanExtensions();
        if (completed == Operation::CheckoutExtension) {
            setExtensionStatus(m_operationPath, QStringLiteral("unknown"));
        } else if (completed == Operation::InstallExtension) {
            setExtensionStatus(m_operationPath, QStringLiteral("latest"));
        }
        appendBackupSummaryToPendingCompletion();
        if (completed == Operation::InstallExtension) finish(m_pendingCompletionMessage);
        else if (completed == Operation::CheckoutExtension) finish(m_pendingCompletionMessage);
        else if (m_updatingAllExtensions) {
            m_updatingAllExtensions = false;
            finish(m_pendingCompletionMessage);
        } else finish(m_pendingCompletionMessage);
        break;
    case Operation::ValidateExtensionUpdate:
        if (!output.isEmpty()) {
            if (m_settings->resetTrackedFilesOnUpdate()) {
                beginResetAction(PendingCoreAction::UpdateExtension, m_operationPath);
            } else {
                setFailure(tr("扩展 %1 存在未提交更改，已跳过且未修改仓库。")
                           .arg(QFileInfo(m_operationPath).fileName()));
            }
            break;
        }
        startGit(Operation::PrepareExtensionUpdateFetch,
                  repositoryArguments(m_operationPath,
                      {QStringLiteral("fetch"), QStringLiteral("--quiet")}));
        break;
    case Operation::ResetExtensionForUpdate:
        startGit(Operation::PrepareExtensionUpdateFetch,
                 repositoryArguments(m_operationPath,
                     {QStringLiteral("fetch"), QStringLiteral("--quiet")}));
        break;
    case Operation::PrepareExtensionUpdateFetch:
        startGit(Operation::ResolveExtensionUpdateBranch,
                 repositoryArguments(m_operationPath,
                     {QStringLiteral("for-each-ref"), QStringLiteral("--contains=HEAD"),
                      QStringLiteral("--format=%(refname:short)"),
                      QStringLiteral("refs/remotes/origin/")}));
        break;
    case Operation::ResolveExtensionUpdateBranch: {
        const QString localBranch = installedExtensionBranch(m_operationPath);
        m_targetRemoteBranch = selectRemoteBranch(output, localBranch);
        if (branchNeedsRecovery(localBranch)) {
            if (m_targetRemoteBranch.isEmpty()) {
                setFailure(tr("扩展 %1 无法安全确定远端分支，已跳过且未修改仓库。")
                           .arg(QFileInfo(m_operationPath).fileName()));
                break;
            }
            startGit(Operation::AttachExtensionUpdateBranch,
                     repositoryArguments(m_operationPath,
                         {QStringLiteral("checkout"), QStringLiteral("-B"),
                          QStringLiteral("minifox/version-extension"), QStringLiteral("HEAD")}));
        } else {
            startGit(Operation::UpdateExtension,
                     repositoryArguments(m_operationPath,
                         {QStringLiteral("pull"), QStringLiteral("--ff-only")}));
        }
        break;
    }
    case Operation::AttachExtensionUpdateBranch:
        startGit(Operation::SetExtensionUpdateUpstream,
                 repositoryArguments(m_operationPath,
                     {QStringLiteral("branch"), QStringLiteral("--set-upstream-to=") + m_targetRemoteBranch,
                      QStringLiteral("minifox/version-extension")}));
        break;
    case Operation::SetExtensionUpdateUpstream:
        startGit(Operation::UpdateExtension,
                 repositoryArguments(m_operationPath,
                     {QStringLiteral("pull"), QStringLiteral("--ff-only")}));
        break;
    case Operation::LoadExtensionHistory:
        parseExtensionHistory(output);
        m_busy = false;
        m_statusMessage = tr("扩展版本列表已加载。");
        emit stateChanged();
        emit extensionVersionsLoaded(true, m_statusMessage);
        break;
    case Operation::ValidateExtensionCheckout:
        if (!output.isEmpty()) {
            if (m_settings->resetTrackedFilesOnUpdate()) {
                beginResetAction(PendingCoreAction::SwitchExtensionVersion, m_operationPath);
            } else {
                setFailure(tr("扩展 %1 存在未提交更改，已取消版本切换。")
                           .arg(QFileInfo(m_operationPath).fileName()));
            }
            break;
        }
        startGit(Operation::CheckoutExtension,
                 repositoryArguments(m_operationPath,
                     {QStringLiteral("checkout"), QStringLiteral("-B"),
                      QStringLiteral("minifox/version-extension"), m_pendingCommit}));
        break;
    case Operation::ResetExtensionForCheckout:
        startGit(Operation::CheckoutExtension,
                 repositoryArguments(m_operationPath,
                     {QStringLiteral("checkout"), QStringLiteral("-B"),
                      QStringLiteral("minifox/version-extension"), m_pendingCommit}));
        break;
    default:
        break;
    }
}

void VersionManager::parseStatus(const QString &output)
{
    const QStringList lines = output.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    m_dirty = hasMeaningfulCoreWorkingTreeChanges(output);
    if (lines.isEmpty()) {
        return;
    }
    QString branchLine = lines.constFirst();
    branchLine.remove(QRegularExpression(QStringLiteral("^##\\s*")));
    const int upstreamSeparator = branchLine.indexOf(QStringLiteral("..."));
    if (upstreamSeparator >= 0) {
        branchLine.truncate(upstreamSeparator);
    }
    const int statusSeparator = branchLine.indexOf(QStringLiteral(" ["));
    if (statusSeparator >= 0) {
        branchLine.truncate(statusSeparator);
    }
    m_branch = branchLine.trimmed();
}

QString VersionManager::findGit() const
{
    QString path = QStandardPaths::findExecutable(QStringLiteral("git.exe"));
    if (!path.isEmpty()) {
        return path;
    }
    const QStringList candidates {
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("git/cmd/git.exe")),
        QDir(m_configuration->comfyRoot()).filePath(QStringLiteral("../git/cmd/git.exe")),
        QStringLiteral("C:/Program Files/Git/cmd/git.exe"),
        QStringLiteral("C:/Program Files/Git/bin/git.exe")
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

void VersionManager::parseCoreHistory(const QString &output)
{
    QVariantList versions;
    for (const QString &record : output.split(QChar(0x1e), Qt::SkipEmptyParts)) {
        const QStringList fields = record.trimmed().split(QChar(0x1f));
        if (fields.size() < 4) continue;
        versions.append(QVariantMap{
            {QStringLiteral("shortCommit"), fields.at(0)},
            {QStringLiteral("commit"), fields.at(1)},
            {QStringLiteral("date"), fields.at(2)},
            {QStringLiteral("subject"), fields.mid(3).join(QStringLiteral(" "))},
            {QStringLiteral("current"), fields.at(1).startsWith(m_commit) || fields.at(0) == m_commit}
        });
    }
    m_coreVersions = versions;
}

void VersionManager::parseStableHistory(const QString &output)
{
    QVariantList versions;
    for (const QString &record : output.split(QChar(0x1e), Qt::SkipEmptyParts)) {
        const QStringList fields = record.trimmed().split(QChar(0x1f));
        if (fields.size() < 4) continue;
        versions.append(QVariantMap{
            {QStringLiteral("shortCommit"), fields.at(0)},
            {QStringLiteral("commit"), fields.at(1)},
            {QStringLiteral("date"), fields.at(2)},
            {QStringLiteral("subject"), fields.mid(3).join(QStringLiteral(" "))},
            {QStringLiteral("current"), fields.at(1).startsWith(m_commit) || fields.at(0) == m_commit}
        });
    }
    m_stableVersions = versions;
}

void VersionManager::parseExtensionHistory(const QString &output)
{
    QVariantList versions;
    for (const QString &record : output.split(QChar(0x1e), Qt::SkipEmptyParts)) {
        const QStringList fields = record.trimmed().split(QChar(0x1f));
        if (fields.size() < 4) {
            continue;
        }
        const QString shortCommit = fields.at(0);
        const QString fullCommit = fields.at(1);
        versions.append(QVariantMap{
            {QStringLiteral("shortCommit"), shortCommit},
            {QStringLiteral("commit"), fullCommit},
            {QStringLiteral("date"), fields.at(2)},
            {QStringLiteral("subject"), fields.mid(3).join(QStringLiteral(" "))},
            {QStringLiteral("current"),
             !m_extensionHistoryCurrentCommit.isEmpty()
                 && (fullCommit.startsWith(m_extensionHistoryCurrentCommit)
                     || shortCommit == m_extensionHistoryCurrentCommit)}
        });
    }
    m_extensionVersions = versions;
}

QStringList VersionManager::networkRouteArguments() const
{
    if (m_networkRoute != 1) return {};
    const QString mirror = QStringLiteral("https://ghfast.top/");
    return {
        QStringLiteral("-c"),
        QStringLiteral("url.%1https://github.com/.insteadOf=https://github.com/").arg(mirror),
        QStringLiteral("-c"),
        QStringLiteral("url.%1https://github.com/.insteadOf=git@github.com:").arg(mirror)
    };
}

QStringList VersionManager::repositoryArguments(const QString &root, const QStringList &arguments) const
{
    QStringList result = networkRouteArguments();
    result.append({
        QStringLiteral("-c"),
        QStringLiteral("safe.directory=%1").arg(QDir::cleanPath(root)),
        QStringLiteral("-C"), root
    });
    result.append(arguments);
    return result;
}

QString VersionManager::readGitValue(const QString &repositoryRoot, const QString &key)
{
    if (key == QStringLiteral("remote")) {
        QFile config(QDir(repositoryRoot).filePath(QStringLiteral(".git/config")));
        if (!config.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        const QString text = QString::fromUtf8(config.readAll());
        const QRegularExpression expression(
            QStringLiteral("\\[remote \\\"origin\\\"\\][^\\[]*?url\\s*=\\s*([^\\r\\n]+)"),
            QRegularExpression::DotMatchesEverythingOption);
        const auto match = expression.match(text);
        return match.hasMatch() ? match.captured(1).trimmed() : QString();
    }
    return {};
}

QString VersionManager::readExtensionDescription(const QString &path)
{
    const QStringList names {QStringLiteral("README.md"), QStringLiteral("readme.md")};
    for (const QString &name : names) {
        QFile file(QDir(path).filePath(name));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        for (QString line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
            line.remove(QRegularExpression(QStringLiteral("^[#>*\\s]+")));
            if (line.size() >= 12 && !line.startsWith(QStringLiteral("!["))) return line.left(240).trimmed();
        }
    }
    return tr("ComfyUI 自定义节点扩展");
}

void VersionManager::scanExtensions(bool resetStatus)
{
    QHash<QString, QString> previousStatuses;
    for (const QVariant &entry : std::as_const(m_installedExtensions)) {
        const QVariantMap extension = entry.toMap();
        previousStatuses.insert(extension.value(QStringLiteral("path")).toString(),
                                extension.value(QStringLiteral("status")).toString());
    }

    QVariantList installed;
    const QDir customNodes(QDir(m_comfyRoot).filePath(QStringLiteral("custom_nodes")));
    const QFileInfoList directories = customNodes.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &directory : directories) {
        if (directory.fileName() == QStringLiteral("__pycache__")) continue;
        const bool enabled = !directory.fileName().endsWith(QStringLiteral(".disabled"));
        QString displayName = directory.fileName();
        if (!enabled) displayName.chop(9);
        const QString headPath = QDir(directory.absoluteFilePath()).filePath(QStringLiteral(".git/HEAD"));
        QFile headFile(headPath);
        QString branch;
        QString commit;
        if (headFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString head = QString::fromUtf8(headFile.readAll()).trimmed();
            if (head.startsWith(QStringLiteral("ref: "))) {
                const QString ref = head.mid(5);
                branch = ref.startsWith(QStringLiteral("refs/heads/"))
                    ? ref.mid(QStringLiteral("refs/heads/").size())
                    : ref.section(QLatin1Char('/'), -1);
                QFile refFile(QDir(directory.absoluteFilePath()).filePath(QStringLiteral(".git/") + ref));
                if (refFile.open(QIODevice::ReadOnly | QIODevice::Text)) commit = QString::fromUtf8(refFile.readAll()).trimmed();
            } else commit = head;
        }
        const bool repository = QFileInfo::exists(
            QDir(directory.absoluteFilePath()).filePath(QStringLiteral(".git")));
        const QString status = repository
            ? (resetStatus ? QStringLiteral("checking")
                           : previousStatuses.value(directory.absoluteFilePath(), QStringLiteral("unknown")))
            : QStringLiteral("unknown");
        const QVariantMap item {
            {QStringLiteral("name"), displayName},
            {QStringLiteral("path"), directory.absoluteFilePath()},
            {QStringLiteral("enabled"), enabled},
            {QStringLiteral("remote"), readGitValue(directory.absoluteFilePath(), QStringLiteral("remote"))},
            {QStringLiteral("branch"), branch},
            {QStringLiteral("commit"), commit.left(7)},
            {QStringLiteral("date"), directory.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))},
            {QStringLiteral("description"), readExtensionDescription(directory.absoluteFilePath())},
            {QStringLiteral("repository"), repository},
            {QStringLiteral("status"), status},
            {QStringLiteral("installed"), true}
        };
        installed.append(item);
    }
    m_installedExtensions = installed;
    updateAvailableExtensions();
    emit stateChanged();
}

void VersionManager::beginExtensionChecks()
{
    m_extensionCheckQueue.clear();
    m_activeExtensionChecks = 0;
    for (const QVariant &entry : std::as_const(m_installedExtensions)) {
        const QVariantMap extension = entry.toMap();
        if (extension.value(QStringLiteral("repository")).toBool()) {
            const QString path = extension.value(QStringLiteral("path")).toString();
            setExtensionStatus(path, QStringLiteral("checking"));
            m_extensionCheckQueue.append(path);
        }
    }
    if (m_remoteUnavailable) {
        for (const QString &path : std::as_const(m_extensionCheckQueue)) {
            setExtensionStatus(path, QStringLiteral("unknown"));
        }
        m_extensionCheckQueue.clear();
    }
    startExtensionCheckJobs();
}

void VersionManager::startExtensionCheckJobs()
{
    constexpr int maximumConcurrentChecks = 8;
    while (m_activeExtensionChecks < maximumConcurrentChecks
           && !m_extensionCheckQueue.isEmpty()) {
        const QString path = m_extensionCheckQueue.takeFirst();
        auto *process = new QProcess(this);
        auto *timeout = new QTimer(process);
        timeout->setObjectName(QStringLiteral("extensionCheckTimeout"));
        timeout->setSingleShot(true);
        process->setProperty("extensionPath", path);
        process->setProperty("extensionPhase", QStringLiteral("fetch"));
        process->setProperty("extensionCheckHandled", false);
        process->setProperty("extensionCheckTimedOut", false);
        process->setProcessChannelMode(QProcess::SeparateChannels);

        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
        environment.insert(QStringLiteral("GCM_INTERACTIVE"), QStringLiteral("Never"));
        process->setProcessEnvironment(environment);

        connect(timeout, &QTimer::timeout, process, [process] {
            process->setProperty("extensionCheckTimedOut", true);
            if (process->state() != QProcess::NotRunning) process->kill();
        });
        connect(process, &QProcess::finished, this,
                [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
            handleExtensionCheckFinished(process, exitCode, exitStatus);
        });
        connect(process, &QProcess::errorOccurred, this,
                [this, process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                handleExtensionCheckFinished(process, -1, QProcess::CrashExit);
            }
        });

        ++m_activeExtensionChecks;
        process->setProgram(m_gitProgram);
        process->setArguments(repositoryArguments(
            path, {QStringLiteral("fetch"), QStringLiteral("--quiet")}));
        process->start();
        timeout->start(30000);
    }

    if (m_extensionCheckQueue.isEmpty() && m_activeExtensionChecks == 0) {
        if (m_refreshErrors.isEmpty()) {
            completeRefresh(true, tr("已安装扩展列表刷新成功。"));
        } else {
            const int total = m_refreshErrors.size();
            QStringList details = m_refreshErrors.mid(0, 3);
            QString message = tr("已安装扩展列表刷新失败：%1").arg(details.join(QStringLiteral("；")));
            if (total > details.size()) {
                message += tr("；另有 %1 个扩展检测失败。").arg(total - details.size());
            }
            completeRefresh(false, message);
        }
    }
}

void VersionManager::handleExtensionCheckFinished(QProcess *process, int exitCode,
                                                  QProcess::ExitStatus exitStatus)
{
    if (!process || process->property("extensionCheckHandled").toBool()) return;

    QTimer *timeout = process->findChild<QTimer *>(QStringLiteral("extensionCheckTimeout"));
    if (timeout) timeout->stop();
    const QString path = process->property("extensionPath").toString();
    const QString phase = process->property("extensionPhase").toString();
    const QString output = ProcessTextDecoder::decode(process->readAllStandardOutput()).trimmed();
    const QString error = ProcessTextDecoder::decode(process->readAllStandardError()).trimmed();
    const bool succeeded = exitStatus == QProcess::NormalExit && exitCode == 0;

    if (phase == QStringLiteral("fetch") && succeeded) {
        process->setProperty("extensionPhase", QStringLiteral("resolve"));
        process->setProperty("extensionCheckTimedOut", false);
        process->setArguments(repositoryArguments(
            path,
            {QStringLiteral("for-each-ref"), QStringLiteral("--contains=HEAD"),
             QStringLiteral("--format=%(refname:short)"),
             QStringLiteral("refs/remotes/origin/")}));
        process->start();
        if (timeout) timeout->start(30000);
        return;
    }

    bool resultSucceeded = succeeded;
    if (phase == QStringLiteral("resolve") && succeeded) {
        const QString targetBranch = selectRemoteBranch(output, installedExtensionBranch(path));
        if (!targetBranch.isEmpty()) {
            process->setProperty("extensionPhase", QStringLiteral("compare"));
            process->setProperty("extensionCheckTimedOut", false);
            process->setArguments(repositoryArguments(
                path,
                {QStringLiteral("rev-list"), QStringLiteral("--left-right"),
                 QStringLiteral("--count"), QStringLiteral("HEAD...") + targetBranch}));
            process->start();
            if (timeout) timeout->start(30000);
            return;
        }
        resultSucceeded = false;
    }

    process->setProperty("extensionCheckHandled", true);
    if (resultSucceeded) {
        const QStringList counts = output.split(
            QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        const bool outdated = counts.size() >= 2 && counts.at(1).toInt() > 0;
        setExtensionStatus(path, outdated ? QStringLiteral("outdated")
                                          : QStringLiteral("latest"));
    } else {
        setExtensionStatus(path, QStringLiteral("unknown"));
        QString reason;
        if (process->property("extensionCheckTimedOut").toBool()) {
            reason = tr("检测超时。");
        } else if (phase == QStringLiteral("resolve") && succeeded) {
            reason = tr("无法确定对应的远端分支。");
        } else if (!error.isEmpty()) {
            reason = error;
        } else {
            reason = process->errorString();
        }
        m_refreshErrors.append(tr("%1：%2").arg(QFileInfo(path).fileName(), reason));
    }

    --m_activeExtensionChecks;
    process->deleteLater();
    startExtensionCheckJobs();
}

void VersionManager::setExtensionStatus(const QString &path, const QString &status)
{
    for (int index = 0; index < m_installedExtensions.size(); ++index) {
        QVariantMap extension = m_installedExtensions.at(index).toMap();
        if (extension.value(QStringLiteral("path")).toString() != path) continue;
        extension.insert(QStringLiteral("status"), status);
        m_installedExtensions[index] = extension;
        emit stateChanged();
        return;
    }
}

QString VersionManager::catalogCachePath() const
{
    return QDir(PortablePaths::dataDirectory()).filePath(QStringLiteral("custom-node-list.json"));
}

void VersionManager::downloadCatalog()
{
    QFile cached(catalogCachePath());
    if (m_catalogExtensions.isEmpty() && cached.size() <= kMaximumCatalogBytes
        && cached.open(QIODevice::ReadOnly)) {
        applyCatalogData(cached.readAll());
    }

    if (m_catalogReply) {
        m_catalogReply->abort();
        m_catalogReply->deleteLater();
    }

    QString catalogUrl = QStringLiteral(
        "https://raw.githubusercontent.com/Comfy-Org/ComfyUI-Manager/main/custom-node-list.json");
    if (m_networkRoute == 1) {
        catalogUrl = QStringLiteral("https://ghfast.top/") + catalogUrl;
    }
    QNetworkRequest request{QUrl(catalogUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Minifox-ComfyUI-Launcher/0.1"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = m_network.get(request);
    const auto catalogData = QSharedPointer<QByteArray>::create();
    catalogData->reserve(256 * 1024);
    const auto drainCatalogData = [reply, catalogData] {
        constexpr qint64 readChunkBytes = 64 * 1024;
        while (reply->bytesAvailable() > 0
               && !reply->property("catalogTooLarge").toBool()) {
            const qint64 remaining = kMaximumCatalogBytes - catalogData->size();
            if (remaining <= 0) {
                reply->setProperty("catalogTooLarge", true);
                catalogData->clear();
                reply->abort();
                return;
            }
            const qint64 requested = qMin(readChunkBytes, remaining + 1);
            const QByteArray chunk = reply->read(requested);
            if (chunk.isEmpty()) {
                return;
            }
            if (chunk.size() > remaining) {
                reply->setProperty("catalogTooLarge", true);
                catalogData->clear();
                reply->abort();
                return;
            }
            catalogData->append(chunk);
        }
    };
    m_catalogReply = reply;
    m_catalogLoading = true;
    emit stateChanged();

    connect(reply, &QNetworkReply::metaDataChanged, this, [reply] {
        const qint64 declaredSize =
            reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (declaredSize > kMaximumCatalogBytes) {
            reply->setProperty("catalogTooLarge", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::readyRead, this, drainCatalogData);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, catalogData, drainCatalogData] {
        if (m_catalogReply != reply) {
            reply->deleteLater();
            return;
        }
        drainCatalogData();
        const bool tooLarge = reply->property("catalogTooLarge").toBool();
        const bool networkOk = !tooLarge && reply->error() == QNetworkReply::NoError;
        const bool downloaded = networkOk && applyCatalogData(*catalogData);
        if (downloaded) {
            PortablePaths::ensureDataDirectory();
            QSaveFile cache(catalogCachePath());
            if (cache.open(QIODevice::WriteOnly)) {
                cache.write(*catalogData);
                cache.commit();
            }
        } else {
            const QString reason = tooLarge
                ? tr("扩展索引超过 16 MB 限制。")
                : networkOk ? tr("服务器返回了无法识别的扩展索引。")
                            : reply->errorString();
            m_lastError = tr("可安装扩展列表刷新失败：%1").arg(reason);
        }
        m_catalogLoading = false;
        m_catalogReply.clear();
        emit stateChanged();
        if (downloaded) {
            completeRefresh(true, tr("可安装扩展列表刷新成功，共 %1 条。").arg(m_availableExtensions.size()));
        } else {
            completeRefresh(false, m_lastError);
        }
        reply->deleteLater();
    });
}

bool VersionManager::applyCatalogData(const QByteArray &data)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;

    const QJsonArray nodes = document.object().value(QStringLiteral("custom_nodes")).toArray();
    if (nodes.isEmpty() || nodes.size() > kMaximumCatalogEntries) return false;
    QVariantList catalog;
    catalog.reserve(nodes.size());
    for (const QJsonValue &value : nodes) {
        const QJsonObject node = value.toObject();
        if (node.value(QStringLiteral("install_type")).toString() != QStringLiteral("git-clone")) continue;
        const QJsonArray files = node.value(QStringLiteral("files")).toArray();
        QString remote;
        for (const QJsonValue &file : files) {
            const QString candidate = file.toString().trimmed();
            if (candidate.startsWith(QStringLiteral("http"), Qt::CaseInsensitive)) {
                remote = candidate;
                break;
            }
        }
        const QString title = node.value(QStringLiteral("title")).toString().trimmed().left(256);
        if (title.isEmpty() || remote.isEmpty()) continue;
        catalog.append(QVariantMap{
            {QStringLiteral("name"), title},
            {QStringLiteral("description"), node.value(QStringLiteral("description")).toString().left(4096)},
            {QStringLiteral("remote"), remote.left(2048)},
            {QStringLiteral("reference"), node.value(QStringLiteral("reference")).toString().left(2048)},
            {QStringLiteral("author"), node.value(QStringLiteral("author")).toString().left(512)},
            {QStringLiteral("installed"), false}
        });
    }
    if (catalog.isEmpty()) return false;
    m_catalogExtensions = catalog;
    updateAvailableExtensions();
    return true;
}

void VersionManager::updateAvailableExtensions()
{
    if (m_catalogExtensions.isEmpty()) {
        m_availableExtensions = m_installedExtensions;
        return;
    }
    QSet<QString> installedUrls;
    QSet<QString> installedNames;
    for (const QVariant &entry : std::as_const(m_installedExtensions)) {
        const QVariantMap extension = entry.toMap();
        installedUrls.insert(canonicalRepositoryUrl(extension.value(QStringLiteral("remote")).toString()));
        installedNames.insert(extension.value(QStringLiteral("name")).toString().toLower());
    }
    QVariantList available;
    available.reserve(m_catalogExtensions.size());
    for (const QVariant &entry : std::as_const(m_catalogExtensions)) {
        QVariantMap extension = entry.toMap();
        const QString canonical = canonicalRepositoryUrl(extension.value(QStringLiteral("remote")).toString());
        const bool installed = (!canonical.isEmpty() && installedUrls.contains(canonical))
                               || installedNames.contains(extension.value(QStringLiteral("name")).toString().toLower());
        extension.insert(QStringLiteral("installed"), installed);
        available.append(extension);
    }
    m_availableExtensions = available;
}

QString VersionManager::canonicalRepositoryUrl(const QString &url)
{
    QString result = url.trimmed().toLower();
    if (result.startsWith(QStringLiteral("git@github.com:"))) {
        result = QStringLiteral("https://github.com/") + result.mid(15);
    }
    const int query = result.indexOf(QRegularExpression(QStringLiteral("[?#]")));
    if (query >= 0) result.truncate(query);
    while (result.endsWith(QLatin1Char('/'))) result.chop(1);
    if (result.endsWith(QStringLiteral(".git"))) result.chop(4);
    return result;
}

QString VersionManager::selectRemoteBranch(const QString &output, const QString &localBranch)
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

QString VersionManager::selectDevelopmentBranch(const QString &output)
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

bool VersionManager::branchNeedsRecovery(const QString &branch)
{
    return branch.isEmpty()
           || branch.startsWith(QStringLiteral("HEAD"), Qt::CaseInsensitive)
           || branch.startsWith(QStringLiteral("minifox/"), Qt::CaseInsensitive);
}

QString VersionManager::installedExtensionBranch(const QString &path) const
{
    for (const QVariant &entry : m_installedExtensions) {
        const QVariantMap extension = entry.toMap();
        if (extension.value(QStringLiteral("path")).toString() == path) {
            return extension.value(QStringLiteral("branch")).toString();
        }
    }
    return {};
}

QString VersionManager::readComfyVersion(const QString &root) const
{
    const QStringList files {
        root + QStringLiteral("/comfyui_version.py"),
        root + QStringLiteral("/pyproject.toml")
    };
    const QRegularExpression versionExpression(
        QStringLiteral("(?:__version__|version)\\s*=\\s*['\"]([^'\"]+)['\"]"));
    for (const QString &path : files) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QString text = QString::fromUtf8(file.readAll());
        const QRegularExpressionMatch match = versionExpression.match(text);
        if (match.hasMatch()) {
            return match.captured(1);
        }
    }
    return {};
}

void VersionManager::setFailure(const QString &message)
{
    const bool notify = m_notifyOnFinish;
    QString effectiveMessage = message;
    if (!m_operationBackupArchives.isEmpty()) {
        effectiveMessage += tr(" 受影响文件已备份到：%1").arg(backupLocationSummary());
    }
    m_gitTimeout.stop();
    m_extensionUpdateQueue.clear();
    m_extensionCheckQueue.clear();
    m_updatingAllExtensions = false;
    m_fullRefresh = false;
    m_operation = Operation::None;
    m_busy = false;
    m_lastError = effectiveMessage;
    m_statusMessage = effectiveMessage;
    emit stateChanged();
    if (m_refreshScope != RefreshScope::None) {
        m_refreshScope = RefreshScope::None;
        emit refreshCompleted(false, effectiveMessage);
    }
    if (notify) {
        m_notifyOnFinish = false;
        m_pendingCompletionMessage.clear();
        emit operationCompleted(false, effectiveMessage);
    }
}

void VersionManager::finish(const QString &message)
{
    m_gitTimeout.stop();
    m_operation = Operation::None;
    m_busy = false;
    m_statusMessage = message;
    emit stateChanged();
    if (m_notifyOnFinish) {
        m_notifyOnFinish = false;
        m_pendingCompletionMessage.clear();
        emit operationCompleted(true, message);
    }
}

void VersionManager::completeRefresh(bool success, const QString &message)
{
    m_gitTimeout.stop();
    m_operation = Operation::None;
    m_busy = false;
    m_fullRefresh = false;
    m_statusMessage = message;
    if (success) m_lastError.clear();
    m_refreshScope = RefreshScope::None;
    emit stateChanged();
    emit refreshCompleted(success, message);
    if (m_notifyOnFinish) {
        const QString completionMessage = success && !m_pendingCompletionMessage.isEmpty()
            ? m_pendingCompletionMessage : message;
        m_notifyOnFinish = false;
        m_pendingCompletionMessage.clear();
        emit operationCompleted(success, completionMessage);
    }
}

QString VersionManager::requirementsFileFor(const QString &targetDir)
{
    const QString path = QDir(targetDir).filePath(QStringLiteral("requirements.txt"));
    return QFileInfo::exists(path) ? path : QString();
}

void VersionManager::queueDependencyCheck(const QString &targetDir)
{
    if (targetDir.isEmpty()) return;
    if (!m_dependencyQueue.contains(targetDir)) {
        m_dependencyQueue.append(targetDir);
    }
    if (!m_installingDependencies) {
        startNextDependencyCheck();
    }
}

void VersionManager::startNextDependencyCheck()
{
    while (!m_dependencyQueue.isEmpty()) {
        m_dependencyDir = m_dependencyQueue.takeFirst();
        const QString requirements = requirementsFileFor(m_dependencyDir);
        if (requirements.isEmpty()) {
            continue;
        }
        const QString python = m_configuration->pythonPath();
        if (python.isEmpty() || !QFileInfo::exists(python)) {
            emit dependencyInstallCompleted(false,
                tr("未配置有效的 Python 环境，无法检查 %1 的依赖。")
                    .arg(QFileInfo(m_dependencyDir).fileName()));
            continue;
        }
        m_installingDependencies = true;
        m_dependencyInstallPhase = false;
        m_dependencyTimedOut = false;
        emit stateChanged();
        m_dependencyProcess.setProgram(python);
        m_dependencyProcess.setArguments({
            QStringLiteral("-m"), QStringLiteral("pip"),
            QStringLiteral("install"), QStringLiteral("--dry-run"),
            QStringLiteral("-r"), requirements});
        m_dependencyTimeout.start(180000);
        m_dependencyProcess.start();
        return;
    }
    m_installingDependencies = false;
    emit stateChanged();
}

void VersionManager::handleDependencyCheckFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_dependencyTimeout.stop();
    const QString output = ProcessTextDecoder::decode(
        m_dependencyProcess.readAllStandardOutput()).trimmed();
    const QString name = QFileInfo(m_dependencyDir).fileName();
    const bool timedOut = m_dependencyTimedOut;
    m_dependencyTimedOut = false;

    if (timedOut) {
        emit dependencyInstallCompleted(false, tr("%1 的依赖检查超时。").arg(name));
        startNextDependencyCheck();
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        QString detail = output;
        if (detail.size() > 200) detail = QStringLiteral("…") + detail.right(200);
        if (detail.isEmpty()) detail = tr("pip 无法运行。");
        emit dependencyInstallCompleted(false, tr("%1 的依赖检查失败：%2").arg(name, detail));
        startNextDependencyCheck();
        return;
    }
    if (!output.contains(QStringLiteral("Would install"))) {
        startNextDependencyCheck();
        return;
    }

    const QString requirements = requirementsFileFor(m_dependencyDir);
    const QString python = m_configuration->pythonPath();
    PortablePaths::ensureDataDirectory();
    const QString stamp = QStringLiteral("%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch());
    m_dependencyMarkerPath = QDir(PortablePaths::dataDirectory())
        .filePath(QStringLiteral("dep-%1.code").arg(stamp));
    m_dependencyBatPath = QDir(PortablePaths::dataDirectory())
        .filePath(QStringLiteral("dep-%1.bat").arg(stamp));

    const QString bat = QStringLiteral(
        "@echo off\r\n"
        "chcp 65001 >nul\r\n"
        "title Minifox - %1\r\n"
        "\"%2\" -m pip install -r \"%3\"\r\n"
        "set CODE=%ERRORLEVEL%\r\n"
        "> \"%4\" echo %CODE%\r\n"
        "if not \"%CODE%\"==\"0\" (\r\n"
        "    echo.\r\n"
        "    echo [Minifox] 依赖安装失败，按任意键关闭窗口…\r\n"
        "    pause >nul\r\n"
        ")\r\n"
        "exit /b %CODE%\r\n").arg(name,
                                QDir::toNativeSeparators(python),
                                QDir::toNativeSeparators(requirements),
                                QDir::toNativeSeparators(m_dependencyMarkerPath));
    QFile batFile(m_dependencyBatPath);
    if (!batFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit dependencyInstallCompleted(false, tr("无法创建 %1 的依赖安装脚本。").arg(name));
        startNextDependencyCheck();
        return;
    }
    batFile.write(bat.toUtf8());
    batFile.close();

    m_dependencyInstallPhase = true;
    emit stateChanged();
    m_dependencyProcess.setProgram(QStringLiteral("cmd.exe"));
    m_dependencyProcess.setArguments({
        QStringLiteral("/c"), QStringLiteral("start"),
        QStringLiteral("Minifox 依赖安装 - %1").arg(name),
        QStringLiteral("/wait"),
        QStringLiteral("cmd"), QStringLiteral("/c"),
        QDir::toNativeSeparators(m_dependencyBatPath)});
    m_dependencyProcess.start();
    if (!m_dependencyProcess.waitForStarted(3000)
        || !m_dependencyJob.attach(m_dependencyProcess.processId())) {
        if (m_dependencyProcess.state() != QProcess::NotRunning) {
            m_dependencyProcess.kill();
        }
        m_dependencyJob.reset();
        m_dependencyInstallPhase = false;
        emit dependencyInstallCompleted(false, tr("无法将 %1 的依赖安装窗口加入安全作业。").arg(name));
        startNextDependencyCheck();
    }
}

void VersionManager::handleDependencyInstallFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode);
    Q_UNUSED(exitStatus);
    m_dependencyJob.reset();
    m_dependencyInstallPhase = false;
    const QString name = QFileInfo(m_dependencyDir).fileName();
    int code = -1;
    QFile marker(m_dependencyMarkerPath);
    if (marker.open(QIODevice::ReadOnly | QIODevice::Text)) {
        bool ok = false;
        code = QString::fromUtf8(marker.readAll()).trimmed().toInt(&ok);
        if (!ok) code = -1;
        marker.close();
        marker.remove();
    }
    QFile::remove(m_dependencyBatPath);
    if (code == 0) {
        emit dependencyInstallCompleted(true, tr("%1：依赖安装完成。").arg(name));
    } else if (code < 0) {
        emit dependencyInstallCompleted(false,
            tr("%1：依赖安装窗口被关闭，无法确认安装结果。").arg(name));
    } else {
        emit dependencyInstallCompleted(false,
            tr("%1：依赖安装失败（退出码 %2）。").arg(name).arg(code));
    }
    startNextDependencyCheck();
}

QString VersionManager::normalizedBranchName() const
{
    if (m_branch != QStringLiteral("master") && m_branch != QStringLiteral("dev")) {
        return {};
    }
    const QString target = commitHasVersionTag(m_commitFull)
        ? QStringLiteral("master") : QStringLiteral("dev");
    return m_branch == target ? QString() : target;
}

bool VersionManager::commitHasVersionTag(const QString &commitFull) const
{
    if (commitFull.isEmpty()) return false;
    const QDir gitDir(QDir(m_comfyRoot).filePath(QStringLiteral(".git")));

    const QDir tagsDir(gitDir.filePath(QStringLiteral("refs/tags")));
    const QFileInfoList looseTags = tagsDir.entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &tagInfo : looseTags) {
        if (!tagInfo.fileName().startsWith(QLatin1Char('v'))) continue;
        QFile tagFile(tagInfo.absoluteFilePath());
        if (tagFile.open(QIODevice::ReadOnly | QIODevice::Text)
            && QString::fromUtf8(tagFile.readAll()).trimmed() == commitFull) {
            return true;
        }
    }

    QFile packed(gitDir.filePath(QStringLiteral("packed-refs")));
    if (!packed.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    bool vTagMayFollow = false;
    const QStringList lines = QString::fromUtf8(packed.readAll()).split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        if (line.startsWith(QLatin1Char('^'))) {
            if (vTagMayFollow && line.mid(1).trimmed() == commitFull) return true;
            vTagMayFollow = false;
            continue;
        }
        vTagMayFollow = false;
        const int separator = line.indexOf(QLatin1Char(' '));
        if (separator < 0) continue;
        const QString ref = line.mid(separator + 1).trimmed();
        if (!ref.startsWith(QStringLiteral("refs/tags/v"))) continue;
        if (line.left(separator) == commitFull) return true;
        vTagMayFollow = true;
    }
    return false;
}
