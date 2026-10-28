#pragma once

#include "Repository.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// Folders under custom_nodes; ComfyUI skips the ones ending in ".disabled".
struct Extension {
    QString path;
    // Folder name without ".disabled".
    QString name;
    bool enabled = true;
    RepositoryInfo info;
};

struct CatalogEntry {
    QString title;
    QString author;
    QString description;
    QString url;
};

namespace extensions {

inline const QString kDisabledSuffix = QStringLiteral(".disabled");

// Local state of every extension folder; runs Git in parallel.
QList<Extension> scan(const git::Tool &tool, const QString &customNodes);
Extension inspect(const git::Tool &tool, const QString &path);

// "https://github.com/a/b.git", "git@github.com:a/b" and "github.com/A/B/"
// all become "github.com/a/b".
QString canonicalUrl(const QString &url);

// Folder name an install of `url` gets, or empty when the URL is unusable.
QString folderFor(const QString &url);
bool isCloneUrl(const QString &url);

// ComfyUI-Manager's custom-node-list.json; only git-clone entries are kept.
QList<CatalogEntry> parseCatalog(const QByteArray &json, QString *error);

// Clones with history but without old file contents, which arrive on demand.
git::Result clone(const git::Tool &tool, const QString &url, const QString &destination,
                  const std::atomic_bool *cancel);

// Tracked paths ComfyUI ships as placeholders that users routinely delete.
bool isPlaceholder(const QString &path);

}
