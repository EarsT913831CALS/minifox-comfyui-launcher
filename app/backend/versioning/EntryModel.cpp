#include "EntryModel.h"

namespace {
constexpr int kEntryRole = Qt::UserRole + 1;
}

EntryModel::EntryModel(QString keyField, QStringList searchFields, QObject *parent)
    : QAbstractListModel(parent), m_keyField(std::move(keyField)), m_searchFields(std::move(searchFields))
{
}

int EntryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_visible.size());
}

QVariant EntryModel::data(const QModelIndex &index, int role) const
{
    if (role != kEntryRole || !index.isValid() || index.row() >= m_visible.size()) return {};
    return m_entries.at(m_visible.at(index.row()));
}

QHash<int, QByteArray> EntryModel::roleNames() const
{
    return {{kEntryRole, "entry"}};
}

void EntryModel::setFilterText(const QString &text)
{
    if (text == m_filter) return;
    m_filter = text;
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit filterTextChanged();
    emit countChanged();
}

void EntryModel::setEntries(const QList<QVariantMap> &entries)
{
    bool sameRows = entries.size() == m_entries.size();
    for (qsizetype index = 0; sameRows && index < entries.size(); ++index)
        sameRows = entries.at(index).value(m_keyField) == m_entries.at(index).value(m_keyField);

    if (!sameRows) {
        beginResetModel();
        m_entries = entries;
        rebuildVisible();
        endResetModel();
        emit countChanged();
        return;
    }
    for (qsizetype index = 0; index < entries.size(); ++index)
        update(entries.at(index).value(m_keyField).toString(), entries.at(index));
}

bool EntryModel::update(const QString &key, const QVariantMap &changes)
{
    const auto found = m_byKey.constFind(key);
    if (found == m_byKey.cend()) return false;
    const int index = found.value();
    QVariantMap &entry = m_entries[index];
    bool changed = false;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        if (entry.value(it.key()) == it.value()) continue;
        entry.insert(it.key(), it.value());
        changed = true;
    }
    if (!changed) return true;
    const bool wasVisible = m_rowOf.at(index) >= 0;
    if (wasVisible != matches(entry)) {
        beginResetModel();
        rebuildVisible();
        endResetModel();
        emit countChanged();
    } else if (wasVisible) {
        const QModelIndex row = createIndex(m_rowOf.at(index), 0);
        emit dataChanged(row, row, {kEntryRole});
    }
    return true;
}

QVariantMap EntryModel::entry(const QString &key) const
{
    const auto found = m_byKey.constFind(key);
    return found == m_byKey.cend() ? QVariantMap() : m_entries.at(found.value());
}

bool EntryModel::matches(const QVariantMap &entry) const
{
    const QStringList terms = m_filter.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (terms.isEmpty()) return true;
    QString haystack;
    for (const QString &field : m_searchFields) haystack += entry.value(field).toString() + QLatin1Char('\n');
    for (const QString &term : terms) {
        if (!haystack.contains(term, Qt::CaseInsensitive)) return false;
    }
    return true;
}

void EntryModel::rebuildVisible()
{
    m_visible.clear();
    m_rowOf.fill(-1, m_entries.size());
    m_byKey.clear();
    for (qsizetype index = 0; index < m_entries.size(); ++index) {
        m_byKey.insert(m_entries.at(index).value(m_keyField).toString(), int(index));
        if (!matches(m_entries.at(index))) continue;
        m_rowOf[index] = int(m_visible.size());
        m_visible.append(int(index));
    }
}
