#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QVariantMap>

// Rows for the version page, each exposed to QML as one "entry" map.
// Rows keep their identity across refreshes, so a status change repaints one
// row instead of rebuilding the list (and losing the scroll position).
class EntryModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    EntryModel(QString keyField, QStringList searchFields, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filterText() const { return m_filter; }
    void setFilterText(const QString &text);

    void setEntries(const QList<QVariantMap> &entries);
    // Merges `changes` into the row with this key; false if there is none.
    bool update(const QString &key, const QVariantMap &changes);
    QVariantMap entry(const QString &key) const;
    const QList<QVariantMap> &entries() const { return m_entries; }

signals:
    void filterTextChanged();
    void countChanged();

private:
    bool matches(const QVariantMap &entry) const;
    void rebuildVisible();

    QString m_keyField;
    QStringList m_searchFields;
    QString m_filter;
    QList<QVariantMap> m_entries;
    QHash<QString, int> m_byKey;
    // Indices into m_entries that pass the filter, in order, and the reverse.
    QList<int> m_visible;
    QList<int> m_rowOf;
};
