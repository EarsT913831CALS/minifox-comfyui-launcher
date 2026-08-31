#pragma once

#include "GitProcessRunner.h"
#include "SafeMergeEngine.h"

#include <QObject>
#include <QProcessEnvironment>

enum class RepositoryKind { Core, Extension };
enum class RepositoryTarget { Stable, Development, CurrentBranch };

struct RepositoryUpdateSpec {
    RepositoryKind kind = RepositoryKind::Extension;
    RepositoryTarget target = RepositoryTarget::CurrentBranch;
    QString root;
    QString displayName;
    QString localBranch;
    QString gitProgram;
    QStringList gitPrefixArguments;
};

struct RepositoryUpdateResult {
    QString root;
    QString displayName;
    bool success = false;
    bool incomplete = false;
    bool timedOut = false;
    QString error;
};

class RepositoryUpdateJob final : public QObject
{
    Q_OBJECT

public:
    explicit RepositoryUpdateJob(const RepositoryUpdateSpec &spec,
                                 QObject *parent = nullptr);
    ~RepositoryUpdateJob() override;

    RepositoryUpdateSpec spec() const;
    void start();
    void cancel();

    static QString selectRemoteBranch(const QString &output,
                                      const QString &localBranch = {});
    static QString selectDevelopmentBranch(const QString &output);
    static bool branchNeedsRecovery(const QString &branch);

signals:
    void statusChanged(const QString &message);
    void finished(const RepositoryUpdateResult &result);

private:
    enum class Stage {
        Idle,
        Status,
        ReadHead,
        Fetch,
        ResolveStable,
        ResolveDevelopment,
        ResolveCurrent,
        CleanCheckout,
        CleanFastForward,
        PrepareWorkingIndex,
        StageLocalChanges,
        WriteLocalTree,
        PrepareBaseIndex,
        CheckoutBaseSnapshot,
        CheckoutLocalSnapshot,
        PrepareTargetIndex,
        CheckoutTargetSnapshot,
        UpdateSafeRef,
        SwitchSafeHead,
        ResetSafeIndex,
        SetUpstream
    };

    void runGit(Stage stage, const QStringList &arguments,
                const QString &indexFile = {}, int inactivityTimeoutMs = 120000);
    void handleProcessFinished(const ProcessResult &result);
    void resolveTarget(const QString &output);
    void beginMutation(const QString &phase);
    void beginCleanMutation();
    void beginSafeMerge();
    void runSafeMerge();
    void handleSafeMergeFinished(const SafeMergeResult &result);
    void complete();
    void fail(const QString &message, bool timedOut = false,
              bool definitelyUnmodified = false);
    void clearTemporaryFiles();
    QStringList repositoryArguments(const QStringList &arguments) const;
    QString processError(const ProcessResult &result) const;
    QString actionKey() const;

    RepositoryUpdateSpec m_spec;
    GitProcessRunner m_runner;
    Stage m_stage = Stage::Idle;
    bool m_dirty = false;
    bool m_started = false;
    bool m_hadInterruptedEntry = false;
    bool m_transactionActive = false;
    QString m_transactionId;
    QString m_originalHead;
    QString m_targetRef;
    QString m_targetBranch;
    QString m_upstreamRef;
    bool m_trackBranch = false;
    QString m_workingIndexPath;
    QString m_baseIndexPath;
    QString m_targetIndexPath;
    QString m_baseSnapshotPath;
    QString m_localSnapshotPath;
    QString m_targetSnapshotPath;
};
