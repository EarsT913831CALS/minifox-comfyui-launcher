#include "ConfigurationPackageManager.h"

#include "ConfigurationManager.h"
#include "PortablePaths.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <limits>
#include <utility>

namespace {

constexpr qsizetype kMaximumPackageBytes = 1024ll * 1024 * 1024;
constexpr qsizetype kMaximumPackageEntries = 8192;
constexpr auto kPackageFormat = "minifox-configuration";

QString cleanLocalPath(const QUrl &url)
{
    return url.isLocalFile() ? QDir::cleanPath(url.toLocalFile())
                             : QDir::cleanPath(url.toString());
}

bool safeArchivePath(const QString &name)
{
    const QString normalized = QDir::fromNativeSeparators(name);
    return !normalized.isEmpty()
        && !normalized.startsWith(QLatin1Char('/'))
        && !normalized.contains(QStringLiteral("../"))
        && normalized != QStringLiteral("..")
        && !QDir::isAbsolutePath(normalized)
        && !normalized.contains(QLatin1Char(':'));
}

bool excludedStatePath(const QString &relativePath)
{
    const QString normalized = QDir::fromNativeSeparators(relativePath);
    const QString topLevel = normalized.section(QLatin1Char('/'), 0, 0);
    return normalized == QStringLiteral("launch-profiles.json")
        || normalized == QStringLiteral("dependency-command.bat")
        || topLevel == QStringLiteral("runtime")
        || topLevel == QStringLiteral("packages")
        || topLevel == QStringLiteral("configurations");
}

quint32 crc32Bytes(const QByteArray &bytes)
{
    quint32 crc = 0xffffffffu;
    for (const unsigned char byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xffffffffu;
}

quint16 readU16(const QByteArray &data, qsizetype offset)
{
    if (offset < 0 || offset + 2 > data.size()) {
        return 0;
    }
    return static_cast<quint16>(
        static_cast<unsigned char>(data[offset])
        | (static_cast<quint16>(static_cast<unsigned char>(data[offset + 1])) << 8));
}

quint32 readU32(const QByteArray &data, qsizetype offset)
{
    if (offset < 0 || offset + 4 > data.size()) {
        return 0;
    }
    return static_cast<quint32>(static_cast<unsigned char>(data[offset]))
        | (static_cast<quint32>(static_cast<unsigned char>(data[offset + 1])) << 8)
        | (static_cast<quint32>(static_cast<unsigned char>(data[offset + 2])) << 16)
        | (static_cast<quint32>(static_cast<unsigned char>(data[offset + 3])) << 24);
}

bool writeStoreZip(const QString &path,
                   const QList<QPair<QString, QByteArray>> &files,
                   QString *error)
{
    struct CentralEntry {
        QByteArray name;
        quint32 crc = 0;
        quint32 size = 0;
        quint32 offset = 0;
    };

    if (files.size() > kMaximumPackageEntries) {
        if (error) {
            *error = QObject::tr("配置包中的文件数量过多。");
        }
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QObject::tr("无法创建配置包：%1").arg(file.errorString());
        }
        return false;
    }
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    QList<CentralEntry> central;
    qsizetype totalBytes = 0;

    for (const auto &[nameString, bytes] : files) {
        const QByteArray name = nameString.toUtf8();
        totalBytes += bytes.size();
        if (!safeArchivePath(nameString)
            || name.size() > std::numeric_limits<quint16>::max()
            || bytes.size() > std::numeric_limits<quint32>::max()
            || totalBytes > kMaximumPackageBytes) {
            if (error) {
                *error = QObject::tr("配置包中包含无效或过大的文件。");
            }
            return false;
        }
        CentralEntry entry;
        entry.name = name;
        entry.crc = crc32Bytes(bytes);
        entry.size = static_cast<quint32>(bytes.size());
        entry.offset = static_cast<quint32>(file.pos());
        central.append(entry);

        stream << quint32(0x04034b50) << quint16(20) << quint16(0x0800)
               << quint16(0) << quint16(0) << quint16(0)
               << entry.crc << entry.size << entry.size
               << static_cast<quint16>(name.size()) << quint16(0);
        file.write(name);
        file.write(bytes);
    }

    const quint32 centralOffset = static_cast<quint32>(file.pos());
    for (const CentralEntry &entry : std::as_const(central)) {
        stream << quint32(0x02014b50) << quint16(20) << quint16(20)
               << quint16(0x0800) << quint16(0) << quint16(0) << quint16(0)
               << entry.crc << entry.size << entry.size
               << static_cast<quint16>(entry.name.size())
               << quint16(0) << quint16(0) << quint16(0) << quint16(0)
               << quint32(0) << entry.offset;
        file.write(entry.name);
    }
    const quint32 centralSize = static_cast<quint32>(file.pos()) - centralOffset;
    stream << quint32(0x06054b50) << quint16(0) << quint16(0)
           << static_cast<quint16>(central.size())
           << static_cast<quint16>(central.size())
           << centralSize << centralOffset << quint16(0);

    if (!file.commit()) {
        if (error) {
            *error = QObject::tr("无法保存配置包：%1").arg(file.errorString());
        }
        return false;
    }
    return true;
}

bool readStoreZip(const QString &path, QHash<QString, QByteArray> *files, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QObject::tr("无法读取配置包：%1").arg(file.errorString());
        }
        return false;
    }
    if (file.size() > kMaximumPackageBytes) {
        if (error) {
            *error = QObject::tr("配置包超过 1 GB 限制。");
        }
        return false;
    }
    const QByteArray data = file.readAll();
    const qsizetype searchStart = qMax<qsizetype>(0, data.size() - 65557);
    qsizetype eocd = -1;
    for (qsizetype offset = data.size() - 22; offset >= searchStart; --offset) {
        if (readU32(data, offset) == 0x06054b50) {
            eocd = offset;
            break;
        }
    }
    if (eocd < 0) {
        if (error) {
            *error = QObject::tr("所选文件不是有效的 ZIP 配置包。");
        }
        return false;
    }
    const quint16 count = readU16(data, eocd + 10);
    const quint32 centralSize = readU32(data, eocd + 12);
    const quint32 centralOffset = readU32(data, eocd + 16);
    if (count > kMaximumPackageEntries
        || static_cast<quint64>(centralOffset) + centralSize
               > static_cast<quint64>(data.size())) {
        if (error) {
            *error = QObject::tr("配置包目录无效。");
        }
        return false;
    }

    qsizetype cursor = centralOffset;
    qsizetype totalBytes = 0;
    for (quint16 index = 0; index < count; ++index) {
        if (readU32(data, cursor) != 0x02014b50 || cursor + 46 > data.size()) {
            if (error) {
                *error = QObject::tr("配置包目录项损坏。");
            }
            return false;
        }
        const quint16 flags = readU16(data, cursor + 8);
        const quint16 method = readU16(data, cursor + 10);
        const quint32 crc = readU32(data, cursor + 16);
        const quint32 compressedSize = readU32(data, cursor + 20);
        const quint32 uncompressedSize = readU32(data, cursor + 24);
        const quint16 nameLength = readU16(data, cursor + 28);
        const quint16 extraLength = readU16(data, cursor + 30);
        const quint16 commentLength = readU16(data, cursor + 32);
        const quint32 localOffset = readU32(data, cursor + 42);
        if ((flags & 0x0001) != 0 || method != 0
            || compressedSize != uncompressedSize
            || cursor + 46 + nameLength + extraLength + commentLength > data.size()) {
            if (error) {
                *error = QObject::tr("配置包使用了不支持的加密或压缩方式。");
            }
            return false;
        }
        const QString name = QString::fromUtf8(data.mid(cursor + 46, nameLength));
        if (!safeArchivePath(name)
            || readU32(data, localOffset) != 0x04034b50
            || localOffset + 30 > static_cast<quint32>(data.size())) {
            if (error) {
                *error = QObject::tr("配置包包含不安全的文件路径。");
            }
            return false;
        }
        const quint16 localNameLength = readU16(data, localOffset + 26);
        const quint16 localExtraLength = readU16(data, localOffset + 28);
        const quint64 contentOffset = static_cast<quint64>(localOffset) + 30
            + localNameLength + localExtraLength;
        if (contentOffset + uncompressedSize > static_cast<quint64>(data.size())) {
            if (error) {
                *error = QObject::tr("配置包文件内容不完整。");
            }
            return false;
        }
        const QByteArray bytes = data.mid(static_cast<qsizetype>(contentOffset),
                                          static_cast<qsizetype>(uncompressedSize));
        totalBytes += bytes.size();
        if (totalBytes > kMaximumPackageBytes || crc32Bytes(bytes) != crc) {
            if (error) {
                *error = QObject::tr("配置包校验失败。");
            }
            return false;
        }
        files->insert(QDir::fromNativeSeparators(name), bytes);
        cursor += 46 + nameLength + extraLength + commentLength;
    }
    return true;
}

bool collectPortableState(QHash<QString, QByteArray> *state, QString *error)
{
    const QDir root(PortablePaths::dataDirectory());
    if (!root.exists()) {
        return true;
    }
    QDirIterator iterator(root.absolutePath(), QDir::Files | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QFileInfo info = iterator.fileInfo();
        const QString relative = QDir::fromNativeSeparators(root.relativeFilePath(path));
        if (info.isSymLink() || excludedStatePath(relative)) {
            continue;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) {
                *error = QObject::tr("无法读取便携配置文件：%1").arg(relative);
            }
            return false;
        }
        state->insert(relative, file.readAll());
    }
    return true;
}

bool packageState(const QHash<QString, QByteArray> &files,
                  QHash<QString, QByteArray> *state,
                  QString *error)
{
    for (auto iterator = files.cbegin(); iterator != files.cend(); ++iterator) {
        if (!iterator.key().startsWith(QStringLiteral(".minifox/"))) {
            continue;
        }
        const QString relative = iterator.key().mid(9);
        if (relative.isEmpty() || excludedStatePath(relative)) {
            if (error) {
                *error = QObject::tr("配置包试图写入受保护的运行时目录。");
            }
            return false;
        }
        state->insert(relative, iterator.value());
    }
    return true;
}

bool writePortableState(const QHash<QString, QByteArray> &state, QString *error)
{
    const QDir root(PortablePaths::dataDirectory());
    QHash<QString, QByteArray> current;
    if (!collectPortableState(&current, error)) {
        return false;
    }

    const auto removeFiles = [&root](const QHash<QString, QByteArray> &files,
                                     QString *removeError) {
        for (auto iterator = files.cbegin(); iterator != files.cend(); ++iterator) {
            const QString path = root.filePath(iterator.key());
            if (QFileInfo::exists(path) && !QFile::remove(path)) {
                if (removeError) {
                    *removeError = QObject::tr("无法替换便携配置文件：%1")
                                       .arg(iterator.key());
                }
                return false;
            }
        }
        return true;
    };
    const auto writeFiles = [&root](const QHash<QString, QByteArray> &files,
                                    QString *writeError) {
        for (auto iterator = files.cbegin(); iterator != files.cend(); ++iterator) {
            const QString path = root.filePath(iterator.key());
            if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
                if (writeError) {
                    *writeError = QObject::tr("无法创建便携配置目录：%1")
                                      .arg(QFileInfo(path).absolutePath());
                }
                return false;
            }
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly)
                || file.write(iterator.value()) != iterator.value().size()
                || !file.commit()) {
                if (writeError) {
                    *writeError = QObject::tr("无法写入便携配置文件：%1")
                                      .arg(iterator.key());
                }
                return false;
            }
        }
        return true;
    };

    QString operationError;
    if (removeFiles(current, &operationError) && writeFiles(state, &operationError)) {
        return true;
    }

    QString rollbackError;
    QHash<QString, QByteArray> partial;
    collectPortableState(&partial, nullptr);
    removeFiles(partial, nullptr);
    if (!writeFiles(current, &rollbackError) && !rollbackError.isEmpty()) {
        operationError += QObject::tr("；恢复原配置失败：%1").arg(rollbackError);
    }
    if (error) {
        *error = operationError;
    }
    return false;
}

bool validPackage(const QHash<QString, QByteArray> &files,
                  QJsonObject *profile,
                  QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument manifest = QJsonDocument::fromJson(
        files.value(QStringLiteral("manifest.json")), &parseError);
    if (parseError.error != QJsonParseError::NoError || !manifest.isObject()
        || manifest.object().value(QStringLiteral("format")).toString()
               != QString::fromLatin1(kPackageFormat)
        || manifest.object().value(QStringLiteral("schemaVersion")).toInt() != 1) {
        if (error) {
            *error = QObject::tr("文件不是可识别的 Minifox 配置包。");
        }
        return false;
    }
    const QJsonDocument profileDocument = QJsonDocument::fromJson(
        files.value(QStringLiteral("profile.json")), &parseError);
    if (parseError.error != QJsonParseError::NoError || !profileDocument.isObject()
        || profileDocument.object().value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
        if (error) {
            *error = QObject::tr("配置包缺少有效的启动配置。");
        }
        return false;
    }
    *profile = profileDocument.object();
    return true;
}

} // namespace

ConfigurationPackageManager::ConfigurationPackageManager(
    ConfigurationManager *configuration,
    QObject *parent)
    : QObject(parent),
      m_configuration(configuration)
{
}

bool ConfigurationPackageManager::restartRequired() const
{
    return m_restartRequired;
}

QString ConfigurationPackageManager::lastError() const
{
    return m_lastError;
}

QString ConfigurationPackageManager::lastMessage() const
{
    return m_lastMessage;
}

QString ConfigurationPackageManager::snapshotPath(const QString &profileId) const
{
    return QDir(PortablePaths::dataDirectory())
        .filePath(QStringLiteral("configurations/%1.zip").arg(profileId));
}

bool ConfigurationPackageManager::captureCurrentProfile(const QString &destination)
{
    QHash<QString, QByteArray> state;
    QString error;
    if (!collectPortableState(&state, &error)) {
        setError(error);
        return false;
    }

    const QJsonObject manifest{
        {QStringLiteral("format"), QString::fromLatin1(kPackageFormat)},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}
    };
    QList<QPair<QString, QByteArray>> files{
        {QStringLiteral("manifest.json"),
         QJsonDocument(manifest).toJson(QJsonDocument::Indented)},
        {QStringLiteral("profile.json"),
         QJsonDocument(QJsonObject::fromVariantMap(
                           m_configuration->currentProfileSnapshot()))
             .toJson(QJsonDocument::Indented)}
    };
    QStringList paths = state.keys();
    paths.sort();
    for (const QString &relative : std::as_const(paths)) {
        files.append({QStringLiteral(".minifox/%1").arg(relative), state.value(relative)});
    }
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())
        || !writeStoreZip(destination, files, &error)) {
        setError(error.isEmpty() ? tr("无法创建配置包目录。") : error);
        return false;
    }
    return true;
}

bool ConfigurationPackageManager::applyPackageState(const QString &packagePath, bool *changed)
{
    QHash<QString, QByteArray> files;
    QHash<QString, QByteArray> target;
    QHash<QString, QByteArray> current;
    QJsonObject profile;
    QString error;
    if (!readStoreZip(packagePath, &files, &error)
        || !validPackage(files, &profile, &error)
        || !packageState(files, &target, &error)
        || !collectPortableState(&current, &error)) {
        setError(error);
        return false;
    }
    *changed = current != target;
    if (*changed && !writePortableState(target, &error)) {
        setError(error);
        return false;
    }
    return true;
}

bool ConfigurationPackageManager::switchProfile(int index)
{
    if (index == m_configuration->currentProfileIndex()) {
        return true;
    }
    const QString targetId = m_configuration->profileIdAt(index);
    if (targetId.isEmpty()) {
        setError(tr("所选启动配置不存在。"));
        return false;
    }
    if (!captureCurrentProfile(snapshotPath(
            m_configuration->currentProfileSnapshot().value(QStringLiteral("id")).toString()))) {
        return false;
    }

    bool changed = false;
    const QString targetSnapshot = snapshotPath(targetId);
    if (QFileInfo::exists(targetSnapshot)
        && !applyPackageState(targetSnapshot, &changed)) {
        return false;
    }
    m_configuration->setCurrentProfileIndex(index);
    if (!QFileInfo::exists(targetSnapshot)) {
        captureCurrentProfile(targetSnapshot);
    }
    if (changed) {
        m_restartRequired = true;
        setMessage(tr("配置包含不同的界面、皮肤或首页布局，请重启启动器以完整应用。"));
    } else {
        setMessage(tr("已切换启动配置。"));
    }
    return true;
}

bool ConfigurationPackageManager::addProfile()
{
    const QString outgoingId = m_configuration->currentProfileSnapshot()
                                   .value(QStringLiteral("id")).toString();
    if (!captureCurrentProfile(snapshotPath(outgoingId))) {
        return false;
    }
    m_configuration->addProfile();
    const QString createdId = m_configuration->currentProfileSnapshot()
                                  .value(QStringLiteral("id")).toString();
    if (!captureCurrentProfile(snapshotPath(createdId))) {
        return false;
    }
    setMessage(tr("已新建启动配置。"));
    return true;
}

bool ConfigurationPackageManager::duplicateCurrentProfile()
{
    const QString outgoingId = m_configuration->currentProfileSnapshot()
                                   .value(QStringLiteral("id")).toString();
    if (!captureCurrentProfile(snapshotPath(outgoingId))) {
        return false;
    }
    m_configuration->duplicateCurrentProfile();
    const QString createdId = m_configuration->currentProfileSnapshot()
                                  .value(QStringLiteral("id")).toString();
    if (!captureCurrentProfile(snapshotPath(createdId))) {
        return false;
    }
    setMessage(tr("已复制启动配置。"));
    return true;
}

bool ConfigurationPackageManager::exportPackage(const QUrl &destination)
{
    QString path = cleanLocalPath(destination);
    if (QFileInfo(path).suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) != 0) {
        path += QStringLiteral(".zip");
    }
    if (path.isEmpty() || !captureCurrentProfile(path)) {
        return false;
    }
    setMessage(tr("配置已导出：%1").arg(QDir::toNativeSeparators(path)));
    return true;
}

bool ConfigurationPackageManager::importPackage(const QUrl &source)
{
    const QString sourcePath = cleanLocalPath(source);
    QHash<QString, QByteArray> files;
    QHash<QString, QByteArray> importedState;
    QJsonObject profile;
    QString error;
    if (!readStoreZip(sourcePath, &files, &error)
        || !validPackage(files, &profile, &error)
        || !packageState(files, &importedState, &error)) {
        setError(error);
        return false;
    }
    const QString outgoingSnapshot = snapshotPath(
        m_configuration->currentProfileSnapshot().value(QStringLiteral("id")).toString());
    if (!captureCurrentProfile(outgoingSnapshot)) {
        return false;
    }

    QFile configurationFile(PortablePaths::configurationFile());
    if (!configurationFile.open(QIODevice::ReadOnly)) {
        setError(tr("无法读取现有启动配置：%1").arg(configurationFile.errorString()));
        return false;
    }
    QJsonParseError configurationParseError;
    const QJsonDocument configurationDocument = QJsonDocument::fromJson(
        configurationFile.readAll(), &configurationParseError);
    configurationFile.close();
    if (configurationParseError.error != QJsonParseError::NoError
        || !configurationDocument.isObject()) {
        setError(tr("现有启动配置文件无效，已取消导入。"));
        return false;
    }
    QJsonObject root = configurationDocument.object();
    QJsonArray profiles = root.value(QStringLiteral("profiles")).toArray();
    QSet<QString> names;
    for (const QJsonValue &value : std::as_const(profiles)) {
        names.insert(ConfigurationManager::limitedProfileName(
                         value.toObject().value(QStringLiteral("name")).toString())
                         .toCaseFolded());
    }
    const QString importedName = profile.value(QStringLiteral("name")).toString().trimmed();
    QString name = ConfigurationManager::limitedProfileName(importedName);
    if (name != importedName) {
        setError(tr("导入的配置名超过 18 个字符，无法导入。请先缩短原配置名。"));
        return false;
    }
    const QString baseName = name;
    for (int suffix = 2; names.contains(name.toCaseFolded()); ++suffix) {
        const QString candidate = QStringLiteral("%1 (%2)").arg(baseName).arg(suffix);
        if (ConfigurationManager::limitedProfileName(candidate) != candidate) {
            setError(tr("重名后的配置名“%1”超过 18 个字符，无法导入。请先缩短原配置名。")
                         .arg(candidate));
            return false;
        }
        name = candidate;
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    profile.insert(QStringLiteral("id"), id);
    profile.insert(QStringLiteral("name"), name);
    profiles.append(profile);
    root.insert(QStringLiteral("schemaVersion"), 2);
    root.insert(QStringLiteral("profiles"), profiles);

    const QString internalPath = snapshotPath(id);
    if (!QDir().mkpath(QFileInfo(internalPath).absolutePath())
        || QFileInfo::exists(internalPath)
        || !QFile::copy(sourcePath, internalPath)) {
        setError(tr("无法保存导入配置的界面状态。"));
        return false;
    }

    QSaveFile output(PortablePaths::configurationFile());
    if (!output.open(QIODevice::WriteOnly)) {
        const QString writeError = tr("无法写入导入的启动配置：%1")
                                       .arg(output.errorString());
        QFile::remove(internalPath);
        setError(writeError);
        return false;
    }
    output.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!output.commit()) {
        const QString commitError = tr("无法提交导入的启动配置：%1")
                                        .arg(output.errorString());
        QFile::remove(internalPath);
        setError(commitError);
        return false;
    }
    m_configuration->reloadFromDisk();
    setMessage(tr("配置已导入，当前使用配置未切换。"));
    return true;
}

bool ConfigurationPackageManager::deleteProfiles(const QStringList &profileIds)
{
    const QString currentId = m_configuration->currentProfileSnapshot()
                                  .value(QStringLiteral("id")).toString();
    QStringList removableIds;
    for (const QString &profileId : profileIds) {
        if (!profileId.isEmpty() && profileId != currentId
            && !removableIds.contains(profileId)) {
            removableIds.append(profileId);
        }
    }
    if (removableIds.isEmpty()) {
        setError(tr("请选择至少一个非当前使用的配置。"));
        return false;
    }
    if (!m_configuration->removeProfiles(removableIds)) {
        setError(m_configuration->lastError());
        return false;
    }
    for (const QString &profileId : std::as_const(removableIds)) {
        QFile::remove(snapshotPath(profileId));
    }
    setMessage(tr("已删除 %1 个配置。").arg(removableIds.size()));
    return true;
}

bool ConfigurationPackageManager::restartLauncher()
{
    const QString executable = QCoreApplication::applicationFilePath();
    QStringList arguments = QCoreApplication::arguments();
    if (!arguments.isEmpty()) {
        arguments.removeFirst();
    }
    if (!QProcess::startDetached(executable, arguments,
                                 QCoreApplication::applicationDirPath())) {
        setError(tr("无法重新启动启动器。"));
        return false;
    }
    QCoreApplication::quit();
    return true;
}

void ConfigurationPackageManager::setError(const QString &message)
{
    m_lastError = message;
    m_lastMessage.clear();
    emit stateChanged();
}

void ConfigurationPackageManager::setMessage(const QString &message)
{
    m_lastError.clear();
    m_lastMessage = message;
    emit stateChanged();
}
