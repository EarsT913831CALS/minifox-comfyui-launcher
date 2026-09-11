#include <QCoreApplication>
#include "SafeDataPath.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QRegularExpression>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

bool SafeDataPath::canonicalRelative(const QString &path)
{
    if (path.isEmpty() || path.contains('\\') || QDir::isAbsolutePath(path)) return false;
    static const QRegularExpression device("^(CON|CONIN\\$|CONOUT\\$|PRN|AUX|NUL|COM[0-9¹²³]|LPT[0-9¹²³])(?:\\.|$)",
                                           QRegularExpression::CaseInsensitiveOption);
    for (const auto &part : path.split('/')) {
        if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.')
            || part.endsWith(' ') || device.match(part).hasMatch()) return false;
        for (const QChar c : part)
            if (c.unicode() < 32 || c.unicode() == 127 || QStringLiteral(":*?\"<>|~").contains(c))
                return false;
    }
    return true;
}

SafeDataPath::~SafeDataPath()
{
#ifdef Q_OS_WIN
    for (void *handle : m_handles) CloseHandle(handle);
#endif
}

bool SafeDataPath::lock(const QString &absolutePath, QString *error)
{
    const auto fail = [&] {
        if (error) *error = QCoreApplication::translate("SafeDataPath", "路径包含链接、别名或不可访问的目录，已停止写入：%1").arg(absolutePath);
        return false;
    };
    if (!QDir::isAbsolutePath(absolutePath)) return fail();
    const QString clean = QDir::fromNativeSeparators(QDir::cleanPath(absolutePath));
    QString current = QDir::rootPath();
#ifdef Q_OS_WIN
    if (clean.size() >= 3 && clean[1] == ':' && clean[2] == '/') current = clean.left(3);
    else if (clean.startsWith("//")) current = clean.section('/', 0, 3) + '/';
#endif
    const QString relative = QDir(current).relativeFilePath(clean);
    if (relative == ".") return true;
    if (!canonicalRelative(relative)) return fail();
    const QStringList parts = relative.split('/');
    for (qsizetype i = 0; i < parts.size(); ++i) {
        current = QDir(current).filePath(parts[i]);
#ifdef Q_OS_WIN
        if (m_lockedDirectories.contains(current.toCaseFolded())) continue;
        const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(current.utf16()));
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD code = GetLastError();
            if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) continue;
            return fail();
        }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return fail();
        if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
            HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(current.utf16()), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE) return fail();
            BY_HANDLE_FILE_INFORMATION info{};
            if (!GetFileInformationByHandle(handle, &info)
                || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                CloseHandle(handle); return fail();
            }
            m_handles.append(handle);
            m_lockedDirectories.insert(current.toCaseFolded());
        } else if (i + 1 < parts.size()) return fail();
#else
        const QFileInfo info(current);
        if (info.isSymLink() || (info.exists() && i + 1 < parts.size() && !info.isDir())) return fail();
#endif
    }
    return true;
}

bool SafeDataPath::flushFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite)) {
        if (error) *error = file.errorString();
        return false;
    }
#ifdef Q_OS_WIN
    const bool ok = FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(file.handle())));
#else
    const bool ok = fsync(file.handle()) == 0;
#endif
    if (!ok && error) *error = QCoreApplication::translate("SafeDataPath", "无法将配置文件写入磁盘：%1").arg(path);
    return ok;
}
