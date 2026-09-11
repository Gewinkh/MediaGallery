#pragma once
#include <QObject>
#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include "tags/TagUndoMark.h"

//  Die Rueckgaengig-Schritte EINES Ordners. Der Stapel haengt an der Ablage,
//  nicht an der Ansicht: haben zwei Haelften denselben Ordner offen, teilen
//  sich ihre TagManager dasselbe Objekt - sonst zeigte jede Seite eine eigene
//  Geschichte derselben Datei. Er haelt nur Daten.
class TagUndoStack : public QObject {
    Q_OBJECT
public:
    struct Step {
        quint64             id = 0;
        mg::tagmark::Mark   mark;       // beide Richtungen des Vorgangs
        QString    folder;              // zu welchem Ordner der Stand gehoert
        QByteArray state;               // Sidecar des offenen Ordners VORHER
        QHash<QString, QByteArray> foreign;
        bool       foreignComplete = true;
        int        bytes = 0;           // grobe Groesse, fuer den RAM-Deckel
        //  Ein Schritt haelt ENTWEDER den ganzen Stand (`state`) ODER nur die
        //  Aenderung: die Tags der beruehrten Dateien VORHER und die
        //  Tag-Registrierung. Eine Zuordnung beruehrt eine Handvoll Dateien;
        //  der ganze Stand kostete bei 20.000 Dateien 12 ms und 262 KB je
        //  Schritt. Kommt im selben Durchlauf ein Vorgang dazu, der mehr
        //  aendert als Datei-Tags, wird der Schritt auf den ganzen Stand
        //  gehoben.
        bool                        delta = false;
        QHash<QString, QStringList> tagsBefore;
        QHash<QString, QColor>      colorsBefore;
        // Nur für Zuordnungs-Schritte: wie viele Dateien dazu- oder weggekommen sind und worauf. Betrifft ein Schritt
        // mehrere Gegenstände, fällt der Gegenstand aus der Marke - `+5` ist ehrlicher als `+5 T:x`, wenn auch T:y dabei war.
        bool                addCounts = false;
        int                 addN = 0, delN = 0;
        mg::tagmark::Thing  cntThing = mg::tagmark::Thing::Tag;
        QString             cntName;
        QStringList         cntPath;
        bool                cntMixed = false;
    };

    //  Deckel; der Baum-Durchgang ist der Ausreisser und hat einen eigenen
    //  (in `TagManager`).
    static constexpr int    kMaxSteps = 20;
    static constexpr qint64 kMaxBytes = 16 * 1024 * 1024;

    QList<Step> undo;
    QList<Step> redo;
    qint64      bytes  = 0;
    quint64     nextId = 1;
    //  Laeuft ein Unterordner-Durchgang, ist nichts umkehrbar - er haengt seine
    //  Schnappschuesse erst am Ende an. Gilt fuer BEIDE Haelften.
    int         sweepsPending = 0;

    Step* byId(quint64 id);
    //  Die AELTESTEN fallen zuerst - der juengste ist der, den der Nutzer
    //  gleich zurueckzunehmen versucht.
    void  prune();
    void  clear();

signals:
    void changed();
};
