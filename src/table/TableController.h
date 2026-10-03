#pragma once
//  TableController - der Zustand EINER geoeffneten Tabellendatei (CSV/TSV).
//  Je Kachel eine Instanz, damit zwei Haelften verschiedene Dateien zeigen.
//  Anzeigen, Suchen, Filtern, Sortieren, Bearbeiten und Zurueckschreiben.
#include "core/PdfVorschau.h"
#include "table/DelimitedText.h"
#include "table/TableFilter.h"
#include "table/TableFormula.h"
#include "table/TableSearch.h"
#include "table/TableSort.h"
#include "table/TableWidths.h"

#include <QObject>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

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
    Q_PROPERTY(QVariantList blocks READ blocks NOTIFY rowsChanged)
    Q_PROPERTY(int currentBlock READ currentBlock WRITE setCurrentBlock NOTIFY blockChanged)

    //  `rowCount` zaehlt die GEZEIGTEN Zeilen (mit Filter weniger), `totalRows`
    //  alle des Blocks. Eigenes Signal: ein Filter oder eine neue Zeile darf
    //  nicht die Spaltenliste neu aufbauen lassen.
    Q_PROPERTY(int rowCount    READ rowCount    NOTIFY rowsChanged)
    Q_PROPERTY(int totalRows   READ totalRows   NOTIFY rowsChanged)
    //  `columnCount` zaehlt ALLE Spalten des Blocks, `columns` traegt nur die
    //  gezeigten. Die Fusszeile nennt beide - sonst waere nicht zu sehen, dass
    //  etwas fehlt.
    Q_PROPERTY(int columnCount READ columnCount NOTIFY stateChanged)
    Q_PROPERTY(QVariantList columns READ columns NOTIFY stateChanged)
    Q_PROPERTY(int hiddenColumnCount READ hiddenColumnCount NOTIFY stateChanged)

    //  Sortieren und Filtern aendern die DATEI NICHT (s. `TableFilter.h`);
    //  `sortColumn` ist -1, solange die Datei in ihrer eigenen Reihenfolge steht.
    Q_PROPERTY(int  sortColumn    READ sortColumn    NOTIFY sortChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY sortChanged)
    Q_PROPERTY(bool sorting       READ sorting       NOTIFY sortChanged)
    Q_PROPERTY(bool    filterActive READ filterActive NOTIFY sortChanged)
    Q_PROPERTY(int     filterColumn READ filterColumn NOTIFY sortChanged)
    Q_PROPERTY(QString filterText   READ filterText   NOTIFY sortChanged)
    //  Steigt bei jeder Aenderung an Reihenfolge, Spaltenauswahl oder Inhalt.
    //  Die Zellen haengen ihre Bindung daran - `cell()` ist eine Funktion, und
    //  ohne einen gelesenen Wert wertet QML sie nie neu aus.
    Q_PROPERTY(int  contentRevision READ contentRevision NOTIFY sortChanged)

    //  Suche: die Trefferliste entsteht im Arbeitsfaden - gemessen kostet ein
    //  Lauf ueber 100.000 Zeilen mal 20 Spalten 57 ms, im GUI-Faden waere das
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
    //  Schraegstrich-Daten als MM/TT/JJJJ statt TT/MM/JJJJ - beim Sortieren und Filtern.
    Q_PROPERTY(bool slashDateMonthFirst READ slashDateMonthFirst WRITE setSlashDateMonthFirst
                                        NOTIFY sortChanged)
    //  Zahlen mit Tausenderzeichen ANZEIGEN. Reine Darstellung - die Datei
    //  bleibt Byte fuer Byte, und das Eingabefeld zeigt weiter den Rohwert.
    Q_PROPERTY(bool groupDigits READ groupDigits WRITE setGroupDigits NOTIFY sortChanged)

    //  Bearbeiten. Eine gekappte oder abgeschnittene Datei bleibt lesend -
    //  zurueckgeschrieben fehlte ihr der Rest. Zeilen und Spalten lassen sich
    //  nur in EINER Tabelle umbauen, nicht in der flachen Gesamtansicht.
    Q_PROPERTY(bool editable          READ editable          NOTIFY stateChanged)
    Q_PROPERTY(bool structureEditable READ structureEditable NOTIFY blockChanged)
    Q_PROPERTY(bool    modified  READ modified  NOTIFY editChanged)
    Q_PROPERTY(bool    saving    READ saving    NOTIFY editChanged)
    Q_PROPERTY(QString saveError READ saveError NOTIFY editChanged)
    //  Wurde die Datei zuletzt als Kopie gesichert, steht hier ihr Name.
    Q_PROPERTY(QString savedCopy READ savedCopy NOTIFY editChanged)
    Q_PROPERTY(bool    canUndo   READ canUndo   NOTIFY editChanged)
    Q_PROPERTY(bool    canRedo   READ canRedo   NOTIFY editChanged)

    //  Wie viele Zellen der Datei eine Formel tragen. Die Fusszeile nennt die
    //  Zahl - ohne sie waere nicht zu sehen, dass eine gezeigte Zahl gerechnet
    //  und nicht getippt ist.
    Q_PROPERTY(int formulaCount READ formulaCount NOTIFY rowsChanged)

    Q_PROPERTY(QStringList warnings READ warnings NOTIFY stateChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY stateChanged)
    Q_PROPERTY(bool cp1252    READ cp1252    NOTIFY stateChanged)
    Q_PROPERTY(bool pdfBusy   READ pdfBusy   NOTIFY pdfBusyChanged)

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
    int totalRows() const;
    int columnCount() const { return m_spaltenZahl; }
    QVariantList columns() const { return m_spalten; }
    int hiddenColumnCount() const { return int(m_versteckt.size()); }

    int  sortColumn() const    { return m_sortSpalte; }
    bool sortAscending() const { return m_sortRichtung == SortRichtung::Auf; }
    bool sorting() const       { return m_sortLaeuft; }
    bool    filterActive() const { return m_filter.aktiv(); }
    int     filterColumn() const { return m_filter.spalte; }
    QString filterText() const   { return m_filter.text; }
    int  contentRevision() const { return m_inhaltRevision; }

    //  Aufsteigend -> absteigend -> Dateireihenfolge. Der dritte Klick ist der
    //  Weg heraus, ohne ein eigenes Bedienelement dafuer.
    Q_INVOKABLE void sortByColumn(int column);
    Q_INVOKABLE void clearSort();

    //  Nur Zeilen zeigen, in denen `text` steht - in `column` oder, bei -1, in
    //  irgendeiner gezeigten Spalte; Vergleiche und Spannen s. `FilterAusdruck`.
    //  Leerer Text hebt den Filter auf.
    Q_INVOKABLE void setFilter(int column, const QString& text, bool caseSensitive,
                               bool wholeCell);
    Q_INVOKABLE void clearFilter();

    //  Ausgeblendete Spalten werden auch nicht DURCHSUCHT - ein Treffer, den man
    //  nicht sehen kann, waere ein Sprung ins Nichts.
    Q_INVOKABLE void setColumnHidden(int column, bool hidden);
    Q_INVOKABLE bool columnHidden(int column) const { return m_versteckt.contains(column); }
    Q_INVOKABLE void showAllColumns();

    //  Die GEZEIGTEN Spalten mit Tabulator getrennt - das Format, das eine
    //  Tabellenkalkulation aus der Zwischenablage wieder in Spalten zerlegt.
    Q_INVOKABLE QString rowText(int row) const;
    //  Ein Bereich als Tabulatortext. Die Spalten sind STELLEN in der gezeigten
    //  Liste, nicht Feldnummern - ein Bereich spannt ueber das, was man sieht.
    //  Ueber dem Deckel kommt eine leere Zeichenkette zurueck: der Aufbau
    //  laeuft im GUI-Faden, und was niemand mehr einfuegt, wird nicht gebaut.
    Q_INVOKABLE QString rangeText(int row1, int col1, int row2, int col2) const;

    //  Von Hand gesetzte Spaltenbreite in Pixeln; 0 stellt sie wieder auf
    //  automatisch. Gehalten wird sie in der Beidatei neben der Datei.
    Q_INVOKABLE void setColumnWidth(int column, int px);

    //  Formatierung je SPALTE - ebenfalls in der Beidatei. Eine leere Farbe
    //  heisst „nicht gesetzt"; `clearColumnFormat` raeumt alle drei weg.
    Q_INVOKABLE void setColumnBold(int column, bool bold);
    Q_INVOKABLE void setColumnColor(int column, const QString& color);
    Q_INVOKABLE void setColumnBackground(int column, const QString& color);
    Q_INVOKABLE void clearColumnFormat(int column);
    Q_INVOKABLE bool columnHasFormat(int column) const {
        return m_formate.contains(column);
    }

    QStringList warnings() const { return m_warnungen; }
    bool truncated() const { return m_datei && m_datei->abgeschnitten; }
    bool cp1252() const    { return m_datei && m_datei->cp1252; }

    //  EIN Feld - der Weg der Anzeige. Eine ganze Zeile zurueckzugeben kopierte
    //  je sichtbarer Zeile alle Spalten statt der gezeigten. Eine Formelzelle
    //  liefert ihr ERGEBNIS.
    Q_INVOKABLE QString cell(int row, int column) const;
    //  Der Text, wie er in der Datei steht - bei einer Formel also `=A1+B2`.
    //  Das Eingabefeld bearbeitet ihn, nie das Ergebnis.
    Q_INVOKABLE QString cellRaw(int row, int column) const;
    //  Traegt die Zelle eine Formel? Die Anzeige zeichnet sie dann ab.
    Q_INVOKABLE bool cellIsFormula(int row, int column) const;
    int formulaCount() const { return m_formelZahl; }
    //  Die Nummer, die links neben der Zeile steht. Mit Filter die der Datei,
    //  damit die Luecken sichtbar bleiben.
    Q_INVOKABLE int rowNumber(int row) const;

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
    bool slashDateMonthFirst() const { return m_monatZuerst; }
    void setSlashDateMonthFirst(bool v);
    bool groupDigits() const { return m_gruppiert; }
    void setGroupDigits(bool v);

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

    bool    editable() const;
    bool    structureEditable() const;
    bool    modified() const  { return m_rev != m_gespeichertRev; }
    bool    saving() const    { return m_speichert; }
    QString saveError() const { return m_speicherFehler; }
    QString savedCopy() const { return m_kopieName; }
    bool    canUndo() const   { return !m_undo.empty(); }
    bool    canRedo() const   { return !m_redo.empty(); }

    //  Alle Aenderungen nehmen Anzeigezeilen und ABSOLUTE Spaltennummern
    //  (`columns[i].index`) und landen je als EIN Rueckgaengig-Schritt.
    Q_INVOKABLE bool setCell(int row, int column, const QString& text);
    //  Vor der Anzeigezeile `row`; `row == rowCount` haengt an.
    Q_INVOKABLE bool insertRows(int row, int count);
    Q_INVOKABLE bool removeRows(int row, int count);
    //  Vor der Spalte `column`; `column == columnCount` haengt an.
    Q_INVOKABLE bool insertColumn(int column);
    Q_INVOKABLE bool removeColumn(int column);
    Q_INVOKABLE bool setColumnName(int column, const QString& name);
    //  Ein Block aus der Zwischenablage (Zeilen am Umbruch, Zellen am Tabulator)
    //  ab dieser Zelle; fehlende Zeilen kommen am Ende dazu. Liefert die Zahl
    //  der gesetzten Zellen.
    Q_INVOKABLE int pasteText(int row, int column, const QString& text);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    //  Schreiben im Arbeitsfaden. Wurde die Datei inzwischen woanders geaendert,
    //  wird sie NICHT ueberschrieben (s. `saveError`).
    Q_INVOKABLE void save();
    //  Wie `save`, aber wartet, bis geschrieben ist - vor dem Verlassen der
    //  Datei. Laesst sich die Datei nicht schreiben, landen die Aenderungen in
    //  einer Kopie daneben, statt verloren zu gehen.
    Q_INVOKABLE void flush();
    //  Die Aenderungen als `<name>_edited(.n).<endung>` neben der Datei.
    Q_INVOKABLE void saveCopy();
    //  Verwirft alle Aenderungen und liest neu.
    Q_INVOKABLE void reload();
    //  Neu lesen, falls die Datei auf der Platte eine andere ist.
    Q_INVOKABLE bool reloadIfChangedOnDisk();

    //  PDF so, wie die Tabelle gezeigt wird, oder alle Tabellen der Datei vollstaendig. `opt`:
    //  print, landscape, all, grid, first, last, font, background, text, headerBackground, headerText.
    bool pdfBusy() const { return m_pdfBusy; }
    Q_INVOKABLE QString pdfTarget() const;
    Q_INVOKABLE void countPdfPages(const QVariantMap& opt);
    Q_INVOKABLE void exportPdf(const QString& target, const QVariantMap& opt);
    //  Das ganze PDF in eine Vorschau-Datei, an deren Seiten man auswaehlt -> `pdfPreviewReady`; `pages` in `opt`
    //  (1-basiert) waehlt beim Schreiben einzelne Seiten.
    Q_INVOKABLE void previewPdf(const QVariantMap& opt);

signals:
    void sourceChanged();
    void stateChanged();
    void blockChanged();
    void searchChanged();
    void sortChanged();
    void rowsChanged();
    void editChanged();
    void saved(bool ok);
    void pdfBusyChanged();
    void pdfPagesCounted(int pages);
    void pdfPreviewReady(const QString& path, int pages);
    void pdfExportFinished(bool ok, const QString& target, const QString& error);

private:
    //  Je Zeile der Tabelle: woher sie in der Datei stammt und wann sie sich
    //  zuletzt geaendert hat. Erst angelegt, wenn etwas bearbeitet wird.
    struct ZeilenInfo {
        quint32 id = 0;
        qint32  herkunft = -1;      // Datensatz der Grundlage, -1 = neu/umgebaut
        quint32 rev = 0;
    };
    struct Zellwechsel {
        int     zeile = 0;          // absolut
        int     spalte = 0;
        QString alt;
        QString neu;
        int     felderVorher = 0;
    };
    struct Weg {
        int        zeile = 0;
        int        anzeige = -1;
        Zeile      inhalt;
        ZeilenInfo info;
    };
    struct Schritt {
        enum class Art { Zellen, ZeilenEin, ZeilenAus, SpalteEin, SpalteAus, Gruppe };
        Art     art = Art::Zellen;
        //  Die Grundlage, auf die sich gespeicherte `ZeilenInfo`s beziehen;
        //  nach einem Speichern gilt ihre Herkunft nicht mehr.
        quint32 basisGen = 0;
        int     bereich = -1;
        QList<Zellwechsel> zellen;
        int pos = 0;                        // ZeilenEin: absolute Zeile
        int anzeige = -1;                   // ZeilenEin: Anzeigeposition
        QList<Zeile> neueZeilen;
        QList<ZeilenInfo> neueInfos;
        QList<Weg> weg;                     // ZeilenAus
        int spalte = -1;                    // SpalteEin/SpalteAus
        //  Breite und Format der entfernten Spalte, damit Rueckgaengig sie
        //  wiederherstellen kann - sie haengen an der Nummer und waeren sonst
        //  mit dem Aufruecken verloren.
        int           wegBreite = 0;
        SpaltenFormat wegFormat;
        int von = 0;                        // betroffene Zeilen beim ersten Mal
        int bis = 0;
        QList<std::pair<int, ZeilenInfo>> betroffen;
        QList<QString> werte;               // SpalteAus, parallel zu `betroffen`
        std::vector<Schritt> teile;         // Gruppe
        qsizetype zeichen = 0;
    };
    struct Grundlage {
        quint64 hash = 0;
        qint64  groesse = -1;
        qint64  zeit = 0;
    };
    struct SpeicherErgebnis;

    void neuLesen();
    void ergebnisUebernehmen(std::shared_ptr<Datei> d, const QString& fehler, Grundlage g);
    void spaltenNeuRechnen();
    //  Formelzellen suchen und auswerten - je Block mit seinem eigenen Bezug.
    //  Laeuft nach dem Lesen und nach jeder Aenderung, VOR den Spaltenbreiten
    //  und vor Suche und Ordnung: die rechnen alle mit dem gezeigten Wert.
    void formelnNeuRechnen();
    void bloeckeNeuBauen();
    void sucheStarten();
    void suchErgebnis(QList<Treffer> treffer, bool mehr, QList<int> proBlock);
    void ordnungNeuBauen();
    void ordnungErgebnis(QList<int> ordnung, bool aktiv);
    //  Anzeigezeile -> absolute Zeile in der Datei.
    int  rohZeile(int anzeige) const;
    //  Maske der gezeigten Spalten; leer = alle.
    QList<bool> spaltenMaske() const;
    //  Der gerade gezeigte Bereich; bei -1 die ganze Datei als EIN Bereich.
    Bereich aktiv() const;
    //  Der Index des Bereichs, in dem umgebaut wird (0 bei nur einem).
    int  aktiverBereich() const;

    //  Die Datei zum Aendern. Haelt ein Arbeitsfaden die alte noch, bekommt der
    //  Controller eine eigene Kopie - der Faden liest sonst, waehrend hier
    //  geschrieben wird.
    Datei& schreibDatei();
    void infoAnlegen();
    void markiere(int zeile, bool umgebaut);
    void schrittAusfuehren(Schritt& s, bool vorwaerts);
    void schrittAblegen(Schritt s);
    //  `formeln` steuert den Durchlauf durch die Formelzellen. Er kostet an
    //  100.000 Zeilen mal 20 Spalten 8,2 ms - den zahlte sonst jede Zelle
    //  einer Tabelle, die gar keine Formel kennt.
    void nachAenderung(bool struktur, bool spalten, bool formeln = true);
    //  Ist an dieser Aenderung ueberhaupt eine Formel beteiligt?
    bool formelBetroffen(const QString& a, const QString& b) const {
        return m_formelZahl > 0 || istFormel(a) || istFormel(b);
    }
    void zeilenEinsetzen(int bereich, int pos, const QList<Zeile>& zeilen,
                         const QList<ZeilenInfo>& infos, int anzeige);
    void zeilenEntfernen(int bereich, int pos, int anzahl);
    //  Alles, was an der SPALTENNUMMER haengt, ruecken lassen: ausgeblendete
    //  Spalten, Sortier- und Filterspalte, Breiten und Formate. Beim Entfernen
    //  kommen Breite und Format der weggefallenen Spalte heraus, damit
    //  Rueckgaengig sie wiederherstellen kann.
    bool spaltenIndexVerschieben(int spalte, int delta, int* wegBreite = nullptr,
                                 SpaltenFormat* wegFormat = nullptr);
    //  Breiten und Formate in die Beidatei.
    void spaltenAblegen();
    //  Alle drei Formatgriffe laufen hier zusammen.
    void formatAendern(int column, const std::function<void(SpaltenFormat&)>& aendere);
    enum class Modus { Normal, Verlassen, Kopie };
    void speichernStarten(Modus modus);
    void speicherErgebnis(const std::shared_ptr<SpeicherErgebnis>& e);

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
    QSet<int> m_versteckt;          // absolute Spaltennummern
    QHash<int, int> m_breiten;      // Spalte -> Breite in Pixeln, von Hand gesetzt
    QHash<int, SpaltenFormat> m_formate;

    //  Unveraenderlich, sobald gebaut - deshalb darf sie ein Arbeitsfaden halten.
    std::shared_ptr<const Werte> m_werte;
    int  m_formelZahl = 0;
    bool m_dezimalKomma = false;

    int          m_sortSpalte = -1;
    SortRichtung m_sortRichtung = SortRichtung::Keine;
    FilterRegel  m_filter;
    bool         m_sortLaeuft = false;
    int          m_inhaltRevision = 0;
    //  Absolute Zeilennummern in Anzeigereihenfolge, gueltig bei `m_ordnungAktiv`.
    QList<int>   m_ordnung;
    bool         m_ordnungAktiv = false;
    std::shared_ptr<std::atomic<bool>> m_sortAbbruch;

    Suchzustand  m_suche;
    QString      m_suchText;
    SuchOptionen m_suchOpt;
    int          m_suchAb = 0;
    bool         m_monatZuerst = false;
    bool         m_gruppiert = false;
    int          m_andereBloecke = 0;
    QList<int>   m_trefferProBlock;   // je Bereich, Reihenfolge wie m_bereiche
    bool         m_suchLaeuft = false;

    QList<ZeilenInfo> m_info;       // leer = unberuehrt
    quint32 m_naechsteId = 0;
    quint32 m_rev = 0;
    quint32 m_gespeichertRev = 0;
    quint32 m_basisRev = 0;
    quint32 m_basisGen = 0;
    quint32 m_ladeGen = 0;
    Grundlage   m_grundlage;
    QList<int>  m_basisZeilenNr;
    std::vector<Schritt> m_undo;
    std::vector<Schritt> m_redo;
    qsizetype m_undoZeichen = 0;
    bool    m_speichert = false;
    bool    m_nochmal = false;
    QString m_speicherFehler;
    QString m_kopieName;

    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_abbruch;
    std::shared_ptr<std::atomic<bool>> m_suchAbbruch;

    //  Eigener Faden: ein langer Export soll Suche und Sortierung nicht aufhalten.
    QThreadPool m_pdfPool;
    std::shared_ptr<std::atomic<bool>> m_pdfAbbruch = std::make_shared<std::atomic<bool>>(false);
    bool m_pdfBusy = false;
    int  m_pdfZaehlGen = 0;
    int  m_vorschauGen = 0;
    mg::PdfVorschau m_vorschau;
    enum class PdfArt { Schreiben, Zaehlen, Vorschau };
    void pdfStarten(PdfArt art, const QString& ziel, const QVariantMap& opt, int gen);
};

}  // namespace mg::table
