#include "SkinManager.h"

#include "ApplicationSettings.h"
#include "PortablePaths.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <limits>
#include <utility>

namespace {

constexpr auto kBuiltinId = "builtin.default";
constexpr int kSkinSchemaVersion = 2;
constexpr qsizetype kMaximumPackageBytes = 256 * 1024 * 1024;
constexpr qsizetype kMaximumPackageEntries = 1024;
constexpr qsizetype kMaximumHomeFolders = 8;

QString cleanLocalPath(const QUrl &url)
{
    return url.isLocalFile() ? QDir::cleanPath(url.toLocalFile())
                             : QDir::cleanPath(url.toString());
}

QJsonObject objectValue(const QJsonObject &object, const QString &key)
{
    return object.value(key).isObject() ? object.value(key).toObject() : QJsonObject{};
}

double boundedNumber(const QJsonValue &value, double fallback, double minimum, double maximum)
{
    const double number = value.isDouble() ? value.toDouble() : fallback;
    return qBound(minimum, number, maximum);
}

QString safeName(QString name, const QString &fallback)
{
    name = name.trimmed();
    return name.isEmpty() ? fallback : name.left(64);
}

QJsonArray defaultHomeFolders()
{
    return {
        QJsonObject{
            {QStringLiteral("kind"), QStringLiteral("root")},
            {QStringLiteral("path"), QStringLiteral("${COMFYUI}")}
        },
        QJsonObject{
            {QStringLiteral("kind"), QStringLiteral("custom_nodes")},
            {QStringLiteral("path"), QStringLiteral("${COMFYUI}/custom_nodes")}
        },
        QJsonObject{
            {QStringLiteral("kind"), QStringLiteral("input")},
            {QStringLiteral("path"), QStringLiteral("${COMFYUI}/input")}
        },
        QJsonObject{
            {QStringLiteral("kind"), QStringLiteral("output")},
            {QStringLiteral("path"), QStringLiteral("${COMFYUI}/output")}
        }
    };
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

void writeU16(QDataStream &stream, quint16 value)
{
    stream << value;
}

void writeU32(QDataStream &stream, quint32 value)
{
    stream << value;
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

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QObject::tr("无法创建皮肤包：%1").arg(file.errorString());
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
                *error = QObject::tr("皮肤包中包含无效文件。");
            }
            return false;
        }
        CentralEntry entry;
        entry.name = name;
        entry.crc = crc32Bytes(bytes);
        entry.size = static_cast<quint32>(bytes.size());
        entry.offset = static_cast<quint32>(file.pos());
        central.append(entry);

        writeU32(stream, 0x04034b50);
        writeU16(stream, 20);
        writeU16(stream, 0x0800);
        writeU16(stream, 0);
        writeU16(stream, 0);
        writeU16(stream, 0);
        writeU32(stream, entry.crc);
        writeU32(stream, entry.size);
        writeU32(stream, entry.size);
        writeU16(stream, static_cast<quint16>(name.size()));
        writeU16(stream, 0);
        file.write(name);
        file.write(bytes);
    }

    const quint32 centralOffset = static_cast<quint32>(file.pos());
    for (const CentralEntry &entry : std::as_const(central)) {
        writeU32(stream, 0x02014b50);
        writeU16(stream, 20);
        writeU16(stream, 20);
        writeU16(stream, 0x0800);
        writeU16(stream, 0);
        writeU16(stream, 0);
        writeU16(stream, 0);
        writeU32(stream, entry.crc);
        writeU32(stream, entry.size);
        writeU32(stream, entry.size);
        writeU16(stream, static_cast<quint16>(entry.name.size()));
        writeU16(stream, 0);
        writeU16(stream, 0);
        writeU16(stream, 0);
        writeU16(stream, 0);
        writeU32(stream, 0);
        writeU32(stream, entry.offset);
        file.write(entry.name);
    }
    const quint32 centralSize = static_cast<quint32>(file.pos()) - centralOffset;
    writeU32(stream, 0x06054b50);
    writeU16(stream, 0);
    writeU16(stream, 0);
    writeU16(stream, static_cast<quint16>(central.size()));
    writeU16(stream, static_cast<quint16>(central.size()));
    writeU32(stream, centralSize);
    writeU32(stream, centralOffset);
    writeU16(stream, 0);

    if (!file.commit()) {
        if (error) {
            *error = QObject::tr("无法保存皮肤包：%1").arg(file.errorString());
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
            *error = QObject::tr("无法读取皮肤包：%1").arg(file.errorString());
        }
        return false;
    }
    if (file.size() > kMaximumPackageBytes) {
        if (error) {
            *error = QObject::tr("皮肤包超过 256 MB 限制。");
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
            *error = QObject::tr("皮肤包不是有效的 ZIP 文件。");
        }
        return false;
    }
    const quint16 count = readU16(data, eocd + 10);
    const quint32 centralSize = readU32(data, eocd + 12);
    const quint32 centralOffset = readU32(data, eocd + 16);
    if (count > kMaximumPackageEntries
        || static_cast<quint64>(centralOffset) + centralSize > static_cast<quint64>(data.size())) {
        if (error) {
            *error = QObject::tr("皮肤包目录无效。");
        }
        return false;
    }

    qsizetype cursor = centralOffset;
    qsizetype totalBytes = 0;
    for (quint16 index = 0; index < count; ++index) {
        if (readU32(data, cursor) != 0x02014b50 || cursor + 46 > data.size()) {
            if (error) {
                *error = QObject::tr("皮肤包目录项损坏。");
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
        if ((flags & 0x0001) != 0 || method != 0 || compressedSize != uncompressedSize
            || cursor + 46 + nameLength + extraLength + commentLength > data.size()) {
            if (error) {
                *error = QObject::tr("皮肤包使用了不支持的加密或压缩方式。");
            }
            return false;
        }
        const QString name = QString::fromUtf8(data.mid(cursor + 46, nameLength));
        if (!safeArchivePath(name)
            || readU32(data, localOffset) != 0x04034b50
            || localOffset + 30 > static_cast<quint32>(data.size())) {
            if (error) {
                *error = QObject::tr("皮肤包包含不安全的文件路径。");
            }
            return false;
        }
        const quint16 localNameLength = readU16(data, localOffset + 26);
        const quint16 localExtraLength = readU16(data, localOffset + 28);
        const quint64 contentOffset = static_cast<quint64>(localOffset) + 30
            + localNameLength + localExtraLength;
        if (contentOffset + uncompressedSize > static_cast<quint64>(data.size())) {
            if (error) {
                *error = QObject::tr("皮肤包文件内容不完整。");
            }
            return false;
        }
        const QByteArray bytes = data.mid(static_cast<qsizetype>(contentOffset),
                                          static_cast<qsizetype>(uncompressedSize));
        totalBytes += bytes.size();
        if (totalBytes > kMaximumPackageBytes || crc32Bytes(bytes) != crc) {
            if (error) {
                *error = QObject::tr("皮肤包校验失败。");
            }
            return false;
        }
        files->insert(QDir::fromNativeSeparators(name), bytes);
        cursor += 46 + nameLength + extraLength + commentLength;
    }
    return true;
}

QJsonObject setNestedValue(QJsonObject object,
                           const QStringList &parts,
                           const QJsonValue &value,
                           int index = 0)
{
    if (index >= parts.size()) {
        return object;
    }
    if (index == parts.size() - 1) {
        object.insert(parts.at(index), value);
        return object;
    }
    QJsonObject child = objectValue(object, parts.at(index));
    child = setNestedValue(child, parts, value, index + 1);
    object.insert(parts.at(index), child);
    return object;
}

} // namespace

SkinManager::SkinManager(ApplicationSettings *settings,
                         const QString &storageDirectory,
                         QObject *parent)
    : QObject(parent),
      m_settings(settings),
      m_storageDirectory(storageDirectory.isEmpty()
                             ? PortablePaths::skinsDirectory()
                             : QDir::cleanPath(storageDirectory))
{
    load();
    connect(m_settings, &ApplicationSettings::appearanceChanged, this, [this] {
        if (!m_entries.isEmpty() && m_entries.first().id == QString::fromLatin1(kBuiltinId)) {
            m_entries.first().document = defaultSkinDocument(m_settings);
        }
        emit appearanceChanged();
    });
}

QVariantList SkinManager::skins() const
{
    QVariantList result;
    for (const SkinEntry &entry : m_entries) {
        result.append(QVariantMap{
            {QStringLiteral("id"), entry.id},
            {QStringLiteral("name"), entry.name},
            {QStringLiteral("builtin"), entry.builtin},
            {QStringLiteral("active"), entry.id == activeSkinId()}
        });
    }
    return result;
}

QString SkinManager::activeSkinId() const
{
    const SkinEntry *entry = activeEntry();
    return entry ? entry->id : QString::fromLatin1(kBuiltinId);
}

QString SkinManager::activeSkinName() const
{
    const SkinEntry *entry = activeEntry();
    return entry ? entry->name : tr("默认皮肤");
}

bool SkinManager::activeSkinBuiltin() const
{
    const SkinEntry *entry = activeEntry();
    return !entry || entry->builtin;
}

QVariantMap SkinManager::effectiveAppearance() const
{
    const QJsonObject document = currentDocument();
    const QJsonObject appearance = objectValue(document, QStringLiteral("appearance"));
    QJsonObject result = appearance;
    const QString variantName = m_settings && m_settings->effectiveDark()
        ? QStringLiteral("dark")
        : QStringLiteral("light");
    const QJsonObject variant = objectValue(objectValue(appearance, QStringLiteral("variants")),
                                            variantName);
    for (auto it = variant.begin(); it != variant.end(); ++it) {
        result.insert(it.key(), it.value());
    }
    const QJsonObject background = objectValue(appearance, QStringLiteral("background"));
    const QString asset = background.value(QStringLiteral("asset")).toString();
    result.insert(QStringLiteral("background"), background);
    result.insert(QStringLiteral("backgroundSource"), assetUrl(asset));
    result.insert(QStringLiteral("skinId"), activeSkinId());
    return result.toVariantMap();
}

QVariantList SkinManager::homeItems() const
{
    const QJsonArray items = objectValue(currentDocument(), QStringLiteral("home"))
                                 .value(QStringLiteral("items")).toArray();
    QVariantList result;
    result.reserve(items.size());
    for (const QJsonValue &item : items) {
        result.append(item.toObject().toVariantMap());
    }
    return result;
}

bool SkinManager::editing() const { return m_editing; }
bool SkinManager::canUndo() const { return m_editing && !m_undo.isEmpty(); }
bool SkinManager::canRedo() const { return m_editing && !m_redo.isEmpty(); }
QString SkinManager::lastError() const { return m_lastError; }

QString SkinManager::createSkin(const QString &name)
{
    SkinEntry entry;
    entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.name = uniqueName(safeName(name, tr("新皮肤")));
    entry.directory = skinDirectory(entry.id);
    entry.document = defaultSkinDocument(m_settings);
    entry.document.insert(QStringLiteral("id"), entry.id);
    entry.document.insert(QStringLiteral("name"), entry.name);
    if (!saveEntry(entry)) {
        return {};
    }
    m_entries.append(entry);
    m_activeIndex = m_entries.size() - 1;
    saveIndex();
    emit skinsChanged();
    emit activeSkinChanged();
    emit appearanceChanged();
    emit homeItemsChanged();
    return entry.id;
}

QString SkinManager::duplicateActiveSkin(const QString &name)
{
    const SkinEntry *source = activeEntry();
    if (!source) {
        return createSkin(name);
    }
    SkinEntry entry;
    entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.name = uniqueName(safeName(name, tr("%1 副本").arg(source->name)));
    entry.directory = skinDirectory(entry.id);
    entry.document = currentDocument();
    entry.document.insert(QStringLiteral("id"), entry.id);
    entry.document.insert(QStringLiteral("name"), entry.name);
    if (!QDir().mkpath(QDir(entry.directory).filePath(QStringLiteral("assets")))) {
        setLastError(tr("无法创建皮肤资源目录。"));
        return {};
    }
    if (!source->builtin) {
        const QDir sourceAssets(QDir(source->directory).filePath(QStringLiteral("assets")));
        for (const QFileInfo &file : sourceAssets.entryInfoList(QDir::Files)) {
            QFile::copy(file.absoluteFilePath(),
                        QDir(entry.directory).filePath(QStringLiteral("assets/%1").arg(file.fileName())));
        }
    }
    if (!saveEntry(entry)) {
        return {};
    }
    m_entries.append(entry);
    m_activeIndex = m_entries.size() - 1;
    saveIndex();
    emit skinsChanged();
    emit activeSkinChanged();
    emit appearanceChanged();
    emit homeItemsChanged();
    return entry.id;
}

bool SkinManager::renameActiveSkin(const QString &name)
{
    SkinEntry *entry = activeEntry();
    if (!entry || entry->builtin) {
        setLastError(tr("内置皮肤不能重命名。"));
        return false;
    }
    const QString resolved = uniqueName(safeName(name, entry->name));
    if (resolved == entry->name) {
        return true;
    }
    entry->name = resolved;
    entry->document.insert(QStringLiteral("name"), resolved);
    if (!saveEntry(*entry)) {
        return false;
    }
    emit skinsChanged();
    emit activeSkinChanged();
    return true;
}

bool SkinManager::removeSkin(const QString &id)
{
    const QString targetId = id.isEmpty() ? activeSkinId() : id;
    int index = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == targetId) {
            index = i;
            break;
        }
    }
    if (index <= 0 || m_entries.at(index).builtin) {
        setLastError(tr("内置皮肤不能删除。"));
        return false;
    }
    const QString directory = m_entries.at(index).directory;
    if (!QDir(directory).removeRecursively()) {
        setLastError(tr("无法删除皮肤目录：%1").arg(QDir::toNativeSeparators(directory)));
        return false;
    }
    m_entries.removeAt(index);
    m_activeIndex = 0;
    saveIndex();
    emit skinsChanged();
    emit activeSkinChanged();
    emit appearanceChanged();
    emit homeItemsChanged();
    return true;
}

bool SkinManager::selectSkin(const QString &id)
{
    for (int index = 0; index < m_entries.size(); ++index) {
        if (m_entries.at(index).id == id) {
            if (index == m_activeIndex) {
                return true;
            }
            if (m_editing) {
                cancelEdit();
            }
            m_activeIndex = index;
            saveIndex();
            emit skinsChanged();
            emit activeSkinChanged();
            emit appearanceChanged();
            emit homeItemsChanged();
            return true;
        }
    }
    setLastError(tr("找不到指定皮肤。"));
    return false;
}

bool SkinManager::importSkin(const QUrl &source)
{
    QHash<QString, QByteArray> files;
    QString error;
    if (!readStoreZip(cleanLocalPath(source), &files, &error)) {
        setLastError(error);
        return false;
    }
    if (!files.contains(QStringLiteral("skin.json"))) {
        setLastError(tr("皮肤包缺少 skin.json。"));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(files.value(QStringLiteral("skin.json")),
                                                         &parseError);
    if (!parsed.isObject()) {
        setLastError(tr("皮肤清单无效：%1").arg(parseError.errorString()));
        return false;
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString validationError;
    QJsonObject document = normalizedDocument(parsed.object(), id, tr("导入的皮肤"),
                                              &validationError);
    if (!validationError.isEmpty()) {
        setLastError(validationError);
        return false;
    }
    SkinEntry entry;
    entry.id = id;
    entry.name = uniqueName(document.value(QStringLiteral("name")).toString(tr("导入的皮肤")));
    entry.directory = skinDirectory(id);
    document.insert(QStringLiteral("id"), id);
    document.insert(QStringLiteral("name"), entry.name);
    entry.document = document;

    const QJsonObject appearance = objectValue(document, QStringLiteral("appearance"));
    QSet<QString> requiredAssets;
    const QString backgroundAsset =
        objectValue(appearance, QStringLiteral("background")).value(QStringLiteral("asset")).toString();
    if (!backgroundAsset.isEmpty()) {
        requiredAssets.insert(backgroundAsset);
    }
    const QJsonArray items = objectValue(document, QStringLiteral("home"))
                                 .value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QString asset =
            objectValue(value.toObject(), QStringLiteral("properties"))
                .value(QStringLiteral("asset")).toString();
        if (!asset.isEmpty()) {
            requiredAssets.insert(asset);
        }
    }
    for (const QString &asset : std::as_const(requiredAssets)) {
        if (!safeArchivePath(asset) || !files.contains(asset)) {
            setLastError(tr("皮肤包缺少资源：%1").arg(asset));
            return false;
        }
    }

    if (!QDir().mkpath(QDir(entry.directory).filePath(QStringLiteral("assets")))) {
        setLastError(tr("无法创建导入皮肤目录。"));
        return false;
    }
    for (const QString &asset : std::as_const(requiredAssets)) {
        const QString destination = QDir(entry.directory).filePath(asset);
        if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
            QDir(entry.directory).removeRecursively();
            setLastError(tr("无法创建皮肤资源目录。"));
            return false;
        }
        QSaveFile output(destination);
        if (!output.open(QIODevice::WriteOnly)
            || output.write(files.value(asset)) != files.value(asset).size()
            || !output.commit()) {
            QDir(entry.directory).removeRecursively();
            setLastError(tr("无法写入皮肤资源：%1").arg(asset));
            return false;
        }
    }
    if (!saveEntry(entry)) {
        QDir(entry.directory).removeRecursively();
        return false;
    }
    m_entries.append(entry);
    m_activeIndex = m_entries.size() - 1;
    saveIndex();
    emit skinsChanged();
    emit activeSkinChanged();
    emit appearanceChanged();
    emit homeItemsChanged();
    return true;
}

bool SkinManager::exportSkin(const QUrl &destination)
{
    const SkinEntry *entry = activeEntry();
    if (!entry) {
        return false;
    }
    QList<QPair<QString, QByteArray>> files;
    files.append({QStringLiteral("skin.json"),
                  QJsonDocument(currentDocument()).toJson(QJsonDocument::Indented)});
    if (!entry->builtin) {
        const QDir assets(QDir(entry->directory).filePath(QStringLiteral("assets")));
        for (const QFileInfo &file : assets.entryInfoList(QDir::Files)) {
            QFile input(file.absoluteFilePath());
            if (!input.open(QIODevice::ReadOnly)) {
                setLastError(tr("无法读取皮肤资源：%1").arg(file.fileName()));
                return false;
            }
            files.append({QStringLiteral("assets/%1").arg(file.fileName()), input.readAll()});
        }
    }
    QString path = cleanLocalPath(destination);
    if (!path.endsWith(QStringLiteral(".minifoxskin"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".minifoxskin");
    }
    QString error;
    if (!writeStoreZip(path, files, &error)) {
        setLastError(error);
        return false;
    }
    return true;
}

QString SkinManager::importImage(const QUrl &source)
{
    if (!ensureWritableSkin()) {
        return {};
    }
    const QString reference = copyAssetIntoSkin(cleanLocalPath(source));
    if (!reference.isEmpty()) {
        emit skinsChanged();
    }
    return reference;
}

bool SkinManager::setBackgroundImage(const QUrl &source)
{
    if (!ensureWritableSkin()) {
        return false;
    }
    const QString reference = copyAssetIntoSkin(cleanLocalPath(source));
    if (reference.isEmpty()) {
        return false;
    }

    SkinEntry *entry = activeEntry();
    QJsonObject document = entry->document;
    QJsonObject appearance = objectValue(document, QStringLiteral("appearance"));
    QJsonObject background = objectValue(appearance, QStringLiteral("background"));
    const bool firstBackground = background.value(QStringLiteral("asset")).toString().isEmpty();
    background.insert(QStringLiteral("asset"), reference);
    background.insert(QStringLiteral("opacity"), 1.0);
    appearance.insert(QStringLiteral("background"), background);
    if (firstBackground) {
        QJsonObject materials = objectValue(appearance, QStringLiteral("materials"));
        if (materials.value(QStringLiteral("pageOpacity")).toDouble(0.94) >= 0.90) {
            materials.insert(QStringLiteral("pageOpacity"), 0.62);
        }
        if (materials.value(QStringLiteral("panelOpacity")).toDouble(0.94) >= 0.90) {
            materials.insert(QStringLiteral("panelOpacity"), 0.84);
        }
        if (materials.value(QStringLiteral("sidebarOpacity")).toDouble(0.96) >= 0.94) {
            materials.insert(QStringLiteral("sidebarOpacity"), 0.88);
        }
        if (materials.value(QStringLiteral("titleBarOpacity")).toDouble(0.98) >= 0.96) {
            materials.insert(QStringLiteral("titleBarOpacity"), 0.90);
        }
        appearance.insert(QStringLiteral("materials"), materials);
    }
    document.insert(QStringLiteral("appearance"), appearance);
    entry->document = document;
    if (!saveEntry(*entry)) {
        return false;
    }

    setLastError({});
    emit skinsChanged();
    emit appearanceChanged();
    return true;
}

QString SkinManager::assetUrl(const QString &assetReference) const
{
    if (assetReference.isEmpty()) {
        return {};
    }
    const SkinEntry *entry = activeEntry();
    if (!entry || entry->builtin) {
        return {};
    }
    const QString path = resolveAssetPath(*entry, assetReference);
    return QFileInfo::exists(path) ? QUrl::fromLocalFile(path).toString() : QString{};
}

void SkinManager::setAppearanceValue(const QString &key, const QVariant &value)
{
    if (key.trimmed().isEmpty() || !ensureWritableSkin()) {
        return;
    }
    SkinEntry *entry = activeEntry();
    QJsonObject document = entry->document;
    QJsonObject appearance = objectValue(document, QStringLiteral("appearance"));
    appearance = setNestedValue(appearance, key.split(QLatin1Char('.'), Qt::SkipEmptyParts),
                                QJsonValue::fromVariant(value));
    document.insert(QStringLiteral("appearance"), appearance);
    entry->document = document;
    saveEntry(*entry);
    emit appearanceChanged();
}

void SkinManager::beginEdit()
{
    if (m_editing) {
        return;
    }
    if (!ensureWritableSkin()) {
        return;
    }
    m_editing = true;
    m_draft = activeEntry()->document;
    m_undo.clear();
    m_redo.clear();
    emit editingChanged();
    emit historyChanged();
    emit homeItemsChanged();
}

QString SkinManager::addItem(const QString &type)
{
    if (!m_editing || !isSupportedItemType(type)) {
        return {};
    }
    pushHistory();
    QJsonObject home = objectValue(m_draft, QStringLiteral("home"));
    QJsonArray items = home.value(QStringLiteral("items")).toArray();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject item{
        {QStringLiteral("id"), id},
        {QStringLiteral("type"), type},
        {QStringLiteral("x"), 0.12},
        {QStringLiteral("y"), 0.12},
        {QStringLiteral("w"), type == QStringLiteral("text") ? 0.34 : 0.42},
        {QStringLiteral("h"), type == QStringLiteral("panel") ? 0.20 : 0.32},
        {QStringLiteral("z"), items.size() + 10},
        {QStringLiteral("opacity"), 1.0},
        {QStringLiteral("properties"), QJsonObject{}}
    };
    if (type == QStringLiteral("text")) {
        item.insert(QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("title"), tr("公告")},
            {QStringLiteral("text"), tr("双击或在属性栏中编辑文字。")}
        });
    } else if (type == QStringLiteral("image")) {
        item.insert(QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("fillMode"), QStringLiteral("cover")},
            {QStringLiteral("focusX"), 0.5},
            {QStringLiteral("focusY"), 0.5},
            {QStringLiteral("zoom"), 1.0}
        });
    } else if (type == QStringLiteral("folders")) {
        item.insert(QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("folders"), defaultHomeFolders()}
        });
    }
    items.append(normalizedItem(item));
    home.insert(QStringLiteral("items"), items);
    m_draft.insert(QStringLiteral("home"), home);
    emit homeItemsChanged();
    return id;
}

bool SkinManager::duplicateItem(const QString &id)
{
    if (!m_editing) {
        return false;
    }
    QJsonObject home = objectValue(m_draft, QStringLiteral("home"));
    QJsonArray items = home.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : std::as_const(items)) {
        QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("id")).toString() != id
            || item.value(QStringLiteral("type")).toString() == QStringLiteral("launch")) {
            continue;
        }
        pushHistory();
        item.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        item.insert(QStringLiteral("x"), qMin(0.92, item.value(QStringLiteral("x")).toDouble() + 0.025));
        item.insert(QStringLiteral("y"), qMin(0.92, item.value(QStringLiteral("y")).toDouble() + 0.025));
        item.insert(QStringLiteral("z"), items.size() + 10);
        items.append(normalizedItem(item));
        home.insert(QStringLiteral("items"), items);
        m_draft.insert(QStringLiteral("home"), home);
        emit homeItemsChanged();
        return true;
    }
    return false;
}

bool SkinManager::updateItem(const QString &id, const QVariantMap &changes)
{
    if (!m_editing || id.isEmpty()) {
        return false;
    }
    QJsonObject home = objectValue(m_draft, QStringLiteral("home"));
    QJsonArray items = home.value(QStringLiteral("items")).toArray();
    for (qsizetype index = 0; index < items.size(); ++index) {
        QJsonObject item = items.at(index).toObject();
        if (item.value(QStringLiteral("id")).toString() != id) {
            continue;
        }
        pushHistory();
        for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
            item.insert(it.key(), QJsonValue::fromVariant(it.value()));
        }
        items.replace(index, normalizedItem(item));
        home.insert(QStringLiteral("items"), items);
        m_draft.insert(QStringLiteral("home"), home);
        emit homeItemsChanged();
        return true;
    }
    return false;
}

bool SkinManager::removeItem(const QString &id)
{
    if (!m_editing) {
        return false;
    }
    QJsonObject home = objectValue(m_draft, QStringLiteral("home"));
    QJsonArray items = home.value(QStringLiteral("items")).toArray();
    for (qsizetype index = 0; index < items.size(); ++index) {
        const QJsonObject item = items.at(index).toObject();
        if (item.value(QStringLiteral("id")).toString() != id) {
            continue;
        }
        if (item.value(QStringLiteral("type")).toString() == QStringLiteral("launch")) {
            setLastError(tr("启动卡是首页必需组件，不能删除。"));
            return false;
        }
        pushHistory();
        items.removeAt(index);
        home.insert(QStringLiteral("items"), items);
        m_draft.insert(QStringLiteral("home"), home);
        emit homeItemsChanged();
        return true;
    }
    return false;
}

bool SkinManager::moveItemLayer(const QString &id, int direction)
{
    if (!m_editing || direction == 0) {
        return false;
    }
    QJsonObject home = objectValue(m_draft, QStringLiteral("home"));
    QJsonArray items = home.value(QStringLiteral("items")).toArray();
    for (qsizetype index = 0; index < items.size(); ++index) {
        QJsonObject item = items.at(index).toObject();
        if (item.value(QStringLiteral("id")).toString() == id) {
            pushHistory();
            item.insert(QStringLiteral("z"),
                        item.value(QStringLiteral("z")).toInt() + (direction > 0 ? 1 : -1));
            items.replace(index, normalizedItem(item));
            home.insert(QStringLiteral("items"), items);
            m_draft.insert(QStringLiteral("home"), home);
            emit homeItemsChanged();
            return true;
        }
    }
    return false;
}

void SkinManager::undo()
{
    if (!canUndo()) {
        return;
    }
    m_redo.append(m_draft);
    m_draft = m_undo.takeLast();
    emit homeItemsChanged();
    emit historyChanged();
}

void SkinManager::redo()
{
    if (!canRedo()) {
        return;
    }
    m_undo.append(m_draft);
    m_draft = m_redo.takeLast();
    emit homeItemsChanged();
    emit historyChanged();
}

bool SkinManager::commitEdit()
{
    if (!m_editing) {
        return true;
    }
    SkinEntry *entry = activeEntry();
    entry->document = m_draft;
    if (!saveEntry(*entry)) {
        return false;
    }
    m_editing = false;
    m_draft = {};
    m_undo.clear();
    m_redo.clear();
    emit editingChanged();
    emit historyChanged();
    emit skinsChanged();
    emit appearanceChanged();
    emit homeItemsChanged();
    return true;
}

void SkinManager::cancelEdit()
{
    if (!m_editing) {
        return;
    }
    m_editing = false;
    m_draft = {};
    m_undo.clear();
    m_redo.clear();
    emit editingChanged();
    emit historyChanged();
    emit homeItemsChanged();
}

void SkinManager::resetHomeLayout()
{
    if (!m_editing) {
        return;
    }
    pushHistory();
    m_draft.insert(QStringLiteral("home"), defaultHome());
    emit homeItemsChanged();
}

QJsonObject SkinManager::defaultSkinDocument(ApplicationSettings *settings)
{
    const QString font = settings ? settings->effectiveFontFamily() : QStringLiteral("Segoe UI");
    const double fontSize = settings ? settings->fontPointSize() : 11.0;
    const QString accent = settings ? settings->effectiveAccentColor() : QStringLiteral("#0067c0");
    return QJsonObject{
        {QStringLiteral("schemaVersion"), kSkinSchemaVersion},
        {QStringLiteral("id"), QString::fromLatin1(kBuiltinId)},
        {QStringLiteral("name"), tr("默认皮肤")},
        {QStringLiteral("appearance"), QJsonObject{
            {QStringLiteral("fontFamily"), font},
            {QStringLiteral("fontSize"), fontSize},
            {QStringLiteral("radius"), 14},
            {QStringLiteral("radiusLarge"), 20},
            {QStringLiteral("materials"), QJsonObject{
                {QStringLiteral("pageOpacity"), 0.94},
                {QStringLiteral("panelOpacity"), 0.94},
                {QStringLiteral("sidebarOpacity"), 0.96},
                {QStringLiteral("titleBarOpacity"), 0.98}
            }},
            {QStringLiteral("background"), QJsonObject{
                {QStringLiteral("asset"), QString{}},
                {QStringLiteral("opacity"), 1.0},
                {QStringLiteral("fillMode"), QStringLiteral("cover")},
                {QStringLiteral("focusX"), 0.5},
                {QStringLiteral("focusY"), 0.5},
                {QStringLiteral("zoom"), 1.0},
                {QStringLiteral("overlay"), QStringLiteral("#00000000")}
            }},
            {QStringLiteral("variants"), QJsonObject{
                {QStringLiteral("light"), QJsonObject{
                    {QStringLiteral("accent"), accent},
                    {QStringLiteral("surface"), QStringLiteral("#f3f3f6")},
                    {QStringLiteral("surfaceRaised"), QStringLiteral("#ffffff")},
                    {QStringLiteral("surfaceSubtle"), QStringLiteral("#eaeaef")},
                    {QStringLiteral("foreground"), QStringLiteral("#1b1b1b")},
                    {QStringLiteral("foregroundSecondary"), QStringLiteral("#5d5d5d")},
                    {QStringLiteral("outline"), QStringLiteral("#d1d1d6")}
                }},
                {QStringLiteral("dark"), QJsonObject{
                    {QStringLiteral("accent"), accent},
                    {QStringLiteral("surface"), QStringLiteral("#151517")},
                    {QStringLiteral("surfaceRaised"), QStringLiteral("#242427")},
                    {QStringLiteral("surfaceSubtle"), QStringLiteral("#1d1d20")},
                    {QStringLiteral("foreground"), QStringLiteral("#ffffff")},
                    {QStringLiteral("foregroundSecondary"), QStringLiteral("#c5c5c5")},
                    {QStringLiteral("outline"), QStringLiteral("#45454a")}
                }}
            }}
        }},
        {QStringLiteral("home"), defaultHome()}
    };
}

QJsonObject SkinManager::defaultHome()
{
    return QJsonObject{
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{
                {QStringLiteral("id"), QStringLiteral("launch-card")},
                {QStringLiteral("type"), QStringLiteral("launch")},
                {QStringLiteral("x"), 0.12},
                {QStringLiteral("y"), 0.16},
                {QStringLiteral("w"), 0.76},
                {QStringLiteral("h"), 0.68},
                {QStringLiteral("z"), 10},
                {QStringLiteral("opacity"), 1.0},
                {QStringLiteral("properties"), QJsonObject{
                    {QStringLiteral("detailed"), true}
                }}
            }
        }}
    };
}

QJsonObject SkinManager::normalizedDocument(const QJsonObject &document,
                                            const QString &fallbackId,
                                            const QString &fallbackName,
                                            QString *error)
{
    const int sourceSchemaVersion =
        document.value(QStringLiteral("schemaVersion")).toInt(1);
    if (sourceSchemaVersion > kSkinSchemaVersion) {
        if (error) {
            *error = tr("皮肤版本过新，当前启动器无法读取。");
        }
        return {};
    }
    QJsonObject result = document;
    result.insert(QStringLiteral("schemaVersion"), kSkinSchemaVersion);
    result.insert(QStringLiteral("id"),
                  safeName(document.value(QStringLiteral("id")).toString(), fallbackId));
    result.insert(QStringLiteral("name"),
                  safeName(document.value(QStringLiteral("name")).toString(), fallbackName));
    if (!result.value(QStringLiteral("appearance")).isObject()) {
        result.insert(QStringLiteral("appearance"), QJsonObject{});
    }
    if (sourceSchemaVersion < 2) {
        QJsonObject appearance = objectValue(result, QStringLiteral("appearance"));
        QJsonObject background =
            objectValue(appearance, QStringLiteral("background"));
        QJsonObject materials = objectValue(appearance, QStringLiteral("materials"));
        const auto matchesLegacyValue = [&materials](const QString &key, double expected) {
            return qAbs(materials.value(key).toDouble(expected) - expected) < 0.0001;
        };
        const bool hasBackground =
            !background.value(QStringLiteral("asset")).toString().isEmpty();
        if (hasBackground) {
            background.insert(QStringLiteral("opacity"), 1.0);
            appearance.insert(QStringLiteral("background"), background);
            result.insert(QStringLiteral("appearance"), appearance);
        }
        const bool usesLegacyMaterialDefaults =
            matchesLegacyValue(QStringLiteral("pageOpacity"), 0.94)
            && matchesLegacyValue(QStringLiteral("panelOpacity"), 0.94)
            && matchesLegacyValue(QStringLiteral("sidebarOpacity"), 0.96)
            && matchesLegacyValue(QStringLiteral("titleBarOpacity"), 0.98);
        if (hasBackground && usesLegacyMaterialDefaults) {
            materials.insert(QStringLiteral("pageOpacity"), 0.62);
            materials.insert(QStringLiteral("panelOpacity"), 0.84);
            materials.insert(QStringLiteral("sidebarOpacity"), 0.88);
            materials.insert(QStringLiteral("titleBarOpacity"), 0.90);
            appearance.insert(QStringLiteral("materials"), materials);
            result.insert(QStringLiteral("appearance"), appearance);
        }
    }
    QJsonObject home = objectValue(result, QStringLiteral("home"));
    QJsonArray normalized;
    bool hasLaunch = false;
    for (const QJsonValue &value : home.value(QStringLiteral("items")).toArray()) {
        const QJsonObject item = normalizedItem(value.toObject());
        if (!isSupportedItemType(item.value(QStringLiteral("type")).toString())) {
            continue;
        }
        hasLaunch |= item.value(QStringLiteral("type")).toString() == QStringLiteral("launch");
        normalized.append(item);
    }
    if (!hasLaunch) {
        normalized.prepend(defaultHome().value(QStringLiteral("items")).toArray().first());
    }
    home.insert(QStringLiteral("items"), normalized);
    result.insert(QStringLiteral("home"), home);
    return result;
}

QJsonObject SkinManager::normalizedItem(const QJsonObject &item)
{
    QJsonObject result = item;
    result.insert(QStringLiteral("id"),
                  safeName(item.value(QStringLiteral("id")).toString(),
                           QUuid::createUuid().toString(QUuid::WithoutBraces)));
    result.insert(QStringLiteral("type"),
                  safeName(item.value(QStringLiteral("type")).toString(),
                           QStringLiteral("panel")));
    double width = boundedNumber(item.value(QStringLiteral("w")), 0.4, 0.08, 1.0);
    double height = boundedNumber(item.value(QStringLiteral("h")), 0.3, 0.08, 1.0);
    double x = boundedNumber(item.value(QStringLiteral("x")), 0.1, 0.0, 1.0);
    double y = boundedNumber(item.value(QStringLiteral("y")), 0.1, 0.0, 1.0);
    x = qMin(x, 1.0 - width);
    y = qMin(y, 1.0 - height);
    result.insert(QStringLiteral("x"), x);
    result.insert(QStringLiteral("y"), y);
    result.insert(QStringLiteral("w"), width);
    result.insert(QStringLiteral("h"), height);
    result.insert(QStringLiteral("z"), qBound(-100, item.value(QStringLiteral("z")).toInt(10), 1000));
    result.insert(QStringLiteral("opacity"),
                  boundedNumber(item.value(QStringLiteral("opacity")), 1.0, 0.05, 1.0));
    if (!result.value(QStringLiteral("properties")).isObject()) {
        result.insert(QStringLiteral("properties"), QJsonObject{});
    }
    if (result.value(QStringLiteral("type")).toString() == QStringLiteral("image")) {
        QJsonObject properties = result.value(QStringLiteral("properties")).toObject();
        static const QSet<QString> fillModes{
            QStringLiteral("cover"),
            QStringLiteral("contain"),
            QStringLiteral("stretch"),
            QStringLiteral("tile")
        };
        QString fillMode = properties.value(QStringLiteral("fillMode")).toString();
        if (!fillModes.contains(fillMode)) {
            fillMode = QStringLiteral("cover");
        }
        properties.insert(QStringLiteral("fillMode"), fillMode);
        properties.insert(QStringLiteral("focusX"),
                          boundedNumber(properties.value(QStringLiteral("focusX")),
                                        0.5, 0.0, 1.0));
        properties.insert(QStringLiteral("focusY"),
                          boundedNumber(properties.value(QStringLiteral("focusY")),
                                        0.5, 0.0, 1.0));
        properties.insert(QStringLiteral("zoom"),
                          boundedNumber(properties.value(QStringLiteral("zoom")),
                                        1.0, 1.0, 4.0));
        result.insert(QStringLiteral("properties"), properties);
    } else if (result.value(QStringLiteral("type")).toString()
               == QStringLiteral("folders")) {
        QJsonObject properties = result.value(QStringLiteral("properties")).toObject();
        QJsonArray folders = properties.value(QStringLiteral("folders")).toArray();
        if (folders.isEmpty()) {
            folders = defaultHomeFolders();
        }

        QJsonArray normalizedFolders;
        const qsizetype count = qMin(folders.size(), kMaximumHomeFolders);
        for (qsizetype index = 0; index < count; ++index) {
            if (!folders.at(index).isObject()) {
                continue;
            }
            const QJsonObject source = folders.at(index).toObject();
            QJsonObject folder{
                {QStringLiteral("title"),
                 source.value(QStringLiteral("title")).toString().trimmed().left(64)},
                {QStringLiteral("path"),
                 source.value(QStringLiteral("path")).toString().trimmed().left(2048)}
            };
            const QString kind = source.value(QStringLiteral("kind")).toString();
            if (kind == QStringLiteral("root")
                || kind == QStringLiteral("custom_nodes")
                || kind == QStringLiteral("input")
                || kind == QStringLiteral("output")) {
                folder.insert(QStringLiteral("kind"), kind);
            }
            normalizedFolders.append(folder);
        }
        if (normalizedFolders.isEmpty()) {
            normalizedFolders.append(defaultHomeFolders().first());
        }
        properties.insert(QStringLiteral("folders"), normalizedFolders);
        result.insert(QStringLiteral("properties"), properties);
    }
    return result;
}

bool SkinManager::isSupportedItemType(const QString &type)
{
    static const QSet<QString> supported{
        QStringLiteral("launch"),
        QStringLiteral("image"),
        QStringLiteral("folders"),
        QStringLiteral("text"),
        QStringLiteral("version"),
        QStringLiteral("panel")
    };
    return supported.contains(type);
}

SkinManager::SkinEntry *SkinManager::activeEntry()
{
    return m_activeIndex >= 0 && m_activeIndex < m_entries.size()
        ? &m_entries[m_activeIndex]
        : nullptr;
}

const SkinManager::SkinEntry *SkinManager::activeEntry() const
{
    return m_activeIndex >= 0 && m_activeIndex < m_entries.size()
        ? &m_entries.at(m_activeIndex)
        : nullptr;
}

QJsonObject SkinManager::currentDocument() const
{
    const SkinEntry *entry = activeEntry();
    if (!entry) {
        return defaultSkinDocument(m_settings);
    }
    return m_editing ? m_draft : entry->document;
}

bool SkinManager::ensureWritableSkin()
{
    if (!activeSkinBuiltin()) {
        return true;
    }
    return !duplicateActiveSkin(tr("我的皮肤")).isEmpty();
}

bool SkinManager::load()
{
    m_entries.clear();
    SkinEntry builtin;
    builtin.id = QString::fromLatin1(kBuiltinId);
    builtin.name = tr("默认皮肤");
    builtin.builtin = true;
    builtin.document = defaultSkinDocument(m_settings);
    m_entries.append(builtin);
    m_activeIndex = 0;

    QDir().mkpath(m_storageDirectory);
    const QDir root(m_storageDirectory);
    for (const QFileInfo &directory : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        SkinEntry entry;
        if (loadCustomSkin(directory.absoluteFilePath(), &entry)) {
            m_entries.append(entry);
        }
    }

    QFile indexFile(root.filePath(QStringLiteral("index.json")));
    if (indexFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument document = QJsonDocument::fromJson(indexFile.readAll());
        indexFile.close();
        const QString active = document.object().value(QStringLiteral("activeSkinId")).toString();
        for (int index = 0; index < m_entries.size(); ++index) {
            if (m_entries.at(index).id == active) {
                m_activeIndex = index;
                break;
            }
        }
    }
    saveIndex();
    return true;
}

bool SkinManager::loadCustomSkin(const QString &directory, SkinEntry *entry)
{
    QFile file(QDir(directory).filePath(QStringLiteral("skin.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!parsed.isObject()) {
        setLastError(tr("已跳过损坏的皮肤：%1").arg(QFileInfo(directory).fileName()));
        return false;
    }
    const QString fallbackId = QFileInfo(directory).fileName();
    QString error;
    const QJsonObject sourceDocument = parsed.object();
    const bool needsMigration =
        sourceDocument.value(QStringLiteral("schemaVersion")).toInt(1)
        < kSkinSchemaVersion;
    const QJsonObject document = normalizedDocument(sourceDocument, fallbackId,
                                                    tr("未命名皮肤"), &error);
    if (!error.isEmpty()) {
        setLastError(error);
        return false;
    }
    entry->id = document.value(QStringLiteral("id")).toString(fallbackId);
    entry->name = document.value(QStringLiteral("name")).toString(tr("未命名皮肤"));
    entry->directory = directory;
    entry->document = document;
    file.close();
    if (needsMigration && !saveEntry(*entry)) {
        return false;
    }
    return true;
}

bool SkinManager::saveIndex()
{
    if (!QDir().mkpath(m_storageDirectory)) {
        setLastError(tr("无法创建皮肤目录：%1").arg(m_storageDirectory));
        return false;
    }
    QSaveFile file(QDir(m_storageDirectory).filePath(QStringLiteral("index.json")));
    if (!file.open(QIODevice::WriteOnly)) {
        setLastError(tr("无法保存皮肤索引：%1").arg(file.errorString()));
        return false;
    }
    const QJsonObject index{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("activeSkinId"), activeSkinId()}
    };
    file.write(QJsonDocument(index).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        setLastError(tr("无法提交皮肤索引：%1").arg(file.errorString()));
        return false;
    }
    setLastError({});
    return true;
}

bool SkinManager::saveEntry(SkinEntry &entry)
{
    if (entry.builtin) {
        return false;
    }
    if (!QDir().mkpath(QDir(entry.directory).filePath(QStringLiteral("assets")))) {
        setLastError(tr("无法创建皮肤目录：%1").arg(entry.directory));
        return false;
    }
    entry.document.insert(QStringLiteral("schemaVersion"), kSkinSchemaVersion);
    entry.document.insert(QStringLiteral("id"), entry.id);
    entry.document.insert(QStringLiteral("name"), entry.name);
    QSaveFile file(QDir(entry.directory).filePath(QStringLiteral("skin.json")));
    if (!file.open(QIODevice::WriteOnly)) {
        setLastError(tr("无法保存皮肤：%1").arg(file.errorString()));
        return false;
    }
    file.write(QJsonDocument(entry.document).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        setLastError(tr("无法提交皮肤：%1").arg(file.errorString()));
        return false;
    }
    return true;
}

void SkinManager::refreshAfterDocumentChange(bool appearance, bool home)
{
    emit skinsChanged();
    if (appearance) {
        emit appearanceChanged();
    }
    if (home) {
        emit homeItemsChanged();
    }
}

void SkinManager::pushHistory()
{
    if (!m_editing) {
        return;
    }
    m_undo.append(m_draft);
    if (m_undo.size() > 100) {
        m_undo.removeFirst();
    }
    m_redo.clear();
    emit historyChanged();
}

void SkinManager::setLastError(const QString &message)
{
    if (message == m_lastError) {
        return;
    }
    m_lastError = message;
    emit lastErrorChanged();
}

QString SkinManager::uniqueName(const QString &requested) const
{
    QSet<QString> names;
    for (const SkinEntry &entry : m_entries) {
        names.insert(entry.name.toCaseFolded());
    }
    QString candidate = requested;
    int suffix = 2;
    while (names.contains(candidate.toCaseFolded())) {
        candidate = tr("%1 (%2)").arg(requested).arg(suffix++);
    }
    return candidate;
}

QString SkinManager::skinDirectory(const QString &id) const
{
    return QDir(m_storageDirectory).filePath(id);
}

QString SkinManager::resolveAssetPath(const SkinEntry &entry, const QString &reference) const
{
    const QString normalized = QDir::fromNativeSeparators(reference);
    if (!safeArchivePath(normalized) || !normalized.startsWith(QStringLiteral("assets/"))) {
        return {};
    }
    const QString root = QDir::fromNativeSeparators(QDir(entry.directory).absolutePath());
    const QString candidate = QDir::fromNativeSeparators(
        QFileInfo(QDir(root).filePath(normalized)).absoluteFilePath());
    return candidate.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive)
        ? candidate
        : QString{};
}

QString SkinManager::copyAssetIntoSkin(const QString &sourcePath)
{
    SkinEntry *entry = activeEntry();
    if (!entry || entry->builtin) {
        return {};
    }
    QFileInfo source(sourcePath);
    if (!source.isFile()) {
        setLastError(tr("找不到图片文件。"));
        return {};
    }
    QString extension = source.suffix().toLower();
    if (extension == QStringLiteral("jpeg")) {
        extension = QStringLiteral("jpg");
    }
    static const QSet<QString> supported{
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("webp")
    };
    if (!supported.contains(extension)) {
        setLastError(tr("仅支持 PNG、JPEG 和 WebP 图片。"));
        return {};
    }
    QImageReader reader(source.absoluteFilePath());
    if (!reader.canRead() || reader.size().width() <= 0 || reader.size().height() <= 0
        || static_cast<qint64>(reader.size().width()) * reader.size().height() > 100000000) {
        setLastError(tr("图片损坏或像素尺寸超过限制。"));
        return {};
    }
    QFile input(source.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly)) {
        setLastError(tr("无法读取图片：%1").arg(input.errorString()));
        return {};
    }
    const QByteArray bytes = input.readAll();
    if (bytes.size() > 64 * 1024 * 1024) {
        setLastError(tr("单张图片不能超过 64 MB。"));
        return {};
    }
    const QString hash =
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    const QString reference = QStringLiteral("assets/%1.%2").arg(hash, extension);
    const QString destination = QDir(entry->directory).filePath(reference);
    if (QFileInfo::exists(destination)) {
        return reference;
    }
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        setLastError(tr("无法创建皮肤资源目录。"));
        return {};
    }
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(bytes) != bytes.size()
        || !output.commit()) {
        setLastError(tr("无法复制图片到皮肤目录。"));
        return {};
    }
    return reference;
}
