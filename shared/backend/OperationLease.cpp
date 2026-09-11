#include <QCoreApplication>
#include "OperationLease.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QStandardPaths>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

OperationLease::OperationLease() = default;
OperationLease::~OperationLease() = default;
bool OperationLease::held() const { return !m_locks.empty(); }
void OperationLease::release() { m_locks.clear(); }

QString OperationLease::canonicalResource(const QString &path) {
    if (path.isEmpty()) return {};
    QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    QStringList missing;
    while (!info.exists() && !info.isSymLink()) {
        missing.prepend(info.fileName());
        const QString parent = info.absolutePath();
        if (parent == info.absoluteFilePath()) break;
        info.setFile(parent);
    }
    QString result = info.canonicalFilePath();
    if (result.isEmpty()) result = info.absoluteFilePath();
#ifdef Q_OS_WIN
    // QFileInfo's canonical path does not resolve every Windows junction form.
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(info.absoluteFilePath().utf16()), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        const DWORD size = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
        if (size) {
            QString finalPath(size, QChar());
            const DWORD length = GetFinalPathNameByHandleW(handle,
                reinterpret_cast<LPWSTR>(finalPath.data()), size, FILE_NAME_NORMALIZED);
            if (length && length < size) {
                finalPath.truncate(length);
                if (finalPath.startsWith(QStringLiteral("\\\\?\\UNC\\"))) finalPath = "//" + finalPath.mid(8);
                else if (finalPath.startsWith(QStringLiteral("\\\\?\\"))) finalPath = finalPath.mid(4);
                result = QDir::fromNativeSeparators(finalPath);
            }
        }
        CloseHandle(handle);
    }
#endif
    for (const QString &part : missing) result = QDir(result).filePath(part);
    result = QDir::cleanPath(result);
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

QString OperationLease::pythonEnvironment(const QString &python) {
    if (python.isEmpty()) return {};
    QDir directory(QFileInfo(python).absolutePath());
    if (directory.dirName().compare("Scripts", Qt::CaseInsensitive) == 0) directory.cdUp();
    return canonicalResource(directory.absolutePath());
}

bool OperationLease::acquire(const QStringList &resources, QString *error) {
    if (held()) { if (error) *error = QCoreApplication::translate("OperationLease", "当前操作尚未结束。"); return false; }
    QStringList keys;
    for (const QString &path : resources) {
        const QString key = canonicalResource(path);
        if (!key.isEmpty() && !keys.contains(key)) keys.append(key);
    }
    keys.sort();
    // GenericDataLocation is independent of executable location/application name.
    const QString directory = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath("Minifox/operation-locks");
    if (keys.isEmpty() || !QDir().mkpath(directory)) {
        if (error) *error = QCoreApplication::translate("OperationLease", "无法建立操作锁。");
        return false;
    }
    for (const QString &key : keys) {
        const QString name = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
        auto lock = std::make_unique<QLockFile>(QDir(directory).filePath(name + ".lock"));
        lock->setStaleLockTime(0);
        if (!lock->tryLock(0)) {
            release();
            if (error) *error = QCoreApplication::translate("OperationLease", "该 ComfyUI 或 Python 环境正在运行、更新或安装依赖，请等待操作结束后重试。");
            return false;
        }
        m_locks.push_back(std::move(lock));
    }
    return true;
}

QStringList OperationLease::repositoryResources(const QString &root, const QString &python) {
    QStringList resources{root, pythonEnvironment(python)};
    const QDir extensions(QDir(root).filePath("custom_nodes"));
    for (const auto &entry : extensions.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        resources.append(entry.absoluteFilePath());
    return resources;
}
