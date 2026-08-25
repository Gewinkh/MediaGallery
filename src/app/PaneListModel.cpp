#include "app/PaneListModel.h"

#include "app/PaneController.h"

PaneListModel::PaneListModel(const std::vector<PaneController*>& panes, QObject* parent)
    : QAbstractListModel(parent), m_panes(panes) {}

int PaneListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : int(m_panes.size());
}

QVariant PaneListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_panes.size()))
        return {};
    if (role != PaneObjectRole)
        return {};
    return QVariant::fromValue(static_cast<QObject*>(m_panes[size_t(index.row())]));
}

QHash<int, QByteArray> PaneListModel::roleNames() const {
    return { { PaneObjectRole, QByteArrayLiteral("paneObject") } };
}

void PaneListModel::beginInsert(int row) { beginInsertRows(QModelIndex(), row, row); }
void PaneListModel::endInsert()          { endInsertRows(); }
void PaneListModel::beginRemove(int row) { beginRemoveRows(QModelIndex(), row, row); }
void PaneListModel::endRemove()          { endRemoveRows(); }

void PaneListModel::beginMove(int from, int to) {
    //  Qt zählt das Ziel VOR dem Herausnehmen: nach hinten verschieben braucht
    //  deshalb `to + 1`, sonst bleibt die Zeile stehen.
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), (to > from) ? to + 1 : to);
}
void PaneListModel::endMove() { endMoveRows(); }
