#pragma once
#include <QString>
#include <QUrl>
namespace ExternalContent {
bool allowedUrl(const QUrl &url);
QUrl repositoryUrl(const QString &remote);
QString markdown(const QString &source);
QString redactUrl(const QString &source);
}
