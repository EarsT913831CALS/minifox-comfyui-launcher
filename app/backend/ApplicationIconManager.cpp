#include "ApplicationIconManager.h"

#include "ApplicationSettings.h"
#include "PortablePaths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>

namespace {

constexpr int normalizedIconSize = 1024;
constexpr qreal normalizedIconCornerRadiusRatio = 0.22;

void applyRoundedMask(QImage *image)
{
    QImage mask(image->size(), QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);

    {
        QPainter maskPainter(&mask);
        maskPainter.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        const QRectF bounds(
            0.5,
            0.5,
            image->width() - 1.0,
            image->height() - 1.0);
        const qreal radius =
            qMin(image->width(), image->height()) * normalizedIconCornerRadiusRatio;
        path.addRoundedRect(bounds, radius, radius);
        maskPainter.fillPath(path, Qt::white);
    }

    QPainter imagePainter(image);
    imagePainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    imagePainter.drawImage(0, 0, mask);
}

QUrl builtInIconSource(bool dark)
{
    return QUrl(dark
                    ? QStringLiteral("qrc:/minifox/icons/app-icon-dark.png")
                    : QStringLiteral("qrc:/minifox/icons/app-icon-light.png"));
}

QString iconLoadPath(const QUrl &source)
{
    if (source.scheme() == QStringLiteral("qrc")) {
        return QStringLiteral(":") + source.path();
    }
    return source.isLocalFile() ? source.toLocalFile() : source.toString();
}

QByteArray fileDigest(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return hash.result();
}

QUrl dataUrlForPng(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QUrl(QStringLiteral("data:image/png;base64,%1")
                    .arg(QString::fromLatin1(file.readAll().toBase64())));
}

} // namespace

ApplicationIconManager::ApplicationIconManager(
    ApplicationSettings *settings,
    QObject *parent)
    : QObject(parent),
      m_settings(settings)
{
    QString startupMode = m_settings->applicationIconMode();
    if (startupMode == QStringLiteral("custom") && !customIconAvailable()) {
        startupMode = QStringLiteral("theme");
        m_settings->setApplicationIconMode(startupMode);
    }

    const QUrl startupSource = sourceForMode(startupMode);
    m_activeIconKey = keyForMode(startupMode);
    m_activeIconSource = startupMode == QStringLiteral("custom")
        ? dataUrlForPng(customIconPath())
        : startupSource;
    applyStartupIcon(startupSource);

    connect(
        m_settings,
        &ApplicationSettings::applicationIconChanged,
        this,
        &ApplicationIconManager::stateChanged);
    connect(
        m_settings,
        &ApplicationSettings::appearanceChanged,
        this,
        &ApplicationIconManager::stateChanged);
}

QString ApplicationIconManager::mode() const
{
    return m_settings->applicationIconMode();
}

void ApplicationIconManager::setMode(const QString &mode)
{
    if (mode == QStringLiteral("custom") && !customIconAvailable()) {
        setLastError(tr("请先选择一张自定义应用图标。"));
        return;
    }
    setLastError({});
    m_settings->setApplicationIconMode(mode);
}

QUrl ApplicationIconManager::pendingIconSource() const
{
    return sourceForMode(mode());
}

QUrl ApplicationIconManager::activeIconSource() const
{
    return m_activeIconSource;
}

bool ApplicationIconManager::customIconAvailable() const
{
    const QString path = customIconPath();
    return QFileInfo::exists(path) && QImageReader(path).canRead();
}

bool ApplicationIconManager::restartRequired() const
{
    return keyForMode(mode()) != m_activeIconKey;
}

QString ApplicationIconManager::lastError() const
{
    return m_lastError;
}

bool ApplicationIconManager::importCustomIcon(const QUrl &source)
{
    const QString sourcePath = source.isLocalFile() ? source.toLocalFile() : QString{};
    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);
    const QImage input = reader.read();
    if (sourcePath.isEmpty() || input.isNull()) {
        setLastError(tr("无法读取所选图标文件：%1").arg(reader.errorString()));
        return false;
    }

    QImage normalized(
        normalizedIconSize,
        normalizedIconSize,
        QImage::Format_ARGB32_Premultiplied);
    normalized.fill(Qt::transparent);

    const QImage scaled = input.scaled(
        normalized.size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    {
        QPainter painter(&normalized);
        painter.drawImage(
            QPoint(
                (normalized.width() - scaled.width()) / 2,
                (normalized.height() - scaled.height()) / 2),
            scaled);
    }
    applyRoundedMask(&normalized);

    QString error;
    if (!PortablePaths::ensureDataDirectory(&error)) {
        setLastError(error);
        return false;
    }
    if (!QDir().mkpath(PortablePaths::iconsDirectory())) {
        setLastError(tr("无法创建应用图标目录：%1")
                         .arg(QDir::toNativeSeparators(PortablePaths::iconsDirectory())));
        return false;
    }

    QSaveFile output(customIconPath());
    if (!output.open(QIODevice::WriteOnly)) {
        setLastError(tr("无法保存自定义应用图标：%1").arg(output.errorString()));
        return false;
    }
    QImageWriter writer(&output, "png");
    writer.setQuality(100);
    if (!writer.write(normalized) || !output.commit()) {
        setLastError(tr("无法保存自定义应用图标：%1").arg(writer.errorString()));
        return false;
    }

    setLastError({});
    m_settings->setApplicationIconMode(QStringLiteral("custom"));
    emit stateChanged();
    return true;
}

void ApplicationIconManager::reset()
{
    const QString path = customIconPath();
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        setLastError(tr("无法删除自定义应用图标：%1")
                         .arg(QDir::toNativeSeparators(path)));
        return;
    }
    setLastError({});
    m_settings->setApplicationIconMode(QStringLiteral("theme"));
    emit stateChanged();
}

QUrl ApplicationIconManager::sourceForMode(const QString &mode) const
{
    if (mode == QStringLiteral("custom") && customIconAvailable()) {
        return QUrl::fromLocalFile(customIconPath());
    }
    if (mode == QStringLiteral("dark")) {
        return builtInIconSource(true);
    }
    if (mode == QStringLiteral("light")) {
        return builtInIconSource(false);
    }
    return builtInIconSource(m_settings->effectiveDark());
}

QString ApplicationIconManager::keyForMode(const QString &mode) const
{
    if (mode == QStringLiteral("custom") && customIconAvailable()) {
        return QStringLiteral("custom:%1")
            .arg(QString::fromLatin1(fileDigest(customIconPath()).toHex()));
    }
    const QUrl source = sourceForMode(mode);
    return source.path().endsWith(QStringLiteral("dark.png"))
        ? QStringLiteral("builtin:dark")
        : QStringLiteral("builtin:light");
}

QString ApplicationIconManager::customIconPath() const
{
    return QDir(PortablePaths::iconsDirectory()).filePath(QStringLiteral("custom.png"));
}

void ApplicationIconManager::applyStartupIcon(const QUrl &source)
{
    QIcon icon(iconLoadPath(source));
    if (icon.isNull()) {
        icon = QIcon(iconLoadPath(builtInIconSource(false)));
    }
    QGuiApplication::setWindowIcon(icon);
}

void ApplicationIconManager::setLastError(const QString &message)
{
    if (message == m_lastError) {
        return;
    }
    m_lastError = message;
    emit lastErrorChanged();
}
