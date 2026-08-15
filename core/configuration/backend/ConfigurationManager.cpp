#include "ConfigurationManager.h"

#include "LaunchParameterCatalog.h"
#include "PortablePaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextBoundaryFinder>
#include <QUrl>
#include <QUuid>

namespace {

int graphemeCount(const QString &text)
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    finder.toStart();
    int count = 0;
    while (finder.toNextBoundary() >= 0) {
        ++count;
    }
    return count;
}

QString leftGraphemes(const QString &text, int maximumCharacters)
{
    if (text.isEmpty() || maximumCharacters <= 0) {
        return {};
    }

    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    finder.toStart();
    int boundary = 0;
    for (int count = 0; count < maximumCharacters; ++count) {
        const int next = finder.toNextBoundary();
        if (next < 0) {
            return text;
        }
        boundary = next;
    }
    return finder.toNextBoundary() < 0 ? text : text.left(boundary);
}

QString existingBundledPython(const QString &comfyRoot)
{
    QStringList candidates;
    if (!comfyRoot.isEmpty()) {
        const QDir comfyDirectory(comfyRoot);
        candidates.append({
            comfyDirectory.filePath(QStringLiteral(".venv/Scripts/python.exe")),
            comfyDirectory.filePath(QStringLiteral("../python/python.exe")),
            comfyDirectory.filePath(QStringLiteral("../python_embeded/python.exe"))
        });
    }

    const QDir applicationDirectory(QCoreApplication::applicationDirPath());
    candidates.append({
        applicationDirectory.filePath(QStringLiteral("python/python.exe")),
        applicationDirectory.filePath(QStringLiteral("python_embeded/python.exe"))
    });

    for (const QString &candidate : candidates) {
        const QFileInfo file(candidate);
        if (file.exists() && file.isFile()) {
            return QDir::cleanPath(file.absoluteFilePath());
        }
    }
    return {};
}

bool sameExecutable(const QString &left, const QString &right)
{
    if (left.isEmpty() || right.isEmpty()) {
        return false;
    }
    const QFileInfo leftFile(left);
    const QFileInfo rightFile(right);
    const QString leftPath = leftFile.canonicalFilePath().isEmpty()
        ? QDir::cleanPath(leftFile.absoluteFilePath())
        : leftFile.canonicalFilePath();
    const QString rightPath = rightFile.canonicalFilePath().isEmpty()
        ? QDir::cleanPath(rightFile.absoluteFilePath())
        : rightFile.canonicalFilePath();
#ifdef Q_OS_WIN
    return leftPath.compare(rightPath, Qt::CaseInsensitive) == 0;
#else
    return leftPath == rightPath;
#endif
}

bool shouldAdoptBundledPython(const QString &currentPython, const QString &bundledPython)
{
    if (bundledPython.isEmpty()) {
        return false;
    }
    const QFileInfo currentFile(currentPython);
    return currentPython.isEmpty()
        || !currentFile.exists()
        || sameExecutable(currentPython, QStandardPaths::findExecutable(QStringLiteral("python.exe")));
}

} // namespace

ConfigurationManager::ConfigurationManager(const QString &storagePath, QObject *parent)
    : QObject(parent),
      m_storagePath(storagePath.isEmpty() ? PortablePaths::configurationFile() : storagePath)
{
    load();
    validate();
}

QString ConfigurationManager::limitedProfileName(const QString &name, int maximumCharacters)
{
    const QString trimmed = name.trimmed();
    return leftGraphemes(trimmed, maximumCharacters);
}

QString ConfigurationManager::profileNameWithSuffix(const QString &baseName,
                                                    const QString &suffix)
{
    const QString limitedSuffix = leftGraphemes(suffix, MaximumProfileNameCharacters);
    const int baseLimit = qMax(0, MaximumProfileNameCharacters
                                    - graphemeCount(limitedSuffix));
    return leftGraphemes(baseName.trimmed(), baseLimit) + limitedSuffix;
}

QStringList ConfigurationManager::profileNames() const
{
    QStringList names;
    names.reserve(m_profiles.size());
    for (const auto &profile : m_profiles) {
        names.append(profile.name);
    }
    return names;
}

QVariantList ConfigurationManager::profileEntries() const
{
    QVariantList entries;
    entries.reserve(m_profiles.size());
    for (int index = 0; index < m_profiles.size(); ++index) {
        const auto &profile = m_profiles.at(index);
        entries.append(QVariantMap{
            {QStringLiteral("id"), profile.id},
            {QStringLiteral("name"), profile.name},
            {QStringLiteral("current"), index == m_currentProfileIndex}
        });
    }
    return entries;
}

int ConfigurationManager::currentProfileIndex() const
{
    return m_currentProfileIndex;
}

void ConfigurationManager::setCurrentProfileIndex(int index)
{
    if (index < 0 || index >= m_profiles.size() || index == m_currentProfileIndex) {
        return;
    }
    m_currentProfileIndex = index;
    ++m_parameterRevision;
    save();
    validate();
    emit currentProfileChanged();
    emit parameterRevisionChanged();
}

QString ConfigurationManager::currentProfileName() const
{
    return currentProfile().name;
}

void ConfigurationManager::setCurrentProfileName(const QString &name)
{
    const QString limitedName = limitedProfileName(name);
    if (limitedName.isEmpty() || limitedName == currentProfile().name) {
        return;
    }
    currentProfile().name = limitedName;
    save();
    emit profilesChanged();
    emit currentProfileChanged();
}

QString ConfigurationManager::pythonPath() const
{
    return currentProfile().pythonPath;
}

void ConfigurationManager::setPythonPath(const QString &path)
{
    const QString normalized = normalizePath(path);
    if (normalized == currentProfile().pythonPath) {
        return;
    }
    currentProfile().pythonPath = normalized;
    updateAfterEdit();
}

QString ConfigurationManager::comfyRoot() const
{
    return currentProfile().comfyRoot;
}

void ConfigurationManager::setComfyRoot(const QString &path)
{
    const QString normalized = normalizePath(path);
    if (normalized == currentProfile().comfyRoot) {
        return;
    }
    currentProfile().comfyRoot = normalized;
    const QString bundledPython = existingBundledPython(normalized);
    if (shouldAdoptBundledPython(currentProfile().pythonPath, bundledPython)) {
        currentProfile().pythonPath = bundledPython;
    }
    updateAfterEdit();
}

QString ConfigurationManager::customArguments() const
{
    return currentProfile().customArguments;
}

void ConfigurationManager::setCustomArguments(const QString &arguments)
{
    if (arguments == currentProfile().customArguments) {
        return;
    }
    currentProfile().customArguments = arguments;
    updateAfterEdit();
}

QVariantList ConfigurationManager::environmentEntries() const
{
    const auto &environment = currentProfile().environment;
    const QStringList errors = environmentEntryErrors(currentProfile());
    QVariantList entries;
    entries.reserve(environment.size());
    for (qsizetype index = 0; index < environment.size(); ++index) {
        const auto &entry = environment.at(index);
        entries.append(QVariantMap{
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("value"), entry.value},
            {QStringLiteral("enabled"), entry.enabled},
            {QStringLiteral("error"), errors.at(index)}
        });
    }
    return entries;
}

QVariantList ConfigurationManager::categories() const
{
    return LaunchParameterCatalog::categories();
}

int ConfigurationManager::parameterRevision() const
{
    return m_parameterRevision;
}

bool ConfigurationManager::isValid() const
{
    return m_validationErrors.isEmpty();
}

QStringList ConfigurationManager::validationErrors() const
{
    return m_validationErrors;
}

QString ConfigurationManager::lastError() const
{
    return m_lastError;
}

void ConfigurationManager::retranslate()
{
    validate();
    emit currentProfileChanged();
    emit parameterRevisionChanged();
}

QVariantList ConfigurationManager::parametersForCategory(const QString &category) const
{
    QVariantList result;
    for (const auto &definition : LaunchParameterCatalog::parameters()) {
        if (definition.category == category) {
            result.append(definition.toVariant(parameterValue(definition.key)));
        }
    }
    return result;
}

QVariant ConfigurationManager::parameterValue(const QString &key) const
{
    const auto &values = currentProfile().parameters;
    if (values.contains(key)) {
        return values.value(key);
    }
    const auto *definition = LaunchParameterCatalog::find(key);
    return definition ? definition->defaultValue : QVariant{};
}

void ConfigurationManager::setParameterValue(const QString &key, const QVariant &value)
{
    const auto *definition = LaunchParameterCatalog::find(key);
    if (!definition || parameterValue(key) == value) {
        return;
    }

    if (value == definition->defaultValue) {
        currentProfile().parameters.remove(key);
    } else {
        currentProfile().parameters.insert(key, value);
    }
    updateAfterEdit(true);
}

void ConfigurationManager::addProfile(const QString &name)
{
    const QString profileName = limitedProfileName(name.trimmed().isEmpty()
        ? tr("新配置 %1").arg(m_profiles.size() + 1)
        : name);
    m_profiles.append(makeDefaultProfile(profileName));
    m_currentProfileIndex = m_profiles.size() - 1;
    ++m_parameterRevision;
    save();
    validate();
    emit profilesChanged();
    emit currentProfileChanged();
    emit parameterRevisionChanged();
}

void ConfigurationManager::duplicateCurrentProfile()
{
    Profile copy = currentProfile();
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name = profileNameWithSuffix(copy.name, tr(" - 副本"));
    m_profiles.append(copy);
    m_currentProfileIndex = m_profiles.size() - 1;
    ++m_parameterRevision;
    save();
    validate();
    emit profilesChanged();
    emit currentProfileChanged();
    emit parameterRevisionChanged();
}

bool ConfigurationManager::removeProfiles(const QStringList &profileIds)
{
    QSet<QString> requested(profileIds.cbegin(), profileIds.cend());
    const QString currentId = currentProfile().id;
    requested.remove(currentId);
    if (requested.isEmpty()) {
        setLastError(tr("当前使用的启动配置不能删除。"));
        return false;
    }

    const qsizetype previousCount = m_profiles.size();
    m_profiles.removeIf([&requested](const Profile &profile) {
        return requested.contains(profile.id);
    });
    if (m_profiles.size() == previousCount) {
        setLastError(tr("没有可删除的启动配置。"));
        return false;
    }

    for (int index = 0; index < m_profiles.size(); ++index) {
        if (m_profiles.at(index).id == currentId) {
            m_currentProfileIndex = index;
            break;
        }
    }
    ++m_parameterRevision;
    save();
    validate();
    emit profilesChanged();
    emit currentProfileChanged();
    emit parameterRevisionChanged();
    return true;
}

int ConfigurationManager::addEnvironmentEntry()
{
    const int index = currentProfile().environment.size();
    currentProfile().environment.append({QString(), QString(), true});
    updateAfterEdit();
    return index;
}

void ConfigurationManager::updateEnvironmentEntry(
    int index,
    const QString &name,
    const QString &value,
    bool enabled)
{
    if (index < 0 || index >= currentProfile().environment.size()) {
        return;
    }
    auto &entry = currentProfile().environment[index];
    if (entry.name == name && entry.value == value && entry.enabled == enabled) {
        return;
    }
    entry = {name.trimmed(), value, enabled};
    updateAfterEdit();
}

void ConfigurationManager::removeEnvironmentEntry(int index)
{
    if (index < 0 || index >= currentProfile().environment.size()) {
        return;
    }
    currentProfile().environment.removeAt(index);
    updateAfterEdit();
}

QVariantMap ConfigurationManager::currentProfileSnapshot() const
{
    const auto &profile = currentProfile();
    return {
        {QStringLiteral("id"), profile.id},
        {QStringLiteral("name"), profile.name},
        {QStringLiteral("pythonPath"), profile.pythonPath},
        {QStringLiteral("comfyRoot"), profile.comfyRoot},
        {QStringLiteral("customArguments"), profile.customArguments},
        {QStringLiteral("parameters"), profile.parameters},
        {QStringLiteral("environment"), environmentEntries()}
    };
}

QString ConfigurationManager::profileIdAt(int index) const
{
    return index >= 0 && index < m_profiles.size() ? m_profiles.at(index).id : QString{};
}

void ConfigurationManager::reloadFromDisk()
{
    m_profiles.clear();
    m_currentProfileIndex = 0;
    load();
    validate();
    ++m_parameterRevision;
    emit profilesChanged();
    emit currentProfileChanged();
    emit parameterRevisionChanged();
}

ConfigurationManager::Profile &ConfigurationManager::currentProfile()
{
    Q_ASSERT(!m_profiles.isEmpty());
    return m_profiles[m_currentProfileIndex];
}

const ConfigurationManager::Profile &ConfigurationManager::currentProfile() const
{
    Q_ASSERT(!m_profiles.isEmpty());
    return m_profiles[m_currentProfileIndex];
}

ConfigurationManager::Profile ConfigurationManager::makeDefaultProfile(const QString &name) const
{
    const QString comfyRoot = findDefaultComfyRoot();
    return {
        QUuid::createUuid().toString(QUuid::WithoutBraces),
        limitedProfileName(name),
        findDefaultPython(comfyRoot),
        comfyRoot,
        QString(),
        QVariantMap(),
        {}
    };
}

void ConfigurationManager::load()
{
    QFile file(m_storagePath);
    if (!file.exists()) {
        m_profiles.append(makeDefaultProfile(tr("默认配置")));
        save();
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setLastError(tr("无法读取启动配置：%1").arg(file.errorString()));
        m_profiles.append(makeDefaultProfile(tr("默认配置")));
        return;
    }

    const QByteArray sourceData = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(sourceData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setLastError(tr("启动配置文件格式无效：%1").arg(parseError.errorString()));
        m_profiles.append(makeDefaultProfile(tr("默认配置")));
        return;
    }

    const QJsonObject root = document.object();
    const int schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(1);
    const bool migrationRequired = schemaVersion < 2;
    bool bundledPythonRepaired = false;
    bool profileNamesNormalized = false;
    const QJsonArray profiles = root.value(QStringLiteral("profiles")).toArray();
    const QString currentId = root.value(QStringLiteral("currentProfileId")).toString();
    for (const auto &value : profiles) {
        const QJsonObject object = value.toObject();
        Profile profile;
        profile.id = object.value(QStringLiteral("id")).toString();
        const QString storedName = object.value(QStringLiteral("name")).toString();
        profile.name = limitedProfileName(storedName);
        profileNamesNormalized = profileNamesNormalized || profile.name != storedName;
        profile.pythonPath = object.value(QStringLiteral("pythonPath")).toString();
        profile.comfyRoot = object.value(QStringLiteral("comfyRoot")).toString();
        profile.customArguments = object.value(QStringLiteral("customArguments")).toString();
        profile.parameters = object.value(QStringLiteral("parameters")).toObject().toVariantMap();
        for (const auto &environmentValue : object.value(QStringLiteral("environment")).toArray()) {
            const auto environmentObject = environmentValue.toObject();
            profile.environment.append({
                environmentObject.value(QStringLiteral("name")).toString().trimmed(),
                environmentObject.value(QStringLiteral("value")).toString(),
                environmentObject.value(QStringLiteral("enabled")).toBool(true)
            });
        }
        if (profile.id.isEmpty()) {
            profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        if (profile.name.isEmpty()) {
            profile.name = tr("未命名配置");
        }
        if (migrationRequired) {
            profile.environment.removeIf([](const EnvironmentEntry &entry) {
                return entry.value == QStringLiteral("1")
                    && (entry.name == QStringLiteral("PYTHONUTF8")
                        || entry.name == QStringLiteral("PYTHONUNBUFFERED"));
            });
        }
        const QString bundledPython = existingBundledPython(profile.comfyRoot);
        if (shouldAdoptBundledPython(profile.pythonPath, bundledPython)
            && !sameExecutable(profile.pythonPath, bundledPython)) {
            profile.pythonPath = bundledPython;
            bundledPythonRepaired = true;
        }
        if (profile.id == currentId) {
            m_currentProfileIndex = m_profiles.size();
        }
        m_profiles.append(profile);
    }

    if (m_profiles.isEmpty()) {
        m_profiles.append(makeDefaultProfile(tr("默认配置")));
        m_currentProfileIndex = 0;
    } else if (m_currentProfileIndex >= m_profiles.size()) {
        m_currentProfileIndex = 0;
    }

    if (migrationRequired || bundledPythonRepaired || profileNamesNormalized) {
        save();
    }
}

bool ConfigurationManager::save()
{
    const QFileInfo fileInfo(m_storagePath);
    if (m_storagePath == PortablePaths::configurationFile()) {
        QString error;
        if (!PortablePaths::ensureDataDirectory(&error)) {
            setLastError(error);
            return false;
        }
    } else if (!QDir().mkpath(fileInfo.absolutePath())) {
        setLastError(tr("无法创建配置目录：%1").arg(fileInfo.absolutePath()));
        return false;
    }

    QJsonArray profiles;
    for (const auto &profile : m_profiles) {
        QJsonArray environment;
        for (const auto &entry : profile.environment) {
            environment.append(QJsonObject{
                {QStringLiteral("name"), entry.name},
                {QStringLiteral("value"), entry.value},
                {QStringLiteral("enabled"), entry.enabled}
            });
        }
        profiles.append(QJsonObject{
            {QStringLiteral("id"), profile.id},
            {QStringLiteral("name"), profile.name},
            {QStringLiteral("pythonPath"), profile.pythonPath},
            {QStringLiteral("comfyRoot"), profile.comfyRoot},
            {QStringLiteral("customArguments"), profile.customArguments},
            {QStringLiteral("parameters"), QJsonObject::fromVariantMap(profile.parameters)},
            {QStringLiteral("environment"), environment}
        });
    }

    const QJsonObject root {
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("currentProfileId"), currentProfile().id},
        {QStringLiteral("profiles"), profiles}
    };

    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setLastError(tr("无法保存启动配置：%1").arg(file.errorString()));
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        setLastError(tr("无法提交启动配置：%1").arg(file.errorString()));
        return false;
    }
    setLastError({});
    return true;
}

void ConfigurationManager::updateAfterEdit(bool parametersChanged)
{
    if (parametersChanged) {
        ++m_parameterRevision;
    }
    save();
    validate();
    emit currentProfileChanged();
    if (parametersChanged) {
        emit parameterRevisionChanged();
    }
}

QStringList ConfigurationManager::environmentEntryErrors(const Profile &profile) const
{
    static const QRegularExpression environmentName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    QHash<QString, int> enabledNameCounts;
    for (const auto &entry : profile.environment) {
        const QString name = entry.name.trimmed();
        if (entry.enabled && environmentName.match(name).hasMatch()) {
            ++enabledNameCounts[name.toCaseFolded()];
        }
    }

    QStringList errors;
    errors.reserve(profile.environment.size());
    for (const auto &entry : profile.environment) {
        const QString name = entry.name.trimmed();
        QString error;
        if (entry.enabled && name.isEmpty()) {
            error = tr("请输入环境变量名称。");
        } else if (entry.enabled && !environmentName.match(name).hasMatch()) {
            error = tr("环境变量名称只能包含英文字母、数字和下划线，且不能以数字开头。");
        } else if (entry.enabled && enabledNameCounts.value(name.toCaseFolded()) > 1) {
            error = tr("环境变量名称重复：%1").arg(name);
        }
        errors.append(error);
    }
    return errors;
}

void ConfigurationManager::validate()
{
    QStringList errors;
    const auto &profile = currentProfile();
    const QFileInfo python(profile.pythonPath);
    if (profile.pythonPath.isEmpty()) {
        errors.append(tr("请选择 Python 可执行文件。"));
    } else if (!python.exists() || !python.isFile()) {
        errors.append(tr("Python 可执行文件不存在。"));
    }

    const QDir comfyDirectory(profile.comfyRoot);
    if (profile.comfyRoot.isEmpty()) {
        errors.append(tr("请选择 ComfyUI 根目录。"));
    } else if (!comfyDirectory.exists(QStringLiteral("main.py"))) {
        errors.append(tr("ComfyUI 根目录中未找到 main.py。"));
    }

    const int port = parameterValue(QStringLiteral("port")).toInt();
    if (port < 1 || port > 65535) {
        errors.append(tr("端口必须在 1 到 65535 之间。"));
    }

    const bool hasKey = !parameterValue(QStringLiteral("tlsKeyfile")).toString().trimmed().isEmpty();
    const bool hasCertificate = !parameterValue(QStringLiteral("tlsCertfile")).toString().trimmed().isEmpty();
    if (hasKey != hasCertificate) {
        errors.append(tr("TLS 私钥与证书必须同时设置。"));
    }

    for (const QString &error : environmentEntryErrors(profile)) {
        if (!error.isEmpty() && !errors.contains(error)) {
            errors.append(error);
        }
    }

    if (errors != m_validationErrors) {
        m_validationErrors = errors;
        emit validationChanged();
    }
}

void ConfigurationManager::setLastError(const QString &message)
{
    if (message == m_lastError) {
        return;
    }
    m_lastError = message;
    emit lastErrorChanged();
}

QString ConfigurationManager::normalizePath(const QString &path)
{
    const QUrl url(path);
    QString result = url.isLocalFile() ? url.toLocalFile() : path;
    result = QDir::fromNativeSeparators(result.trimmed());
    return result.isEmpty() ? result : QDir::cleanPath(result);
}

QString ConfigurationManager::findDefaultComfyRoot()
{
    QDir directory(QCoreApplication::applicationDirPath());
    for (int level = 0; level < 6; ++level) {
        const QString adjacent = directory.filePath(QStringLiteral("ComfyUI"));
        if (QFileInfo::exists(QDir(adjacent).filePath(QStringLiteral("main.py")))) {
            return QDir::cleanPath(adjacent);
        }
        if (QFileInfo::exists(directory.filePath(QStringLiteral("main.py")))
            && QFileInfo::exists(directory.filePath(QStringLiteral("comfy/cli_args.py")))) {
            return directory.absolutePath();
        }
        if (!directory.cdUp()) {
            break;
        }
    }
    return {};
}

QString ConfigurationManager::findDefaultPython(const QString &comfyRoot)
{
    const QString environmentOverride = qEnvironmentVariable("MINIFOX_PYTHON");
    if (QFileInfo::exists(environmentOverride)) {
        return QDir::cleanPath(environmentOverride);
    }

    const QString bundledPython = existingBundledPython(comfyRoot);
    if (!bundledPython.isEmpty()) {
        return bundledPython;
    }

    const QStringList candidates {
        QDir(QDir::homePath()).filePath(QStringLiteral("micromamba/envs/comfy-neo-env/python.exe"))
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
    return QStandardPaths::findExecutable(QStringLiteral("python.exe"));
}
