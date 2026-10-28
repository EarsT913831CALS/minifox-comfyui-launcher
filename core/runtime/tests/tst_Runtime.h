#pragma once

#include "LaunchCommandBuilder.h"
#include "ApplicationSettings.h"
#include "CommandPromptBuilder.h"
#include "ConfigurationManager.h"
#include "LogModel.h"
#include "RuntimeManager.h"
#include "ZludaBootstrap.h"
#include "ExternalContent.h"

#include <QFontDatabase>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPalette>
#include <QHostAddress>
#include <QRegularExpression>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QQuickWindow>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QWheelEvent>

#include <algorithm>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

class RuntimeTest final : public QObject
{
    Q_OBJECT

public:
    static void initMain()
    {
        QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));
    }

private slots:
    void safeNoticeDoesNotFetchImages();
    void runtimeFreezesProfileAndBlocksSharedPython_data();
    void runtimeFreezesProfileAndBlocksSharedPython();
    void serviceUrlFollowsCustomArguments_data();
    void serviceUrlFollowsCustomArguments();
    void disabledCustomNodesDoNotGateStartup_data();
    void disabledCustomNodesDoNotGateStartup();
    void readinessRequiresComfySystemStats_data();
    void readinessRequiresComfySystemStats();
    void occupiedPortBlocksLaunch();
    void progressFollowedByErrorKeepsError();
    void initTestCase();
    void defaultsStayImplicit();
    void commandPromptActivatesSelectedEnvironment();
    void explicitModesAndCustomArguments();
    void progressBridgeEnablesArgumentParsingBeforeProgressImport();
    void environmentIsAppliedAndFullyDisplayed();
    void sensitiveProfileSnapshotsCanBeRedacted();
    void proxySettingsAreAppliedToChildEnvironment();
    void appearanceSettingsExposeEffectiveValues();
    void quickControlsUseTheApplicationPaletteWhenCreated();
    void directControlPaletteBindingsOverrideStyleDefaults();
    void applicationSettingsPersistAcrossInstances();
    void environmentEntriesAreValidatedAndLegacyDefaultsMigrated();
    void bundledPythonRepairsSystemPythonProfile();
    void runtimeSurvivesFastChildFailure();
    void runtimeAcceptsBareVcsDependencies();
    void runtimeBlocksMissingDependencies();
    void zludaBackendClassificationProtectsNvidia();
    void installedTorchBackendUsesVersionMetadata();
    void zludaRuntimePreparationStagesAliases();
    void zludaCachedContentIsVerified();
    void progressBridgeRejectsAdjacentModules();
    void zludaLocalIntegrationWhenConfigured();
    void zludaRuntimeManagerIntegrationWhenConfigured();
    void runtimeShutdownReleasesChildPort();
    void profilesPersistWithoutLeavingTheTestDirectory();
    void launchConfigurationEditsUseTargetedNotifications();
    void tqdmProgressIsSeparatedFromConsoleLog();
    void carriageReturnLineEndingsRemainNormalLogLines();
    void consoleDecodesSplitUtf8AndLocalText();
    void consoleDisplayTextSupportsDocumentSelection();
    void consoleViewRefreshesWhenLogCountChanges();
    void consoleHistoryIsBounded();
    void attentionOptionsUseRequestedOrderAndFlags();
    void profileNamesAreLimitedAndSuffixesFit();
    void currentProfileIsProtectedFromBatchDeletion();
    void catalogDefaultOptionsAreDescriptive();
    void englishCatalogContainsNoChineseLabels();
};
