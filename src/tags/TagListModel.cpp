#include "tags/TagListModel.h"

TagListModel::TagListModel(QObject* parent) : QAbstractListModel(parent) {}

int TagListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : int(m_shown.size());
}

QVariant TagListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_shown.size()) return {};
    if (role == NameRole || role == Qt::DisplayRole) return m_shown.at(index.row());
    return {};
}

QHash<int, QByteArray> TagListModel::roleNames() const {
    return { { NameRole, "name" } };
}

void TagListModel::setSource(const QStringList& list) {
    if (m_source == list) return;
    m_source = list;
    emit sourceChanged();
    rebuild();
}

void TagListModel::setFilter(const QString& text) {
    if (m_filter == text) return;
    m_filter = text;
    emit filterChanged();
    rebuild();
}

//  Gemeinsamer Anfang und gemeinsames Ende bleiben stehen, nur das Stueck
//  dazwischen wird gemeldet. Bei einem geloeschten oder neuen Tag in einer
//  sortierten Liste ist das genau EINE Zeile - die Oberflaeche baut dann auch
//  nur diese eine.
void TagListModel::rebuild() {
    QStringList neu;
    neu.reserve(m_source.size());
    for (const QString& t : m_source)
        if (m_filter.isEmpty() || t.contains(m_filter, Qt::CaseInsensitive))
            neu.append(t);
    if (neu == m_shown) return;

    int vorn = 0;
    while (vorn < m_shown.size() && vorn < neu.size() && m_shown.at(vorn) == neu.at(vorn))
        ++vorn;
    int hinten = 0;
    while (hinten < m_shown.size() - vorn && hinten < neu.size() - vorn
           && m_shown.at(m_shown.size() - 1 - hinten) == neu.at(neu.size() - 1 - hinten))
        ++hinten;

    const int altMitte = int(m_shown.size()) - vorn - hinten;
    const int neuMitte = int(neu.size())     - vorn - hinten;

    if (altMitte > 0) {
        beginRemoveRows(QModelIndex(), vorn, vorn + altMitte - 1);
        m_shown.remove(vorn, altMitte);
        endRemoveRows();
    }
    if (neuMitte > 0) {
        beginInsertRows(QModelIndex(), vorn, vorn + neuMitte - 1);
        for (int k = 0; k < neuMitte; ++k) m_shown.insert(vorn + k, neu.at(vorn + k));
        endInsertRows();
    }
    if (altMitte != neuMitte) emit countChanged();
}
