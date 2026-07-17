#include "ConfigurationManager.h"

#include "LaunchParameterCatalog.h"
#include "PortablePaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

ConfigurationManager::ConfigurationManager(const QString &storagePath, QObject *parent)
    : QObject(parent),
      m_storagePath(storagePath.isEmpty() ? PortablePaths::configurationFile() : storagePath)
{
    load();
    validate();
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
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty() || trimmedName == currentProfile().name) {
        return;
    }
    currentProfile().name = trimmedName;
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
    QVariantList entries;
    entries.reserve(currentProfile().environment.size());
    for (const auto &entry : currentProfile().environment) {
        entries.append(QVariantMap{
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("value"), entry.value},
            {QStringLiteral("enabled"), entry.enabled}
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
    const QString profileName = name.trimmed().isEmpty()
        ? tr("新配置 %1").arg(m_profiles.size() + 1)
        : name.trimmed();
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
    copy.name = tr("%1 - 副本").arg(copy.name);
    m_profiles.append(copy);
    m_currentProfileIndex = m_profiles.size() - 1;
    ++m_parameterRevision;
    save();
    validate();
    emit profilesChanged();
    emit currentProfileChanged();
    emit parameterRevisionChanged();
}

bool ConfigurationManager::removeCurrentProfile()
{
    if (m_profiles.size() <= 1) {
        setLastError(tr("至少需要保留一个启动配置。"));
        return false;
    }
    m_profiles.removeAt(m_currentProfileIndex);
    m_currentProfileIndex = qMin(m_currentProfileIndex, m_profiles.size() - 1);
    ++m_parameterRevision;
    save();
    validate();
    emit profilesChanged();
    emit currentProfileChanged();
    emit parameterRevisionChanged();
    return true;
}

void ConfigurationManager::addEnvironmentEntry()
{
    currentProfile().environment.append({QString(), QString(), true});
    updateAfterEdit();
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
        name,
        findDefaultPython(comfyRoot),
        comfyRoot,
        QString(),
        QVariantMap(),
        {
            {QStringLiteral("PYTHONUTF8"), QStringLiteral("1"), true},
            {QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"), true}
        }
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

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setLastError(tr("启动配置文件格式无效：%1").arg(parseError.errorString()));
        m_profiles.append(makeDefaultProfile(tr("默认配置")));
        return;
    }

    const QJsonObject root = document.object();
    const QJsonArray profiles = root.value(QStringLiteral("profiles")).toArray();
    const QString currentId = root.value(QStringLiteral("currentProfileId")).toString();
    for (const auto &value : profiles) {
        const QJsonObject object = value.toObject();
        Profile profile;
        profile.id = object.value(QStringLiteral("id")).toString();
        profile.name = object.value(QStringLiteral("name")).toString();
        profile.pythonPath = object.value(QStringLiteral("pythonPath")).toString();
        profile.comfyRoot = object.value(QStringLiteral("comfyRoot")).toString();
        profile.customArguments = object.value(QStringLiteral("customArguments")).toString();
        profile.parameters = object.value(QStringLiteral("parameters")).toObject().toVariantMap();
        for (const auto &environmentValue : object.value(QStringLiteral("environment")).toArray()) {
            const auto environmentObject = environmentValue.toObject();
            profile.environment.append({
                environmentObject.value(QStringLiteral("name")).toString(),
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
        if (profile.environment.isEmpty()) {
            profile.environment = {
                {QStringLiteral("PYTHONUTF8"), QStringLiteral("1"), true},
                {QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"), true}
            };
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
        {QStringLiteral("schemaVersion"), 1},
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

    const QRegularExpression environmentName(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    for (const auto &entry : profile.environment) {
        if (entry.enabled && !entry.name.isEmpty() && !environmentName.match(entry.name).hasMatch()) {
            errors.append(tr("环境变量名称无效：%1").arg(entry.name));
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

    const QStringList candidates {
        QDir(comfyRoot).filePath(QStringLiteral(".venv/Scripts/python.exe")),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("python_embeded/python.exe")),
        QDir(QDir::homePath()).filePath(QStringLiteral("micromamba/envs/comfy-neo-env/python.exe"))
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
    return QStandardPaths::findExecutable(QStringLiteral("python.exe"));
}
