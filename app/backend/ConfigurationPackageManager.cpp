#include "ConfigurationPackageManager.h"

#include "ConfigurationManager.h"
#include "PortablePaths.h"
#include "SafeDataPath.h"
#include <QLockFile>
#include <QRegularExpression>

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

constexpr qsizetype kMaximumPackageBytes = 64ll * 1024 * 1024;
constexpr qsizetype kMaximumPackageEntries = 8192;
constexpr auto kPackageFormat = "minifox-configuration";

QString cleanLocalPath(const QUrl &url)
{
    return url.isLocalFile() ? QDir::cleanPath(url.toLocalFile())
                             : QDir::cleanPath(url.toString());
}

bool safeArchivePath(const QString &name)
{
    return SafeDataPath::canonicalRelative(name);
}

bool portableStatePath(const QString &path)
{
    if (!safeArchivePath(path)) return false;
    if (path == QStringLiteral("application-settings.json")
        || path == QStringLiteral("icons/custom.png")
        || path == QStringLiteral("skins/index.json")) return true;
    const QStringList parts = path.split('/');
    if (parts.size() < 3 || parts[0] != QStringLiteral("skins")) return false;
    // Skin packages may reference nested raster assets and uppercase suffixes.
    return (parts.size() == 3 && parts[2] == QStringLiteral("skin.json"))
        || (parts.size() >= 4 && parts[2] == QStringLiteral("assets")
            && QStringList{"png", "jpg", "jpeg", "webp"}.contains(QFileInfo(path).suffix().toLower()));
}

QString pendingStatePath()
{
    return QDir(PortablePaths::dataDirectory()).filePath("configuration-state.pending.zip");
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

    qsizetype totalBytes = 0;
    for (const auto &[name, bytes] : files) {
        Q_UNUSED(name);
        if (bytes.size() > kMaximumPackageBytes - totalBytes) {
            if (error) {
                *error = QObject::tr("配置包超过 64 MB 限制。");
            }
            return false;
        }
        totalBytes += bytes.size();
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

    for (const auto &[nameString, bytes] : files) {
        const QByteArray name = nameString.toUtf8();
        if (!safeArchivePath(nameString)
            || name.size() > std::numeric_limits<quint16>::max()
            || bytes.size() > std::numeric_limits<quint32>::max()) {
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

    if (file.size() > kMaximumPackageBytes) {
        file.cancelWriting();
        if (error) {
            *error = QObject::tr("配置包超过 64 MB 限制。");
        }
        return false;
    }
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
            *error = QObject::tr("配置包超过 64 MB 限制。");
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

    QSet<QString> names;
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
        if (!safeArchivePath(name) || names.contains(name.toCaseFolded())
            || readU32(data, localOffset) != 0x04034b50
            || localOffset + 30 > static_cast<quint32>(data.size())) {
            if (error) {
                *error = QObject::tr("配置包包含不安全的文件路径。");
            }
            return false;
        }
        names.insert(name.toCaseFolded());
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
        if (data.mid(localOffset + 30, localNameLength) != name.toUtf8()
            || readU16(data, localOffset + 8) != method
            || readU32(data, localOffset + 18) != compressedSize
            || readU32(data, localOffset + 22) != uncompressedSize
            || readU32(data, localOffset + 14) != crc) {
            if (error) *error = QObject::tr("配置包的文件头与目录不一致。");
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
    SafeDataPath guard;
    if (!guard.lock(root.absolutePath(), error)) return false;
    QStringList paths{"application-settings.json", "icons/custom.png", "skins/index.json"};
    const QString skinRoot = root.filePath("skins");
    if (!guard.lock(skinRoot, error)) return false;
    for (const QFileInfo &skin : QDir(skinRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!guard.lock(skin.absoluteFilePath(), error)) return false;
        const QString prefix = "skins/" + skin.fileName() + '/';
        paths.append(prefix + "skin.json");
        const QString assets = QDir(skin.absoluteFilePath()).filePath("assets");
        if (!guard.lock(assets, error)) return false;
        QStringList pending{assets};
        while (!pending.isEmpty()) {
            const QString directory = pending.takeLast();
            if (!guard.lock(directory, error)) return false;
            for (const QFileInfo &asset : QDir(directory).entryInfoList(
                     QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
                if (!guard.lock(asset.absoluteFilePath(), error)) return false;
                if (asset.isDir()) pending.append(asset.absoluteFilePath());
                else {
                    const QString relative = root.relativeFilePath(asset.absoluteFilePath());
                    if (portableStatePath(relative)) paths.append(relative);
                }
                if (paths.size() + pending.size() > kMaximumPackageEntries) {
                    if (error) *error = QObject::tr("配置包含过多资源路径。");
                    return false;
                }
            }
        }
    }
    qsizetype totalBytes = 0;
    for (const QString &relative : paths) {
        const QString path = root.filePath(relative);
        if (!guard.lock(path, error)) return false;
        if (!QFileInfo::exists(path)) continue;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) {
                *error = QObject::tr("无法读取便携配置文件：%1").arg(relative);
            }
            return false;
        }
        if (file.size() > kMaximumPackageBytes - totalBytes || state->size() >= kMaximumPackageEntries) {
            if (error) *error = QObject::tr("配置数据超过配置包容量限制。");
            return false;
        }
        const QByteArray bytes = file.read(kMaximumPackageBytes - totalBytes + 1);
        totalBytes += bytes.size();
        if (totalBytes > kMaximumPackageBytes || !file.atEnd() || file.error() != QFileDevice::NoError) {
            if (error) *error = QObject::tr("配置读取失败或读取期间超过容量限制。");
            return false;
        }
        state->insert(relative, bytes);
    }
    return true;
}

bool packageState(const QHash<QString, QByteArray> &files,
                  QHash<QString, QByteArray> *state,
                  QString *error)
{
    for (auto iterator = files.cbegin(); iterator != files.cend(); ++iterator) {
        if (iterator.key() == QStringLiteral("manifest.json")
            || iterator.key() == QStringLiteral("profile.json")) continue;
        if (!iterator.key().startsWith(QStringLiteral(".minifox/"))) {
            if (error) *error = QObject::tr("配置包包含未允许的数据文件：%1").arg(iterator.key());
            return false;
        }
        const QString relative = iterator.key().mid(9);
        if (!portableStatePath(relative)) {
            if (error) {
                *error = QObject::tr("配置包试图写入受保护的运行时目录。");
            }
            return false;
        }
        state->insert(relative, iterator.value());
    }
    return true;
}

// A durable preimage is published before any destination changes. Recovery
// rolls back idempotently before settings/managers are constructed on startup.
bool restoreState(const QHash<QString, QByteArray> &state, const QStringList &remove,
                  QString *error)
{
    const QDir root(PortablePaths::dataDirectory());
    SafeDataPath guard;
    if (!guard.lock(root.absolutePath(), error)) return false;
    for (const QString &name : state.keys() + remove) {
        if (!portableStatePath(name) || !guard.lock(root.filePath(name), error)) {
            if (error && error->isEmpty()) *error = QObject::tr("配置恢复记录包含受保护路径。");
            return false;
        }
    }
    for (auto it = state.cbegin(); it != state.cend(); ++it) {
        const QString path = root.filePath(it.key());
        if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !guard.lock(path, error)) return false;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) != it.value().size()
            || !file.commit() || !SafeDataPath::flushFile(path, error)) {
            if (error && error->isEmpty()) *error = QObject::tr("无法写入配置：%1").arg(it.key());
            return false;
        }
        QFile check(path);
        if (!check.open(QIODevice::ReadOnly) || check.readAll() != it.value()) {
            if (error) *error = QObject::tr("配置写入后校验失败：%1").arg(it.key());
            return false;
        }
    }
    for (const QString &name : remove) {
        if (state.contains(name)) continue;
        const QString path = root.filePath(name);
        if (QFileInfo::exists(path) && !QFile::remove(path)) {
            if (error) *error = QObject::tr("无法移除旧配置：%1").arg(name);
            return false;
        }
    }
    return true;
}

bool restoreProfiles(const QByteArray &bytes, QString *error)
{
    if (bytes.isEmpty()) return true;
    if (!QJsonDocument::fromJson(bytes).isObject()) {
        if (error) *error = QObject::tr("启动配置恢复资料损坏。");
        return false;
    }
    SafeDataPath guard;
    const QString path = PortablePaths::configurationFile();
    if (!guard.lock(path, error)) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()
        || !SafeDataPath::flushFile(path, error)) {
        if (error && error->isEmpty()) *error = QObject::tr("无法提交启动配置选择。");
        return false;
    }
    QFile check(path);
    return check.open(QIODevice::ReadOnly) && check.readAll() == bytes;
}

bool recoverPortableState(QString *error)
{
    SafeDataPath guard;
    if (!guard.lock(pendingStatePath(), error)) return false;
    if (!QFileInfo::exists(pendingStatePath())) return true;
    QHash<QString, QByteArray> files, before;
    if (!readStoreZip(pendingStatePath(), &files, error)) return false;
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(files.take("manifest.json"), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value("format") != "minifox-state-rollback-v1"
        || !document.object().value("remove").isArray()) {
        if (error) *error = QObject::tr("配置恢复记录损坏；已保留记录和现有文件。");
        return false;
    }
    QStringList remove;
    const QByteArray profiles = files.take("profiles-before.json");
    if (document.object().value("restoreProfiles").toBool()
        && (profiles.isEmpty() || !QJsonDocument::fromJson(profiles).isObject())) {
        if (error) *error = QObject::tr("配置恢复记录缺少原启动配置，已停止恢复。");
        return false;
    }
    for (const auto &value : document.object().value("remove").toArray()) {
        if (!value.isString() || !portableStatePath(value.toString())) {
            if (error) *error = QObject::tr("配置恢复记录包含无效路径。");
            return false;
        }
        remove.append(value.toString());
    }
    if (!packageState(files, &before, error) || !restoreState(before, remove, error)
        || !restoreProfiles(profiles, error)) return false;
    if (!QFile::remove(pendingStatePath())) {
        if (error) *error = QObject::tr("无法完成配置恢复；已保留恢复记录。");
        return false;
    }
    return true;
}

bool writePortableState(const QHash<QString, QByteArray> &state, QString *error,
                        const QString &targetProfileId = {})
{
    SafeDataPath guard;
    if (!guard.lock(pendingStatePath(), error) || !PortablePaths::ensureDataDirectory(error)) return false;
    QLockFile lock(QDir(PortablePaths::dataDirectory()).filePath("configuration-state.lock"));
    if (!lock.tryLock(0)) {
        if (error) *error = QObject::tr("另一个启动器正在应用配置，请稍后重试。");
        return false;
    }
    if (!recoverPortableState(error)) return false;
    QHash<QString, QByteArray> current;
    if (!collectPortableState(&current, error)) return false;
    QByteArray originalProfiles, targetProfiles;
    if (!targetProfileId.isEmpty()) {
        const QString path = PortablePaths::configurationFile();
        if (!guard.lock(path, error)) return false;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = file.errorString();
            return false;
        }
        originalProfiles = file.readAll();
        auto document = QJsonDocument::fromJson(originalProfiles);
        bool found = false;
        for (const auto &profile : document.object().value("profiles").toArray())
            found |= profile.toObject().value("id").toString() == targetProfileId;
        if (!found) {
            if (error) *error = QObject::tr("待切换的启动配置不存在。");
            return false;
        }
        auto object = document.object(); object.insert("currentProfileId", targetProfileId);
        targetProfiles = QJsonDocument(object).toJson();
    }
    // Validate the entire target before the journal or any portable file changes.
    for (const QString &name : state.keys()) {
        if (!portableStatePath(name) || !guard.lock(QDir(PortablePaths::dataDirectory()).filePath(name), error)) {
            if (error && error->isEmpty()) *error = QObject::tr("配置包包含受保护路径。");
            return false;
        }
    }
    const QJsonObject metadata{{"format", "minifox-state-rollback-v1"},
                              {"restoreProfiles", !originalProfiles.isEmpty()},
                              {"remove", QJsonArray::fromStringList(state.keys())}};
    QList<QPair<QString, QByteArray>> backup{{"manifest.json", QJsonDocument(metadata).toJson()}};
    if (!originalProfiles.isEmpty()) backup.append({"profiles-before.json", originalProfiles});
    for (auto it = current.cbegin(); it != current.cend(); ++it)
        backup.append({".minifox/" + it.key(), it.value()});
    if (!writeStoreZip(pendingStatePath(), backup, error)
        || !SafeDataPath::flushFile(pendingStatePath(), error)) return false;
    // Read-back validation before publishing any changes.
    QHash<QString, QByteArray> verified;
    if (!readStoreZip(pendingStatePath(), &verified, error)) return false;
    if (!restoreState(state, current.keys(), error) || !restoreProfiles(targetProfiles, error)) {
        QString rollbackError;
        if (!recoverPortableState(&rollbackError) && error)
            *error += QObject::tr("；原配置恢复待完成：%1").arg(rollbackError);
        return false;
    }
    if (!QFile::remove(pendingStatePath())) {
        if (error) *error = QObject::tr("配置提交未完成，已保留恢复记录。");
        return false;
    }
    return true;
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

bool ConfigurationPackageManager::recoverPendingState(QString *error)
{
    SafeDataPath guard;
    if (!guard.lock(pendingStatePath(), error)) return false;
    if (!QFileInfo::exists(pendingStatePath())) return true;
    QLockFile lock(QDir(PortablePaths::dataDirectory()).filePath("configuration-state.lock"));
    if (!lock.tryLock(0)) {
        if (error) *error = QObject::tr("另一个启动器正在应用配置。");
        return false;
    }
    return recoverPortableState(error);
}

bool ConfigurationPackageManager::flushPendingChanges()
{
    QString recoveryError;
    if (!recoverPendingState(&recoveryError)) {
        setError(recoveryError);
        return false;
    }
    if (m_configuration->savePendingChanges()) {
        return true;
    }
    setError(m_configuration->lastError());
    return false;
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

bool ConfigurationPackageManager::captureCurrentProfile(const QString &destination,
                                                         bool includeSensitiveValues)
{
    QHash<QString, QByteArray> state;
    QString error;
    if (includeSensitiveValues && !collectPortableState(&state, &error)) {
        setError(error);
        return false;
    }

    const QJsonObject manifest{
        {QStringLiteral("mode"), includeSensitiveValues ? QStringLiteral("full") : QStringLiteral("shared-profile")},
        {QStringLiteral("format"), QString::fromLatin1(kPackageFormat)},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}
    };
    QList<QPair<QString, QByteArray>> files{
        {QStringLiteral("manifest.json"),
         QJsonDocument(manifest).toJson(QJsonDocument::Indented)},
        {QStringLiteral("profile.json"),
         QJsonDocument(QJsonObject::fromVariantMap(
                           m_configuration->currentProfileSnapshot(includeSensitiveValues)))
             .toJson(QJsonDocument::Indented)}
    };
    QStringList paths = state.keys();
    paths.sort();
    for (const QString &relative : std::as_const(paths)) {
        files.append({QStringLiteral(".minifox/%1").arg(relative), state.value(relative)});
    }
    SafeDataPath destinationGuard;
    if (!destinationGuard.lock(destination, &error)
        || !QDir().mkpath(QFileInfo(destination).absolutePath())
        || !destinationGuard.lock(destination, &error)
        || !writeStoreZip(destination, files, &error)) {
        setError(error.isEmpty() ? tr("无法创建配置包目录。") : error);
        return false;
    }
    return true;
}

bool ConfigurationPackageManager::applyPackageState(const QString &packagePath, bool *changed,
                                                    const QString &targetId)
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
    if (QJsonDocument::fromJson(files.value("manifest.json")).object().value("mode").toString() == "shared-profile") {
        if (!target.isEmpty()) { setError(tr("分享配置包不能包含界面状态。")); return false; }
        target = current;
    }
    *changed = current != target;
    if (!writePortableState(target, &error, targetId)) {
        setError(error);
        return false;
    }
    return true;
}

bool ConfigurationPackageManager::switchProfile(int index)
{
    if (!flushPendingChanges()) {
        return false;
    }

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
        && !applyPackageState(targetSnapshot, &changed, targetId)) {
        return false;
    }
    if (QFileInfo::exists(targetSnapshot)) m_configuration->reloadFromDisk();
    m_configuration->setCurrentProfileIndex(index);
    if (!flushPendingChanges()) {
        return false;
    }

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
    if (!flushPendingChanges()) {
        return false;
    }

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
    if (!flushPendingChanges()) {
        return false;
    }

    setMessage(tr("已新建启动配置。"));
    return true;
}

bool ConfigurationPackageManager::duplicateCurrentProfile()
{
    if (!flushPendingChanges()) {
        return false;
    }

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
    if (!flushPendingChanges()) {
        return false;
    }

    setMessage(tr("已复制启动配置。"));
    return true;
}

bool ConfigurationPackageManager::exportPackage(const QUrl &destination,
                                                bool includeSensitiveValues)
{
    if (!flushPendingChanges()) {
        return false;
    }

    QString path = cleanLocalPath(destination);
    if (QFileInfo(path).suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) != 0) {
        path += QStringLiteral(".zip");
    }
    if (path.isEmpty() || !captureCurrentProfile(path, includeSensitiveValues)) {
        return false;
    }
    setMessage(tr("配置已导出：%1").arg(QDir::toNativeSeparators(path)));
    return true;
}

bool ConfigurationPackageManager::importPackage(const QUrl &source)
{
    if (!flushPendingChanges()) {
        return false;
    }

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
    SafeDataPath importGuard;
    QList<QPair<QString, QByteArray>> validatedFiles;
    for (auto it = files.cbegin(); it != files.cend(); ++it) validatedFiles.append({it.key(), it.value()});
    if (!importGuard.lock(internalPath, &error)
        || !importGuard.lock(PortablePaths::configurationFile(), &error)
        || !QDir().mkpath(QFileInfo(internalPath).absolutePath())
        || !importGuard.lock(internalPath, &error)
        || QFileInfo::exists(internalPath)
        || !writeStoreZip(internalPath, validatedFiles, &error)) {
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
    if (!flushPendingChanges()) {
        return false;
    }

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
    if (!flushPendingChanges()) {
        return false;
    }

    setMessage(tr("已删除 %1 个配置。").arg(removableIds.size()));
    return true;
}

bool ConfigurationPackageManager::restartLauncher()
{
    if (!flushPendingChanges()) {
        return false;
    }

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
