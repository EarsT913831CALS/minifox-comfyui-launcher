#pragma once

#include <QString>
#include <QList>
#include <QSet>

// Holds existing ancestor directories against rename/reparse replacement while
// the caller reads or publishes data. Never accepts Windows path aliases.
class SafeDataPath final
{
public:
    ~SafeDataPath();
    SafeDataPath() = default;
    SafeDataPath(const SafeDataPath &) = delete;
    SafeDataPath &operator=(const SafeDataPath &) = delete;
    static bool canonicalRelative(const QString &path);
    static bool flushFile(const QString &path, QString *error = nullptr);
    bool lock(const QString &absolutePath, QString *error = nullptr);
private:
    QList<void *> m_handles;
    QSet<QString> m_lockedDirectories;
};
