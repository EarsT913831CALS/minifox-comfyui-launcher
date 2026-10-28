// ZLUDA detection, packages and staging.
#include "tst_Runtime.h"

void RuntimeTest::zludaBackendClassificationProtectsNvidia()
{
    using Backend = ZludaBootstrap::BackendKind;
    using Adapter = ZludaBootstrap::SystemAdapterKind;

    QCOMPARE(ZludaBootstrap::classifyBackend(
                 QStringLiteral("11.8"), {}, {QStringLiteral("NVIDIA GeForce RTX 4090")}),
             Backend::Nvidia);
    QCOMPARE(ZludaBootstrap::classifyBackend(
                 QStringLiteral("11.8"), {}, {QStringLiteral("AMD Radeon 780M Graphics [ZLUDA]")}),
             Backend::Zluda);
    QCOMPARE(ZludaBootstrap::classifyBackend(
                 {}, QStringLiteral("6.2"), {QStringLiteral("AMD Radeon RX 7900 XTX")}),
             Backend::Rocm);
    QCOMPARE(ZludaBootstrap::classifyBackend(
                 QStringLiteral("11.8"), {},
                 {QStringLiteral("AMD Radeon 780M Graphics [ZLUDA]"),
                  QStringLiteral("NVIDIA GeForce RTX 4060")}),
             Backend::Nvidia);

    QVERIFY(ZludaBootstrap::shouldProbeAdapters(
        {QStringLiteral("AMD Radeon 780M Graphics VEN_1002")}));
    QVERIFY(!ZludaBootstrap::shouldProbeAdapters(
        {QStringLiteral("NVIDIA GeForce RTX 4090 VEN_10DE")}));
    QVERIFY(!ZludaBootstrap::shouldProbeAdapters(
        {QStringLiteral("AMD Radeon 780M Graphics VEN_1002"),
         QStringLiteral("NVIDIA GeForce RTX 4060 VEN_10DE")}));
    QVERIFY(!ZludaBootstrap::shouldProbeAdapters(
        {QStringLiteral("AMD Radeon 780M Graphics")}, QStringLiteral("off")));
    QVERIFY(!ZludaBootstrap::shouldProbeAdapters(
        {QStringLiteral("NVIDIA GeForce RTX 4090")}, QStringLiteral("force")));
    QVERIFY(!ZludaBootstrap::shouldProbeAdapters(
        {QStringLiteral("AMD Radeon 780M Graphics"),
         QStringLiteral("NVIDIA GeForce RTX 4090")}, QStringLiteral("force")));
    QVERIFY(ZludaBootstrap::shouldProbeAdapters(
        {QStringLiteral("AMD Radeon 780M Graphics")}, QStringLiteral("force")));

    QCOMPARE(ZludaBootstrap::classifySystemAdapters(
                 {QStringLiteral("AMD Radeon 780M Graphics VEN_1002")}),
             Adapter::AmdOnly);
    QCOMPARE(ZludaBootstrap::classifySystemAdapters(
                 {QStringLiteral("NVIDIA GeForce RTX 4090 VEN_10DE")}),
             Adapter::NvidiaOnly);
    QCOMPARE(ZludaBootstrap::classifySystemAdapters(
                 {QStringLiteral("AMD Radeon 780M Graphics VEN_1002"),
                  QStringLiteral("NVIDIA GeForce RTX 4060 VEN_10DE")}),
             Adapter::Mixed);
    QCOMPARE(ZludaBootstrap::classifySystemAdapters({}), Adapter::Unknown);
}

void RuntimeTest::installedTorchBackendUsesVersionMetadata()
{
    using Backend = ZludaBootstrap::BackendKind;

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QDir root(temporaryDirectory.path());
    QVERIFY(root.mkpath(QStringLiteral("venv/Scripts")));
    QVERIFY(root.mkpath(QStringLiteral("venv/Lib/site-packages/torch")));

    const QString pythonPath = root.filePath(QStringLiteral("venv/Scripts/python.exe"));
    const QString versionPath =
        root.filePath(QStringLiteral("venv/Lib/site-packages/torch/version.py"));
    const auto writeFile = [](const QString &path, const QByteArray &contents) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && file.write(contents) == contents.size();
    };

    QVERIFY(writeFile(pythonPath, QByteArrayLiteral("python")));
    QVERIFY(writeFile(versionPath,
                      QByteArrayLiteral("__version__ = '2.7.0+rocm6.4'\n"
                                        "cuda = None\n"
                                        "hip = '6.4.0'\n")));
    QCOMPARE(ZludaBootstrap::classifyInstalledTorch(pythonPath), Backend::Rocm);

    QVERIFY(writeFile(versionPath,
                      QByteArrayLiteral("__version__: str = '2.8.0+cu128'\n"
                                        "cuda: str = '12.8'\n"
                                        "hip = None\n")));
    QCOMPARE(ZludaBootstrap::classifyInstalledTorch(pythonPath), Backend::Nvidia);

    QVERIFY(writeFile(versionPath,
                      QByteArrayLiteral("__version__ = '2.8.0+cpu'\n"
                                        "cuda = None\n"
                                        "hip = None\n")));
    QCOMPARE(ZludaBootstrap::classifyInstalledTorch(pythonPath), Backend::Unknown);
}

void RuntimeTest::zludaRuntimePreparationStagesAliases()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QDir root(temporaryDirectory.path());
    QVERIFY(root.mkpath(QStringLiteral("ComfyUI")));
    QVERIFY(root.mkpath(QStringLiteral("python/Lib/site-packages/torch/lib")));
    QVERIFY(root.mkpath(QStringLiteral("rocm/bin/rocblas/library")));

    const auto writeFile = [](const QString &path, const QByteArray &contents) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        return file.write(contents) == contents.size();
    };

    const QString pythonPath = root.filePath(QStringLiteral("python/python.exe"));
    QVERIFY(writeFile(pythonPath, QByteArrayLiteral("python")));
    const QString torchLib = root.filePath(QStringLiteral("python/Lib/site-packages/torch/lib"));
    QVERIFY(writeFile(QDir(torchLib).filePath(QStringLiteral("cublas64_11.dll")), QByteArrayLiteral("native")));
    QVERIFY(writeFile(QDir(torchLib).filePath(QStringLiteral("cusparse64_11.dll")), QByteArrayLiteral("native")));
    QVERIFY(writeFile(QDir(torchLib).filePath(QStringLiteral("nvrtc64_112_0.dll")), QByteArrayLiteral("native")));

    const QString rocmBin = root.filePath(QStringLiteral("rocm/bin"));
    QVERIFY(writeFile(QDir(rocmBin).filePath(QStringLiteral("amdhip64.dll")), QByteArrayLiteral("hip")));
    QVERIFY(writeFile(QDir(rocmBin).filePath(QStringLiteral("hipInfo.exe")), QByteArrayLiteral("hipinfo")));
    const QString tensileLibrary =
        QDir(rocmBin).filePath(QStringLiteral("rocblas/library"));
    QVERIFY(writeFile(QDir(tensileLibrary).filePath(
                          QStringLiteral("TensileLibrary_lazy_gfx903.dat")),
                      QByteArrayLiteral("gfx903")));

    QProcessEnvironment environment;
    environment.insert(QStringLiteral("PYTHONPATH"), QStringLiteral("C:/existing"));
    environment.insert(QStringLiteral("HIP_PATH"), root.filePath(QStringLiteral("rocm")));
    environment.insert(QStringLiteral("MINIFOX_ZLUDA_GFX_ARCH"), QStringLiteral("gfx903"));

    const ZludaBootstrap::Preparation preparation = ZludaBootstrap::prepare(
        pythonPath, root.filePath(QStringLiteral("ComfyUI")), environment);
    QVERIFY2(preparation.valid, qPrintable(preparation.error));
    QCOMPARE(QDir::cleanPath(preparation.sourceDirectory),
             QDir::cleanPath(root.filePath(QStringLiteral(".minifox/packages/zluda-hip57"))));
    QCOMPARE(preparation.gfxArchitecture, QStringLiteral("gfx903"));
    QCOMPARE(QDir::cleanPath(preparation.tensileLibraryDirectory),
             QDir::cleanPath(tensileLibrary));
    QCOMPARE(preparation.preloadNames,
             QStringList({QStringLiteral("nvcuda.dll"),
                          QStringLiteral("nvrtc64_112_0.dll"),
                          QStringLiteral("cublas64_11.dll"),
                          QStringLiteral("cusparse64_11.dll"),
                          QStringLiteral("nvml.dll")}));
    QVERIFY(QFileInfo::exists(
        QDir(preparation.runtimeDirectory).filePath(QStringLiteral("cublas64_11.dll"))));
    QVERIFY(QFileInfo::exists(
        QDir(preparation.bootstrapDirectory).filePath(QStringLiteral("sitecustomize.py"))));
    QFile bootstrapFile(
        QDir(preparation.bootstrapDirectory).filePath(QStringLiteral("sitecustomize.py")));
    QVERIFY(bootstrapFile.open(QIODevice::ReadOnly));
    const QByteArray bootstrapScript = bootstrapFile.readAll();
    QVERIFY(bootstrapScript.contains(
        "HIP_HOME = _join_rocm_home('hip') if ROCM_HOME else None"));
    QVERIFY(bootstrapScript.contains("HIP_HOME = ROCM_HOME"));
    QVERIFY(bootstrapScript.contains("module.backends.cudnn.enabled = False"));
    QVERIFY(bootstrapScript.contains("_force_flash_sdp_off"));
    QVERIFY(bootstrapScript.contains("_force_mem_efficient_sdp_off"));
    QVERIFY(bootstrapScript.contains("class _MinifoxTorchLoader"));
    QVERIFY(!bootstrapScript.contains("import torch as"));
    bootstrapFile.close();
    QVERIFY(QFileInfo::exists(
        root.filePath(QStringLiteral(".minifox/packages/zluda-hip57"))));
    QVERIFY(!QFileInfo::exists(
        root.filePath(QStringLiteral(".minifox/packages/tensile-gfx903.extpack"))));
    QVERIFY(QFileInfo::exists(QDir(preparation.tensileLibraryDirectory).filePath(
        QStringLiteral("TensileLibrary_lazy_gfx903.dat"))));
    QVERIFY(QFileInfo(root.filePath(QStringLiteral(".cache"))).isDir());
    QVERIFY(QFileInfo(preparation.zludaCacheDirectory).isDir());
    QVERIFY(QFileInfo(preparation.tritonCacheDirectory).isDir());
    QVERIFY(QFileInfo(preparation.torchInductorCacheDirectory).isDir());
    QVERIFY(preparation.rocmBinCandidates.contains(QDir::cleanPath(rocmBin)));

#ifdef Q_OS_WIN
    const QString lockedRuntimeDll =
        QDir(preparation.runtimeDirectory).filePath(QStringLiteral("nvcuda.dll"));
    const HANDLE lockedDll = CreateFileW(
        reinterpret_cast<LPCWSTR>(lockedRuntimeDll.utf16()),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    QVERIFY(lockedDll != INVALID_HANDLE_VALUE);
    const ZludaBootstrap::Preparation repeatedPreparation = ZludaBootstrap::prepare(
        pythonPath, root.filePath(QStringLiteral("ComfyUI")), environment);
    CloseHandle(lockedDll);
    QVERIFY2(repeatedPreparation.valid, qPrintable(repeatedPreparation.error));
    QCOMPARE(QDir::cleanPath(repeatedPreparation.runtimeDirectory),
             QDir::cleanPath(preparation.runtimeDirectory));
#endif

    ZludaBootstrap::apply(preparation, rocmBin, environment);
    QCOMPARE(environment.value(QStringLiteral("MINIFOX_ZLUDA_BOOTSTRAP")),
             QStringLiteral("1"));
    QCOMPARE(environment.value(QStringLiteral("MINIFOX_ROCM_BIN")), rocmBin);
    QCOMPARE(environment.value(QStringLiteral("ZLUDA_NVRTC_LIB")),
             QDir(preparation.runtimeDirectory).filePath(QStringLiteral("nvrtc64_112_0.dll")));
    QCOMPARE(environment.value(QStringLiteral("ZLUDA_CACHE_DIR")),
             preparation.zludaCacheDirectory);
    QCOMPARE(environment.value(QStringLiteral("TRITON_CACHE_DIR")),
             preparation.tritonCacheDirectory);
    QCOMPARE(environment.value(QStringLiteral("TORCHINDUCTOR_CACHE_DIR")),
             preparation.torchInductorCacheDirectory);
    QCOMPARE(environment.value(QStringLiteral("ROCBLAS_TENSILE_LIBPATH")),
             preparation.tensileLibraryDirectory);
    QCOMPARE(environment.value(QStringLiteral("DISABLE_ADDMM_CUDA_LT")),
             QStringLiteral("1"));
    QCOMPARE(environment.value(QStringLiteral("TORCH_BLAS_PREFER_HIPBLASLT")),
             QStringLiteral("0"));
    QCOMPARE(environment.value(QStringLiteral("HIP_PATH")),
             QDir::cleanPath(QDir(rocmBin).absoluteFilePath(QStringLiteral(".."))));
    QCOMPARE(environment.value(QStringLiteral("PATH")).split(QDir::listSeparator()).at(0),
             rocmBin);
    QCOMPARE(environment.value(QStringLiteral("PATH")).split(QDir::listSeparator()).at(1),
             preparation.runtimeDirectory);
    QCOMPARE(environment.value(QStringLiteral("PYTHONPATH")).split(QDir::listSeparator()).constFirst(),
             preparation.bootstrapDirectory);
    // Tamper after preparation: the Python loading boundary must reject the DLL
    // before LoadLibrary, even though all C++ checks previously succeeded.
    const QString actualPython = QStandardPaths::findExecutable("python"); QVERIFY(!actualPython.isEmpty());
    const QString sideDll = QDir(preparation.runtimeDirectory).filePath("amdhip64.dll");
    QVERIFY(writeFile(sideDll, "untrusted dependency"));
    const auto rejectedDll = ZludaBootstrap::prepare(pythonPath, root.filePath("ComfyUI"), environment);
    QVERIFY(!rejectedDll.valid); QVERIFY(!rejectedDll.error.isEmpty());
    QProcess loadProbe; loadProbe.setProcessEnvironment(environment);
    loadProbe.start(actualPython, {"-S", "-c", "import runpy,sys; m=runpy.run_path(sys.argv[1]); m['_minifox_preload_zluda']()",
        QDir(preparation.bootstrapDirectory).filePath("sitecustomize.py")});
    QVERIFY(loadProbe.waitForFinished()); QVERIFY(loadProbe.exitCode() != 0);
    QVERIFY(loadProbe.readAllStandardError().contains("Unexpected ZLUDA runtime directory contents"));
    QVERIFY(QFile::remove(sideDll));
    const QString adjacent = QDir(preparation.bootstrapDirectory).filePath("ctypes.py");
    QVERIFY(writeFile(adjacent, "raise RuntimeError('ADJACENT_BOOTSTRAP_MODULE')\n"));
    const auto rejected = ZludaBootstrap::prepare(pythonPath, root.filePath("ComfyUI"), environment);
    QVERIFY(!rejected.valid); QVERIFY(!rejected.error.isEmpty()); QVERIFY(QFile::remove(adjacent));
    const QString malicious = root.filePath("poison.py");
    QVERIFY(writeFile(malicious, "print('STALE_BYTECODE_LOADED')\n"));
    const QString sitecustomize = QDir(preparation.bootstrapDirectory).filePath("sitecustomize.py");
    QProcess compiler;
    compiler.start(actualPython, {"-c", "import py_compile,importlib.util,sys; py_compile.compile(sys.argv[1], cfile=importlib.util.cache_from_source(sys.argv[2]), invalidation_mode=py_compile.PycInvalidationMode.UNCHECKED_HASH)",
                                 malicious, sitecustomize});
    QVERIFY(compiler.waitForFinished()); QCOMPARE(compiler.exitCode(), 0);
    const auto cleared = ZludaBootstrap::prepare(pythonPath, root.filePath("ComfyUI"), environment);
    QVERIFY2(cleared.valid, qPrintable(cleared.error));
    QVERIFY(QDir(QDir(preparation.bootstrapDirectory).filePath("__pycache__")).entryList({"*.pyc"}, QDir::Files).isEmpty());
    QProcess startup; startup.setProcessEnvironment(environment);
    startup.start(actualPython, {"-c", "print('BOOTSTRAP_CONTROL_OK')"});
    QVERIFY(startup.waitForFinished()); QCOMPARE(startup.exitCode(), 0);
    const auto startupOutput = startup.readAllStandardOutput();
    QVERIFY(startupOutput.contains("BOOTSTRAP_CONTROL_OK")); QVERIFY(!startupOutput.contains("STALE_BYTECODE_LOADED"));
    QVERIFY2(startup.readAllStandardError().isEmpty(), "The generated sitecustomize must import successfully");
    const QString runtimeDll = QDir(preparation.runtimeDirectory).filePath("nvcuda.dll");
    QFile tamper(runtimeDll); QVERIFY(tamper.open(QIODevice::ReadWrite));
    QByteArray byte = tamper.read(1); byte[0] ^= 1;
    QVERIFY(tamper.seek(0)); QCOMPARE(tamper.write(byte), 1); tamper.close();
    QProcess process; process.setProcessEnvironment(environment);
    process.start(actualPython, {"-S", "-c",
        "import runpy,sys; m=runpy.run_path(sys.argv[1]); m['_minifox_preload_zluda']()",
        QDir(preparation.bootstrapDirectory).filePath("sitecustomize.py")});
    QVERIFY(process.waitForFinished(15000));
    QVERIFY(process.exitCode() != 0);
    const auto rejection = process.readAllStandardError();
    QVERIFY2(rejection.contains("ZLUDA DLL integrity check failed"), rejection.constData());
}

void RuntimeTest::zludaCachedContentIsVerified()
{
    QTemporaryDir temp; QString error;
    QVERIFY2(ZludaBootstrap::preloadEmbeddedPackages(temp.path(), &error), qPrintable(error));
    const auto corrupt = [](const QString &path) {
        QFile file(path); if (!file.open(QIODevice::ReadWrite)) return false;
        const auto size = file.size(); QByteArray byte = file.read(1);
        if (byte.isEmpty()) return false;
        byte[0] ^= 1;
        return file.seek(0) && file.write(byte) == 1 && file.size() == size;
    };
    const auto contents = [](const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; };
    for (const QString &name : {QString("zluda-hip57"), QString("zluda-hip71")}) {
        const QString archive = temp.filePath(".minifox/packages/" + name + ".extpack");
        const QString dll = temp.filePath(".minifox/packages/" + name + "/nvcuda.dll");
        const QByteArray originalArchive = contents(archive), originalDll = contents(dll);
        QVERIFY(corrupt(archive)); QVERIFY(corrupt(dll));
        QVERIFY2(ZludaBootstrap::preloadEmbeddedPackages(temp.path(), &error), qPrintable(error));
        QCOMPARE(contents(archive), originalArchive); QCOMPARE(contents(dll), originalDll);
    }
    const QString extra = temp.filePath(".minifox/packages/zluda-hip57/unknown.dll");
    QFile file(extra); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("unexpected"); file.close();
    QVERIFY(!ZludaBootstrap::preloadEmbeddedPackages(temp.path(), &error)); QVERIFY(!error.isEmpty());
    QVERIFY(file.remove());
#ifdef Q_OS_WIN
    const QString package = temp.filePath(".minifox/packages/zluda-hip57");
    QVERIFY(QDir().rename(package, package + "-original"));
    QTemporaryDir outside;
    QProcess junction; junction.start("cmd.exe", {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(package),
                                                QDir::toNativeSeparators(outside.path())});
    QVERIFY(junction.waitForFinished()); QCOMPARE(junction.exitCode(), 0);
    QVERIFY(!ZludaBootstrap::preloadEmbeddedPackages(temp.path(), &error));
    QVERIFY(QDir(outside.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
    QVERIFY(QDir().rmdir(package));
#endif
}

void RuntimeTest::zludaLocalIntegrationWhenConfigured()
{
    const QString pythonPath = qEnvironmentVariable("MINIFOX_TEST_ZLUDA_PYTHON");
    const QString comfyRoot = qEnvironmentVariable("MINIFOX_TEST_ZLUDA_COMFY_ROOT");
    if (pythonPath.isEmpty() || comfyRoot.isEmpty()) {
        QSKIP("Local ZLUDA integration paths were not provided");
    }
    QVERIFY2(ZludaBootstrap::shouldProbeSystem(),
             qPrintable(ZludaBootstrap::systemAdapterNames().join(QStringLiteral(" | "))));

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString rocmPath = qEnvironmentVariable("MINIFOX_TEST_ROCM_PATH");
    if (!rocmPath.isEmpty()) {
        environment.insert(QStringLiteral("HIP_PATH"), rocmPath);
    }

    const ZludaBootstrap::Preparation preparation =
        ZludaBootstrap::prepare(pythonPath, comfyRoot, environment);
    QVERIFY2(preparation.valid, qPrintable(preparation.error));
    QVERIFY(!preparation.rocmBinCandidates.isEmpty());
    ZludaBootstrap::apply(preparation, preparation.rocmBinCandidates.constFirst(), environment);

    QProcess probe;
    probe.setWorkingDirectory(comfyRoot);
    probe.setProcessEnvironment(environment);
    probe.setProgram(pythonPath);
    probe.setArguments({QStringLiteral("-c"), ZludaBootstrap::preflightScript()});
    probe.start();
    QVERIFY2(probe.waitForFinished(45000), qPrintable(probe.errorString()));
    const QByteArray output = probe.readAllStandardOutput();
    const QByteArray error = probe.readAllStandardError();
    QVERIFY2(probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0,
             qPrintable(QString::fromUtf8(error)));
    QVERIFY(output.contains("\"ok\": true"));
    QVERIFY(output.contains("\"active\": true"));
    QVERIFY(output.contains("\"cudnn_enabled\": false"));
    QVERIFY(output.contains("\"flash_sdp_enabled\": false"));
    QVERIFY(output.contains("\"mem_efficient_sdp_enabled\": false"));
    QVERIFY(output.contains("\"hip_include\":"));
}

void RuntimeTest::zludaRuntimeManagerIntegrationWhenConfigured()
{
    const QString pythonPath = qEnvironmentVariable("MINIFOX_TEST_ZLUDA_PYTHON");
    const QString rocmPath = qEnvironmentVariable("MINIFOX_TEST_ROCM_PATH");
    if (pythonPath.isEmpty() || rocmPath.isEmpty()) {
        QSKIP("Local ZLUDA integration paths were not provided");
    }

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    QDir root(temporaryDirectory.path());
    QVERIFY(root.mkdir(QStringLiteral("ComfyUI")));
    const QString comfyRoot = root.filePath(QStringLiteral("ComfyUI"));
    const QString marker = root.filePath(QStringLiteral("runtime-started.txt"));

    QFile mainFile(QDir(comfyRoot).filePath(QStringLiteral("main.py")));
    QVERIFY(mainFile.open(QIODevice::WriteOnly));
    mainFile.write(QStringLiteral(
        "from pathlib import Path\n"
        "import os\n"
        "assert os.environ.get('MINIFOX_ZLUDA_ACTIVE') == '1'\n"
        "Path(r'%1').write_text('started', encoding='utf-8')\n")
                       .arg(QDir::toNativeSeparators(marker)).toUtf8());
    mainFile.close();
    QFile requirementsFile(QDir(comfyRoot).filePath(QStringLiteral("requirements.txt")));
    QVERIFY(requirementsFile.open(QIODevice::WriteOnly));
    requirementsFile.close();

    ConfigurationManager configuration(
        root.filePath(QStringLiteral("profiles.json")));
    configuration.setComfyRoot(comfyRoot);
    configuration.setPythonPath(pythonPath);
    int environmentIndex = configuration.addEnvironmentEntry();
    configuration.updateEnvironmentEntry(
        environmentIndex, QStringLiteral("MINIFOX_ZLUDA_BOOTSTRAP"),
        QStringLiteral("force"), true);
    environmentIndex = configuration.addEnvironmentEntry();
    configuration.updateEnvironmentEntry(
        environmentIndex, QStringLiteral("HIP_PATH"),
        rocmPath, true);

    ApplicationSettings settings(root.filePath(QStringLiteral("settings.json")));
    RuntimeManager runtime(&configuration, &settings);
    runtime.start();
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker), 90000);
    runtime.shutdown();
}
