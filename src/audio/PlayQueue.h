#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "audio/Xoshiro.h"

// ─────────────────────────────────────────────────────────────────────────────
//  PlayQueue - welcher Titel kommt als Nächstes?
//
//  Die Liste kommt aus der Galerie (Anzeigereihenfolge, bereits gefiltert und
//  sortiert). Diese Klasse hält nur die REIHENFOLGE, in der gespielt wird, und
//  die Stelle darin - die Wiedergabe selbst macht `AudioEngine`.
//
//  ZUFALL = GEMISCHTE LISTE, keine Wiederholung (Festlegung des Nutzers):
//  Fisher-Yates über eine Indexliste, jeder Titel kommt genau einmal; „Zurück"
//  geht die Mischung rückwärts. Kosten: 4 Byte je Titel (10 000 Titel = 40 kB)
//  und ein Durchlauf beim Mischen. Zufallszahlen aus `Xoshiro` (s. dort).
//
//  WIEDERHOLUNG × ZUFALL sind frei kombinierbar; es gilt die Tabelle aus
//  `NEXT.md` ▸ A4. Kern: **„eine wiederholen" gilt nur beim NATÜRLICHEN Ende** -
//  wer weiterschaltet, bekommt den nächsten nach Zufallsregel.
// ─────────────────────────────────────────────────────────────────────────────
class PlayQueue : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(int  repeat  READ repeatInt WRITE setRepeatInt NOTIFY repeatChanged)
    Q_PROPERTY(int  count   READ count   NOTIFY itemsChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged)

public:
    enum class Repeat { Off = 0, One = 1, All = 2 };
    Q_ENUM(Repeat)

    explicit PlayQueue(QObject* parent = nullptr);
    //  Fester Saatwert - für Testtreiber, die eine bestimmte Mischung erwarten.
    //  Im Betrieb kommt die Saat aus `QRandomGenerator::system()`.
    PlayQueue(uint64_t seed, QObject* parent);

    //  Die sichtbare Liste der Galerie. Ein bereits laufender Titel bleibt
    //  laufend, sofern er noch dabei ist - sonst beginnt die Liste von vorn.
    void setItems(const QStringList& paths);
    QStringList items() const { return m_items; }
    int count() const { return int(m_items.size()); }

    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool on);
    Repeat repeat() const { return m_repeat; }
    void setRepeat(Repeat r);
    int  repeatInt() const { return int(m_repeat); }
    void setRepeatInt(int r) { setRepeat(static_cast<Repeat>(r)); }

    //  Die Liste in ABSPIEL-Reihenfolge (bei Zufall also gemischt) samt Stelle
    //  darin - das ist, was eine Warteschlangen-Anzeige zeigen muss: „was kommt
    //  als Nächstes", nicht „wie liegt es im Ordner".
    QStringList orderedItems() const;
    int         orderedPos() const { return m_pos; }
    //  Umkehrung für die Anzeige: Platz in der Abspielfolge -> Pfad.
    QString     pathAtOrder(int orderPos) const;
    //  Bei diesem Platz der Abspielfolge weitermachen (Klick in der Liste).
    bool        startAtOrder(int orderPos);

    QString currentPath() const;
    int     currentItemIndex() const { return m_pos >= 0 && m_pos < m_order.size()
                                              ? m_order.at(m_pos) : -1; }

    //  Bei diesem Titel anfangen (Doppelklick in der Galerie). Liefert false,
    //  wenn er nicht in der Liste steht.
    bool startAt(const QString& path);

    //  Der nächste Titel. `natural` = der laufende ist zu Ende (dann greift
    //  „eine wiederholen"); false = der Nutzer hat weitergeschaltet.
    //  Leerer Rückgabewert heißt: hier ist Schluss.
    QString advance(bool natural);
    //  ZURÜCK heißt: der Titel, den man WIRKLICH vorher gehört hat - dafür gibt
    //  es eine Historie. Ohne sie lief „zurück" die aktuelle Ordnung rückwärts,
    //  und die ändert sich beim Ein-/Ausschalten des Zufalls: man landete dann
    //  bei einem Titel, der nie gespielt wurde (Nutzerbefund).
    QString back();

signals:
    void shuffleChanged();
    void repeatChanged();
    void itemsChanged();
    void currentChanged();

private:
    void rebuildOrder(int keepItemIndex);
    void noteHistory();

    QStringList     m_items;
    QVector<int>    m_order;     // Reihenfolge als Indizes in m_items
    int             m_pos = -1;  // Stelle in m_order
    bool            m_shuffle = false;
    //  Wurde schon ein Titel GEWÄHLT? Vorher darf eine frische Mischung
    //  irgendwo anfangen; danach bleibt der laufende Titel, wo er ist.
    bool            m_started = false;
    //  Zuletzt gespielte Titel (Item-Indizes, jüngster zuletzt). Gedeckelt,
    //  damit eine lange Sitzung den Speicher nicht wachsen lässt.
    QList<int>      m_history;
    static constexpr int kMaxHistory = 200;
    Repeat          m_repeat = Repeat::Off;
    mg::Xoshiro     m_rng;
};
