#pragma once
#include "OperationLease.h"
#include "version/RepositoryGitContext.h"

#include "ProcessJob.h"
#include "version/SafeMergeEngine.h"

#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QVariantList>

class ConfigurationManager;
class ApplicationSettings;
class QNetworkReply;
class RepositoryUpdateCoordinator;

class VersionManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool installingDependencies READ installingDependencies NOTIFY stateChanged)
    Q_PROPERTY(bool updating READ updating NOTIFY stateChanged)
    Q_PROPERTY(bool catalogLoading READ catalogLoading NOTIFY stateChanged)
    Q_PROPERTY(bool repository READ repository NOTIFY stateChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY stateChanged)
    Q_PROPERTY(bool canCheck READ canCheck NOTIFY stateChanged)
    Q_PROPERTY(bool canUpdate READ canUpdate NOTIFY stateChanged)
    Q_PROPERTY(QString launcherVersion READ launcherVersion CONSTANT)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
    Q_PROPERTY(QString platformText READ platformText CONSTANT)
    Q_PROPERTY(QString comfyRoot READ comfyRoot NOTIFY stateChanged)
    Q_PROPERTY(QString comfyVersion READ comfyVersion NOTIFY stateChanged)
    Q_PROPERTY(QString branch READ branch NOTIFY stateChanged)
    Q_PROPERTY(QString commit READ commit NOTIFY stateChanged)
    Q_PROPERTY(QString commitFull READ commitFull NOTIFY stateChanged)
    Q_PROPERTY(int networkRoute READ networkRoute WRITE setNetworkRoute NOTIFY stateChanged)
    Q_PROPERTY(QString commitDate READ commitDate NOTIFY stateChanged)
    Q_PROPERTY(QString commitSubject READ commitSubject NOTIFY stateChanged)
    Q_PROPERTY(QString remoteUrl READ remoteUrl NOTIFY stateChanged)
    Q_PROPERTY(QVariantList coreVersions READ coreVersions NOTIFY stateChanged)
    Q_PROPERTY(QVariantList stableVersions READ stableVersions NOTIFY stateChanged)
    Q_PROPERTY(QVariantList installedExtensions READ installedExtensions NOTIFY stateChanged)
    Q_PROPERTY(QVariantList availableExtensions READ availableExtensions NOTIFY stateChanged)
    Q_PROPERTY(QVariantList extensionVersions READ extensionVersions NOTIFY stateChanged)
    Q_PROPERTY(QString repositoryState READ repositoryState NOTIFY stateChanged)
    Q_PROPERTY(QString updateState READ updateState NOTIFY stateChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(bool interruptedOperation READ interruptedOperation NOTIFY stateChanged)
    Q_PROPERTY(QString interruptedOperationDescription READ interruptedOperationDescription NOTIFY stateChanged)
    Q_PROPERTY(int aheadCount READ aheadCount NOTIFY stateChanged)
    Q_PROPERTY(int behindCount READ behindCount NOTIFY stateChanged)

public:
    explicit VersionManager(ConfigurationManager *configuration,
                            ApplicationSettings *settings,
                            QObject *parent = nullptr);

    bool busy() const;
    bool installingDependencies() const;
    bool updating() const;
    bool catalogLoading() const;
    bool repository() const;
    bool dirty() const;
    bool canCheck() const;
    bool canUpdate() const;
    QString launcherVersion() const;
    QString qtVersion() const;
    QString platformText() const;
    QString comfyRoot() const;
    QString comfyVersion() const;
    QString branch() const;
    QString commit() const;
    QString commitFull() const;
    int networkRoute() const;
    void setNetworkRoute(int route);
    QString commitDate() const;
    QString commitSubject() const;
    QString remoteUrl() const;
    QVariantList coreVersions() const;
    QVariantList stableVersions() const;
    QVariantList installedExtensions() const;
    QVariantList availableExtensions() const;
    QVariantList extensionVersions() const;
    QString repositoryState() const;
    QString updateState() const;
    QString statusMessage() const;
    QString lastError() const;
    bool interruptedOperation() const;
    QString interruptedOperationDescription() const;
    int aheadCount() const;
    int behindCount() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshCore();
    Q_INVOKABLE void refreshInstalledExtensions();
    Q_INVOKABLE void refreshAvailableExtensions();
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void updateComfyUi(int channel);
    Q_INVOKABLE void switchCoreVersion(const QString &commit, int channel);
    Q_INVOKABLE void switchBranch(const QString &branch, int repositorySource);
    Q_INVOKABLE void cleanComfyUiRepository();
    Q_INVOKABLE void recoverInterruptedOperations();
    Q_INVOKABLE void updateExtension(const QString &path);
    Q_INVOKABLE void updateAllExtensions();
    Q_INVOKABLE void loadExtensionVersions(const QString &path, const QString &currentCommit);
    Q_INVOKABLE void switchExtensionVersion(const QString &path, const QString &commit);
    Q_INVOKABLE void installExtension(const QString &url);
    Q_INVOKABLE void removeExtension(const QString &path);
    Q_INVOKABLE void setExtensionEnabled(const QString &path, bool enabled);
    void retranslate();

signals:
    void stateChanged();
    void refreshCompleted(bool success, const QString &message);
    void operationCompleted(bool success, const QString &message);
    void dependencyInstallCompleted(bool success, const QString &message);
    void extensionVersionsLoaded(bool success, const QString &message);

private:
    friend class VersionManagerTest;
    enum class Operation {
        None,
        RefreshStatus,
        RefreshLog,
        Fetch,
        ResolveCoreCompareBranch,
        Compare,
        CollectTrackedBackup,
        CollectUntrackedBackup,
        CreateBackupArchive,
        ValidateCoreUpdate,
        ValidateCoreVersion,
        ValidateCoreBranch,
        ResolveSafeMergePaths,
        PrepareSafeMergeIndex,
        StageSafeMergeChanges,
        WriteSafeMergeTree,
        PrepareSafeMergeBaseSnapshot,
        CheckoutSafeMergeBaseSnapshot,
        CheckoutSafeMergeLocalSnapshot,
        PrepareSafeMergeTargetSnapshot,
        CheckoutSafeMergeTargetSnapshot,
        UpdateSafeMergeRef,
        SwitchSafeMergeHead,
        ResetSafeMergeIndex,
        SetSafeMergeUpstream,
        ResetCoreForUpdate,
        PrepareCoreUpdateFetch,
        ResolveStableUpdateCommit,
        ResolveDevelopmentUpdateBranch,
        ResolveCoreUpdateBranch,
        AttachCoreUpdateBranch,
        SetCoreUpdateUpstream,
        Pull,
        ResolveDevelopmentHistoryBranch,
        LoadCoreHistory,
        LoadStableHistory,
        ResetCoreForVersion,
        CheckoutCore,
        ResetCoreForBranch,
        ResetCoreForCleanup,
        CleanCore,
        SetCoreBranchRemote,
        FetchCoreBranchRemote,
        CheckoutBranch,
        ValidateExtensionUpdate,
        ResetExtensionForUpdate,
        PrepareExtensionUpdateFetch,
        ResolveExtensionUpdateBranch,
        AttachExtensionUpdateBranch,
        SetExtensionUpdateUpstream,
        UpdateExtension,
        LoadExtensionHistory,
        ValidateExtensionCheckout,
        ResolveExtensionCheckoutBranch,
        ResetExtensionForCheckout,
        CheckoutExtension,
        InstallExtension
    };

    enum class RefreshScope {
        None,
        Core,
        InstalledExtensions,
        AvailableExtensions
    };

    enum class PendingCoreAction {
        None,
        Update,
        SwitchVersion,
        SwitchBranch,
        FullClean,
        UpdateExtension,
        SwitchExtensionVersion
    };

    struct BackupArchiveRequest {
        QString sourceRoot;
        QString category;
        QString label;
        QByteArray fileList;
    };

    void startGit(Operation operation, const QStringList &arguments,
                  const QByteArray &standardInput = QByteArray());
    void startProcess(Operation operation, const QString &program,
                      const QStringList &arguments,
                      const QByteArray &standardInput = QByteArray());
    // Skip Git when a snapshot has no paths.
    void startSafeMergeCheckoutIndex(Operation operation, const QString &snapshotPath,
                                     const QStringList &paths);
    void beginSafeMerge(PendingCoreAction action, const QString &repositoryRoot,
                        const QString &targetRef, const QString &targetBranch = {},
                        bool trackBranch = false, const QString &upstreamRef = {});
    bool beginVersionTransaction(PendingCoreAction action, const QString &repositoryRoot,
                                 const QString &targetRef = {}, const QString &targetBranch = {},
                                 const QString &phase = {});
    QString transactionActionKey(PendingCoreAction action) const;
    void loadVersionTransaction();
    void hideInterruptedOperationWhileUpdating();
    bool clearVersionTransaction();
    bool completeVersionTransaction();
    void clearSafeMergeState();
    void startSafeSnapshotMerge();
    void handleSafeSnapshotMergeFinished(bool success, const QString &error);
    void finishSafeMerge();
    void finishVerifiedSafeMerge();
    void beginResetAction(PendingCoreAction action, const QString &repositoryRoot);
    void collectBackupPaths(const QByteArray &output);
    void createBackupArchivesOrContinue();
    void startNextBackupArchive();
    void continuePendingCoreAction();
    void clearBackupState();
    bool pruneBackupArchives(const QString &categoryDirectory, int maximum, QString *error);
    bool pruneBackupDays(const QString &backupRoot, QString *error);
    QString backupLocationSummary() const;
    void resetOperationBackupSummary();
    void appendBackupSummaryToPendingCompletion();
    void startExtensionUpdate(const QString &path);
    void loadLocalState();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void parseStatus(const QString &output);
    void parseCoreHistory(const QString &output);
    void parseStableHistory(const QString &output);
    void parseExtensionHistory(const QString &output);
    void scanExtensions(bool resetStatus = false);
    void beginExtensionChecks();
    void startExtensionCheckJobs();
    void handleExtensionCheckFinished(QProcess *process, int exitCode,
                                      QProcess::ExitStatus exitStatus);
    void setExtensionStatus(const QString &path, const QString &status);
    void downloadCatalog();
    bool applyCatalogData(const QByteArray &data);
    void updateAvailableExtensions();
    QString catalogCachePath() const;
    QStringList repositoryArguments(const QString &root, const QStringList &arguments) const;
    QStringList networkRouteArguments() const;
    static QString readGitValue(const QString &repositoryRoot, const QString &key);
    static QString readExtensionDescription(const QString &path);
    static QString canonicalRepositoryUrl(const QString &url);
    static QString selectRemoteBranch(const QString &output, const QString &localBranch = {});
    static QString selectDevelopmentBranch(const QString &output);
    static bool branchNeedsRecovery(const QString &branch);
    QString installedExtensionBranch(const QString &path) const;
    QString findGit() const;
    QString readComfyVersion(const QString &root) const;
    void setFailure(const QString &message);
    void finish(QString message = {});
    void completeRefresh(bool success, const QString &message);
    void queueDependencyCheck(const QString &targetDir);
    void startNextDependencyCheck();
    void handleDependencyCheckFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleDependencyInstallFinished(int exitCode, QProcess::ExitStatus exitStatus);
    static QString requirementsFileFor(const QString &targetDir);

    ConfigurationManager *m_configuration;
    bool acquireMutationLease(const QString &repository = {});
    RepositoryGitContext gitContext() const;
    OperationLease m_mutationLease;
    RepositoryGitContext m_operationGit;
    QString m_operationPython;
    bool m_operationReset = false;
    ApplicationSettings *m_settings;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_catalogReply;
    QProcess m_process;
    QTimer m_gitTimeout;
    QProcess m_dependencyProcess;
    ProcessJob m_dependencyJob;
    QTimer m_dependencyTimeout;
    QStringList m_dependencyQueue;
    QString m_dependencyDir;
    bool m_dependencyInstallPhase = false;
    bool m_dependencyTimedOut = false;
    bool m_installingDependencies = false;
    Operation m_operation = Operation::None;
    QString m_gitProgram;
    QString m_comfyRoot;
    QString m_comfyVersion;
    QString m_branch;
    QString m_commit;
    QString m_commitFull;
    int m_networkRoute = 0;
    QString m_commitDate;
    QString m_commitSubject;
    QString m_remoteUrl;
    QString m_statusMessage;
    QString m_lastError;
    bool m_interruptedOperation = false;
    QStringList m_interruptedOperationNames;
    bool m_transactionInProgress = false;
    bool m_resetBackupReady = false;
    QString m_transactionId;
    QString m_transactionAction;
    QString m_transactionRoot;
    QString m_transactionTargetRef;
    QString m_transactionTargetBranch;
    QString m_transactionPhase;
    bool m_busy = false;
    bool m_repository = false;
    bool m_dirty = false;
    bool m_operationTrackedChanges = false;
    int m_aheadCount = 0;
    int m_behindCount = 0;
    QVariantList m_coreVersions;
    QVariantList m_stableVersions;
    QVariantList m_installedExtensions;
    QVariantList m_availableExtensions;
    QVariantList m_extensionVersions;
    QVariantList m_catalogExtensions;
    QString m_operationPath;
    QString m_targetRemoteBranch;
    QString m_developmentRemoteBranch;
    QString m_pendingCommit;
    QString m_pendingBranch;
    QString m_pendingBranchRemoteUrl;
    PendingCoreAction m_pendingCoreAction = PendingCoreAction::None;
    QString m_backupSourceRoot;
    QByteArray m_backupPaths;
    QList<BackupArchiveRequest> m_backupArchiveQueue;
    QStringList m_createdBackupArchives;
    QStringList m_operationBackupArchives;
    QString m_backupListPath;
    QString m_backupArchivePath;
    QString m_backupArchiveTempPath;
    QString m_backupRootPath;
    QString m_currentBackupCategoryDirectory;
    QString m_extensionHistoryCurrentCommit;
    int m_requestedCoreChannel = 0;
    QStringList m_extensionUpdateQueue;
    QStringList m_extensionCheckQueue;
    QStringList m_refreshErrors;
    int m_activeExtensionChecks = 0;
    bool m_updatingAllExtensions = false;
    bool m_fullRefresh = false;
    bool m_remoteUnavailable = false;
    bool m_catalogLoading = false;
    RepositoryUpdateCoordinator *m_updateCoordinator = nullptr;
    bool m_repositoryBatchCore = false;
    QStringList m_repositoryBatchDependencyRoots;
    bool m_reconcilingVersionTransactions = false;
    bool m_notifyOnFinish = false;
    QString m_pendingCompletionMessage;
    QString m_safeMergeRoot;
    QString m_safeMergeTargetRef;
    QString m_safeMergeTargetBranch;
    QString m_safeMergeUpstreamRef;
    QString m_safeMergeIndexPath;
    QString m_safeMergeBaseIndexPath;
    QString m_safeMergeTargetIndexPath;
    QString m_safeMergeBaseSnapshotPath;
    QString m_safeMergeLocalSnapshotPath;
    QString m_safeMergeTargetSnapshotPath;
    bool m_safeMergeTrackBranch = false;
    QList<LocalPathEntry> m_safeMergeLocalEntries;
    SparseSnapshotPaths m_safeMergePlan;
    bool m_safeMergeRequested = false;
    PendingCoreAction m_safeMergeAction = PendingCoreAction::None;
    RefreshScope m_refreshScope = RefreshScope::None;
};
