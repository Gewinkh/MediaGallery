#pragma once
//  TableController - der Zustand EINER geoeffneten Tabellendatei (CSV/TSV).
//  Je Kachel eine Instanz, damit zwei Haelften verschiedene Dateien zeigen.
//  Zeigt nur an; Bearbeiten und Zurueckschreiben sind noch nicht gebaut.
#include "table/DelimitedText.h"
#include "table/TableSearch.h"

#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <atomic>
#include <memory>

namespace mg::table {

class TableController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool    busy   READ busy   NOTIFY stateChanged)
    Q_PROPERTY(bool    ready  READ ready  NOTIFY stateChanged)
    Q_PROPERTY(QString error  READ error  NOTIFY stateChanged)

    //  Beides wird beim Lesen ERKANNT und nur berichtet - die Anzeige nennt es
    //  in der Fusszeile. Ein Schalter dafuer stand dort einmal und ist wieder
    //  entfallen: das Raten trifft, und vier Knoepfe fuer den Ausnahmefall
    //  standen dauerhaft im Weg.
    Q_PROPERTY(QString separator READ separator NOTIFY stateChanged)
    Q_PROPERTY(bool headerRow READ headerRow NOTIFY stateChanged)

    //  Mehrere Tabellen in einer Datei: je Leerzeile ein Block. `blocks` traegt
    //  {index, title, rows}; `currentBlock` waehlt einen aus, **-1 zeigt die
    //  ganze Datei flach** - der Rueckfallweg, wenn die Erkennung danebenliegt.
    Q_PROPERTY(int blockCount READ blockCount NOTIFY stateChanged)
    Q_PROPERTY(QVariantList blocks READ blocks NOTIFY stateChanged)
    Q_PROPERTY(int currentBlock READ currentBlock WRITE setCurrentBlock NOTIFY blockChanged)

    Q_PROPERTY(int rowCount    READ rowCount    NOTIFY stateChanged)
    Q_PROPERTY(int columnCount READ columnCount NOTIFY stateChanged)
    Q_PROPERTY(QVariantList columns READ columns NOTIFY stateChanged)

    //  Suche: die Trefferliste entsteht im Arbeitsfaden - gemessen kostet ein
    //  Lauf ueber 100.000 Zeilen mal 20 Spalten 119 ms, im GUI-Faden waere das
    //  je Tastendruck ein Ruckler. `searchRevision` steigt bei jeder Aenderung;
    //  die Anzeige haengt ihre Zell-Bindungen daran.
    Q_PROPERTY(int  matchCount   READ matchCount   NOTIFY searchChanged)
    Q_PROPERTY(int  matchIndex   READ matchIndex   NOTIFY searchChanged)
    Q_PROPERTY(int  matchRow     READ matchRow     NOTIFY searchChanged)
    Q_PROPERTY(int  matchColumn  READ matchColumn  NOTIFY searchChanged)
    Q_PROPERTY(bool matchOverflow READ matchOverflow NOTIFY searchChanged)
    //  Treffer in den ANDEREN Bloecken derselben Datei - nur gezaehlt, wenn im
    //  gezeigten keiner steht: sonst suchte man in einer Datei mit fuenf
    //  Tabellen im Nichts, ohne es zu merken.
    Q_PROPERTY(int  otherBlockMatches READ otherBlockMatches NOTIFY searchChanged)
    Q_PROPERTY(bool searching     READ searching     NOTIFY searchChanged)
    Q_PROPERTY(int  searchRevision READ searchRevision NOTIFY searchChanged)

    Q_PROPERTY(QStringList warnings READ warnings NOTIFY stateChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY stateChanged)
    Q_PROPERTY(bool cp1252    READ cp1252    NOTIFY stateChanged)

public:
    explicit TableController(QObject* parent = nullptr);
    ~TableController() override;

    QString source() const { return m_source; }
    void    setSource(const QString& pathOrUrl);

    bool    busy() const  { return m_busy; }
    bool    ready() const { return m_datei && m_datei->ok; }
    QString error() const { return m_fehler; }

    QString separator() const;
    bool    headerRow() const;

    int          blockCount() const { return int(m_bereiche.size()); }
    QVariantList blocks() const { return m_bloecke; }
    int          currentBlock() const { return m_block; }
    void         setCurrentBlock(int i);

    int rowCount() const;
    int columnCount() const { return m_spaltenZahl; }
    QVariantList columns() const { return m_spalten; }

    QStringList warnings() const { return m_warnungen; }
    bool truncated() const { return m_datei && m_datei->abgeschnitten; }
    bool cp1252() const    { return m_datei && m_datei->cp1252; }

    //  EIN Feld - der Weg der Anzeige. Eine ganze Zeile zurueckzugeben kopierte
    //  je sichtbarer Zeile alle Spalten statt der gezeigten.
    Q_INVOKABLE QString cell(int row, int column) const;

    int  matchCount() const  { return m_suche.anzahl(); }
    int  matchIndex() const  { return m_suche.index(); }
    int  matchRow() const    { return m_suche.index() < 0 ? -1 : m_suche.aktuell().zeile; }
    int  matchColumn() const { return m_suche.index() < 0 ? -1 : m_suche.aktuell().spalte; }
    bool matchOverflow() const { return m_suche.mehr(); }
    int  otherBlockMatches() const { return m_andereBloecke; }
    //  Die ANDEREN Tabellen mit Treffern: {index, title, count} - damit die
    //  Leiste sie benennen und anklickbar machen kann.
    Q_INVOKABLE QVariantList otherBlocksWithMatches() const;
    Q_INVOKABLE void jumpToBlock(int index);
    bool searching() const   { return m_suchLaeuft; }
    int  searchRevision() const { return m_suche.revision(); }

    //  Neu suchen. `fromRow` ist die Zeile, ab der der erste Treffer gesucht
    //  wird - die Anzeige gibt ihre oberste sichtbare mit, damit der Sprung
    //  nicht ans Dateiende zurueckfaellt.
    Q_INVOKABLE void search(const QString& text, bool caseSensitive,
                            bool wholeCell, int fromRow);
    //  Einen Treffer weiter (+1) oder zurueck (-1), umlaufend.
    Q_INVOKABLE void stepMatch(int delta);
    Q_INVOKABLE void clearSearch();
    //  Die Frage je sichtbarer Zeile: WELCHE Spalten sind Treffer? Je Zelle zu
    //  fragen hiesse, je Zelle eine Marke anzulegen - gemessen 3,78 -> 4,32 ms
    //  je Bild, auch ohne laufende Suche.
    Q_INVOKABLE QVariantList rowMatches(int row) const;
    Q_INVOKABLE bool cellMatches(int row, int column) const {
        return m_suche.trifft(row, column);
    }

    //  Ist die Zeile eine Leerzeile der Datei? Die Anzeige laesst sie dann
    //  ohne Streifen stehen, damit die Luecke als Luecke zu sehen ist.
    Q_INVOKABLE bool rowEmpty(int row) const;

signals:
    void sourceChanged();
    void stateChanged();
    void blockChanged();
    void searchChanged();

private:
    void neuLesen();
    void ergebnisUebernehmen(std::shared_ptr<Datei> d, const QString& fehler);
    void spaltenNeuRechnen();
    void bloeckeNeuBauen();
    void sucheStarten();
    void suchErgebnis(QList<Treffer> treffer, bool mehr, QList<int> proBlock);
    //  Der gerade gezeigte Bereich; bei -1 die ganze Datei als EIN Bereich.
    Bereich aktiv() const;

    QString m_source;
    QString m_fehler;
    bool    m_busy = false;
    QList<Bereich> m_bereiche;
    QVariantList   m_bloecke;
    int            m_block = -1;

    std::shared_ptr<Datei> m_datei;
    QVariantList m_spalten;
    QStringList  m_warnungen;
    int m_spaltenZahl = 0;

    Suchzustand  m_suche;
    QString      m_suchText;
    SuchOptionen m_suchOpt;
    int          m_suchAb = 0;
    int          m_andereBloecke = 0;
    QList<int>   m_trefferProBlock;   // je Bereich, Reihenfolge wie m_bereiche
    bool         m_suchLaeuft = false;

    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_abbruch;
    std::shared_ptr<std::atomic<bool>> m_suchAbbruch;
};

}  // namespace mg::table
