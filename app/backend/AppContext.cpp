#include "AppContext.h"

#include "ApplicationIconManager.h"
#include "ApplicationSettings.h"
#include "ConfigurationPackageManager.h"
#include "ConfigurationManager.h"
#include "PortablePaths.h"
#include "RuntimeManager.h"
#include "SkinManager.h"
#include "VersionManager.h"
#include "WindowChromeController.h"
#include "ZludaBootstrap.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLocale>
#include <QQmlEngine>
#include <QThread>

AppContext::AppContext(QObject *parent)
    : QObject(parent)
{
    // Establish a readable UI language before any manager creates localized
    // first-run data or reports a startup error. A saved language preference
    // is applied immediately after the settings file has been loaded.
    installLanguage(QStringLiteral("system"));
    m_settings = new ApplicationSettings({}, this);
    installLanguage(m_settings->language());

    m_configuration = new ConfigurationManager({}, this);
    m_configurationPackages = new ConfigurationPackageManager(m_configuration, this);
    m_appIcon = new ApplicationIconManager(m_settings, this);
    m_runtime = new RuntimeManager(m_configuration, m_settings, this);
    m_skins = new SkinManager(m_settings, {}, this);
    m_windowChrome = new WindowChromeController(this);
    m_versions = new VersionManager(m_configuration, m_settings, this);
    connect(m_versions, &VersionManager::stateChanged, this, [this] {
        m_runtime->setVersionOperationBlocked(
            m_versions->updating() || m_versions->interruptedOperation());
    });
    m_runtime->setVersionOperationBlocked(
        m_versions->updating() || m_versions->interruptedOperation());

    QString portableError;
    if (!PortablePaths::ensureDataDirectory(&portableError)) {
        qWarning().noquote() << portableError;
    }
    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    m_zludaPreloadThread = QThread::create([applicationDirectory] {
        QString preloadError;
        if (!ZludaBootstrap::preloadEmbeddedPackages(applicationDirectory, &preloadError)) {
            qWarning().noquote() << preloadError;
        }
    });
    connect(m_zludaPreloadThread, &QThread::finished,
            m_zludaPreloadThread, &QObject::deleteLater);
    m_zludaPreloadThread->start();

    connect(m_settings, &ApplicationSettings::languageChanged, this, &AppContext::applyLanguage);
}

AppContext::~AppContext()
{
    if (m_zludaPreloadThread && m_zludaPreloadThread->isRunning()) {
        m_zludaPreloadThread->requestInterruption();
        m_zludaPreloadThread->wait();
    }
}

ApplicationIconManager *AppContext::appIcon() const { return m_appIcon; }
ConfigurationManager *AppContext::configuration() const { return m_configuration; }
ConfigurationPackageManager *AppContext::configurationPackages() const
{
    return m_configurationPackages;
}
RuntimeManager *AppContext::runtime() const { return m_runtime; }
ApplicationSettings *AppContext::settings() const { return m_settings; }
SkinManager *AppContext::skins() const { return m_skins; }
WindowChromeController *AppContext::windowChrome() const { return m_windowChrome; }
VersionManager *AppContext::versions() const { return m_versions; }

void AppContext::setQmlEngine(QQmlEngine *engine)
{
    m_qmlEngine = engine;
}

void AppContext::applyLanguage()
{
    installLanguage(m_settings ? m_settings->language() : QStringLiteral("system"));

    if (m_configuration) m_configuration->retranslate();
    if (m_runtime) m_runtime->retranslate();
    if (m_skins) m_skins->retranslate();
    if (m_versions) m_versions->retranslate();
    if (m_qmlEngine) {
        m_qmlEngine->retranslate();
    }
}

void AppContext::installLanguage(const QString &preference)
{
    QCoreApplication::removeTranslator(&m_translator);

    const QString language = ApplicationSettings::effectiveLanguage(preference);
    QLocale::setDefault(QLocale(language));

    if (language == QStringLiteral("en_US")
        && m_translator.load(QStringLiteral(":/i18n/qml_en_US.qm"))) {
        QCoreApplication::installTranslator(&m_translator);
    }
}
