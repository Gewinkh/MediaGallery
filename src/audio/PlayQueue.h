#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "audio/Xoshiro.h"

// Haelt nur die Reihenfolge und die Stelle darin - die Wiedergabe macht AudioEngine.
// Zufall = gemischte Liste ohne Wiederholung (Fisher-Yates, 4 Byte je Titel).
// "Eine wiederholen" gilt nur beim natuerlichen Ende, nicht beim Weiterschalten.
// DREI Ordnungen, unabhaengig voneinander: die Liste, wie die Galerie sie zeigt;
// die EIGENE, vom Nutzer gezogene; und die Mischung. Der Zufall ersetzt die
// eigene Ordnung, solange er an ist - er mischt nicht ueber sie hinweg.
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
    Q_INVOKABLE void setItems(const QStringList& paths);

    //  Die EIGENE Reihenfolge (Pfade). Sie pflegt sich an `m_items`: was nicht
    //  mehr da ist, faellt raus, was neu ist, haengt ans Ende. Leer heisst
    //  „keine eigene Ordnung" - dann gilt die Liste der Galerie.
    void        setCustomOrder(const QStringList& paths);
    QStringList customOrder() const { return m_eigene; }
    bool        hasCustomOrder() const { return !m_eigene.isEmpty(); }
    //  Einen Eintrag der ABSPIELfolge an eine andere Stelle ziehen. Bei Zufall
    //  wird nur die MISCHUNG umgestellt - fluechtig, nur fuer diese Sitzung;
    //  ohne Zufall wird die eigene Ordnung mitgeschrieben. Liefert false, wenn
    //  nichts zu tun war.
    bool        moveOrder(int von, int nach);
    //  Wird beim Ziehen etwas festgehalten? Bei Zufall nicht.
    bool        moveIsPersistent() const { return !m_shuffle; }
    void        clearCustomOrder();
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
    // Was beim NATÜRLICHEN Ende folgen würde, ohne etwas zu verändern - Grundlage des lückenlosen Übergangs. Leer
    // heißt Schluss, ebenso bei "Zufall + alles wiederholen" am Listenende: dort wird beim Weiterschalten neu gemischt.
    QString     peekNext(bool natural = true) const;
    //  Bei diesem Platz der Abspielfolge weitermachen (Klick in der Liste).
    bool        startAtOrder(int orderPos);

    QString currentPath() const;
    int     currentItemIndex() const { return m_pos >= 0 && m_pos < m_order.size()
                                              ? m_order.at(m_pos) : -1; }

    //  Bei diesem Titel anfangen (Doppelklick in der Galerie). Liefert false,
    //  wenn er nicht in der Liste steht.
    Q_INVOKABLE bool startAt(const QString& path);

    //  Der nächste Titel. `natural` = der laufende ist zu Ende (dann greift
    //  „eine wiederholen"); false = der Nutzer hat weitergeschaltet.
    //  Leerer Rückgabewert heißt: hier ist Schluss.
    Q_INVOKABLE QString advance(bool natural);
    // ZURÜCK heißt: der Titel, den man WIRKLICH vorher gehört hat - dafür gibt es eine Historie. Ohne sie lief
    // "zurück" die aktuelle Ordnung rückwärts, und die ändert sich beim Umschalten des Zufalls.
    QString back();

signals:
    void customOrderChanged();
    void shuffleChanged();
    void repeatChanged();
    void itemsChanged();
    void currentChanged();

private:
    void rebuildOrder(int keepItemIndex);
    void noteHistory();
    //  Die eigene Ordnung an die heutige Liste anpassen; liefert true, wenn
    //  sich dabei etwas geaendert hat (dann ist sie neu abzulegen).
    bool pflegeEigene();
    //  Die Grundfolge OHNE Mischung: die eigene, wenn es sie gibt, sonst die
    //  Liste der Galerie.
    QVector<int> grundfolge() const;

    QStringList     m_items;
    //  Pfade, nicht Nummern: die Liste der Galerie wechselt mit jedem Filter,
    //  eine Nummer zeigte danach woanders hin.
    QStringList     m_eigene;
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
