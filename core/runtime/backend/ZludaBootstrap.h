#pragma once

#include <QByteArray>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

class ZludaBootstrap final
{
public:
    enum class SystemAdapterKind {
        Unknown,
        AmdOnly,
        NvidiaOnly,
        Mixed
    };

    enum class BackendKind {
        Unknown,
        Nvidia,
        Zluda,
        Rocm,
        Other
    };

    struct Detection {
        BackendKind backend = BackendKind::Unknown;
        QString torchVersion;
        QString cudaVersion;
        QString hipVersion;
        QStringList deviceNames;
        QString error;
    };

    struct Preparation {
        bool valid = false;
        QString sourceDirectory;
        QString runtimeDirectory;
        QString bootstrapDirectory;
        QString zludaCacheDirectory;
        QString tritonCacheDirectory;
        QString torchInductorCacheDirectory;
        QString gfxArchitecture;
        QString tensileLibraryDirectory;
        QStringList preloadNames;
        QStringList rocmBinCandidates;
        QString error;
    };

    static QString detectionScript();
    static QString preflightScript();
    static Detection parseDetectionOutput(const QByteArray &output);
    static BackendKind classifyBackend(const QString &cudaVersion,
                                       const QString &hipVersion,
                                       const QStringList &deviceNames);

    static QStringList systemAdapterNames();
    static SystemAdapterKind classifySystemAdapters(const QStringList &adapterNames);
    static bool shouldProbeAdapters(const QStringList &adapterNames,
                                    const QString &mode = QStringLiteral("auto"));
    static bool shouldProbeSystem(const QString &mode = QStringLiteral("auto"));

    static Preparation prepare(const QString &pythonPath,
                               const QString &comfyRoot,
                               const QProcessEnvironment &environment);
    static void apply(const Preparation &preparation,
                      const QString &rocmBin,
                      QProcessEnvironment &environment);
};
