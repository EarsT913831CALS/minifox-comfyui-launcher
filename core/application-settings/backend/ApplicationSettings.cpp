#include "ApplicationSettings.h"

#include "PortablePaths.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QPalette>
#include <QSaveFile>
#include <QStyleHints>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {

constexpr const char *proxyEnvironmentNames[] = {
    "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY",
    "http_proxy", "https_proxy", "all_proxy"
};

QColor blended(const QColor &foreground, const QColor &background, qreal foregroundWeight)
{
    const qreal backgroundWeight = 1.0 - foregroundWeight;
    return QColor::fromRgbF(
        foreground.redF() * foregroundWeight + background.redF() * backgroundWeight,
        foreground.greenF() * foregroundWeight + background.greenF() * backgroundWeight,
        foreground.blueF() * foregroundWeight + background.blueF() * backgroundWeight);
}

QColor systemAccentColor()
{
#ifdef Q_OS_WIN
    DWORD colorizationColor = 0;
    BOOL opaqueBlend = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&colorizationColor, &opaqueBlend))) {
        return QColor::fromRgb(
            static_cast<int>((colorizationColor >> 16) & 0xff),
            static_cast<int>((colorizationColor >> 8) & 0xff),
            static_cast<int>(colorizationColor & 0xff));
    }
#endif

    const QColor paletteAccent = QGuiApplication::palette().color(QPalette::Highlight);
    return paletteAccent.isValid() ? paletteAccent : QColor(QStringLiteral("#0067c0"));
}

QPalette applicationPalette(bool dark, const QColor &accent)
{
    const QColor surface = dark ? QColor(QStringLiteral("#202020")) : QColor(QStringLiteral("#f7f7f7"));
    const QColor surfaceRaised = dark ? QColor(QStringLiteral("#2b2b2b")) : QColor(QStringLiteral("#ffffff"));
    const QColor surfaceSubtle = dark ? QColor(QStringLiteral("#282828")) : QColor(QStringLiteral("#f3f3f3"));
    const QColor onSurface = dark ? QColor(QStringLiteral("#ffffff")) : QColor(QStringLiteral("#1b1b1b"));
    const QColor onSurfaceSecondary = dark ? QColor(QStringLiteral("#c5c5c5")) : QColor(QStringLiteral("#5d5d5d"));
    const QColor outline = dark ? QColor(QStringLiteral("#454545")) : QColor(QStringLiteral("#d6d6d6"));
    const QColor shadow = dark ? QColor(QStringLiteral("#000000")) : QColor(QStringLiteral("#707070"));
    const QColor error = dark ? QColor(QStringLiteral("#ff99a4")) : QColor(QStringLiteral("#c42b1c"));
    const QColor info = dark ? QColor(QStringLiteral("#60cdff")) : QColor(QStringLiteral("#0067c0"));
    const qreal accentLuminance = 0.2126 * accent.redF() + 0.7152 * accent.greenF() + 0.0722 * accent.blueF();
    const QColor onAccent = accentLuminance > 0.56 ? QColor(QStringLiteral("#1b1b1b"))
                                                  : QColor(QStringLiteral("#ffffff"));
    const QColor disabledText = blended(onSurface, surface, 0.42);
    const QColor disabledSecondary = blended(onSurfaceSecondary, surface, 0.42);
    const QColor disabledAccent = blended(accent, surface, 0.42);

    QPalette palette;
    const auto setRole = [&palette](QPalette::ColorRole role, const QColor &active, const QColor &disabled) {
        palette.setColor(QPalette::Active, role, active);
        palette.setColor(QPalette::Inactive, role, active);
        palette.setColor(QPalette::Disabled, role, disabled);
    };

    setRole(QPalette::Window, surface, surface);
    setRole(QPalette::WindowText, onSurface, disabledText);
    setRole(QPalette::Base, surfaceRaised, surfaceRaised);
    setRole(QPalette::AlternateBase, surfaceSubtle, surfaceSubtle);
    setRole(QPalette::Text, onSurface, disabledText);
    setRole(QPalette::Button, surfaceRaised, surfaceRaised);
    setRole(QPalette::ButtonText, onSurface, disabledText);
    setRole(QPalette::BrightText, error, blended(error, surface, 0.42));
    setRole(QPalette::Light, surfaceRaised, surfaceRaised);
    setRole(QPalette::Midlight, surfaceSubtle, surfaceSubtle);
    setRole(QPalette::Mid, outline, outline);
    setRole(QPalette::Dark, outline, outline);
    setRole(QPalette::Shadow, shadow, shadow);
    setRole(QPalette::Highlight, accent, disabledAccent);
    setRole(QPalette::HighlightedText, onAccent, blended(onAccent, accent, 0.55));
    setRole(QPalette::Link, info, blended(info, surface, 0.42));
    setRole(QPalette::LinkVisited, info, blended(info, surface, 0.42));
    setRole(QPalette::ToolTipBase, surfaceRaised, surfaceRaised);
    setRole(QPalette::ToolTipText, onSurface, disabledText);
    setRole(QPalette::PlaceholderText, onSurfaceSecondary, disabledSecondary);
    setRole(QPalette::Accent, accent, disabledAccent);
    return palette;
}

} // namespace

ApplicationSettings::ApplicationSettings(const QString &storagePath, QObject *parent)
    : QObject(parent),
      m_storagePath(storagePath.isEmpty() ? PortablePaths::settingsFile() : storagePath)
{
    m_systemAccentColor = systemAccentColor().name(QColor::HexRgb);
    load();
    applyAppearance();
    applyProxy();
    QCoreApplication::instance()->installNativeEventFilter(this);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_themeMode == QStringLiteral("system")) {
            refreshSystemAccentColor();
            applyAppearance();
            emit appearanceChanged();
        }
    });
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) {
            refreshSystemAccentColor();
        }
    });
}

ApplicationSettings::~ApplicationSettings()
{
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
}

QString ApplicationSettings::effectiveLanguage(const QString &preference,
                                                QLocale::Language systemLanguage)
{
    if (preference == QStringLiteral("zh_CN") || preference == QStringLiteral("en_US")) {
        return preference;
    }
    return systemLanguage == QLocale::Chinese
        ? QStringLiteral("zh_CN") : QStringLiteral("en_US");
}

QString ApplicationSettings::themeMode() const { return m_themeMode; }

void ApplicationSettings::setThemeMode(const QString &mode)
{
    if ((mode != QStringLiteral("system") && mode != QStringLiteral("light") && mode != QStringLiteral("dark"))
        || mode == m_themeMode) {
        return;
    }
    m_themeMode = mode;
    applyAppearance();
    save();
    emit appearanceChanged();
}

QString ApplicationSettings::language() const { return m_language; }

void ApplicationSettings::setLanguage(const QString &language)
{
    if ((language != QStringLiteral("system") && language != QStringLiteral("zh_CN")
         && language != QStringLiteral("en_US")) || language == m_language) {
        return;
    }
    m_language = language;
    save();
    emit languageChanged();
}

QString ApplicationSettings::fontFamily() const { return m_fontFamily; }

void ApplicationSettings::setFontFamily(const QString &family)
{
    if (family == m_fontFamily) {
        return;
    }
    m_fontFamily = family;
    applyAppearance();
    save();
    emit appearanceChanged();
}

QString ApplicationSettings::effectiveFontFamily() const
{
    return m_fontFamily.isEmpty()
        ? QFontDatabase::systemFont(QFontDatabase::GeneralFont).family()
        : m_fontFamily;
}

double ApplicationSettings::fontPointSize() const { return m_fontPointSize; }

void ApplicationSettings::setFontPointSize(double size)
{
    size = qBound(8.0, size, 24.0);
    if (qFuzzyCompare(size, m_fontPointSize)) {
        return;
    }
    m_fontPointSize = size;
    applyAppearance();
    save();
    emit appearanceChanged();
}

QString ApplicationSettings::accentColor() const { return m_accentColor; }

QString ApplicationSettings::accentMode() const { return m_accentMode; }

void ApplicationSettings::setAccentMode(const QString &mode)
{
    if ((mode != QStringLiteral("system") && mode != QStringLiteral("custom"))
        || mode == m_accentMode) {
        return;
    }
    m_accentMode = mode;
    if (m_accentMode == QStringLiteral("system")) {
        m_systemAccentColor = systemAccentColor().name(QColor::HexRgb);
    }
    applyAppearance();
    save();
    emit appearanceChanged();
}

void ApplicationSettings::setAccentColor(const QString &color)
{
    const QColor parsed(color);
    if (!parsed.isValid() || parsed.name(QColor::HexRgb) == m_accentColor) {
        return;
    }
    m_accentColor = parsed.name(QColor::HexRgb);
    if (m_accentMode == QStringLiteral("custom")) {
        applyAppearance();
    }
    save();
    emit appearanceChanged();
}

QString ApplicationSettings::effectiveAccentColor() const
{
    return m_accentMode == QStringLiteral("custom") ? m_accentColor : m_systemAccentColor;
}

bool ApplicationSettings::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(eventType)
    Q_UNUSED(result)
#ifdef Q_OS_WIN
    const auto *nativeMessage = static_cast<MSG *>(message);
    if (nativeMessage
        && (nativeMessage->message == WM_DWMCOLORIZATIONCOLORCHANGED
            || nativeMessage->message == WM_SETTINGCHANGE)) {
        QMetaObject::invokeMethod(this, &ApplicationSettings::refreshSystemAccentColor,
                                  Qt::QueuedConnection);
    }
#else
    Q_UNUSED(message)
#endif
    return false;
}

bool ApplicationSettings::effectiveDark() const
{
    if (m_themeMode == QStringLiteral("dark")) {
        return true;
    }
    if (m_themeMode == QStringLiteral("light")) {
        return false;
    }
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

QString ApplicationSettings::applicationIconMode() const
{
    return m_applicationIconMode;
}

void ApplicationSettings::setApplicationIconMode(const QString &mode)
{
    if ((mode != QStringLiteral("theme")
         && mode != QStringLiteral("light")
         && mode != QStringLiteral("dark")
         && mode != QStringLiteral("custom"))
        || mode == m_applicationIconMode) {
        return;
    }
    m_applicationIconMode = mode;
    save();
    emit applicationIconChanged();
}

QString ApplicationSettings::consoleTheme() const { return m_consoleTheme; }

void ApplicationSettings::setConsoleTheme(const QString &theme)
{
    if ((theme != QStringLiteral("system") && theme != QStringLiteral("light")
         && theme != QStringLiteral("dark")) || theme == m_consoleTheme) {
        return;
    }
    m_consoleTheme = theme;
    save();
    emit outputChanged();
}

QString ApplicationSettings::consoleFontFamily() const { return m_consoleFontFamily; }

void ApplicationSettings::setConsoleFontFamily(const QString &family)
{
    if (family == m_consoleFontFamily) {
        return;
    }
    m_consoleFontFamily = family;
    save();
    emit outputChanged();
}

double ApplicationSettings::consoleFontSize() const { return m_consoleFontSize; }

void ApplicationSettings::setConsoleFontSize(double size)
{
    size = qBound(7.0, size, 24.0);
    if (qFuzzyCompare(size, m_consoleFontSize)) {
        return;
    }
    m_consoleFontSize = size;
    save();
    emit outputChanged();
}

bool ApplicationSettings::consoleWordWrap() const { return m_consoleWordWrap; }

void ApplicationSettings::setConsoleWordWrap(bool enabled)
{
    if (enabled == m_consoleWordWrap) {
        return;
    }
    m_consoleWordWrap = enabled;
    save();
    emit outputChanged();
}

bool ApplicationSettings::showTimestamps() const { return m_showTimestamps; }

void ApplicationSettings::setShowTimestamps(bool enabled)
{
    if (enabled == m_showTimestamps) {
        return;
    }
    m_showTimestamps = enabled;
    save();
    emit outputChanged();
}

bool ApplicationSettings::reducedMotion() const { return m_reducedMotion; }

void ApplicationSettings::setReducedMotion(bool enabled)
{
    if (enabled == m_reducedMotion) {
        return;
    }
    m_reducedMotion = enabled;
    save();
    emit appearanceChanged();
}

QString ApplicationSettings::windowAspectRatio() const
{
    return m_windowAspectRatio;
}

void ApplicationSettings::setWindowAspectRatio(const QString &ratio)
{
    static const QStringList supported {
        QStringLiteral("screen"),
        QStringLiteral("16:10"),
        QStringLiteral("16:9"),
        QStringLiteral("3:2"),
        QStringLiteral("4:3")
    };
    if (!supported.contains(ratio) || ratio == m_windowAspectRatio) {
        return;
    }
    m_windowAspectRatio = ratio;
    save();
    emit windowChanged();
}

QString ApplicationSettings::proxyMode() const { return m_proxyMode; }

void ApplicationSettings::setProxyMode(const QString &mode)
{
    if ((mode != QStringLiteral("system") && mode != QStringLiteral("none")
         && mode != QStringLiteral("manual")) || mode == m_proxyMode) {
        return;
    }
    m_proxyMode = mode;
    applyProxy();
    save();
    emit proxyChanged();
}

QString ApplicationSettings::proxyHost() const { return m_proxyHost; }

void ApplicationSettings::setProxyHost(const QString &host)
{
    if (host == m_proxyHost) {
        return;
    }
    m_proxyHost = host.trimmed();
    applyProxy();
    save();
    emit proxyChanged();
}

int ApplicationSettings::proxyPort() const { return m_proxyPort; }

void ApplicationSettings::setProxyPort(int port)
{
    port = qBound(1, port, 65535);
    if (port == m_proxyPort) {
        return;
    }
    m_proxyPort = port;
    applyProxy();
    save();
    emit proxyChanged();
}

bool ApplicationSettings::resetTrackedFilesOnUpdate() const
{
    return m_resetTrackedFilesOnUpdate;
}

void ApplicationSettings::setResetTrackedFilesOnUpdate(bool enabled)
{
    if (enabled == m_resetTrackedFilesOnUpdate) {
        return;
    }
    m_resetTrackedFilesOnUpdate = enabled;
    save();
    emit versionControlChanged();
}

QString ApplicationSettings::proxyUrl() const
{
    if (m_proxyMode != QStringLiteral("manual") || m_proxyHost.isEmpty()) {
        return {};
    }

    QString host = m_proxyHost;
    if (!host.contains(QStringLiteral("://"))) {
        host.prepend(QStringLiteral("http://"));
    }
    QUrl url(host);
    if (!url.isValid() || url.host().isEmpty()) {
        return {};
    }
    if (url.port() < 0) {
        url.setPort(m_proxyPort);
    }
    return url.toString(QUrl::FullyEncoded);
}

void ApplicationSettings::applyToProcessEnvironment(QProcessEnvironment &environment) const
{
    if (m_proxyMode == QStringLiteral("system")) {
        return;
    }

    for (const char *name : proxyEnvironmentNames) {
        environment.remove(QString::fromLatin1(name));
    }

    const QString url = proxyUrl();
    if (url.isEmpty()) {
        return;
    }
    environment.insert(QStringLiteral("HTTP_PROXY"), url);
    environment.insert(QStringLiteral("HTTPS_PROXY"), url);
    environment.insert(QStringLiteral("ALL_PROXY"), url);
}

QString ApplicationSettings::lastError() const { return m_lastError; }

void ApplicationSettings::load()
{
    QFile file(m_storagePath);
    if (!file.exists()) {
        save();
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setLastError(tr("无法读取应用设置：%1").arg(file.errorString()));
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        setLastError(tr("应用设置文件格式无效。"));
        return;
    }
    const QJsonObject object = document.object();
    m_themeMode = object.value(QStringLiteral("themeMode")).toString(m_themeMode);
    m_language = object.value(QStringLiteral("language")).toString(m_language);
    m_fontFamily = object.value(QStringLiteral("fontFamily")).toString(m_fontFamily);
    m_fontPointSize = object.value(QStringLiteral("fontPointSize")).toDouble(m_fontPointSize);
    m_accentMode = object.value(QStringLiteral("accentMode")).toString(m_accentMode);
    if (m_accentMode != QStringLiteral("system") && m_accentMode != QStringLiteral("custom")) {
        m_accentMode = QStringLiteral("system");
    }
    const QColor storedAccent(object.value(QStringLiteral("accentColor")).toString(m_accentColor));
    if (storedAccent.isValid()) {
        m_accentColor = storedAccent.name(QColor::HexRgb);
    }
    m_applicationIconMode =
        object.value(QStringLiteral("applicationIconMode")).toString(m_applicationIconMode);
    if (m_applicationIconMode != QStringLiteral("theme")
        && m_applicationIconMode != QStringLiteral("light")
        && m_applicationIconMode != QStringLiteral("dark")
        && m_applicationIconMode != QStringLiteral("custom")) {
        m_applicationIconMode = QStringLiteral("theme");
    }
    m_consoleTheme = object.value(QStringLiteral("consoleTheme")).toString(m_consoleTheme);
    m_consoleFontFamily = object.value(QStringLiteral("consoleFontFamily")).toString(m_consoleFontFamily);
    m_consoleFontSize = object.value(QStringLiteral("consoleFontSize")).toDouble(m_consoleFontSize);
    m_consoleWordWrap = object.value(QStringLiteral("consoleWordWrap")).toBool(m_consoleWordWrap);
    m_showTimestamps = object.value(QStringLiteral("showTimestamps")).toBool(m_showTimestamps);
    m_reducedMotion = object.value(QStringLiteral("reducedMotion")).toBool(m_reducedMotion);
    m_windowAspectRatio =
        object.value(QStringLiteral("windowAspectRatio")).toString(m_windowAspectRatio);
    if (m_windowAspectRatio != QStringLiteral("screen")
        && m_windowAspectRatio != QStringLiteral("16:10")
        && m_windowAspectRatio != QStringLiteral("16:9")
        && m_windowAspectRatio != QStringLiteral("3:2")
        && m_windowAspectRatio != QStringLiteral("4:3")) {
        m_windowAspectRatio = QStringLiteral("screen");
    }
    m_proxyMode = object.value(QStringLiteral("proxyMode")).toString(m_proxyMode);
    m_proxyHost = object.value(QStringLiteral("proxyHost")).toString(m_proxyHost);
    m_proxyPort = object.value(QStringLiteral("proxyPort")).toInt(m_proxyPort);
    m_resetTrackedFilesOnUpdate = object.value(
        QStringLiteral("resetTrackedFilesOnUpdate")).toBool(m_resetTrackedFilesOnUpdate);
}

bool ApplicationSettings::save()
{
    const QFileInfo fileInfo(m_storagePath);
    if (m_storagePath == PortablePaths::settingsFile()) {
        QString error;
        if (!PortablePaths::ensureDataDirectory(&error)) {
            setLastError(error);
            return false;
        }
    } else if (!QDir().mkpath(fileInfo.absolutePath())) {
        setLastError(tr("无法创建设置目录：%1").arg(fileInfo.absolutePath()));
        return false;
    }

    const QJsonObject object {
        {QStringLiteral("schemaVersion"), 3},
        {QStringLiteral("themeMode"), m_themeMode},
        {QStringLiteral("language"), m_language},
        {QStringLiteral("fontFamily"), m_fontFamily},
        {QStringLiteral("fontPointSize"), m_fontPointSize},
        {QStringLiteral("accentMode"), m_accentMode},
        {QStringLiteral("accentColor"), m_accentColor},
        {QStringLiteral("applicationIconMode"), m_applicationIconMode},
        {QStringLiteral("consoleTheme"), m_consoleTheme},
        {QStringLiteral("consoleFontFamily"), m_consoleFontFamily},
        {QStringLiteral("consoleFontSize"), m_consoleFontSize},
        {QStringLiteral("consoleWordWrap"), m_consoleWordWrap},
        {QStringLiteral("showTimestamps"), m_showTimestamps},
        {QStringLiteral("reducedMotion"), m_reducedMotion},
        {QStringLiteral("windowAspectRatio"), m_windowAspectRatio},
        {QStringLiteral("proxyMode"), m_proxyMode},
        {QStringLiteral("proxyHost"), m_proxyHost},
        {QStringLiteral("proxyPort"), m_proxyPort},
        {QStringLiteral("resetTrackedFilesOnUpdate"), m_resetTrackedFilesOnUpdate}
    };

    QSaveFile file(m_storagePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setLastError(tr("无法保存应用设置：%1").arg(file.errorString()));
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        setLastError(tr("无法提交应用设置：%1").arg(file.errorString()));
        return false;
    }
    setLastError({});
    return true;
}

void ApplicationSettings::applyAppearance()
{
    if (m_themeMode == QStringLiteral("dark")) {
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    } else if (m_themeMode == QStringLiteral("light")) {
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    } else {
        QGuiApplication::styleHints()->unsetColorScheme();
    }

    QColor accent(effectiveAccentColor());
    if (!accent.isValid()) {
        accent = QColor(QStringLiteral("#0067c0"));
    }
    QGuiApplication::setPalette(applicationPalette(effectiveDark(), accent));

    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (!m_fontFamily.isEmpty()) {
        const QString systemFamily = font.family();
        font.setFamilies({m_fontFamily, systemFamily});
    }
    font.setPointSizeF(m_fontPointSize);
    QGuiApplication::setFont(font);
}

void ApplicationSettings::refreshSystemAccentColor()
{
    const QString color = systemAccentColor().name(QColor::HexRgb);
    if (color == m_systemAccentColor) {
        return;
    }
    m_systemAccentColor = color;
    if (m_accentMode == QStringLiteral("system")) {
        applyAppearance();
        emit appearanceChanged();
    }
}

void ApplicationSettings::applyProxy()
{
    if (m_proxyMode == QStringLiteral("system")) {
        QNetworkProxyFactory::setUseSystemConfiguration(true);
        return;
    }

    QNetworkProxyFactory::setUseSystemConfiguration(false);
    if (m_proxyMode == QStringLiteral("manual") && !m_proxyHost.isEmpty()) {
        QNetworkProxy::setApplicationProxy(
            QNetworkProxy(QNetworkProxy::HttpProxy, m_proxyHost, static_cast<quint16>(m_proxyPort)));
    } else {
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    }
}

void ApplicationSettings::setLastError(const QString &message)
{
    if (message == m_lastError) {
        return;
    }
    m_lastError = message;
    emit lastErrorChanged();
}
