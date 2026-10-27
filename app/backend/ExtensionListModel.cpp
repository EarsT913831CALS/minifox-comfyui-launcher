#include "ExtensionListModel.h"

ExtensionListModel::ExtensionListModel(const QString &keyField, QObject *parent)
    : QAbstractListModel(parent), m_keyField(keyField)
{
}

void ExtensionListModel::setEntries(const QVariantList &entries)
{
    // The version manager republishes on every state change; an unchanged
    // list still shares its data with the copy held here.
    if (entries.size() == m_entries.size() && entries.constData() == m_entries.constData()) {
        return;
    }
    bool sameRows = entries.size() == m_entries.size();
    for (qsizetype row = 0; sameRows && row < entries.size(); ++row) {
        sameRows = entries.at(row).toMap().value(m_keyField)
            == m_entries.at(row).toMap().value(m_keyField);
    }
    if (!sameRows) {
        beginResetModel();
        m_entries = entries;
        endResetModel();
        return;
    }
    const QVariantList previous = std::exchange(m_entries, entries);
    for (qsizetype row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row) != previous.at(row)) {
            const QModelIndex changed = index(int(row));
            emit dataChanged(changed, changed, {EntryRole});
        }
    }
}

QVariantMap ExtensionListModel::entry(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).toMap() : QVariantMap();
}

int ExtensionListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant ExtensionListModel::data(const QModelIndex &index, int role) const
{
    if (role != EntryRole || !checkIndex(index, CheckIndexOption::IndexIsValid)) {
        return {};
    }
    return m_entries.at(index.row());
}

QHash<int, QByteArray> ExtensionListModel::roleNames() const
{
    return {{EntryRole, QByteArrayLiteral("entry")}};
}

ExtensionFilterModel::ExtensionFilterModel(ExtensionListModel *source,
                                           const QStringList &searchFields,
                                           QObject *parent)
    : QSortFilterProxyModel(parent), m_source(source), m_searchFields(searchFields)
{
    setSourceModel(source);
    for (const auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved}) {
        connect(this, signal, this, &ExtensionFilterModel::countChanged);
    }
    connect(this, &QAbstractItemModel::modelReset, this, &ExtensionFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &ExtensionFilterModel::countChanged);
}

QString ExtensionFilterModel::filterText() const
{
    return m_filterText;
}

void ExtensionFilterModel::setFilterText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed == m_filterText) return;
    m_filterText = trimmed;
    invalidateRowsFilter();
    emit filterTextChanged();
    emit countChanged();
}

int ExtensionFilterModel::count() const
{
    return rowCount();
}

bool ExtensionFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &) const
{
    if (m_filterText.isEmpty()) return true;
    const QVariantMap entry = m_source->entry(sourceRow);
    QStringList values;
    values.reserve(m_searchFields.size());
    for (const QString &field : m_searchFields) values.append(entry.value(field).toString());
    return values.join(QLatin1Char(' ')).contains(m_filterText, Qt::CaseInsensitive);
}
