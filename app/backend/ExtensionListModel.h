#pragma once

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QVariantList>

// Rows of installed or catalog extensions, exposed to QML as one "entry" map
// per row. Status updates keep the row objects and only report changed rows.
class ExtensionListModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role { EntryRole = Qt::UserRole + 1 };

    explicit ExtensionListModel(const QString &keyField, QObject *parent = nullptr);

    void setEntries(const QVariantList &entries);
    QVariantMap entry(int row) const;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    QString m_keyField;
    QVariantList m_entries;
};

// Filters in C++ so the view only creates delegates for matching rows. Hiding
// rows in QML (zero height) made the ListView instantiate every catalog entry.
class ExtensionFilterModel final : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    ExtensionFilterModel(ExtensionListModel *source, const QStringList &searchFields,
                         QObject *parent = nullptr);

    QString filterText() const;
    void setFilterText(const QString &text);
    int count() const;

signals:
    void filterTextChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    ExtensionListModel *m_source;
    QStringList m_searchFields;
    QString m_filterText;
};
