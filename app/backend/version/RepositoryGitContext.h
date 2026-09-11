#pragma once
#include <QProcessEnvironment>
#include <QStringList>

// Per-operation options also used by implicit promisor fetches in local commands.
// Kept in memory only: proxy credentials do not belong in recovery journals.
struct RepositoryGitContext {
    QStringList prefixArguments;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
};
