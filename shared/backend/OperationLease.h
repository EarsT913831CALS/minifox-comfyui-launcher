#pragma once
#include <QStringList>
#include <memory>
#include <vector>
class QLockFile;

// One lease spans checking, mutation, dependency installation and running.
class OperationLease final {
public:
    OperationLease();
    ~OperationLease();
    bool acquire(const QStringList &resources, QString *error);
    void release();
    bool held() const;
    static QString canonicalResource(const QString &path);
    static QString pythonEnvironment(const QString &python);
    static QStringList repositoryResources(const QString &root, const QString &python);
private:
    std::vector<std::unique_ptr<QLockFile>> m_locks;
};
