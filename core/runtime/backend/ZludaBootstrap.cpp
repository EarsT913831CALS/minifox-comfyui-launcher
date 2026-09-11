#include <QCoreApplication>
#include "ZludaBootstrap.h"

#include "ProcessTextDecoder.h"
#include "SafeDataPath.h"
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QLockFile>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QResource>
#include <QSaveFile>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

static void initializeZludaPackageResources()
{
    static const bool initialized = [] {
        Q_INIT_RESOURCE(minifox_zluda_packages);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace {

bool containsAny(const QString &value, const QStringList &needles)
{
    for (const QString &needle : needles) {
        if (value.contains(needle, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

void appendUniquePath(QStringList &paths, const QString &candidate)
{
    if (candidate.trimmed().isEmpty()) {
        return;
    }
    QFileInfo info(candidate);
    QString path = info.isDir() ? info.absoluteFilePath() : candidate;
    const auto hasHipRuntime = [](const QString &directory) {
        const QDir dir(directory);
        return QFileInfo(dir.filePath(QStringLiteral("amdhip64.dll"))).isFile()
            || QFileInfo(dir.filePath(QStringLiteral("amdhip64_7.dll"))).isFile();
    };
    const auto hasHipInfo = [](const QString &directory) {
        return QFileInfo(QDir(directory).filePath(QStringLiteral("hipInfo.exe"))).isFile();
    };
    if (hasHipRuntime(QDir(path).filePath(QStringLiteral("bin")))
        && hasHipInfo(QDir(path).filePath(QStringLiteral("bin")))) {
        path = QDir(path).filePath(QStringLiteral("bin"));
    }
    if (!hasHipRuntime(path) || !hasHipInfo(path)) {
        return;
    }
    path = QDir::cleanPath(path);
    for (const QString &existing : std::as_const(paths)) {
        if (existing.compare(path, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    paths.append(path);
}

QString cacheTagForRocmBin(const QString &rocmBin)
{
    const QString rocmRoot = QDir::cleanPath(QDir(rocmBin).absoluteFilePath(QStringLiteral("..")));
    QString version = QFileInfo(rocmRoot).fileName().trimmed();
    static const QRegularExpression versionExpression(QStringLiteral(R"(^[0-9]+(?:\.[0-9]+)*)"));
    if (!versionExpression.match(version).hasMatch()) {
        if (QFileInfo(QDir(rocmBin).filePath(QStringLiteral("amdhip64_7.dll"))).isFile()) {
            version = QStringLiteral("7");
        } else if (QFileInfo(QDir(rocmBin).filePath(QStringLiteral("amdhip64.dll"))).isFile()) {
            version = QStringLiteral("5");
        }
    }
    version.replace(QRegularExpression(QStringLiteral(R"([^A-Za-z0-9._-])")), QStringLiteral("-"));
    return version.isEmpty() ? QStringLiteral("hip-unknown") : QStringLiteral("hip-%1").arg(version);
}

int hipMajorVersionForRocmBin(const QString &rocmBin)
{
    const QRegularExpression expression(QStringLiteral(R"(hip-([0-9]+))"),
                                        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(cacheTagForRocmBin(rocmBin));
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

QString pythonEnvironmentRoot(const QString &pythonPath)
{
    QDir directory(QFileInfo(pythonPath).absolutePath());
    if (directory.dirName().compare(QStringLiteral("Scripts"), Qt::CaseInsensitive) == 0) {
        directory.cdUp();
    }
    return directory.absolutePath();
}

QString torchLibraryDirectory(const QString &pythonPath)
{
    return QDir(pythonEnvironmentRoot(pythonPath))
        .filePath(QStringLiteral("Lib/site-packages/torch/lib"));
}

QStringList matchingFileNames(const QString &directory, const QString &pattern)
{
    return QDir(directory).entryList({pattern}, QDir::Files, QDir::Name);
}

QString findSourceFile(const QString &sourceDirectory,
                       const QString &genericName,
                       const QString &targetName)
{
    const QString generic = QDir(sourceDirectory).filePath(genericName);
    if (QFileInfo::exists(generic)) {
        return generic;
    }
    const QString versioned = QDir(sourceDirectory).filePath(targetName);
    return QFileInfo::exists(versioned) ? versioned : QString();
}

bool hasZludaFiles(const QString &directory)
{
    const QDir source(directory);
    if (!QFileInfo::exists(source.filePath(QStringLiteral("nvcuda.dll")))) {
        return false;
    }
    const bool cublas = QFileInfo::exists(source.filePath(QStringLiteral("cublas.dll")))
        || !source.entryList({QStringLiteral("cublas64_*.dll")}, QDir::Files).isEmpty();
    const bool cusparse = QFileInfo::exists(source.filePath(QStringLiteral("cusparse.dll")))
        || !source.entryList({QStringLiteral("cusparse64_*.dll")}, QDir::Files).isEmpty();
    const bool nvrtc = QFileInfo::exists(source.filePath(QStringLiteral("nvrtc.dll")))
        || !source.entryList({QStringLiteral("nvrtc64_*.dll")}, QDir::Files).isEmpty();
    return cublas && cusparse && nvrtc;
}

QString configuredOrPortableZludaSource(const QString &portableRoot,
                                        const QProcessEnvironment &environment)
{
    QStringList candidates;
    candidates.append(environment.value(QStringLiteral("MINIFOX_ZLUDA_DIR")));
    const QDir root(portableRoot);
    candidates.append({
        root.filePath(QStringLiteral("zluda")),
        root.filePath(QStringLiteral(".zluda")),
        root.filePath(QStringLiteral("runtime/zluda")),
        root.filePath(QStringLiteral(".minifox/packages/zluda"))
    });

    for (const QString &candidate : std::as_const(candidates)) {
        const QString clean = QDir::cleanPath(candidate);
        if (hasZludaFiles(clean)) {
            return clean;
        }
    }
    return {};
}

QString pathZludaSource(const QProcessEnvironment &environment)
{
    for (const QString &candidate :
         environment.value(QStringLiteral("PATH")).split(QDir::listSeparator(),
                                                           Qt::SkipEmptyParts)) {
        const QString clean = QDir::cleanPath(candidate);
        if (hasZludaFiles(clean)) {
            return clean;
        }
    }
    return {};
}

QString tarExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("tar.exe"));
}

QString extractAkiExtpack(const QString &portableRoot,
                          const QString &runtimeDirectory,
                          QString *error)
{
    const QDir launcherDirectory(QDir(portableRoot).filePath(QStringLiteral(".launcher")));
    const QFileInfoList archives = launcherDirectory.entryInfoList(
        {QStringLiteral("zluda*.extpack")}, QDir::Files, QDir::Time);
    if (archives.isEmpty()) {
        return {};
    }

    const QString extractedDirectory =
        QDir(runtimeDirectory).filePath(QStringLiteral("aki-extpack"));
    if (hasZludaFiles(extractedDirectory)) {
        return extractedDirectory;
    }
    if (!QDir().mkpath(extractedDirectory)) {
        if (error) {
            *error = QCoreApplication::translate("ZludaBootstrap", "无法创建 ZLUDA 运行时目录：%1").arg(extractedDirectory);
        }
        return {};
    }

    const QString tar = tarExecutable();
    if (tar.isEmpty()) {
        if (error) {
            *error = QCoreApplication::translate("ZludaBootstrap", "找到秋叶 ZLUDA 扩展包，但系统缺少 tar.exe，无法解包。");
        }
        return {};
    }

    QProcess extractor;
    extractor.setProgram(tar);
    extractor.setArguments({
        QStringLiteral("-xf"),
        archives.constFirst().absoluteFilePath(),
        QStringLiteral("-C"),
        extractedDirectory
    });
    extractor.start();
    if (!extractor.waitForFinished(15000)
        || extractor.exitStatus() != QProcess::NormalExit
        || extractor.exitCode() != 0
        || !hasZludaFiles(extractedDirectory)) {
        if (error) {
            const QString detail = ProcessTextDecoder::decode(
                extractor.readAllStandardError()).trimmed();
            *error = detail.isEmpty()
                ? QCoreApplication::translate("ZludaBootstrap", "无法解包秋叶 ZLUDA 扩展包。")
                : QCoreApplication::translate("ZludaBootstrap", "无法解包秋叶 ZLUDA 扩展包：%1").arg(detail);
        }
        return {};
    }
    return extractedDirectory;
}

bool filesHaveSameContents(const QString &firstPath, const QString &secondPath)
{
    const QFileInfo firstInfo(firstPath);
    const QFileInfo secondInfo(secondPath);
    if (!firstInfo.isFile()
        || !secondInfo.isFile()
        || firstInfo.size() != secondInfo.size()) {
        return false;
    }

    QFile first(firstPath);
    QFile second(secondPath);
    if (!first.open(QIODevice::ReadOnly) || !second.open(QIODevice::ReadOnly)) {
        return false;
    }

    constexpr qint64 chunkSize = 1024 * 1024;
    while (true) {
        const QByteArray firstChunk = first.read(chunkSize);
        const QByteArray secondChunk = second.read(chunkSize);
        if (firstChunk != secondChunk) {
            return false;
        }
        if (firstChunk.isEmpty()) {
            return first.atEnd()
                && second.atEnd()
                && first.error() == QFileDevice::NoError
                && second.error() == QFileDevice::NoError;
        }
    }
}

bool copyAtomically(const QString &source, const QString &destination, QString *error)
{
    // A previously prepared ZLUDA DLL may already be mapped into a Python
    // process. Windows cannot replace that file, but no replacement is needed
    // when the embedded package contains the exact same bytes.
    if (filesHaveSameContents(source, destination)) {
        return true;
    }

    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QCoreApplication::translate("ZludaBootstrap", "无法读取 ZLUDA 文件：%1").arg(source);
        }
        return false;
    }
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(input.readAll()) < 0
        || !output.commit()) {
        if (error) {
            *error = QCoreApplication::translate("ZludaBootstrap", "无法写入 ZLUDA 运行时文件：%1").arg(destination);
        }
        return false;
    }
    return true;
}

QByteArray fileSha256(const QString &path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return {};
    return hash.result().toHex();
}

QJsonObject embeddedManifest(const QString &packageName)
{
    initializeZludaPackageResources();
    QFile file(QStringLiteral(":/minifox/zluda/manifest.json"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(packageName).toObject();
}

QString materializeEmbeddedArchive(const QString &portableRoot,
                                   const QString &packageName,
                                   QString *error)
{
    const QJsonObject manifest = embeddedManifest(packageName);
    const QByteArray expected = manifest.value("archiveSha256").toString().toLatin1();
    const QString resource = QStringLiteral(":/minifox/zluda/%1").arg(packageName);
    if (expected.size() != 64 || fileSha256(resource) != expected) {
        if (error) *error = QCoreApplication::translate("ZludaBootstrap", "内置 ZLUDA 运行包与可信清单不一致：%1").arg(packageName);
        return {};
    }
    const QString directory = QDir(portableRoot).filePath(".minifox/packages");
    const QString archive = QDir(directory).filePath(packageName);
    SafeDataPath guard;
    if (!guard.lock(archive, error) || !QDir().mkpath(directory) || !guard.lock(archive, error)) return {};
    if (fileSha256(archive) == expected) return archive;
    if (!copyAtomically(resource, archive, error) || fileSha256(archive) != expected) {
        if (error && error->isEmpty()) *error = QCoreApplication::translate("ZludaBootstrap", "ZLUDA 缓存归档校验失败。");
        return {};
    }
    return archive;
}

bool expectedPackageEntries(const QString &directory, const QJsonObject &files,
                            const QString &legacyStamp, QString *error)
{
    SafeDataPath guard;
    if (!guard.lock(directory, error)) return false;
    for (const QFileInfo &entry : QDir(directory).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
        if (!guard.lock(entry.absoluteFilePath(), error)) return false;
        if (!entry.isFile() || (!files.contains(entry.fileName()) && entry.fileName() != legacyStamp)) {
            if (error) *error = QCoreApplication::translate("ZludaBootstrap", "ZLUDA 缓存包含非预期条目，请检查：%1").arg(entry.absoluteFilePath());
            return false;
        }
    }
    return true;
}

bool packageMatches(const QString &directory, const QJsonObject &files)
{
    if (files.isEmpty()) return false;
    for (auto it = files.begin(); it != files.end(); ++it)
        if (fileSha256(QDir(directory).filePath(it.key())) != it.value().toString().toLatin1()) return false;
    return true;
}

bool extractEmbeddedArchive(const QString &archivePath,
                            const QString &destination,
                            const QString &validationPattern,
                            QString *error)
{
    Q_UNUSED(validationPattern);
    const QString name = QFileInfo(archivePath).fileName();
    const QJsonObject manifest = embeddedManifest(name);
    const QJsonObject files = manifest.value("files").toObject();
    const QString legacyStamp = ".minifox-package-" + QFileInfo(archivePath).completeBaseName();
    SafeDataPath guard;
    if (!guard.lock(archivePath, error) || !guard.lock(destination, error)) return false;
    QLockFile lock(destination + ".lock");
    if (!lock.tryLock(30000)) {
        if (error) *error = QCoreApplication::translate("ZludaBootstrap", "另一个进程正在准备 ZLUDA 运行包。");
        return false;
    }
    if (files.isEmpty() || fileSha256(archivePath) != manifest.value("archiveSha256").toString().toLatin1()) {
        if (error) *error = QCoreApplication::translate("ZludaBootstrap", "ZLUDA 归档校验失败，未解包。");
        return false;
    }
    if (!expectedPackageEntries(destination, files, legacyStamp, error)) return false;
    if (packageMatches(destination, files)) return true;

    QTemporaryDir staging(QDir(QFileInfo(destination).absolutePath()).filePath("zluda-staging-XXXXXX"));
    if (!staging.isValid()) {
        if (error) *error = QCoreApplication::translate("ZludaBootstrap", "无法创建 ZLUDA 临时解包目录。");
        return false;
    }
    // Extract a private copy of the embedded archive, never the mutable cache.
    const QString stagedArchive = staging.filePath("package.extpack");
    if (!copyAtomically(":/minifox/zluda/" + name, stagedArchive, error)) return false;
    const QString unpack = staging.filePath("files");
    if (!QDir().mkpath(unpack)) return false;
    QProcess extractor;
    extractor.start(tarExecutable(), {"-xf", stagedArchive, "-C", unpack});
    if (!extractor.waitForFinished(30000) || extractor.exitStatus() != QProcess::NormalExit
        || extractor.exitCode() != 0) {
        extractor.kill(); extractor.waitForFinished(1000);
        if (error) *error = QCoreApplication::translate("ZludaBootstrap", "无法解包内置 ZLUDA 运行包：%1").arg(ProcessTextDecoder::decode(extractor.readAllStandardError()));
        return false;
    }
    if (!expectedPackageEntries(unpack, files, {}, error) || !packageMatches(unpack, files)) {
        if (error && error->isEmpty()) *error = QCoreApplication::translate("ZludaBootstrap", "内置 ZLUDA 解包文件校验失败。");
        return false;
    }
    if (!QDir().mkpath(destination) || !guard.lock(destination, error)) return false;
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QString target = QDir(destination).filePath(it.key());
        if (!guard.lock(target, error) || !copyAtomically(QDir(unpack).filePath(it.key()), target, error)) return false;
    }
    if (!packageMatches(destination, files)) {
        if (error) *error = QCoreApplication::translate("ZludaBootstrap", "ZLUDA 发布后校验失败，已阻止启动。");
        return false;
    }
    return true;
}

QString extractEmbeddedZluda(const QString &portableRoot,
                             const QString &rocmBin,
                             QString *error)
{
    const QString packageName = hipMajorVersionForRocmBin(rocmBin) >= 7
        ? QStringLiteral("zluda-hip71.extpack")
        : QStringLiteral("zluda-hip57.extpack");
    const QString archive =
        materializeEmbeddedArchive(portableRoot, packageName, error);
    if (archive.isEmpty()) {
        return {};
    }
    const QString destination =
        QDir(portableRoot).filePath(QStringLiteral(".minifox/packages/%1"))
            .arg(QFileInfo(packageName).completeBaseName());
    if (!extractEmbeddedArchive(archive, destination, QStringLiteral("nvcuda.dll"), error)
        || !hasZludaFiles(destination)) {
        if (error && error->isEmpty()) {
            *error = QCoreApplication::translate("ZludaBootstrap", "内置 ZLUDA 运行包不完整。");
        }
        return {};
    }
    return destination;
}

bool preloadEmbeddedZludaPackage(const QString &portableRoot,
                                 const QString &packageName,
                                 QString *error)
{
    const QString archive = materializeEmbeddedArchive(portableRoot, packageName, error);
    if (archive.isEmpty()) {
        return false;
    }
    const QString destination =
        QDir(portableRoot).filePath(QStringLiteral(".minifox/packages/%1"))
            .arg(QFileInfo(packageName).completeBaseName());
    return extractEmbeddedArchive(archive, destination, QStringLiteral("nvcuda.dll"), error)
        && hasZludaFiles(destination);
}

QString detectGfxArchitecture(const QString &rocmBin, QString *error)
{
    const QString hipInfo = QDir(rocmBin).filePath(QStringLiteral("hipInfo.exe"));
    if (!QFileInfo::exists(hipInfo)) {
        if (error) {
            *error = QCoreApplication::translate("ZludaBootstrap", "HIP_PATH 中缺少 hipInfo.exe，无法识别 AMD 架构：%1")
                         .arg(rocmBin);
        }
        return {};
    }

    QProcess probe;
    QProcessEnvironment probeEnvironment = QProcessEnvironment::systemEnvironment();
    probeEnvironment.insert(
        QStringLiteral("PATH"),
        rocmBin + QDir::listSeparator() + probeEnvironment.value(QStringLiteral("PATH")));
    probe.setProcessEnvironment(probeEnvironment);
    probe.setProgram(hipInfo);
    probe.start();
    if (!probe.waitForFinished(15000)
        || probe.exitStatus() != QProcess::NormalExit
        || probe.exitCode() != 0) {
        if (error) {
            *error = QCoreApplication::translate("ZludaBootstrap", "HIP 设备检测失败：%1")
                         .arg(ProcessTextDecoder::decode(
                             probe.readAllStandardError()).trimmed());
        }
        return {};
    }
    const QString output = ProcessTextDecoder::decode(probe.readAllStandardOutput());
    const QRegularExpression expression(
        QStringLiteral(R"(gcnArchName:\s*(gfx[0-9a-f]+))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(output);
    if (!match.hasMatch()) {
        if (error) {
            *error = QCoreApplication::translate("ZludaBootstrap", "HIP 已安装，但 hipInfo 未返回 gcnArchName。");
        }
        return {};
    }
    return match.captured(1).toLower();
}

QString locateHipTensileLibrary(const QString &rocmBin,
                                const QString &architecture,
                                QString *error)
{
    const QDir systemLibrary(QDir(rocmBin).filePath(QStringLiteral("rocblas/library")));
    if (!systemLibrary.exists()
        || systemLibrary.entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "HIP 中缺少 rocBLAS/Tensile 库。请把适用于 %1 的架构文件安装到：%2")
                         .arg(architecture, systemLibrary.absolutePath());
        }
        return {};
    }
    return systemLibrary.absolutePath();
}

bool writeBootstrapScript(const QString &bootstrapDirectory, QString *error)
{
    static const QByteArray script = R"PY(import sys
_bootstrap_directory = __file__.replace("\\", "/").rsplit("/", 1)[0].rstrip("/").casefold()
sys.path[:] = [p for p in sys.path
               if p.replace("\\", "/").rstrip("/").casefold() != _bootstrap_directory]
import ctypes
import hashlib
import json
import importlib.abc
import importlib.machinery
import os
import sys


class _MinifoxTorchCppExtensionLoader(importlib.machinery.SourceFileLoader):
    def get_code(self, fullname):
        source = self.get_source(fullname)
        source = source.replace(
            "HIP_HOME = _join_rocm_home('hip') if ROCM_HOME else None",
            "HIP_HOME = ROCM_HOME",
        )
        source = source.replace(
            "    elif IS_WINDOWS:\n        raise",
            "    elif IS_WINDOWS and False:\n        raise",
        )
        return self.source_to_code(source, self.get_filename(fullname))


class _MinifoxTorchCppExtensionFinder(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname != "torch.utils.cpp_extension":
            return None
        spec = importlib.machinery.PathFinder.find_spec(fullname, path)
        if spec is None or not isinstance(
            spec.loader, importlib.machinery.SourceFileLoader
        ):
            return spec
        spec.loader = _MinifoxTorchCppExtensionLoader(
            spec.loader.name, spec.loader.path
        )
        return spec


def _minifox_patch_torch(module):
    module.backends.cudnn.enabled = False
    module.backends.cuda.enable_flash_sdp(False)
    module.backends.cuda.enable_math_sdp(True)
    module.backends.cuda.enable_mem_efficient_sdp(False)

    _enable_flash_sdp = module.backends.cuda.enable_flash_sdp
    _enable_mem_efficient_sdp = module.backends.cuda.enable_mem_efficient_sdp

    def _force_flash_sdp_off(_enabled):
        return _enable_flash_sdp(False)

    def _force_mem_efficient_sdp_off(_enabled):
        return _enable_mem_efficient_sdp(False)

    module.backends.cuda.enable_flash_sdp = _force_flash_sdp_off
    module.backends.cuda.enable_mem_efficient_sdp = _force_mem_efficient_sdp_off


def _minifox_preload_zluda():
    global _minifox_zluda_loaded
    if _minifox_zluda_loaded:
        return
    _hashes = json.loads(os.environ["MINIFOX_ZLUDA_SHA256"])
    _entries = list(os.scandir(_runtime))
    if ({entry.name for entry in _entries} != set(_hashes)
            or any(not entry.is_file(follow_symlinks=False) for entry in _entries)):
        raise RuntimeError("Unexpected ZLUDA runtime directory contents")
    # Validate even retained aliases before the OS may search this directory
    # for the import dependencies of one of the explicitly loaded DLLs.
    for _entry in _entries:
        _hash = hashlib.sha256()
        with open(_entry.path, "rb") as _file:
            for _chunk in iter(lambda: _file.read(1024 * 1024), b""):
                _hash.update(_chunk)
        if _hash.hexdigest() != _hashes[_entry.name]:
            raise RuntimeError("ZLUDA DLL integrity check failed: " + _entry.name)
    for _name in os.environ["MINIFOX_ZLUDA_PRELOAD"].split(";"):
        if _name:
            _path = os.path.join(_runtime, _name)
            _expected = json.loads(os.environ["MINIFOX_ZLUDA_SHA256"])[_name]
            # Deny writes and renames between hashing and LoadLibrary.
            _kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            _kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                           ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
            _kernel.CreateFileW.restype = ctypes.c_void_p
            _kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            _handle = _kernel.CreateFileW(_path, 0x80000000, 1, None, 3, 0, None)
            if _handle == ctypes.c_void_p(-1).value:
                raise RuntimeError("Unable to lock ZLUDA DLL: " + _name)
            try:
                with open(_path, "rb") as _file:
                    _hash = hashlib.sha256()
                    for _chunk in iter(lambda: _file.read(1024 * 1024), b""):
                        _hash.update(_chunk)
                    _actual = _hash.hexdigest()
                if _actual != _expected:
                    raise RuntimeError("ZLUDA DLL integrity check failed: " + _name)
                _minifox_zluda_handles.append(ctypes.WinDLL(_path))
            finally:
                _kernel.CloseHandle(_handle)
    _minifox_zluda_loaded = True


class _MinifoxTorchLoader(importlib.abc.Loader):
    def __init__(self, loader):
        self._loader = loader

    def create_module(self, spec):
        create_module = getattr(self._loader, "create_module", None)
        return create_module(spec) if create_module is not None else None

    def exec_module(self, module):
        _minifox_preload_zluda()
        self._loader.exec_module(module)
        _minifox_patch_torch(module)


class _MinifoxTorchFinder(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname != "torch":
            return None
        spec = importlib.machinery.PathFinder.find_spec(fullname, path)
        if spec is not None and spec.loader is not None:
            spec.loader = _MinifoxTorchLoader(spec.loader)
        return spec


if os.environ.get("MINIFOX_ZLUDA_BOOTSTRAP") == "1":
    _minifox_zluda_dll_dirs = []
    _minifox_zluda_handles = []
    _minifox_zluda_loaded = False
    _runtime = os.environ["MINIFOX_ZLUDA_DLL_DIR"]
    _rocm = os.environ["MINIFOX_ROCM_BIN"]
    for _path in (_rocm, _runtime):
        if hasattr(os, "add_dll_directory"):
            _minifox_zluda_dll_dirs.append(os.add_dll_directory(_path))
    sys.meta_path.insert(0, _MinifoxTorchCppExtensionFinder())
    sys.meta_path.insert(0, _MinifoxTorchFinder())
    os.environ["MINIFOX_ZLUDA_ACTIVE"] = "1"
)PY";

    SafeDataPath guard;
    const QString scriptPath = QDir(bootstrapDirectory).filePath(QStringLiteral("sitecustomize.py"));
    if (!guard.lock(scriptPath, error) || !QDir().mkpath(bootstrapDirectory)
        || !guard.lock(scriptPath, error)) {
        if (error) {
            *error = QCoreApplication::translate("ZludaBootstrap", "无法创建 ZLUDA Python 引导目录：%1").arg(bootstrapDirectory);
        }
        return false;
    }
    // Python imports sitecustomize before the bridge. Do not allow adjacent
    // modules or stale bytecode to replace the generated checker itself.
    for (const QFileInfo &entry : QDir(bootstrapDirectory).entryInfoList(
             QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
        if (!guard.lock(entry.absoluteFilePath(), error)) return false;
        if (entry.fileName() == "sitecustomize.py" && entry.isFile()) continue;
        if (entry.fileName() == "__pycache__" && entry.isDir()) {
            const auto cachedFiles = QDir(entry.absoluteFilePath()).entryInfoList(
                QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
            for (const QFileInfo &cached : cachedFiles) {
                if (!guard.lock(cached.absoluteFilePath(), error)) return false;
                if (!cached.isFile() || !cached.fileName().startsWith("sitecustomize.")
                    || cached.suffix() != "pyc" || !QFile::remove(cached.absoluteFilePath())) {
                    if (error) *error = QCoreApplication::translate("ZludaBootstrap", "ZLUDA 引导缓存存在非预期内容：%1").arg(cached.absoluteFilePath());
                    return false;
                }
            }
            continue;
        }
        if (error) *error = QCoreApplication::translate("ZludaBootstrap", "ZLUDA 引导目录存在非预期内容：%1").arg(entry.absoluteFilePath());
        return false;
    }
    QSaveFile file(scriptPath);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(script) != script.size()
        || !file.commit()) {
        if (error) {
            *error = QCoreApplication::translate("ZludaBootstrap", "无法写入 ZLUDA Python 引导脚本。");
        }
        return false;
    }
    return true;
}

QString firstOrFallback(const QStringList &values, const QString &fallback)
{
    return values.isEmpty() ? fallback : values.constFirst();
}

} // namespace

QString ZludaBootstrap::detectionScript()
{
    return QStringLiteral(
        "import json,torch;"
        "names=[torch.cuda.get_device_name(i) for i in range(torch.cuda.device_count())];"
        "print(json.dumps({'torch':torch.__version__,'cuda':torch.version.cuda,"
        "'hip':getattr(torch.version,'hip',None),'available':torch.cuda.is_available(),"
        "'devices':names},ensure_ascii=False))");
}

QString ZludaBootstrap::preflightScript()
{
    return QStringLiteral(R"PY(
import json
import torch
from torch.utils import cpp_extension

names = [torch.cuda.get_device_name(i) for i in range(torch.cuda.device_count())]
if not torch.cuda.is_available() or not names:
    raise RuntimeError("ZLUDA 引导后没有可用的 CUDA 设备")
if torch.backends.cudnn.enabled:
    raise RuntimeError("ZLUDA 兼容层未禁用不受支持的 cuDNN 路径")
if cpp_extension.HIP_HOME != cpp_extension.ROCM_HOME:
    raise RuntimeError("Windows ZLUDA 的 HIP 扩展编译路径未正确修补")
hip_include = cpp_extension._join_rocm_home("include")
a = torch.randn((1, 64, 1), device="cuda", dtype=torch.float32)
b = torch.arange(512, device="cuda", dtype=torch.float32).reshape(1, 1, 512)
c = a @ b
start = torch.cuda.Event(enable_timing=True)
end = torch.cuda.Event(enable_timing=True)
start.record()
d = c * 1.0
end.record()
torch.cuda.synchronize()
elapsed = start.elapsed_time(end)
print(json.dumps({
    "ok": tuple(c.shape) == (1, 64, 512),
    "devices": names,
    "elapsed_ms": elapsed,
    "cudnn_enabled": torch.backends.cudnn.enabled,
    "flash_sdp_enabled": torch.backends.cuda.flash_sdp_enabled(),
    "mem_efficient_sdp_enabled": torch.backends.cuda.mem_efficient_sdp_enabled(),
    "hip_home": cpp_extension.HIP_HOME,
    "rocm_home": cpp_extension.ROCM_HOME,
    "hip_include": hip_include,
    "active": __import__("os").environ.get("MINIFOX_ZLUDA_ACTIVE") == "1"
}, ensure_ascii=False))
)PY");
}

ZludaBootstrap::Detection ZludaBootstrap::parseDetectionOutput(const QByteArray &output)
{
    Detection result;
    const QList<QByteArray> lines = output.trimmed().split('\n');
    for (auto iterator = lines.crbegin(); iterator != lines.crend(); ++iterator) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(iterator->trimmed(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            continue;
        }
        const QJsonObject object = document.object();
        result.torchVersion = object.value(QStringLiteral("torch")).toString();
        result.cudaVersion = object.value(QStringLiteral("cuda")).toString();
        result.hipVersion = object.value(QStringLiteral("hip")).toString();
        for (const QJsonValue &value : object.value(QStringLiteral("devices")).toArray()) {
            result.deviceNames.append(value.toString());
        }
        result.backend = classifyBackend(result.cudaVersion, result.hipVersion, result.deviceNames);
        return result;
    }
    result.error = QCoreApplication::translate("ZludaBootstrap", "无法解析 PyTorch GPU 探测结果。");
    return result;
}

ZludaBootstrap::BackendKind ZludaBootstrap::classifyBackend(
    const QString &cudaVersion,
    const QString &hipVersion,
    const QStringList &deviceNames)
{
    bool hasAmd = false;
    bool hasNvidia = false;
    bool hasZluda = false;
    for (const QString &name : deviceNames) {
        hasAmd |= containsAny(name, {QStringLiteral("AMD"), QStringLiteral("Radeon")});
        hasNvidia |= containsAny(name, {QStringLiteral("NVIDIA"), QStringLiteral("GeForce"),
                                        QStringLiteral("Quadro"), QStringLiteral("Tesla")});
        hasZluda |= name.contains(QStringLiteral("ZLUDA"), Qt::CaseInsensitive);
    }
    if (hasNvidia) {
        return BackendKind::Nvidia;
    }
    if (!hipVersion.trimmed().isEmpty()) {
        return BackendKind::Rocm;
    }
    if (!cudaVersion.trimmed().isEmpty() && !deviceNames.isEmpty() && (hasAmd || hasZluda)) {
        return BackendKind::Zluda;
    }
    return deviceNames.isEmpty() ? BackendKind::Unknown : BackendKind::Other;
}

ZludaBootstrap::BackendKind ZludaBootstrap::classifyInstalledTorch(
    const QString &pythonPath)
{
    const QDir pythonDirectory = QFileInfo(pythonPath).absoluteDir();
    QStringList candidates;
    const auto appendCandidate = [&candidates](const QString &path) {
        const QString cleanPath = QDir::cleanPath(path);
        if (!candidates.contains(cleanPath, Qt::CaseInsensitive)) {
            candidates.append(cleanPath);
        }
    };
    appendCandidate(pythonDirectory.filePath(QStringLiteral("Lib/site-packages/torch/version.py")));
    appendCandidate(pythonDirectory.filePath(QStringLiteral("../Lib/site-packages/torch/version.py")));
    appendCandidate(pythonDirectory.filePath(QStringLiteral("lib/site-packages/torch/version.py")));
    appendCandidate(pythonDirectory.filePath(QStringLiteral("../lib/site-packages/torch/version.py")));

    QString contents;
    for (const QString &candidate : std::as_const(candidates)) {
        QFile file(candidate);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        contents = QString::fromUtf8(file.readAll());
        break;
    }
    if (contents.isEmpty()) {
        return BackendKind::Unknown;
    }

    QString cudaVersion;
    QString hipVersion;
    QString torchVersion;
    static const QRegularExpression assignment(
        QStringLiteral(R"((?m)^\s*(cuda|hip|__version__)\s*(?::[^=\r\n]+)?=\s*(['"])([^'"]*)\2)"));
    auto matches = assignment.globalMatch(contents);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString name = match.captured(1);
        const QString value = match.captured(3).trimmed();
        if (name == QStringLiteral("cuda")) {
            cudaVersion = value;
        } else if (name == QStringLiteral("hip")) {
            hipVersion = value;
        } else {
            torchVersion = value;
        }
    }

    if (!hipVersion.isEmpty()
        || torchVersion.contains(QStringLiteral("rocm"), Qt::CaseInsensitive)) {
        return BackendKind::Rocm;
    }
    if (!cudaVersion.isEmpty()
        || QRegularExpression(QStringLiteral(R"(\+cu\d+)"),
                              QRegularExpression::CaseInsensitiveOption)
               .match(torchVersion).hasMatch()) {
        return BackendKind::Nvidia;
    }
    return BackendKind::Unknown;
}

QStringList ZludaBootstrap::systemAdapterNames()
{
    QStringList adapters;
#ifdef Q_OS_WIN
    DISPLAY_DEVICEW device {};
    device.cb = sizeof(device);
    for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &device, 0); ++index) {
        const QString name = QString::fromWCharArray(device.DeviceString).trimmed();
        const QString id = QString::fromWCharArray(device.DeviceID).trimmed();
        if (!name.isEmpty() || !id.isEmpty()) {
            adapters.append(QStringLiteral("%1 %2").arg(name, id).trimmed());
        }
        device = {};
        device.cb = sizeof(device);
    }
#endif
    return adapters;
}

ZludaBootstrap::SystemAdapterKind ZludaBootstrap::classifySystemAdapters(
    const QStringList &adapterNames)
{
    bool hasAmd = false;
    bool hasNvidia = false;
    for (const QString &adapter : adapterNames) {
        hasAmd |= containsAny(adapter, {QStringLiteral("AMD"), QStringLiteral("Radeon"),
                                        QStringLiteral("VEN_1002")});
        hasNvidia |= containsAny(adapter, {QStringLiteral("NVIDIA"), QStringLiteral("GeForce"),
                                           QStringLiteral("Quadro"), QStringLiteral("Tesla"),
                                           QStringLiteral("VEN_10DE")});
    }
    if (hasAmd && hasNvidia) {
        return SystemAdapterKind::Mixed;
    }
    if (hasAmd) {
        return SystemAdapterKind::AmdOnly;
    }
    if (hasNvidia) {
        return SystemAdapterKind::NvidiaOnly;
    }
    return SystemAdapterKind::Unknown;
}

bool ZludaBootstrap::shouldProbeAdapters(const QStringList &adapterNames, const QString &mode)
{
    if (mode.compare(QStringLiteral("off"), Qt::CaseInsensitive) == 0
        || mode.compare(QStringLiteral("disable"), Qt::CaseInsensitive) == 0
        || mode == QStringLiteral("0")) {
        return false;
    }

    // Even the diagnostic "force" mode must not inject ZLUDA into an NVIDIA
    // or mixed-adapter process. ZLUDA is eligible only on an AMD-only system.
    return classifySystemAdapters(adapterNames) == SystemAdapterKind::AmdOnly;
}

bool ZludaBootstrap::shouldProbeSystem(const QString &mode)
{
    return shouldProbeAdapters(systemAdapterNames(), mode);
}

bool ZludaBootstrap::preloadEmbeddedPackages(const QString &portableRoot,
                                             QString *error)
{
    QString firstError;
    for (const QString &packageName : {
             QStringLiteral("zluda-hip57.extpack"),
             QStringLiteral("zluda-hip71.extpack")
         }) {
        QString packageError;
        if (!preloadEmbeddedZludaPackage(portableRoot, packageName, &packageError)
            && firstError.isEmpty()) {
            firstError = packageError;
        }
    }
    if (!firstError.isEmpty()) {
        if (error) {
            *error = firstError;
        }
        return false;
    }
    return true;
}

ZludaBootstrap::Preparation ZludaBootstrap::prepare(
    const QString &pythonPath,
    const QString &comfyRoot,
    const QProcessEnvironment &environment)
{
    Preparation result;
    const QString portableRoot = QFileInfo(comfyRoot).absoluteDir().absolutePath();
    result.runtimeDirectory =
        QDir(portableRoot).filePath(QStringLiteral(".minifox/runtime/zluda"));
    result.bootstrapDirectory =
        QDir(portableRoot).filePath(QStringLiteral(".minifox/runtime/python-bootstrap"));

    // HIP is the only external prerequisite. Prefer the machine-level HIP_PATH
    // installed by AMD; the remaining probes are compatibility fallbacks.
    appendUniquePath(result.rocmBinCandidates,
                     environment.value(QStringLiteral("HIP_PATH")));
    appendUniquePath(result.rocmBinCandidates,
                     environment.value(QStringLiteral("ROCM_PATH")));
    appendUniquePath(result.rocmBinCandidates,
                     environment.value(QStringLiteral("MINIFOX_ROCM_PATH")));
    for (const QString &path : environment.value(QStringLiteral("PATH")).split(
             QDir::listSeparator(), Qt::SkipEmptyParts)) {
        appendUniquePath(result.rocmBinCandidates, path);
    }
#ifdef Q_OS_WIN
    const QDir rocmRoot(QStringLiteral("C:/Program Files/AMD/ROCm"));
    const QFileInfoList versions = rocmRoot.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    for (const QFileInfo &version : versions) {
        appendUniquePath(result.rocmBinCandidates,
                         QDir(version.absoluteFilePath()).filePath(QStringLiteral("bin")));
    }
#endif
    if (result.rocmBinCandidates.isEmpty()) {
        result.error = QStringLiteral(
            "未找到有效的 AMD HIP_PATH；请先安装 AMD HIP SDK 后重启启动器。");
        return result;
    }

    const QString selectedRocmBin = result.rocmBinCandidates.constFirst();
    const QString cacheRoot = QDir(portableRoot).filePath(
        QStringLiteral(".cache/%1").arg(cacheTagForRocmBin(selectedRocmBin)));
    result.zludaCacheDirectory =
        QDir(cacheRoot).filePath(QStringLiteral("zluda"));
    result.tritonCacheDirectory =
        QDir(cacheRoot).filePath(QStringLiteral("triton"));
    result.torchInductorCacheDirectory =
        QDir(cacheRoot).filePath(QStringLiteral("torchinductor"));

    for (const QString &cacheDirectory : {
             result.zludaCacheDirectory,
             result.tritonCacheDirectory,
             result.torchInductorCacheDirectory
         }) {
        if (!QDir().mkpath(cacheDirectory)) {
            result.error = QCoreApplication::translate("ZludaBootstrap", "无法创建运行时缓存目录：%1").arg(cacheDirectory);
            return result;
        }
    }

    result.gfxArchitecture =
        environment.value(QStringLiteral("MINIFOX_ZLUDA_GFX_ARCH")).trimmed().toLower();
    if (result.gfxArchitecture.isEmpty()) {
        result.gfxArchitecture =
            detectGfxArchitecture(selectedRocmBin, &result.error);
    }
    if (result.gfxArchitecture.isEmpty()) {
        return result;
    }
    result.tensileLibraryDirectory = locateHipTensileLibrary(
        selectedRocmBin,
        result.gfxArchitecture,
        &result.error);
    if (!result.error.isEmpty()) {
        return result;
    }

    // Developer overrides remain available for diagnostics. Normal users always
    // receive and extract the package embedded in the Minifox executable.
    QString source;
    const QString configured = environment.value(QStringLiteral("MINIFOX_ZLUDA_DIR")).trimmed();
    const bool externalSource = !configured.isEmpty();
    if (externalSource) {
        source = QDir::cleanPath(configured);
        if (!QDir::isAbsolutePath(source) || !hasZludaFiles(source)) {
            result.error = QCoreApplication::translate("ZludaBootstrap", "MINIFOX_ZLUDA_DIR 必须是明确选择且完整的可信 ZLUDA 绝对目录。");
            return result;
        }
    } else {
        source = extractEmbeddedZluda(portableRoot, selectedRocmBin, &result.error);
    }
    if (source.isEmpty()) return result;
    SafeDataPath sourceGuard;
    if (!sourceGuard.lock(source, &result.error)
        || !sourceGuard.lock(result.runtimeDirectory, &result.error)) return result;
    const QString packageName = hipMajorVersionForRocmBin(selectedRocmBin) >= 7
        ? QStringLiteral("zluda-hip71.extpack") : QStringLiteral("zluda-hip57.extpack");
    const QJsonObject trustedFiles = externalSource ? QJsonObject{} : embeddedManifest(packageName).value("files").toObject();
    result.sourceDirectory = source;

    if (!QDir().mkpath(result.runtimeDirectory)) {
        result.error = QCoreApplication::translate("ZludaBootstrap", "无法创建 ZLUDA 运行时目录：%1").arg(result.runtimeDirectory);
        return result;
    }

    const QString torchLib = torchLibraryDirectory(pythonPath);
    const QString cublasName = firstOrFallback(
        matchingFileNames(torchLib, QStringLiteral("cublas64_*.dll")),
        QStringLiteral("cublas64_11.dll"));
    const QString cusparseName = firstOrFallback(
        matchingFileNames(torchLib, QStringLiteral("cusparse64_*.dll")),
        QStringLiteral("cusparse64_11.dll"));
    const QString nvrtcName = firstOrFallback(
        matchingFileNames(torchLib, QStringLiteral("nvrtc64_*.dll")),
        QStringLiteral("nvrtc64_112_0.dll"));

    const QList<QPair<QString, QString>> files {
        {QDir(source).filePath(QStringLiteral("nvcuda.dll")), QStringLiteral("nvcuda.dll")},
        {findSourceFile(source, QStringLiteral("nvrtc.dll"), nvrtcName), nvrtcName},
        {findSourceFile(source, QStringLiteral("cublas.dll"), cublasName), cublasName},
        {findSourceFile(source, QStringLiteral("cusparse.dll"), cusparseName), cusparseName}
    };
    const auto copyVerified = [&](const QString &sourceFile, const QString &targetName) {
        const QString target = QDir(result.runtimeDirectory).filePath(targetName);
        if (!sourceGuard.lock(sourceFile, &result.error) || !sourceGuard.lock(target, &result.error)) return false;
        const QString expected = externalSource ? QString::fromLatin1(fileSha256(sourceFile))
            : trustedFiles.value(QFileInfo(sourceFile).fileName()).toString();
        if (expected.size() != 64 || fileSha256(sourceFile) != expected.toLatin1()
            || !copyAtomically(sourceFile, target, &result.error)
            || fileSha256(target) != expected.toLatin1()) {
            if (result.error.isEmpty()) result.error = QCoreApplication::translate("ZludaBootstrap", "ZLUDA DLL 校验失败：%1").arg(targetName);
            return false;
        }
        result.preloadNames.append(targetName);
        result.preloadHashes.insert(targetName, expected);
        return true;
    };
    for (const auto &[sourceFile, targetName] : files)
        if (sourceFile.isEmpty() || !copyVerified(sourceFile, targetName)) return result;
    for (const QString &name : {QStringLiteral("nvml.dll"), QStringLiteral("vml.dll")}) {
        const QString optional = QDir(source).filePath(name);
        if (QFileInfo::exists(optional) && !copyVerified(optional, name)) return result;
    }

    // DLL dependencies are also searched in the loading directory. Permit only
    // the selected DLLs plus proven bundled aliases left by another torch version.
    const QJsonObject otherFiles = embeddedManifest(packageName == "zluda-hip57.extpack"
        ? "zluda-hip71.extpack" : "zluda-hip57.extpack").value("files").toObject();
    static const QRegularExpression alias("^(cublas64_[0-9]+|cusparse64_[0-9]+|nvrtc64_[0-9_]+)\\.dll$");
    for (const QFileInfo &entry : QDir(result.runtimeDirectory).entryInfoList(
             QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
        if (!sourceGuard.lock(entry.absoluteFilePath(), &result.error)) return result;
        if (entry.isFile() && result.preloadHashes.contains(entry.fileName())) continue;
        const QString hash = QString::fromLatin1(fileSha256(entry.absoluteFilePath()));
        bool knownAlias = !externalSource && entry.isFile() && alias.match(entry.fileName()).hasMatch();
        bool knownHash = false;
        for (const auto &value : trustedFiles) knownHash |= value.toString() == hash;
        for (const auto &value : otherFiles) knownHash |= value.toString() == hash;
        if (!knownAlias || !knownHash) {
            result.error = QCoreApplication::translate("ZludaBootstrap", "ZLUDA 运行目录包含非预期文件，已阻止加载：%1").arg(entry.absoluteFilePath());
            return result;
        }
        result.preloadHashes.insert(entry.fileName(), hash);
    }

    if (!writeBootstrapScript(result.bootstrapDirectory, &result.error)) {
        return result;
    }

    result.valid = true;
    return result;
}

void ZludaBootstrap::apply(const Preparation &preparation,
                           const QString &rocmBin,
                           QProcessEnvironment &environment)
{
    if (!preparation.valid || rocmBin.isEmpty()) {
        return;
    }
    environment.insert(QStringLiteral("MINIFOX_ZLUDA_BOOTSTRAP"), QStringLiteral("1"));
    environment.insert(QStringLiteral("MINIFOX_ZLUDA_DLL_DIR"), preparation.runtimeDirectory);
    QJsonObject hashes;
    for (auto it = preparation.preloadHashes.cbegin(); it != preparation.preloadHashes.cend(); ++it)
        hashes.insert(it.key(), it.value());
    environment.insert(QStringLiteral("MINIFOX_ZLUDA_SHA256"), QString::fromUtf8(QJsonDocument(hashes).toJson(QJsonDocument::Compact)));
    environment.insert(QStringLiteral("MINIFOX_ZLUDA_PRELOAD"),
                       preparation.preloadNames.join(QLatin1Char(';')));
    environment.insert(QStringLiteral("MINIFOX_ROCM_BIN"), rocmBin);
    environment.insert(QStringLiteral("DISABLE_ADDMM_CUDA_LT"), QStringLiteral("1"));
    environment.insert(QStringLiteral("TORCH_BLAS_PREFER_HIPBLASLT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("ZLUDA_CACHE_DIR"), preparation.zludaCacheDirectory);
    environment.insert(QStringLiteral("TRITON_CACHE_DIR"), preparation.tritonCacheDirectory);
    environment.insert(QStringLiteral("TORCHINDUCTOR_CACHE_DIR"),
                       preparation.torchInductorCacheDirectory);
    if (!preparation.tensileLibraryDirectory.isEmpty()) {
        environment.insert(QStringLiteral("ROCBLAS_TENSILE_LIBPATH"),
                           preparation.tensileLibraryDirectory);
    }
    const QString rocmRoot = QDir::cleanPath(
        QDir(rocmBin).absoluteFilePath(QStringLiteral("..")));
    environment.insert(QStringLiteral("HIP_PATH"), rocmRoot);
    environment.insert(QStringLiteral("ROCM_PATH"), rocmRoot);
    for (const QString &name : preparation.preloadNames) {
        if (name.startsWith(QStringLiteral("nvrtc"), Qt::CaseInsensitive)) {
            environment.insert(
                QStringLiteral("ZLUDA_NVRTC_LIB"),
                QDir(preparation.runtimeDirectory).filePath(name));
            break;
        }
    }

    QStringList pathEntries {
        rocmBin,
        preparation.runtimeDirectory
    };
    const QString existingPath = environment.value(QStringLiteral("PATH"));
    if (!existingPath.isEmpty()) {
        pathEntries.append(existingPath);
    }
    environment.insert(QStringLiteral("PATH"),
                       pathEntries.join(QDir::listSeparator()));

    QStringList pythonPath {
        preparation.bootstrapDirectory
    };
    const QString existingPythonPath = environment.value(QStringLiteral("PYTHONPATH"));
    if (!existingPythonPath.isEmpty()) {
        pythonPath.append(existingPythonPath);
    }
    environment.insert(QStringLiteral("PYTHONPATH"),
                       pythonPath.join(QDir::listSeparator()));
}
