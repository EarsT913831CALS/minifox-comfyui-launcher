// Launch profiles and the parameter catalog.
#include "tst_Runtime.h"

void RuntimeTest::environmentEntriesAreValidatedAndLegacyDefaultsMigrated()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString storagePath = temporaryDirectory.filePath(QStringLiteral("profiles.json"));

    const QJsonArray environment {
        QJsonObject{{QStringLiteral("name"), QStringLiteral("PYTHONUTF8")},
                    {QStringLiteral("value"), QStringLiteral("1")},
                    {QStringLiteral("enabled"), true}},
        QJsonObject{{QStringLiteral("name"), QStringLiteral("PYTHONUNBUFFERED")},
                    {QStringLiteral("value"), QStringLiteral("1")},
                    {QStringLiteral("enabled"), false}},
        QJsonObject{{QStringLiteral("name"), QStringLiteral("CUSTOM_KEEP")},
                    {QStringLiteral("value"), QStringLiteral("kept")},
                    {QStringLiteral("enabled"), true}}
    };
    const QJsonObject profile {
        {QStringLiteral("id"), QStringLiteral("legacy-profile")},
        {QStringLiteral("name"), QStringLiteral("Legacy")},
        {QStringLiteral("parameters"), QJsonObject{}},
        {QStringLiteral("environment"), environment}
    };
    const QJsonObject root {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("currentProfileId"), QStringLiteral("legacy-profile")},
        {QStringLiteral("profiles"), QJsonArray{profile}}
    };

    QFile sourceFile(storagePath);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    const QByteArray sourceData = QJsonDocument(root).toJson();
    QCOMPARE(sourceFile.write(sourceData), static_cast<qint64>(sourceData.size()));
    sourceFile.close();

    ConfigurationManager manager(storagePath);
    QCOMPARE(manager.environmentEntries().size(), 1);
    QCOMPARE(manager.environmentEntries().constFirst().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("CUSTOM_KEEP"));

    {
        QFile migratedFile(storagePath);
        QVERIFY(migratedFile.open(QIODevice::ReadOnly));
        const QJsonObject migratedRoot = QJsonDocument::fromJson(migratedFile.readAll()).object();
        QCOMPARE(migratedRoot.value(QStringLiteral("schemaVersion")).toInt(), 2);
        QCOMPARE(migratedRoot.value(QStringLiteral("profiles")).toArray().at(0).toObject()
                     .value(QStringLiteral("environment")).toArray().size(),
                 1);
    }

    const int invalidIndex = manager.addEnvironmentEntry();
    QVERIFY(!manager.environmentEntries().at(invalidIndex).toMap().value(QStringLiteral("error")).toString().isEmpty());
    manager.updateEnvironmentEntry(invalidIndex, QStringLiteral("1INVALID"), QStringLiteral("value"), true);
    QVERIFY(!manager.environmentEntries().at(invalidIndex).toMap().value(QStringLiteral("error")).toString().isEmpty());
    manager.updateEnvironmentEntry(invalidIndex, QStringLiteral("CUSTOM_ENV"), QStringLiteral("value"), true);
    QVERIFY(manager.environmentEntries().at(invalidIndex).toMap().value(QStringLiteral("error")).toString().isEmpty());

    const int duplicateIndex = manager.addEnvironmentEntry();
    manager.updateEnvironmentEntry(duplicateIndex, QStringLiteral("custom_env"), QStringLiteral("other"), true);
    QVERIFY(!manager.environmentEntries().at(invalidIndex).toMap().value(QStringLiteral("error")).toString().isEmpty());
    QVERIFY(!manager.environmentEntries().at(duplicateIndex).toMap().value(QStringLiteral("error")).toString().isEmpty());
    manager.updateEnvironmentEntry(duplicateIndex, QStringLiteral("custom_env"), QStringLiteral("other"), false);
    QVERIFY(manager.environmentEntries().at(invalidIndex).toMap().value(QStringLiteral("error")).toString().isEmpty());
}

void RuntimeTest::profilesPersistWithoutLeavingTheTestDirectory()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString storagePath = temporaryDirectory.filePath(QStringLiteral("profiles.json"));

    {
        ConfigurationManager manager(storagePath);
        QCOMPARE(manager.profileNames().size(), 1);
        QVERIFY(manager.environmentEntries().isEmpty());
        manager.addProfile(QStringLiteral("GPU profile"));
        manager.setCustomArguments(QStringLiteral("--custom value"));
        const int environmentIndex = manager.addEnvironmentEntry();
        QCOMPARE(environmentIndex, 0);
        manager.updateEnvironmentEntry(environmentIndex, QStringLiteral("CUSTOM_ENV"), QStringLiteral("enabled"), true);
        QCOMPARE(manager.profileNames().size(), 2);
        QCOMPARE(manager.currentProfileName(), QStringLiteral("GPU profile"));
        QVERIFY(manager.savePendingChanges());
    }

    ConfigurationManager restored(storagePath);
    QCOMPARE(restored.profileNames().size(), 2);
    QCOMPARE(restored.currentProfileName(), QStringLiteral("GPU profile"));
    QCOMPARE(restored.customArguments(), QStringLiteral("--custom value"));
    QCOMPARE(restored.environmentEntries().at(0).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("CUSTOM_ENV"));

    restored.setCustomArguments({});
    QVERIFY(restored.savePendingChanges());
    ConfigurationManager withoutCustomArguments(storagePath);
    QVERIFY(withoutCustomArguments.customArguments().isEmpty());

    withoutCustomArguments.setCustomArguments(QStringLiteral("--new-custom value"));
    QVERIFY(withoutCustomArguments.savePendingChanges());
    ConfigurationManager withNewCustomArguments(storagePath);
    QCOMPARE(withNewCustomArguments.customArguments(), QStringLiteral("--new-custom value"));

    QString inactiveProfileId;
    for (const QVariant &entry : withNewCustomArguments.profileEntries()) {
        const QVariantMap profile = entry.toMap();
        if (!profile.value(QStringLiteral("current")).toBool()) {
            inactiveProfileId = profile.value(QStringLiteral("id")).toString();
            break;
        }
    }
    QVERIFY(!inactiveProfileId.isEmpty());
    QVERIFY(withNewCustomArguments.removeProfiles({inactiveProfileId}));
    QCOMPARE(withNewCustomArguments.profileNames().size(), 1);
    QCOMPARE(withNewCustomArguments.currentProfileName(), QStringLiteral("GPU profile"));
}

void RuntimeTest::launchConfigurationEditsUseTargetedNotifications()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString storagePath = temporaryDirectory.filePath(QStringLiteral("profiles.json"));

    ConfigurationManager manager(storagePath);
    QSignalSpy parameterChanges(&manager, &ConfigurationManager::parameterValueChanged);
    QSignalSpy revisionChanges(&manager, &ConfigurationManager::parameterRevisionChanged);
    QSignalSpy profileChanges(&manager, &ConfigurationManager::currentProfileChanged);

    manager.setParameterValue(QStringLiteral("listen"), QStringLiteral("0.0.0.0"));
    QCOMPARE(parameterChanges.count(), 1);
    const auto parameterArguments = parameterChanges.takeFirst();
    QCOMPARE(parameterArguments.at(0).toString(), QStringLiteral("listen"));
    QCOMPARE(parameterArguments.at(1).toString(), QStringLiteral("0.0.0.0"));
    QCOMPARE(revisionChanges.count(), 0);
    QCOMPARE(profileChanges.count(), 0);

    QVERIFY(manager.hasPendingChanges());
    ConfigurationManager beforeParameterSave(storagePath);
    QCOMPARE(beforeParameterSave.parameterValue(QStringLiteral("listen")).toString(),
             QStringLiteral("127.0.0.1"));
    QVERIFY(manager.savePendingChanges());
    QVERIFY(!manager.hasPendingChanges());
    ConfigurationManager restoredParameter(storagePath);
    QCOMPARE(restoredParameter.parameterValue(QStringLiteral("listen")).toString(),
             QStringLiteral("0.0.0.0"));

    const int environmentIndex = manager.addEnvironmentEntry();
    QCOMPARE(environmentIndex, 0);
    profileChanges.clear();
    QSignalSpy environmentChanges(&manager, &ConfigurationManager::environmentEntryChanged);
    manager.updateEnvironmentEntry(environmentIndex, QStringLiteral("CUSTOM_ENV"),
                                   QStringLiteral("live-value"), true);

    QCOMPARE(environmentChanges.count(), 1);
    const auto environmentArguments = environmentChanges.takeFirst();
    QCOMPARE(environmentArguments.at(0).toInt(), environmentIndex);
    const QVariantMap entry = environmentArguments.at(1).toMap();
    QCOMPARE(entry.value(QStringLiteral("name")).toString(), QStringLiteral("CUSTOM_ENV"));
    QCOMPARE(entry.value(QStringLiteral("value")).toString(), QStringLiteral("live-value"));
    QVERIFY(entry.value(QStringLiteral("enabled")).toBool());
    QCOMPARE(revisionChanges.count(), 0);
    QCOMPARE(profileChanges.count(), 0);

    QVERIFY(manager.savePendingChanges());
    ConfigurationManager restoredEnvironment(storagePath);
    const QVariantMap restoredEntry =
        restoredEnvironment.environmentEntries().at(environmentIndex).toMap();
    QCOMPARE(restoredEntry.value(QStringLiteral("name")).toString(), QStringLiteral("CUSTOM_ENV"));
    QCOMPARE(restoredEntry.value(QStringLiteral("value")).toString(), QStringLiteral("live-value"));

    const int duplicateIndex = manager.addEnvironmentEntry();
    environmentChanges.clear();
    manager.updateEnvironmentEntry(duplicateIndex, QStringLiteral("CUSTOM_ENV"),
                                   QStringLiteral("duplicate-value"), true);
    QCOMPARE(environmentChanges.count(), 2);
    for (const auto &arguments : environmentChanges) {
        QVERIFY(!arguments.at(1).toMap().value(QStringLiteral("error")).toString().isEmpty());
    }
}

void RuntimeTest::attentionOptionsUseRequestedOrderAndFlags()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ConfigurationManager manager(directory.filePath(QStringLiteral("profiles.json")));

    QVariantMap attention;
    for (const QVariant &entry : manager.parametersForCategory(QStringLiteral("performance"))) {
        const QVariantMap parameter = entry.toMap();
        if (parameter.value(QStringLiteral("key")).toString() == QStringLiteral("attention")) {
            attention = parameter;
            break;
        }
    }
    QVERIFY(!attention.isEmpty());

    QStringList values;
    QStringList labels;
    QStringList flags;
    for (const QVariant &entry : attention.value(QStringLiteral("options")).toList()) {
        const QVariantMap option = entry.toMap();
        values.append(option.value(QStringLiteral("value")).toString());
        labels.append(option.value(QStringLiteral("label")).toString());
        flags.append(option.value(QStringLiteral("argument")).toString());
    }
    QCOMPARE(values, QStringList({QString(), QStringLiteral("ck"),
                                  QStringLiteral("pytorch"), QStringLiteral("sage"),
                                  QStringLiteral("quad"), QStringLiteral("flash"),
                                  QStringLiteral("split")}));
    const QString defaultLabel = QLocale().language() == QLocale::English
        ? QStringLiteral("Default (automatic selection)")
        : QStringLiteral("默认（自动选择）");
    QCOMPARE(labels, QStringList({defaultLabel,
                                  QStringLiteral("Comfy-Kitchen Attention"),
                                  QStringLiteral("SDP Attention"),
                                  QStringLiteral("Sage Attention"),
                                  QStringLiteral("SubQ Attention"),
                                  QStringLiteral("FlashAttention"),
                                  QStringLiteral("Split Attention")}));
    QCOMPARE(flags, QStringList({QString(), QStringLiteral("--use-ck-attention"),
                                 QStringLiteral("--use-pytorch-cross-attention"),
                                 QStringLiteral("--use-sage-attention"),
                                 QStringLiteral("--use-quad-cross-attention"),
                                 QStringLiteral("--use-flash-attention"),
                                 QStringLiteral("--use-split-cross-attention")}));

    for (int index = 0; index < values.size(); ++index) {
        const QVariantMap profile{
            {QStringLiteral("pythonPath"), QStringLiteral("C:/Python/python.exe")},
            {QStringLiteral("comfyRoot"), QStringLiteral("C:/ComfyUI")},
            {QStringLiteral("parameters"),
             QVariantMap{{QStringLiteral("attention"), values.at(index)}}},
            {QStringLiteral("environment"), QVariantList{}}
        };
        const auto result = LaunchCommandBuilder::build(profile);
        if (flags.at(index).isEmpty()) {
            QCOMPARE(result.arguments, QStringList{QStringLiteral("main.py")});
        } else {
            QVERIFY(result.arguments.contains(flags.at(index)));
        }
    }
}

void RuntimeTest::profileNamesAreLimitedAndSuffixesFit()
{
    const QString longName = QStringLiteral("一二三四五六七八九十甲乙丙丁戊己庚辛壬癸");
    const QString limited = ConfigurationManager::limitedProfileName(longName);
    QCOMPARE(limited, QStringLiteral("一二三四五六七八九十甲乙丙丁戊己庚辛"));

    const QString duplicate = ConfigurationManager::profileNameWithSuffix(
        longName, QStringLiteral(" (2)"));
    QVERIFY(duplicate.endsWith(QStringLiteral(" (2)")));
    QCOMPARE(ConfigurationManager::limitedProfileName(duplicate), duplicate);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ConfigurationManager manager(directory.filePath(QStringLiteral("profiles.json")));
    manager.setCurrentProfileName(longName);
    QCOMPARE(manager.currentProfileName(), limited);
    manager.duplicateCurrentProfile();
    QVERIFY(manager.currentProfileName().endsWith(QStringLiteral(" - 副本")));
    QCOMPARE(ConfigurationManager::limitedProfileName(manager.currentProfileName()),
             manager.currentProfileName());
}

void RuntimeTest::currentProfileIsProtectedFromBatchDeletion()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ConfigurationManager manager(directory.filePath(QStringLiteral("profiles.json")));
    manager.addProfile(QStringLiteral("Second"));
    manager.addProfile(QStringLiteral("Current"));

    QString currentId;
    QStringList inactiveIds;
    for (const QVariant &entry : manager.profileEntries()) {
        const QVariantMap profile = entry.toMap();
        if (profile.value(QStringLiteral("current")).toBool()) {
            currentId = profile.value(QStringLiteral("id")).toString();
        } else {
            inactiveIds.append(profile.value(QStringLiteral("id")).toString());
        }
    }
    QVERIFY(!currentId.isEmpty());
    QCOMPARE(inactiveIds.size(), 2);

    QVERIFY(!manager.removeProfiles({currentId}));
    QCOMPARE(manager.profileNames().size(), 3);
    QCOMPARE(manager.currentProfileName(), QStringLiteral("Current"));

    QStringList mixedSelection = inactiveIds;
    mixedSelection.append(currentId);
    QVERIFY(manager.removeProfiles(mixedSelection));
    QCOMPARE(manager.profileNames(), QStringList{QStringLiteral("Current")});
    QCOMPARE(manager.currentProfileName(), QStringLiteral("Current"));
    QCOMPARE(manager.profileEntries().constFirst().toMap()
                 .value(QStringLiteral("id")).toString(), currentId);
}

void RuntimeTest::catalogDefaultOptionsAreDescriptive()
{
    const QLocale previousLocale;
    QLocale::setDefault(QLocale(QStringLiteral("zh_CN")));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ConfigurationManager manager(directory.filePath(QStringLiteral("profiles.json")));

    for (const QVariant &categoryEntry : manager.categories()) {
        const QVariantMap category = categoryEntry.toMap();
        const QVariantList parameters = manager.parametersForCategory(
            category.value(QStringLiteral("key")).toString());
        for (const QVariant &parameterEntry : parameters) {
            const QVariantMap parameter = parameterEntry.toMap();
            for (const QVariant &optionEntry : parameter.value(QStringLiteral("options")).toList()) {
                const QString label = optionEntry.toMap().value(QStringLiteral("label")).toString();
                QVERIFY2(label != QStringLiteral("默认"),
                         qPrintable(parameter.value(QStringLiteral("key")).toString()));
            }
        }
    }
    QLocale::setDefault(previousLocale);
}

void RuntimeTest::englishCatalogContainsNoChineseLabels()
{
    const QLocale previousLocale;
    QLocale::setDefault(QLocale(QStringLiteral("en_US")));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ConfigurationManager manager(directory.filePath(QStringLiteral("profiles.json")));
    const QRegularExpression chinese(QStringLiteral("[\\x{4e00}-\\x{9fff}]"));

    const QVariantList categories = manager.categories();
    QVERIFY(!categories.isEmpty());
    for (const QVariant &categoryEntry : categories) {
        const QVariantMap category = categoryEntry.toMap();
        QVERIFY(!chinese.match(category.value(QStringLiteral("title")).toString()).hasMatch());
        QVERIFY(!chinese.match(category.value(QStringLiteral("description")).toString()).hasMatch());

        const QVariantList parameters = manager.parametersForCategory(
            category.value(QStringLiteral("key")).toString());
        for (const QVariant &parameterEntry : parameters) {
            const QVariantMap parameter = parameterEntry.toMap();
            QVERIFY(!chinese.match(parameter.value(QStringLiteral("title")).toString()).hasMatch());
            QVERIFY(!chinese.match(parameter.value(QStringLiteral("description")).toString()).hasMatch());
            for (const QVariant &optionEntry : parameter.value(QStringLiteral("options")).toList()) {
                QVERIFY(!chinese.match(optionEntry.toMap().value(QStringLiteral("label")).toString()).hasMatch());
            }
        }
    }
    QLocale::setDefault(previousLocale);
}
