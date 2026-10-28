#include "Extensions.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QtConcurrentMap>

namespace extensions {
namespace {

constexpr int kCatalogEntryLimit = 20000;

QString tr(const char *text)
{
    return QCoreApplication::translate("extensions", text);
}

}

Extension inspect(const git::Tool &tool, const QString &path)
{
    Extension extension;
    extension.path = QDir::cleanPath(path);
    const QString folder = QFileInfo(path).fileName();
    extension.enabled = !folder.endsWith(kDisabledSuffix, Qt::CaseInsensitive);
    extension.name = extension.enabled ? folder : folder.chopped(kDisabledSuffix.size());
    // A folder without its own .git would report the enclosing ComfyUI repository.
    if (QFileInfo::exists(QDir(path).filePath(QStringLiteral(".git"))))
        extension.info = repository::inspect(tool, path);
    return extension;
}

QList<Extension> scan(const git::Tool &tool, const QString &customNodes)
{
    QStringList paths;
    const QDir directory(customNodes);
    for (const QFileInfo &entry : directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
        const QString name = entry.fileName();
        if (name.startsWith(QLatin1Char('.')) || name == QStringLiteral("__pycache__")) continue;
        paths.append(entry.absoluteFilePath());
    }
    QList<Extension> result = QtConcurrent::blockingMapped<QList<Extension>>(
        paths, [&tool](const QString &path) { return inspect(tool, path); });
    return result;
}

QString canonicalUrl(const QString &url)
{
    QString text = url.trimmed();
    // scp-like "git@host:owner/repo"
    static const QRegularExpression scp(QStringLiteral("^[\\w.-]+@([\\w.-]+):(.+)$"));
    const QRegularExpressionMatch match = scp.match(text);
    if (match.hasMatch()) {
        text = match.captured(1) + QLatin1Char('/') + match.captured(2);
    } else {
        const QUrl parsed(text);
        if (parsed.isValid() && !parsed.host().isEmpty()) text = parsed.host() + parsed.path();
    }
    while (text.endsWith(QLatin1Char('/'))) text.chop(1);
    if (text.endsWith(QStringLiteral(".git"), Qt::CaseInsensitive)) text.chop(4);
    if (text.startsWith(QStringLiteral("www."))) text.remove(0, 4);
    return text.toLower();
}

bool isCloneUrl(const QString &url)
{
    const QString text = url.trimmed();
    static const QRegularExpression scp(QStringLiteral("^[\\w.-]+@[\\w.-]+:[^\\s]+$"));
    if (scp.match(text).hasMatch()) return true;
    const QUrl parsed(text, QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();
    return parsed.isValid() && !parsed.host().isEmpty() && parsed.path().size() > 1
           && (scheme == QStringLiteral("https") || scheme == QStringLiteral("http") || scheme == QStringLiteral("ssh")
               || scheme == QStringLiteral("git"));
}

QString folderFor(const QString &url)
{
    if (!isCloneUrl(url)) return {};
    QString text = url.trimmed();
    while (text.endsWith(QLatin1Char('/'))) text.chop(1);
    if (text.endsWith(QStringLiteral(".git"), Qt::CaseInsensitive)) text.chop(4);
    const QString name = text.section(QRegularExpression(QStringLiteral("[/:]")), -1);
    static const QRegularExpression valid(QStringLiteral("^[\\w.\\- ]+$"));
    if (!valid.match(name).hasMatch() || name.startsWith(QLatin1Char('.'))) return {};
    return name;
}

QList<CatalogEntry> parseCatalog(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = tr("插件列表格式不正确。");
        return {};
    }
    const QJsonArray nodes = document.object().value(QStringLiteral("custom_nodes")).toArray();
    QList<CatalogEntry> entries;
    QSet<QString> seen;
    for (const QJsonValue &value : nodes) {
        if (entries.size() >= kCatalogEntryLimit) break;
        const QJsonObject node = value.toObject();
        if (node.value(QStringLiteral("install_type")).toString() != QStringLiteral("git-clone")) continue;
        QString url = node.value(QStringLiteral("files")).toArray().at(0).toString();
        if (!isCloneUrl(url)) url = node.value(QStringLiteral("reference")).toString();
        if (!isCloneUrl(url) || folderFor(url).isEmpty()) continue;
        const QString key = canonicalUrl(url);
        if (seen.contains(key)) continue;
        seen.insert(key);
        CatalogEntry entry;
        entry.url = url.trimmed();
        entry.title = node.value(QStringLiteral("title")).toString().trimmed();
        if (entry.title.isEmpty()) entry.title = folderFor(url);
        entry.author = node.value(QStringLiteral("author")).toString().trimmed();
        entry.description = node.value(QStringLiteral("description")).toString().simplified();
        entries.append(entry);
    }
    if (entries.isEmpty()) *error = tr("插件列表是空的。");
    return entries;
}

git::Result clone(const git::Tool &tool, const QString &url, const QString &destination,
                  const std::atomic_bool *cancel)
{
    return tool.run({}, {QStringLiteral("clone"), QStringLiteral("--filter=blob:none"), QStringLiteral("--quiet"),
                         QStringLiteral("--"), url.trimmed(), QDir::toNativeSeparators(destination)},
                    {.idleTimeoutMs = 120000, .cancel = cancel});
}

bool isPlaceholder(const QString &path)
{
    const QString name = path.section(QLatin1Char('/'), -1);
    return name.endsWith(QStringLiteral("_here")) && (name.startsWith(QStringLiteral("put_"))
                                                       || name.contains(QStringLiteral("_will_be_put_here")));
}

}
