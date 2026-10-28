#include "VersionService.h"

#include "ApplicationSettings.h"
#include "ConfigurationManager.h"
#include "Dependencies.h"
#include "PortablePaths.h"
#include "Transaction.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>

namespace {

const QString kOfficialCore = QStringLiteral("https://github.com/Comfy-Org/ComfyUI.git");
const QString kMirrorCore = QStringLiteral("https://cnb.cool/IndexMirror/ComfyUI.git");
const QUrl kCatalogUrl(QStringLiteral("https://raw.githubusercontent.com/Comfy-Org/ComfyUI-Manager/main/custom-node-list.json"));
constexpr qint64 kCatalogLimit = 16 * 1024 * 1024;
constexpr int kMaxReads = 4;
constexpr int kKeptBackupDays = 30;

QString catalogCache()
{
    return QDir(PortablePaths::dataDirectory()).filePath(QStringLiteral("custom-node-list.json"));
}

QString catalogTag()
{
    return QDir(PortablePaths::dataDirectory()).filePath(QStringLiteral("custom-node-list.etag"));
}

QStringList withoutPlaceholders(const QStringList &paths)
{
    QStringList result;
    for (const QString &path : paths) {
        if (!extensions::isPlaceholder(path)) result.append(path);
    }
    return result;
}

QString comfyVersionOf(const QString &root, const RepositoryInfo &info)
{
    QFile file(QDir(root).filePath(QStringLiteral("comfyui_version.py")));
    if (file.open(QIODevice::ReadOnly)) {
        static const QRegularExpression expression(QStringLiteral("__version__\\s*=\\s*[\"']([^\"']+)[\"']"));
        const QRegularExpressionMatch match = expression.match(QString::fromUtf8(file.read(4096)));
        if (match.hasMatch()) return QStringLiteral("v") + match.captured(1);
    }
    return info.shortHead;
}

// Repositories that older launchers left on a minifox/* helper branch go
// back onto the real branch.
QString ownBranch(const RepositoryInfo &info)
{
    return info.branch.startsWith(QStringLiteral("minifox/")) ? QString() : info.branch;
}

QString remoteBranchName(const QString &remoteBranch)
{
    return remoteBranch.section(QLatin1Char('/'), 1);
}

QVariantList commitList(const QList<Commit> &commits, const QString &head)
{
    QVariantList result;
    for (const Commit &commit : commits) {
        QString subject = commit.subject;
        if (!commit.tag.isEmpty() && !subject.contains(commit.tag)) subject = commit.tag + QStringLiteral(" · ") + subject;
        result.append(QVariantMap{
            {QStringLiteral("commit"), commit.id},
            {QStringLiteral("shortCommit"), commit.shortId},
            {QStringLiteral("subject"), subject},
            {QStringLiteral("date"), commit.date},
            {QStringLiteral("current"), commit.id == head},
        });
    }
    return result;
}

}

VersionService::VersionService(ConfigurationManager *configuration, ApplicationSettings *settings, QObject *parent)
    : QObject(parent),
      m_configuration(configuration),
      m_settings(settings),
      m_installed(new EntryModel(QStringLiteral("path"),
                                 {QStringLiteral("name"), QStringLiteral("remote"), QStringLiteral("branch")}, this)),
      m_catalog(new EntryModel(QStringLiteral("key"),
                               {QStringLiteral("name"), QStringLiteral("description"), QStringLiteral("author"),
                                QStringLiteral("remote")},
                               this)),
      m_dependencies(new Dependencies(this))
{
    m_pool.setMaxThreadCount(kMaxReads);
    connect(m_dependencies, &Dependencies::runningChanged, this, [this] {
        releaseLeaseIfIdle();
        emit stateChanged();
    });
    connect(m_dependencies, &Dependencies::completed, this, &VersionService::dependencyInstallCompleted);
    if (m_configuration)
        connect(m_configuration, &ConfigurationManager::currentProfileChanged, this, &VersionService::reloadConfiguration);
    if (m_settings) connect(m_settings, &ApplicationSettings::proxyChanged, this, &VersionService::rebuildTool);
    reloadConfiguration();

    // Until the journals are rolled back, ComfyUI must not start.
    const QList<transaction::Journal> journals = transaction::pending(journalDirectory());
    if (!journals.isEmpty()) {
        QStringList names;
        for (const transaction::Journal &journal : journals) names.append(journal.name);
        m_interrupted = names.join(QStringLiteral("、"));
        QTimer::singleShot(0, this, [this] { recover(true); });
    }
}

VersionService::~VersionService()
{
    m_shutdown = true;
    m_pool.clear();
    m_pool.waitForDone();
}

// ---- State -----------------------------------------------------------------

bool VersionService::busy() const
{
    return updating() || installingDependencies() || coreChecking() || extensionsChecking() || m_catalogLoading;
}

bool VersionService::updating() const
{
    return !m_changing.isEmpty();
}

bool VersionService::installingDependencies() const
{
    return m_dependencies->running();
}

bool VersionService::canCheck() const
{
    return repository() && !coreBusy() && !interruptedOperation();
}

QString VersionService::launcherVersion() const
{
    return QCoreApplication::applicationVersion();
}

bool VersionService::coreBusy() const
{
    return m_changing.contains(m_comfyRoot) || m_changing.contains(QStringLiteral("recovery"));
}

bool VersionService::coreChecking() const
{
    return m_coreChecks > 0;
}

bool VersionService::canUpdate() const
{
    return repository() && !coreBusy() && !interruptedOperation();
}

QVariantList VersionService::stableVersions() const
{
    return commitList(m_core.stable, m_core.info.head);
}

QVariantList VersionService::coreVersions() const
{
    return commitList(m_core.development, m_core.info.head);
}

int VersionService::outdatedCount() const
{
    int count = 0;
    for (const QVariantMap &entry : m_installed->entries()) {
        if (entry.value(QStringLiteral("status")).toString() == QStringLiteral("outdated")) ++count;
    }
    return count;
}

void VersionService::retranslate()
{
    emit stateChanged();
}

// ---- Scheduling --------------------------------------------------------------

void VersionService::submit(Task task)
{
    m_waiting.append(std::move(task));
    schedule();
    emit stateChanged();
}

bool VersionService::startable(const Task &task) const
{
    if (m_runningKeys.contains(task.key)) return false;
    if (!task.changes) return true;
    if (m_exclusiveRunning) return false;
    return !task.exclusive || m_runningChanges == 0;
}

void VersionService::schedule()
{
    QList<Task> refused;
    QString leaseError;
    // A waiting ComfyUI change keeps later changes from overtaking it.
    bool changesHeld = false;
    for (qsizetype index = 0; index < m_waiting.size() && m_runningKeys.size() < kMaxReads;) {
        const Task &candidate = m_waiting.at(index);
        if ((candidate.changes && changesHeld) || !startable(candidate)) {
            if (candidate.changes && candidate.exclusive) changesHeld = true;
            ++index;
            continue;
        }
        if (candidate.changes && !m_lease.held()
            && !m_lease.acquire(OperationLease::repositoryResources(m_comfyRoot, m_python), &leaseError)) {
            for (qsizetype other = m_waiting.size() - 1; other >= 0; --other) {
                if (m_waiting.at(other).changes) refused.prepend(m_waiting.takeAt(other));
            }
            index = 0;
            continue;
        }

        Task task = m_waiting.takeAt(index);
        m_runningKeys.insert(task.key);
        if (task.changes) {
            ++m_runningChanges;
            m_exclusiveRunning = m_exclusiveRunning || task.exclusive;
        }
        m_pool.start([this, tool = m_tool, work = std::move(task.work), key = task.key, changes = task.changes,
                      exclusive = task.exclusive] {
            Completion done = work(tool);
            QMetaObject::invokeMethod(this, [this, done = std::move(done), key, changes, exclusive] {
                m_runningKeys.remove(key);
                if (changes) {
                    --m_runningChanges;
                    if (exclusive) m_exclusiveRunning = false;
                }
                if (done) done();
                schedule();
                releaseLeaseIfIdle();
                emit stateChanged();
            }, Qt::QueuedConnection);
        });
    }
    for (const Task &task : std::as_const(refused)) {
        if (task.refused) task.refused(leaseError);
    }
    if (!refused.isEmpty()) {
        releaseLeaseIfIdle();
        emit stateChanged();
    }
}

void VersionService::releaseLeaseIfIdle()
{
    if (!m_lease.held() || m_runningChanges > 0 || m_dependencies->running()) return;
    for (const Task &task : std::as_const(m_waiting)) {
        if (task.changes) return;
    }
    m_lease.release();
}

// ---- Configuration -------------------------------------------------------------

void VersionService::reloadConfiguration()
{
    const QString root = m_configuration && !m_configuration->comfyRoot().trimmed().isEmpty()
                             ? QDir::cleanPath(QFileInfo(m_configuration->comfyRoot().trimmed()).absoluteFilePath())
                             : QString();
    m_python = m_configuration ? m_configuration->pythonPath().trimmed() : QString();
    if (root == m_comfyRoot && m_tool.isValid()) return;
    m_comfyRoot = root;
    m_core = {};
    m_coreFetched = false;
    m_extensionsFetched = false;
    m_installed->setEntries({});
    rebuildTool();
    checkCore(false, false);
    emit stateChanged();
}

void VersionService::rebuildTool()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (m_settings) m_settings->applyToProcessEnvironment(environment);
    m_environment = environment;
    m_tool = git::Tool(git::Tool::locate(m_comfyRoot), environment);
}

QString VersionService::customNodes() const
{
    return m_comfyRoot.isEmpty() ? QString() : QDir(m_comfyRoot).filePath(QStringLiteral("custom_nodes"));
}

QString VersionService::backupRoot() const
{
    return QDir(QFileInfo(m_comfyRoot).absolutePath()).filePath(QStringLiteral("backup"));
}

QString VersionService::journalDirectory() const
{
    return QDir(PortablePaths::dataDirectory()).filePath(QStringLiteral("versioning"));
}

// ---- Checking ------------------------------------------------------------------

void VersionService::ensureLoaded(int tab)
{
    if (!m_coreFetched && m_coreChecks == 0) checkCore(true, false);
    if (tab >= 1 && !m_extensionsFetched && !m_scanning) checkExtensions(true, false);
    if (tab == 2 && !m_catalogRequested) {
        m_catalogRequested = true;
        loadCatalogCache();
        downloadCatalog(false);
    }
}

void VersionService::refreshCore()
{
    checkCore(true, true);
}

void VersionService::refreshInstalledExtensions()
{
    checkExtensions(true, true);
}

void VersionService::refreshAvailableExtensions()
{
    m_catalogRequested = true;
    if (m_catalogEntries.isEmpty()) loadCatalogCache();
    downloadCatalog(true);
    if (!m_scanning) checkExtensions(false, false);
}

void VersionService::checkCore(bool fetch, bool report)
{
    const QString root = m_comfyRoot;
    if (root.isEmpty() || !QFileInfo::exists(QDir(root).filePath(QStringLiteral(".git")))) {
        m_core = {};
        m_core.error = root.isEmpty() ? tr("还没有设置 ComfyUI 目录。") : tr("ComfyUI 目录不是 Git 仓库，无法管理版本。");
        if (report) emit refreshCompleted(false, m_core.error);
        emit stateChanged();
        return;
    }
    ++m_coreChecks;
    submit({root, false, false, [this, root, fetch, report](const git::Tool &tool) -> Completion {
        QString fetchError;
        if (fetch) {
            const git::Result result = repository::fetch(tool, root, &m_shutdown);
            if (!result.ok()) fetchError = result.networkMessage();
        }
        Core core;
        core.info = repository::inspect(tool, root);
        core.info.changes = withoutPlaceholders(core.info.changes);
        if (core.info.repository) {
            core.stable = repository::releases(tool, root, 60);
            core.developmentTarget = repository::defaultRemoteBranch(tool, root);
            if (!core.developmentTarget.isEmpty())
                core.development = repository::history(tool, root, {core.developmentTarget}, 100);
            core.version = comfyVersionOf(root, core.info);
        }
        return [this, root, fetch, report, core, fetchError] {
            --m_coreChecks;
            if (root != m_comfyRoot) return;
            m_core = core;
            if (!core.info.repository) m_core.error = core.info.error;
            else if (!fetchError.isEmpty()) m_core.error = tr("检查更新失败：%1").arg(fetchError);
            if (fetch && fetchError.isEmpty()) m_coreFetched = true;
            if (report) {
                const bool ok = core.info.repository && fetchError.isEmpty();
                emit refreshCompleted(ok, ok ? tr("ComfyUI 版本列表已刷新。") : m_core.error);
            }
        };
    }, {}});
}

void VersionService::checkExtensions(bool fetch, bool report)
{
    const QString nodes = customNodes();
    if (nodes.isEmpty() || m_scanning) return;
    m_scanning = true;
    submit({nodes, false, false, [this, nodes, fetch, report](const git::Tool &tool) -> Completion {
        const QList<Extension> found = extensions::scan(tool, nodes);
        return [this, nodes, fetch, report, found] {
            m_scanning = false;
            if (nodes != customNodes()) return;
            publishExtensions(found);
            if (!fetch) return;
            m_extensionsFetched = true;
            int checks = 0;
            for (const Extension &extension : found) {
                if (!extension.info.repository || m_changing.contains(extension.path)) continue;
                checkExtension(extension.path, true);
                ++checks;
            }
            if (!report) return;
            if (checks == 0) emit refreshCompleted(true, tr("扩展列表已刷新。"));
            else m_reportExtensions = true;
        };
    }, {}});
}

void VersionService::reportExtensionsIfDone()
{
    if (!m_reportExtensions || m_extensionChecks > 0) return;
    m_reportExtensions = false;
    int errors = 0;
    for (const QVariantMap &entry : m_installed->entries())
        errors += entry.value(QStringLiteral("status")).toString() == QStringLiteral("error");
    const int outdated = outdatedCount();
    if (errors > 0)
        emit refreshCompleted(false, tr("%n 个扩展检查更新失败，鼠标移到“重试”上可以看到原因。", nullptr, errors));
    else if (outdated > 0)
        emit refreshCompleted(true, tr("扩展检查完成，%n 个可以更新。", nullptr, outdated));
    else
        emit refreshCompleted(true, tr("扩展检查完成，全部是最新版本。"));
}

void VersionService::checkExtension(const QString &path, bool fetch)
{
    ++m_extensionChecks;
    setEntryStatus(path, QStringLiteral("checking"));
    submit({path, false, false, [this, path, fetch](const git::Tool &tool) -> Completion {
        QString error;
        if (fetch) {
            const git::Result result = repository::fetch(tool, path, &m_shutdown);
            if (!result.ok()) error = result.networkMessage();
        }
        const Extension extension = extensions::inspect(tool, path);
        const QString target = extension.info.repository ? repository::updateTarget(tool, path, extension.info) : QString();
        const int behind = target.isEmpty() ? -1 : repository::behind(tool, path, target);
        return [this, path, extension, target, behind, error] {
            --m_extensionChecks;
            QVariantMap entry = extensionEntry(extension);
            entry.insert(QStringLiteral("target"), target);
            entry.insert(QStringLiteral("behind"), behind);
            entry.insert(QStringLiteral("error"), error);
            if (m_changing.contains(path)) entry.insert(QStringLiteral("status"), QStringLiteral("updating"));
            else if (!error.isEmpty()) entry.insert(QStringLiteral("status"), QStringLiteral("error"));
            else if (!extension.info.repository || target.isEmpty()) entry.insert(QStringLiteral("status"), QStringLiteral("unknown"));
            else entry.insert(QStringLiteral("status"), behind > 0 ? QStringLiteral("outdated") : QStringLiteral("latest"));
            m_installed->update(path, entry);
            reportExtensionsIfDone();
        };
    }, {}});
}

QVariantMap VersionService::extensionEntry(const Extension &extension) const
{
    const RepositoryInfo &info = extension.info;
    QString status = QStringLiteral("unknown");
    if (m_changing.contains(extension.path)) status = QStringLiteral("updating");
    else if (info.repository && !info.upstream.isEmpty()) status = info.behind > 0 ? QStringLiteral("outdated") : QStringLiteral("latest");
    return {
        {QStringLiteral("path"), extension.path},
        {QStringLiteral("name"), extension.name},
        {QStringLiteral("enabled"), extension.enabled},
        {QStringLiteral("repository"), info.repository},
        {QStringLiteral("remote"), info.remoteUrl},
        {QStringLiteral("branch"), info.branch},
        {QStringLiteral("commit"), info.shortHead},
        {QStringLiteral("fullCommit"), info.head},
        {QStringLiteral("date"), info.date},
        {QStringLiteral("subject"), info.subject},
        {QStringLiteral("changes"), int(info.changes.size())},
        {QStringLiteral("status"), status},
        {QStringLiteral("error"), info.repository ? QString() : info.error},
    };
}

void VersionService::publishExtensions(const QList<Extension> &extensions)
{
    QList<QVariantMap> entries;
    for (const Extension &extension : extensions) {
        QVariantMap entry = extensionEntry(extension);
        // Keep what the last fetch found until the next one replaces it.
        const QVariantMap previous = m_installed->entry(extension.path);
        if (!previous.isEmpty() && previous.value(QStringLiteral("fullCommit")) == entry.value(QStringLiteral("fullCommit"))) {
            for (const char *key : {"status", "target", "behind", "error"}) {
                const QString name = QString::fromLatin1(key);
                if (previous.contains(name) && !m_changing.contains(extension.path)) entry.insert(name, previous.value(name));
            }
        }
        entries.append(entry);
    }
    m_installed->setEntries(entries);
    markInstalledInCatalog();
    emit stateChanged();
}

void VersionService::setEntryStatus(const QString &path, const QString &status, const QString &error)
{
    m_installed->update(path, {{QStringLiteral("status"), status}, {QStringLiteral("error"), error}});
}

// ---- Catalog -------------------------------------------------------------------

void VersionService::loadCatalogCache()
{
    submit({QStringLiteral("catalog-cache"), false, false, [this](const git::Tool &) -> Completion {
        QFile file(catalogCache());
        QList<CatalogEntry> entries;
        QString error;
        if (file.open(QIODevice::ReadOnly) && file.size() <= kCatalogLimit) entries = extensions::parseCatalog(file.readAll(), &error);
        return [this, entries] {
            // A download that finished first is newer.
            if (!m_catalogEntries.isEmpty() || entries.isEmpty()) return;
            m_catalogEntries = entries;
            markInstalledInCatalog();
        };
    }, {}});
}

void VersionService::downloadCatalog(bool report)
{
    if (m_catalogReply) return;
    QNetworkRequest request(kCatalogUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Minifox-ComfyUI-Launcher/") + QCoreApplication::applicationVersion());
    request.setTransferTimeout(30000);
    QFile tag(catalogTag());
    if (!m_catalogEntries.isEmpty() && QFileInfo::exists(catalogCache()) && tag.open(QIODevice::ReadOnly))
        request.setRawHeader("If-None-Match", tag.readAll().trimmed());
    m_catalogLoading = true;
    m_catalogError.clear();
    m_catalogReply = m_network.get(request);
    connect(m_catalogReply, &QNetworkReply::downloadProgress, this, [reply = m_catalogReply](qint64 received, qint64) {
        if (received > kCatalogLimit && reply) reply->abort();
    });
    connect(m_catalogReply, &QNetworkReply::finished, this, [this, reply = m_catalogReply, report] {
        handleCatalogReply(reply, report);
    });
    emit stateChanged();
}

void VersionService::handleCatalogReply(QNetworkReply *reply, bool report)
{
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto finish = [this, report](bool ok, const QString &message) {
        m_catalogLoading = false;
        if (!ok) m_catalogError = message;
        if (report) emit refreshCompleted(ok, message);
        emit stateChanged();
    };
    if (status == 304) {
        finish(true, tr("插件列表已是最新。"));
        return;
    }
    if (reply->error() != QNetworkReply::NoError || status != 200) {
        const QString detail = reply->error() == QNetworkReply::OperationCanceledError
                                   ? tr("插件列表过大或下载超时。")
                                   : reply->errorString();
        finish(false, m_catalogEntries.isEmpty() ? tr("无法下载插件列表：%1").arg(detail)
                                                 : tr("无法更新插件列表，显示的是上次下载的版本：%1").arg(detail));
        return;
    }
    const QByteArray body = reply->readAll();
    const QByteArray etag = reply->rawHeader("ETag");
    submit({QStringLiteral("catalog-cache"), false, false, [this, body, etag, finish](const git::Tool &) -> Completion {
        QString error;
        const QList<CatalogEntry> entries = extensions::parseCatalog(body, &error);
        if (!entries.isEmpty() && PortablePaths::ensureDataDirectory()) {
            QSaveFile cache(catalogCache());
            if (cache.open(QIODevice::WriteOnly) && cache.write(body) == body.size()) cache.commit();
            QSaveFile tag(catalogTag());
            if (!etag.isEmpty() && tag.open(QIODevice::WriteOnly) && tag.write(etag) == etag.size()) tag.commit();
        }
        return [this, entries, error, finish] {
            if (entries.isEmpty()) {
                finish(false, tr("插件列表无法使用：%1").arg(error));
                return;
            }
            m_catalogEntries = entries;
            markInstalledInCatalog();
            finish(true, tr("插件列表已更新，共 %n 个。", nullptr, int(entries.size())));
        };
    }, {}});
}

void VersionService::markInstalledInCatalog()
{
    if (m_catalogEntries.isEmpty()) return;
    QSet<QString> remotes;
    QSet<QString> folders;
    for (const QVariantMap &entry : m_installed->entries()) {
        const QString remote = entry.value(QStringLiteral("remote")).toString();
        if (!remote.isEmpty()) remotes.insert(extensions::canonicalUrl(remote));
        folders.insert(entry.value(QStringLiteral("name")).toString().toLower());
    }
    QList<QVariantMap> rows;
    rows.reserve(m_catalogEntries.size());
    for (const CatalogEntry &catalog : std::as_const(m_catalogEntries)) {
        const QString key = extensions::canonicalUrl(catalog.url);
        const QVariantMap previous = m_catalog->entry(key);
        rows.append({
            {QStringLiteral("key"), key},
            {QStringLiteral("name"), catalog.title},
            {QStringLiteral("author"), catalog.author},
            {QStringLiteral("description"), catalog.description},
            {QStringLiteral("remote"), catalog.url},
            {QStringLiteral("installed"), remotes.contains(key) || folders.contains(extensions::folderFor(catalog.url).toLower())},
            {QStringLiteral("installing"), m_changing.contains(QDir(customNodes()).filePath(extensions::folderFor(catalog.url)))},
        });
    }
    m_catalog->setEntries(rows);
}

// ---- Changes -------------------------------------------------------------------

void VersionService::changeRepository(Change change)
{
    const bool core = change.root == m_comfyRoot;
    if (m_changing.contains(change.root)) return;
    m_changing.insert(change.root);
    if (!core) setEntryStatus(change.root, QStringLiteral("updating"));

    transaction::Request base;
    base.root = change.root;
    base.name = change.name;
    base.keepLocal = change.keepLocal;
    base.backupRoot = backupRoot();
    base.journalDirectory = journalDirectory();

    Task task;
    task.key = change.root;
    task.changes = true;
    task.exclusive = change.exclusive || core;
    task.work = [this, change, base](const git::Tool &tool) -> Completion {
        transaction::Request request = base;
        QString error = change.prepare ? change.prepare(tool, request) : QString();
        transaction::Outcome outcome;
        bool requirementsChanged = false;
        if (error.isEmpty()) {
            const QString requirements = QStringLiteral("requirements.txt");
            const QString before = repository::blob(tool, base.root, QStringLiteral("HEAD"), requirements);
            outcome = change.clean ? transaction::clean(tool, request) : transaction::apply(tool, request, &m_shutdown);
            if (!outcome.ok) error = outcome.error;
            const QString after = repository::blob(tool, base.root, QStringLiteral("HEAD"), requirements);
            requirementsChanged = !after.isEmpty() && after != before;
            if (!outcome.backupDirectory.isEmpty()) transaction::pruneBackups(base.backupRoot, kKeptBackupDays);
        }
        return [this, change, outcome, error, requirementsChanged] {
            finishChange(change, outcome, error, requirementsChanged);
        };
    };
    task.refused = [this, change](const QString &error) { finishChange(change, {}, error, false); };
    submit(std::move(task));
}

void VersionService::finishChange(const Change &change, const transaction::Outcome &outcome, const QString &error,
                                  bool requirementsChanged)
{
    m_changing.remove(change.root);
    if (!error.isEmpty()) {
        emit operationCompleted(false, tr("%1：%2").arg(change.name, error));
    } else {
        const bool unchanged = outcome.oldHead == outcome.newHead && change.keepLocal && !change.clean;
        emit operationCompleted(true, unchanged ? tr("%1 已经是这个版本。").arg(change.name) : change.success);
        reportOutcome(change.name, outcome.backupDirectory, outcome.conflicts, outcome.moved, !change.keepLocal);
        if (requirementsChanged) m_dependencies->enqueue(change.root, m_python, m_environment);
    }
    if (change.root == m_comfyRoot) checkCore(false, false);
    else checkExtension(change.root, false);
}

void VersionService::changeFolder(const QString &key, const QString &name, std::function<QString()> work,
                                  const QString &success, const QString &dependencies)
{
    if (m_changing.contains(key)) return;
    m_changing.insert(key);
    const auto finish = [this, key, name, success, dependencies](const QString &error) {
        m_changing.remove(key);
        if (error.isEmpty()) {
            emit operationCompleted(true, success);
            if (!dependencies.isEmpty()) m_dependencies->enqueue(dependencies, m_python, m_environment);
        } else {
            emit operationCompleted(false, tr("%1：%2").arg(name, error));
        }
        checkExtensions(false, false);
        markInstalledInCatalog();
    };
    Task task;
    task.key = key;
    task.changes = true;
    task.work = [work, finish](const git::Tool &) -> Completion {
        const QString error = work();
        return [finish, error] { finish(error); };
    };
    task.refused = finish;
    submit(std::move(task));
    markInstalledInCatalog();
}

// ---- ComfyUI -------------------------------------------------------------------

void VersionService::updateComfyUi(int channel)
{
    if (!canUpdate()) return;
    const QString root = m_comfyRoot;
    changeRepository({root, QStringLiteral("ComfyUI"),
                      channel == 0 ? tr("ComfyUI 已更新到最新稳定版。") : tr("ComfyUI 已更新到最新开发版。"), true, true,
                      false, [this, root, channel](const git::Tool &tool, transaction::Request &request) -> QString {
        const git::Result fetched = repository::fetch(tool, root, &m_shutdown);
        if (!fetched.ok()) return tr("检查更新失败：%1").arg(fetched.networkMessage());
        const RepositoryInfo info = repository::inspect(tool, root);
        const QString remoteBranch = repository::defaultRemoteBranch(tool, root);
        if (channel == 0) {
            const QList<Commit> releases = repository::releases(tool, root, 1);
            if (releases.isEmpty()) return tr("远端没有找到稳定版。");
            request.target = releases.constFirst().id;
        } else {
            request.target = repository::resolveCommit(tool, root, repository::updateTarget(tool, root, info));
        }
        if (request.target.isEmpty()) return tr("远端没有找到可以更新的版本。");
        request.branch = ownBranch(info).isEmpty() ? remoteBranchName(remoteBranch) : ownBranch(info);
        if (request.branch.isEmpty()) request.branch = QStringLiteral("master");
        if (info.upstream.isEmpty() && !remoteBranch.isEmpty()) request.upstream = QStringLiteral("origin/") + request.branch;
        return {};
    }});
}

void VersionService::switchCoreVersion(const QString &commit)
{
    if (!canUpdate()) return;
    const QString root = m_comfyRoot;
    changeRepository({root, QStringLiteral("ComfyUI"), tr("ComfyUI 已切换到 %1。").arg(commit.left(7)), true, true, false,
                      [root, commit](const git::Tool &tool, transaction::Request &request) -> QString {
        const RepositoryInfo info = repository::inspect(tool, root);
        request.target = repository::resolveCommit(tool, root, commit);
        if (request.target.isEmpty()) return tr("找不到这个版本，请先刷新列表。");
        request.branch = ownBranch(info).isEmpty() ? QStringLiteral("master") : ownBranch(info);
        return {};
    }});
}

void VersionService::switchBranch(int repositorySource)
{
    if (!canUpdate()) return;
    const QString root = m_comfyRoot;
    const QString url = repositorySource == 1 ? kMirrorCore : kOfficialCore;
    changeRepository({root, QStringLiteral("ComfyUI"),
                      repositorySource == 1 ? tr("已切换到国内镜像的 master 分支。") : tr("已切换到官方仓库的 master 分支。"),
                      true, true, false, [this, root, url](const git::Tool &tool, transaction::Request &request) -> QString {
        const git::Result current = tool.run(root, {QStringLiteral("config"), QStringLiteral("--get"),
                                                    QStringLiteral("remote.origin.url")});
        const QString previous = current.ok() ? current.text().trimmed() : QString();
        const QStringList setUrl = previous.isEmpty()
                                       ? QStringList{QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), url}
                                       : QStringList{QStringLiteral("remote"), QStringLiteral("set-url"), QStringLiteral("origin"), url};
        const git::Result changed = tool.run(root, setUrl);
        if (!changed.ok()) return changed.message();
        const git::Result fetched = repository::fetch(tool, root, &m_shutdown);
        request.target = fetched.ok() ? repository::resolveCommit(tool, root, QStringLiteral("refs/remotes/origin/master")) : QString();
        if (request.target.isEmpty()) {
            if (previous.isEmpty())
                tool.run(root, {QStringLiteral("remote"), QStringLiteral("remove"), QStringLiteral("origin")});
            else
                tool.run(root, {QStringLiteral("remote"), QStringLiteral("set-url"), QStringLiteral("origin"), previous});
            return fetched.ok() ? tr("这个仓库没有 master 分支。") : tr("无法连接这个仓库：%1").arg(fetched.networkMessage());
        }
        request.branch = QStringLiteral("master");
        request.upstream = QStringLiteral("origin/master");
        return {};
    }});
}

void VersionService::discardCoreChanges()
{
    if (!canUpdate()) return;
    const QString root = m_comfyRoot;
    changeRepository({root, QStringLiteral("ComfyUI"), tr("已放弃 ComfyUI 的本地修改。"), true, false, false,
                      [root](const git::Tool &tool, transaction::Request &request) -> QString {
        const RepositoryInfo info = repository::inspect(tool, root);
        request.target = info.head;
        request.branch = ownBranch(info);
        return info.repository ? QString() : info.error;
    }});
}

void VersionService::cleanComfyUiRepository()
{
    if (!canCheck()) return;
    const QString root = m_comfyRoot;
    changeRepository({root, QStringLiteral("ComfyUI"), tr("ComfyUI 已完全清理，移走的文件都在备份文件夹里。"), true, false, true,
                      [root](const git::Tool &tool, transaction::Request &request) -> QString {
        const RepositoryInfo info = repository::inspect(tool, root);
        request.target = info.head;
        request.branch = ownBranch(info);
        return info.repository ? QString() : info.error;
    }});
}

// ---- Extensions ----------------------------------------------------------------

void VersionService::updateExtension(const QString &path)
{
    const QVariantMap entry = m_installed->entry(path);
    if (entry.isEmpty() || !entry.value(QStringLiteral("repository")).toBool() || interruptedOperation()) return;
    const bool fetched = m_extensionsFetched;
    changeRepository({path, entry.value(QStringLiteral("name")).toString(),
                      tr("%1 已更新。").arg(entry.value(QStringLiteral("name")).toString()), false, true, false,
                      [this, path, fetched](const git::Tool &tool, transaction::Request &request) -> QString {
        if (!fetched) {
            const git::Result result = repository::fetch(tool, path, &m_shutdown);
            if (!result.ok()) return tr("检查更新失败：%1").arg(result.networkMessage());
        }
        const RepositoryInfo info = repository::inspect(tool, path);
        const QString target = repository::updateTarget(tool, path, info);
        request.target = repository::resolveCommit(tool, path, target);
        if (request.target.isEmpty()) return tr("远端没有可以更新的分支。");
        request.branch = ownBranch(info);
        if (request.branch.isEmpty()) {
            request.branch = remoteBranchName(target);
            request.upstream = target;
        } else if (info.upstream.isEmpty()) {
            request.upstream = target;
        }
        return {};
    }});
}

void VersionService::checkExtensionNow(const QString &path)
{
    if (!m_installed->entry(path).isEmpty() && !m_changing.contains(path)) checkExtension(path, true);
}

void VersionService::updateAllExtensions()
{
    for (const QVariantMap &entry : m_installed->entries()) {
        if (entry.value(QStringLiteral("status")).toString() == QStringLiteral("outdated"))
            updateExtension(entry.value(QStringLiteral("path")).toString());
    }
}

void VersionService::loadExtensionVersions(const QString &path)
{
    m_versionsPath = path;
    m_extensionVersions.clear();
    m_extensionVersionsError.clear();
    m_extensionVersionsLoading = true;
    emit extensionVersionsChanged();
    submit({path, false, false, [this, path](const git::Tool &tool) -> Completion {
        const RepositoryInfo info = repository::inspect(tool, path);
        const QString target = info.repository ? repository::updateTarget(tool, path, info) : QString();
        QStringList range{QStringLiteral("HEAD")};
        if (!target.isEmpty()) range.append(target);
        const QList<Commit> commits = info.repository ? repository::history(tool, path, range, 200) : QList<Commit>();
        return [path, info, commits, this] {
            if (path != m_versionsPath) return;
            m_extensionVersionsLoading = false;
            m_extensionVersions = commitList(commits, info.head);
            if (!info.repository) m_extensionVersionsError = info.error;
            else if (commits.isEmpty()) m_extensionVersionsError = tr("没有可显示的版本历史。");
            emit extensionVersionsChanged();
        };
    }, {}});
}

void VersionService::switchExtensionVersion(const QString &path, const QString &commit)
{
    const QVariantMap entry = m_installed->entry(path);
    if (entry.isEmpty() || interruptedOperation()) return;
    const QString name = entry.value(QStringLiteral("name")).toString();
    changeRepository({path, name, tr("%1 已切换到 %2。").arg(name, commit.left(7)), false, true, false,
                      [path, commit](const git::Tool &tool, transaction::Request &request) -> QString {
        const RepositoryInfo info = repository::inspect(tool, path);
        request.target = repository::resolveCommit(tool, path, commit);
        request.branch = ownBranch(info);
        return request.target.isEmpty() ? tr("找不到这个版本。") : QString();
    }});
}

void VersionService::discardExtensionChanges(const QString &path)
{
    const QVariantMap entry = m_installed->entry(path);
    if (entry.isEmpty() || interruptedOperation()) return;
    const QString name = entry.value(QStringLiteral("name")).toString();
    changeRepository({path, name, tr("已放弃 %1 的本地修改。").arg(name), false, false, false,
                      [path](const git::Tool &tool, transaction::Request &request) -> QString {
        const RepositoryInfo info = repository::inspect(tool, path);
        request.target = info.head;
        request.branch = ownBranch(info);
        return info.repository ? QString() : info.error;
    }});
}

void VersionService::installExtension(const QString &url)
{
    const QString folder = extensions::folderFor(url);
    if (folder.isEmpty() || customNodes().isEmpty()) {
        emit operationCompleted(false, tr("扩展地址无效：%1").arg(url.trimmed()));
        return;
    }
    const QString destination = QDir(customNodes()).filePath(folder);
    const QString key = extensions::canonicalUrl(url);
    for (const QVariantMap &entry : m_installed->entries()) {
        if (extensions::canonicalUrl(entry.value(QStringLiteral("remote")).toString()) == key
            || entry.value(QStringLiteral("name")).toString().compare(folder, Qt::CaseInsensitive) == 0) {
            emit operationCompleted(false, tr("%1 已经安装过了。").arg(folder));
            return;
        }
    }
    if (QFileInfo::exists(destination) || QFileInfo::exists(destination + extensions::kDisabledSuffix)) {
        emit operationCompleted(false, tr("custom_nodes 里已经有名为 %1 的文件夹。").arg(folder));
        return;
    }
    const git::Tool tool = m_tool;
    const QString source = url.trimmed();
    changeFolder(destination, folder, [this, tool, source, destination]() -> QString {
        QDir().mkpath(QFileInfo(destination).absolutePath());
        const git::Result result = extensions::clone(tool, source, destination, &m_shutdown);
        if (result.ok()) return {};
        // A failed clone may leave a partial folder behind.
        QDir(destination).removeRecursively();
        return result.networkMessage();
    }, tr("已安装 %1，重启 ComfyUI 后生效。").arg(folder), destination);
}

void VersionService::removeExtension(const QString &path)
{
    const QVariantMap entry = m_installed->entry(path);
    if (entry.isEmpty()) return;
    const QString name = entry.value(QStringLiteral("name")).toString();
    changeFolder(path, name, [path]() -> QString {
        if (QFile::moveToTrash(path) || QDir(path).removeRecursively()) return {};
        return tr("无法删除文件夹，可能有文件正被其他程序使用。");
    }, tr("已卸载 %1，文件夹已移到回收站。").arg(name));
}

void VersionService::setExtensionEnabled(const QString &path, bool enabled)
{
    const QVariantMap entry = m_installed->entry(path);
    if (entry.isEmpty() || entry.value(QStringLiteral("enabled")).toBool() == enabled) return;
    const QString name = entry.value(QStringLiteral("name")).toString();
    const QString renamed = QDir(QFileInfo(path).absolutePath()).filePath(enabled ? name : name + extensions::kDisabledSuffix);
    changeFolder(path, name, [path, renamed]() -> QString {
        if (QFileInfo::exists(renamed)) return tr("已经有同名的文件夹 %1。").arg(QFileInfo(renamed).fileName());
        return QDir().rename(path, renamed) ? QString() : tr("无法重命名文件夹，可能有文件正被其他程序使用。");
    }, enabled ? tr("已启用 %1，重启 ComfyUI 后生效。").arg(name) : tr("已停用 %1，重启 ComfyUI 后生效。").arg(name));
}

// ---- Recovery and dependencies -------------------------------------------------

void VersionService::recoverInterruptedOperations()
{
    recover(false);
}

void VersionService::recover(bool automatic)
{
    const QString directory = journalDirectory();
    const QString key = QStringLiteral("recovery");
    if (m_changing.contains(key)) return;
    m_changing.insert(key);
    Task task;
    task.key = key;
    task.changes = true;
    task.exclusive = true;
    task.work = [this, directory, automatic, key](const git::Tool &tool) -> Completion {
        QStringList restored;
        QStringList failed;
        for (const transaction::Journal &journal : transaction::pending(directory)) {
            QString error;
            if (transaction::rollback(tool, journal, &error)) restored.append(journal.name);
            else failed.append(tr("%1（%2）").arg(journal.name, error));
        }
        return [this, restored, failed, automatic, key] {
            m_changing.remove(key);
            if (!failed.isEmpty()) {
                m_interrupted = failed.join(QStringLiteral("、"));
                emit operationCompleted(false, tr("无法撤销上次没有完成的版本操作：%1").arg(m_interrupted));
            } else {
                m_interrupted.clear();
                if (!restored.isEmpty())
                    emit operationCompleted(true, tr("上次没有完成的版本操作（%1）已撤销，文件已恢复到操作之前。")
                                                      .arg(restored.join(QStringLiteral("、"))));
                else if (!automatic)
                    emit operationCompleted(true, tr("没有需要恢复的版本操作。"));
            }
            checkCore(false, false);
            if (!m_installed->entries().isEmpty()) checkExtensions(false, false);
        };
    };
    task.refused = [this, key](const QString &error) {
        m_changing.remove(key);
        emit operationCompleted(false, tr("暂时无法撤销上次没有完成的版本操作：%1").arg(error));
    };
    submit(std::move(task));
}

void VersionService::cancelDependencyInstall()
{
    m_dependencies->cancel();
}

void VersionService::reportOutcome(const QString &name, const QString &backup, const QStringList &conflicts,
                                   const QStringList &moved, bool discarded)
{
    if (backup.isEmpty() || (conflicts.isEmpty() && moved.isEmpty())) return;
    const QString folder = QDir::toNativeSeparators(backup);
    QStringList parts;
    if (discarded && !conflicts.isEmpty())
        parts << tr("已放弃 %1 中 %n 个文件的本地修改。", nullptr, int(conflicts.size())).arg(name);
    else if (!conflicts.isEmpty())
        parts << tr("%1 中有 %n 个文件的本地修改与新版本冲突，冲突的地方已采用新版本。", nullptr, int(conflicts.size())).arg(name);
    if (!moved.isEmpty())
        parts << tr("新版本带来了 %n 个与本地文件同名的文件，原来的文件已移走。", nullptr, int(moved.size()));
    parts << tr("原来的文件都备份在：%1").arg(folder);
    emit mergeConflictsReported(parts.join(QLatin1Char('\n')));
}
