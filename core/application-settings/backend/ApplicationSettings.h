#pragma once

#include <QAbstractNativeEventFilter>
#include <QLocale>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>

class ApplicationSettings final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode NOTIFY appearanceChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY appearanceChanged)
    Q_PROPERTY(QString effectiveFontFamily READ effectiveFontFamily NOTIFY appearanceChanged)
    Q_PROPERTY(double fontPointSize READ fontPointSize WRITE setFontPointSize NOTIFY appearanceChanged)
    Q_PROPERTY(QString accentMode READ accentMode WRITE setAccentMode NOTIFY appearanceChanged)
    Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY appearanceChanged)
    Q_PROPERTY(QString effectiveAccentColor READ effectiveAccentColor NOTIFY appearanceChanged)
    Q_PROPERTY(bool effectiveDark READ effectiveDark NOTIFY appearanceChanged)
    Q_PROPERTY(QString applicationIconMode READ applicationIconMode WRITE setApplicationIconMode NOTIFY applicationIconChanged)
    Q_PROPERTY(QString consoleTheme READ consoleTheme WRITE setConsoleTheme NOTIFY outputChanged)
    Q_PROPERTY(QString consoleFontFamily READ consoleFontFamily WRITE setConsoleFontFamily NOTIFY outputChanged)
    Q_PROPERTY(double consoleFontSize READ consoleFontSize WRITE setConsoleFontSize NOTIFY outputChanged)
    Q_PROPERTY(bool consoleWordWrap READ consoleWordWrap WRITE setConsoleWordWrap NOTIFY outputChanged)
    Q_PROPERTY(bool showTimestamps READ showTimestamps WRITE setShowTimestamps NOTIFY outputChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY appearanceChanged)
    Q_PROPERTY(QString windowAspectRatio READ windowAspectRatio WRITE setWindowAspectRatio NOTIFY windowChanged)
    Q_PROPERTY(QString proxyMode READ proxyMode WRITE setProxyMode NOTIFY proxyChanged)
    Q_PROPERTY(QString proxyHost READ proxyHost WRITE setProxyHost NOTIFY proxyChanged)
    Q_PROPERTY(int proxyPort READ proxyPort WRITE setProxyPort NOTIFY proxyChanged)
    Q_PROPERTY(bool resetTrackedFilesOnUpdate READ resetTrackedFilesOnUpdate WRITE setResetTrackedFilesOnUpdate NOTIFY versionControlChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit ApplicationSettings(const QString &storagePath = {}, QObject *parent = nullptr);
    ~ApplicationSettings() override;

    static QString effectiveLanguage(
        const QString &preference,
        QLocale::Language systemLanguage = QLocale::system().language());

    QString themeMode() const;
    void setThemeMode(const QString &mode);
    QString language() const;
    void setLanguage(const QString &language);
    QString fontFamily() const;
    void setFontFamily(const QString &family);
    QString effectiveFontFamily() const;
    double fontPointSize() const;
    void setFontPointSize(double size);
    QString accentMode() const;
    void setAccentMode(const QString &mode);
    QString accentColor() const;
    void setAccentColor(const QString &color);
    QString effectiveAccentColor() const;
    bool effectiveDark() const;
    QString applicationIconMode() const;
    void setApplicationIconMode(const QString &mode);

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

    QString consoleTheme() const;
    void setConsoleTheme(const QString &theme);
    QString consoleFontFamily() const;
    void setConsoleFontFamily(const QString &family);
    double consoleFontSize() const;
    void setConsoleFontSize(double size);
    bool consoleWordWrap() const;
    void setConsoleWordWrap(bool enabled);
    bool showTimestamps() const;
    void setShowTimestamps(bool enabled);
    bool reducedMotion() const;
    void setReducedMotion(bool enabled);
    QString windowAspectRatio() const;
    void setWindowAspectRatio(const QString &ratio);

    QString proxyMode() const;
    void setProxyMode(const QString &mode);
    QString proxyHost() const;
    void setProxyHost(const QString &host);
    int proxyPort() const;
    void setProxyPort(int port);
    bool resetTrackedFilesOnUpdate() const;
    void setResetTrackedFilesOnUpdate(bool enabled);
    QString proxyUrl() const;
    void applyToProcessEnvironment(QProcessEnvironment &environment) const;
    QString lastError() const;

signals:
    void appearanceChanged();
    void applicationIconChanged();
    void languageChanged();
    void outputChanged();
    void windowChanged();
    void proxyChanged();
    void versionControlChanged();
    void lastErrorChanged();

private:
    void load();
    bool save();
    void applyAppearance();
    void refreshSystemAccentColor();
    void applyProxy();
    void setLastError(const QString &message);

    QString m_storagePath;
    QString m_themeMode = QStringLiteral("system");
    QString m_language = QStringLiteral("system");
    QString m_fontFamily;
    double m_fontPointSize = 11.0;
    QString m_accentMode = QStringLiteral("system");
    QString m_accentColor = QStringLiteral("#0067c0");
    QString m_systemAccentColor = QStringLiteral("#0067c0");
    QString m_applicationIconMode = QStringLiteral("theme");
    QString m_consoleTheme = QStringLiteral("system");
    QString m_consoleFontFamily = QStringLiteral("Cascadia Mono");
    double m_consoleFontSize = 10.0;
    bool m_consoleWordWrap = false;
    bool m_showTimestamps = true;
    bool m_reducedMotion = false;
    QString m_windowAspectRatio = QStringLiteral("screen");
    QString m_proxyMode = QStringLiteral("system");
    QString m_proxyHost;
    int m_proxyPort = 7890;
    bool m_resetTrackedFilesOnUpdate = false;
    QString m_lastError;
};
