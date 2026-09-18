#pragma once
//  TagListModel - die Tagliste des Panels als MODELL statt als JS-Feld.
//  Ein Repeater ueber ein JS-Feld wirft bei JEDER Aenderung alle Elemente weg
//  und baut sie neu; bei 400 Tags sind das rund 600 ms je Vorgang. Dieses
//  Modell vergleicht die neue Liste mit der alten und meldet nur, was sich
//  wirklich geaendert hat - einen geloeschten Tag also als EINE Zeile.
#include <QAbstractListModel>
#include <QStringList>

class TagListModel : public QAbstractListModel {
    Q_OBJECT
    //  Die ganze Liste, wie sie aus `Tags.allTags()` kommt.
    Q_PROPERTY(QStringList source READ source WRITE setSource NOTIFY sourceChanged)
    //  Suchbegriff des Abschnitts; leer = alles.
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles { NameRole = Qt::UserRole + 1 };

    explicit TagListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList source() const { return m_source; }
    void        setSource(const QStringList& list);
    QString     filter() const { return m_filter; }
    void        setFilter(const QString& text);
    int         count() const { return int(m_shown.size()); }

signals:
    void sourceChanged();
    void filterChanged();
    void countChanged();

private:
    //  Die gefilterte Liste neu bilden und den Unterschied melden.
    void rebuild();

    QStringList m_source;
    QString     m_filter;
    QStringList m_shown;
};
