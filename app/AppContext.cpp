#include "AppContext.h"

#include "ApplicationSettings.h"
#include "ConfigurationManager.h"
#include "RuntimeManager.h"

#include <QCoreApplication>
#include <QLocale>
#include <QQmlEngine>

AppContext::AppContext(QObject *parent)
    : QObject(parent),
      m_configuration(new ConfigurationManager({}, this)),
      m_settings(new ApplicationSettings({}, this)),
      m_runtime(new RuntimeManager(m_configuration, m_settings, this))
{
    connect(m_settings, &ApplicationSettings::languageChanged, this, &AppContext::applyLanguage);
    applyLanguage();
}

ConfigurationManager *AppContext::configuration() const { return m_configuration; }
RuntimeManager *AppContext::runtime() const { return m_runtime; }
ApplicationSettings *AppContext::settings() const { return m_settings; }

void AppContext::setQmlEngine(QQmlEngine *engine)
{
    m_qmlEngine = engine;
}

void AppContext::applyLanguage()
{
    QCoreApplication::removeTranslator(&m_translator);

    QString language = m_settings->language();
    if (language == QStringLiteral("system")) {
        language = QLocale::system().name();
    }
    QLocale::setDefault(QLocale(language));

    if (language.startsWith(QStringLiteral("en"), Qt::CaseInsensitive)
        && m_translator.load(QStringLiteral(":/i18n/qml_en_US.qm"))) {
        QCoreApplication::installTranslator(&m_translator);
    }
    m_configuration->retranslate();
    m_runtime->retranslate();
    if (m_qmlEngine) {
        m_qmlEngine->retranslate();
    }
}
