#pragma once
#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QVariant>
#include <vector>

class PaneController;

// ─────────────────────────────────────────────────────────────────────────────
//  PaneListModel - die Hälften des Fensters ALS MODELL, nicht als Liste.
//
//  WARUM ein Modell und nicht die vorhandene `QVariantList panes`:
//  Ein `Repeater` über eine LISTE hat kein Einfügen und kein Entfernen - er
//  kennt nur „die Liste ist eine andere" und baut daraufhin ALLE Delegates
//  neu. Gemessen (Wegwerf-Treiber, Qt 6): eine Hälfte dazu -> die bestehende
//  wird zerstört und neu erzeugt; eine Hälfte zu -> dasselbe. In der App
//  bedeutete das: die geöffnete Datei der anderen Hälfte war weg, samt
//  Zoomstand, Seite und ungespeicherten Notizen.
//  Ein `QAbstractListModel` meldet Einfügen/Entfernen/Verschieben punktgenau -
//  bestehende Delegates bleiben stehen, und ein Tausch VERSCHIEBT die Hälfte
//  samt allem, was in ihr offen ist.
//
//  Die Liste selbst gehört weiterhin dem `AppController`; dieses Modell hält
//  nur eine Referenz darauf und wird von dort um jede Änderung geklammert.
// ─────────────────────────────────────────────────────────────────────────────
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
