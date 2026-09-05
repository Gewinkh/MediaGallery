#pragma once
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QVariant>
#include <vector>

class PaneController;

// Ein Repeater ueber eine LISTE kennt kein Einfuegen: er baut alle Delegates neu, und
// die geoeffnete Datei der anderen Haelfte war samt Zoom und Notizen weg. Ein
// QAbstractListModel meldet punktgenau - ein Tausch verschiebt die Haelfte mit Inhalt.
class PaneListModel : public QAbstractListModel {
    Q_OBJECT
public:
    //  EIN Rolle: die Hälfte selbst. Der Name ist bewusst NICHT `pane` - der
    //  Delegate ist ein `PaneHost`, und der trägt bereits eine Eigenschaft
    //  dieses Namens.
    enum Roles { PaneObjectRole = Qt::UserRole + 1 };

    explicit PaneListModel(const std::vector<PaneController*>& panes,
                           QObject* parent = nullptr);

    int      rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    //  Klammern um die Änderung an der Liste im `AppController`. Jeweils
    //  begin… VOR und end… NACH der Änderung aufrufen.
    void beginInsert(int row);
    void endInsert();
    void beginRemove(int row);
    void endRemove();
    //  Zeile `from` landet an Position `to` (Qts Zielindex-Regel wird intern
    //  angewandt).
    void beginMove(int from, int to);
    void endMove();

private:
    const std::vector<PaneController*>& m_panes;
};
