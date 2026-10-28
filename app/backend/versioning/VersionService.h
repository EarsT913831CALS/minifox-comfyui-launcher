#pragma once

#include "EntryModel.h"
#include "Extensions.h"
#include "Git.h"
#include "OperationLease.h"
#include "Repository.h"
#include "Transaction.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QThreadPool>
#include <QVariantList>

#include <atomic>
#include <functional>

class ApplicationSettings;
class ConfigurationManager;
class Dependencies;
class QNetworkReply;

// Everything the version page does: ComfyUI versions and branches, installed
// extensions, the extension catalog and dependency installs.
//
// Git work runs on worker threads. Reads (fetch, history) run up to four at a
// time; changes to one repository never overlap, and a ComfyUI change runs
// alone. While anything changes, the ComfyUI folder and Python are leased so
// ComfyUI cannot start in the middle of it.
class VersionService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool updating READ updating NOTIFY stateChanged)
    Q_PROPERTY(bool installingDependencies READ installingDependencies NOTIFY stateChanged)
    Q_PROPERTY(bool canCheck READ canCheck NOTIFY stateChanged)
    Q_PROPERTY(bool interruptedOperation READ interruptedOperation NOTIFY stateChanged)
    Q_PROPERTY(QString interruptedOperationDescription READ interruptedOperationDescription NOTIFY stateChanged)
    Q_PROPERTY(QString launcherVersion READ launcherVersion CONSTANT)
    Q_PROPERTY(QString comfyVersion READ comfyVersion NOTIFY stateChanged)
    Q_PROPERTY(QString branch READ branch NOTIFY stateChanged)

    Q_PROPERTY(bool repository READ repository NOTIFY stateChanged)
    Q_PROPERTY(bool coreBusy READ coreBusy NOTIFY stateChanged)
    Q_PROPERTY(bool coreChecking READ coreChecking NOTIFY stateChanged)
    Q_PROPERTY(bool canUpdate READ canUpdate NOTIFY stateChanged)
    Q_PROPERTY(QString remoteUrl READ remoteUrl NOTIFY stateChanged)
    Q_PROPERTY(QString commitFull READ commitFull NOTIFY stateChanged)
    Q_PROPERTY(QString commitDate READ commitDate NOTIFY stateChanged)
    Q_PROPERTY(QString commitSubject READ commitSubject NOTIFY stateChanged)
    Q_PROPERTY(int localChanges READ localChanges NOTIFY stateChanged)
    Q_PROPERTY(QString coreError READ coreError NOTIFY stateChanged)
    Q_PROPERTY(QVariantList stableVersions READ stableVersions NOTIFY stateChanged)
    Q_PROPERTY(QVariantList coreVersions READ coreVersions NOTIFY stateChanged)

    Q_PROPERTY(EntryModel *installedExtensionModel READ installedExtensionModel CONSTANT)
    Q_PROPERTY(EntryModel *availableExtensionModel READ availableExtensionModel CONSTANT)
    Q_PROPERTY(bool extensionsChecking READ extensionsChecking NOTIFY stateChanged)
    Q_PROPERTY(int outdatedCount READ outdatedCount NOTIFY stateChanged)
    Q_PROPERTY(bool catalogLoading READ catalogLoading NOTIFY stateChanged)
    Q_PROPERTY(QString catalogError READ catalogError NOTIFY stateChanged)
    Q_PROPERTY(QVariantList extensionVersions READ extensionVersions NOTIFY extensionVersionsChanged)
    Q_PROPERTY(bool extensionVersionsLoading READ extensionVersionsLoading NOTIFY extensionVersionsChanged)
    Q_PROPERTY(QString extensionVersionsError READ extensionVersionsError NOTIFY extensionVersionsChanged)

public:
    VersionService(ConfigurationManager *configuration, ApplicationSettings *settings, QObject *parent = nullptr);
    ~VersionService() override;

    bool busy() const;
    bool updating() const;
    bool installingDependencies() const;
    bool canCheck() const;
    bool interruptedOperation() const { return !m_interrupted.isEmpty(); }
    QString interruptedOperationDescription() const { return m_interrupted; }
    QString launcherVersion() const;
    QString comfyVersion() const { return m_core.version; }
    QString branch() const { return m_core.info.branch; }

    bool repository() const { return m_core.info.repository; }
    bool coreBusy() const;
    bool coreChecking() const;
    bool canUpdate() const;
    QString remoteUrl() const { return m_core.info.remoteUrl; }
    QString commitFull() const { return m_core.info.head; }
    QString commitDate() const { return m_core.info.date; }
    QString commitSubject() const { return m_core.info.subject; }
    int localChanges() const { return int(m_core.info.changes.size()); }
    QString coreError() const { return m_core.error; }
    QVariantList stableVersions() const;
    QVariantList coreVersions() const;

    EntryModel *installedExtensionModel() const { return m_installed; }
    EntryModel *availableExtensionModel() const { return m_catalog; }
    bool extensionsChecking() const { return m_extensionChecks > 0 || m_scanning; }
    int outdatedCount() const;
    bool catalogLoading() const { return m_catalogLoading; }
    QString catalogError() const { return m_catalogError; }
    QVariantList extensionVersions() const { return m_extensionVersions; }
    bool extensionVersionsLoading() const { return m_extensionVersionsLoading; }
    QString extensionVersionsError() const { return m_extensionVersionsError; }

    // The page calls this when shown: local state at once, one fetch per session.
    Q_INVOKABLE void ensureLoaded(int tab);
    Q_INVOKABLE void refreshCore();
    Q_INVOKABLE void refreshInstalledExtensions();
    Q_INVOKABLE void refreshAvailableExtensions();

    Q_INVOKABLE void updateComfyUi(int channel);
    Q_INVOKABLE void switchCoreVersion(const QString &commit);
    Q_INVOKABLE void switchBranch(int repositorySource);
    Q_INVOKABLE void discardCoreChanges();
    Q_INVOKABLE void cleanComfyUiRepository();

    Q_INVOKABLE void checkExtensionNow(const QString &path);
    Q_INVOKABLE void updateExtension(const QString &path);
    Q_INVOKABLE void updateAllExtensions();
    Q_INVOKABLE void loadExtensionVersions(const QString &path);
    Q_INVOKABLE void switchExtensionVersion(const QString &path, const QString &commit);
    Q_INVOKABLE void discardExtensionChanges(const QString &path);
    Q_INVOKABLE void installExtension(const QString &url);
    Q_INVOKABLE void removeExtension(const QString &path);
    Q_INVOKABLE void setExtensionEnabled(const QString &path, bool enabled);

    Q_INVOKABLE void recoverInterruptedOperations();
    Q_INVOKABLE void cancelDependencyInstall();
    void retranslate();

signals:
    void stateChanged();
    void extensionVersionsChanged();
    void refreshCompleted(bool success, const QString &message);
    void operationCompleted(bool success, const QString &message);
    void mergeConflictsReported(const QString &message);
    void dependencyInstallCompleted(bool success, const QString &message);

private:
    struct Core {
        RepositoryInfo info;
        QList<Commit> stable;
        QList<Commit> development;
        QString version;
        QString error;
        QString developmentTarget;
    };

    // Work runs on a worker and returns what to do on the main thread.
    using Completion = std::function<void()>;
    using Work = std::function<Completion(const git::Tool &tool)>;
    struct Task {
        QString key;
        bool changes = false;
        bool exclusive = false;
        Work work;
        std::function<void(const QString &error)> refused;
    };

    void submit(Task task);
    void schedule();
    bool startable(const Task &task) const;
    void releaseLeaseIfIdle();

    void reloadConfiguration();
    void rebuildTool();
    QString comfyRoot() const { return m_comfyRoot; }
    QString customNodes() const;
    QString backupRoot() const;
    QString journalDirectory() const;

    void checkCore(bool fetch, bool report);
    void checkExtensions(bool fetch, bool report);
    void checkExtension(const QString &path, bool fetch);
    void reportExtensionsIfDone();
    void publishExtensions(const QList<Extension> &extensions);
    QVariantMap extensionEntry(const Extension &extension) const;
    void markInstalledInCatalog();
    void loadCatalogCache();
    void downloadCatalog(bool report);
    void handleCatalogReply(QNetworkReply *reply, bool report);
    void recover(bool automatic);

    // One transaction on a worker, then reporting and dependency installs.
    struct Change {
        QString root;
        QString name;
        QString success;
        bool exclusive = false;
        bool keepLocal = true;
        bool clean = false;
        // Runs on the worker before the transaction: fills target, branch and
        // upstream, and returns an error to stop.
        std::function<QString(const git::Tool &tool, transaction::Request &request)> prepare;
    };
    void changeRepository(Change change);
    void finishChange(const Change &change, const transaction::Outcome &outcome, const QString &error,
                      bool requirementsChanged);
    // Changes to a folder that is not a repository (install, remove, enable).
    void changeFolder(const QString &key, const QString &name, std::function<QString()> work,
                      const QString &success, const QString &dependencies = {});
    void reportOutcome(const QString &name, const QString &backup, const QStringList &conflicts,
                       const QStringList &moved, bool discarded);
    void setEntryStatus(const QString &path, const QString &status, const QString &error = {});

    QPointer<ConfigurationManager> m_configuration;
    QPointer<ApplicationSettings> m_settings;
    QString m_comfyRoot;
    QString m_python;
    git::Tool m_tool;
    QProcessEnvironment m_environment;
    QThreadPool m_pool;
    std::atomic_bool m_shutdown = false;

    QList<Task> m_waiting;
    QSet<QString> m_runningKeys;
    int m_runningChanges = 0;
    bool m_exclusiveRunning = false;
    OperationLease m_lease;

    Core m_core;
    int m_coreChecks = 0;
    bool m_coreFetched = false;
    bool m_extensionsFetched = false;
    bool m_scanning = false;
    int m_extensionChecks = 0;
    bool m_reportExtensions = false;
    QSet<QString> m_changing;
    QString m_interrupted;

    EntryModel *m_installed = nullptr;
    EntryModel *m_catalog = nullptr;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_catalogReply;
    bool m_catalogLoading = false;
    bool m_catalogRequested = false;
    QString m_catalogError;
    QList<CatalogEntry> m_catalogEntries;

    QString m_versionsPath;
    QVariantList m_extensionVersions;
    bool m_extensionVersionsLoading = false;
    QString m_extensionVersionsError;

    Dependencies *m_dependencies = nullptr;
};
