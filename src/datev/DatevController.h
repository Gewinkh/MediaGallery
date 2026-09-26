#pragma once
//  DatevController - der Zustand EINER geoeffneten DATEV-Datei.
//  Je Kachel eine Instanz (wie PdfEditController), damit zwei Haelften
//  verschiedene Dateien zeigen koennen. Schreibt nie: in eine Buchungsdatei
//  zurueckzuschreiben waere ein Schaden, den keine Bequemlichkeit aufwiegt.
#include "datev/DatevCsv.h"
#include "table/TableFilter.h"
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

namespace mg::datev {

class DatevController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool    busy   READ busy   NOTIFY stateChanged)
    Q_PROPERTY(bool    ready  READ ready  NOTIFY stateChanged)
    Q_PROPERTY(QString error  READ error  NOTIFY stateChanged)

    Q_PROPERTY(QString identifier  READ identifier  NOTIFY stateChanged)
    Q_PROPERTY(int     version     READ version     NOTIFY stateChanged)
    Q_PROPERTY(QString formatName  READ formatName  NOTIFY stateChanged)
    Q_PROPERTY(QString createdAt   READ createdAt   NOTIFY stateChanged)
    //  Je Eintrag { number, name, value } - `name` leer, solange das Feld im
    //  Katalog nicht benannt ist.
    Q_PROPERTY(QVariantList headerFields READ headerFields NOTIFY stateChanged)

    //  `rowCount` zaehlt die GEZEIGTEN Buchungen, `totalRows` alle.
    Q_PROPERTY(int rowCount    READ rowCount    NOTIFY rowsChanged)
    Q_PROPERTY(int totalRows   READ totalRows   NOTIFY rowsChanged)
    Q_PROPERTY(int columnCount READ columnCount NOTIFY stateChanged)
    //  Sichtbare Spalten als { index, title }. 125 Spalten sind nicht lesbar -
    //  vorgegeben sind deshalb nur die, die in mindestens einer Buchung etwas
    //  enthalten (in der Vorlage 12 statt 125).
    Q_PROPERTY(QVariantList columns READ columns NOTIFY columnsChanged)
    Q_PROPERTY(bool showAllColumns READ showAllColumns WRITE setShowAllColumns
                                   NOTIFY columnsChanged)
    //  Zusaetzlich EINZELN ausgeblendete. Der Schalter oben entscheidet, welche
    //  Spalten ueberhaupt in Frage kommen; das hier nimmt daraus weitere heraus.
    Q_PROPERTY(int hiddenColumnCount READ hiddenColumnCount NOTIFY columnsChanged)

    //  Sortieren aendert die DATEI NICHT (s. `table/TableSort.h`) - fuer eine
    //  Buchhaltungsdatei ist genau das die Bedingung dafuer.
    Q_PROPERTY(int  sortColumn    READ sortColumn    NOTIFY sortChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY sortChanged)
    Q_PROPERTY(bool sorting       READ sorting       NOTIFY sortChanged)
    Q_PROPERTY(int  contentRevision READ contentRevision NOTIFY sortChanged)
    Q_PROPERTY(bool    filterActive READ filterActive NOTIFY sortChanged)
    Q_PROPERTY(int     filterColumn READ filterColumn NOTIFY sortChanged)
    Q_PROPERTY(QString filterText   READ filterText   NOTIFY sortChanged)

    //  Mit Filter ueber die GEZEIGTEN Buchungen - die Fusszeile sagt dann dazu,
    //  dass es nicht alle sind.
    Q_PROPERTY(double sumDebit  READ sumDebit  NOTIFY rowsChanged)
    Q_PROPERTY(double sumCredit READ sumCredit NOTIFY rowsChanged)
    Q_PROPERTY(double sumDiff   READ sumDiff   NOTIFY rowsChanged)

    //  Suche: dieselbe Maschine und dieselben Namen wie in der CSV-Ansicht
    //  (s. table/TableSearch.h) - beide Flaechen benutzen denselben Suchbalken.
    //  Der Lauf gehoert in den Arbeitsfaden: gemessen 119 ms je 100.000 Zeilen
    //  mal 20 Spalten.
    Q_PROPERTY(int  matchCount   READ matchCount   NOTIFY searchChanged)
    Q_PROPERTY(int  matchIndex   READ matchIndex   NOTIFY searchChanged)
    Q_PROPERTY(int  matchRow     READ matchRow     NOTIFY searchChanged)
    Q_PROPERTY(int  matchColumn  READ matchColumn  NOTIFY searchChanged)
    Q_PROPERTY(bool matchOverflow READ matchOverflow NOTIFY searchChanged)
    Q_PROPERTY(bool searching     READ searching     NOTIFY searchChanged)
    Q_PROPERTY(int  searchRevision READ searchRevision NOTIFY searchChanged)
    //  Zahlen mit Tausenderzeichen ANZEIGEN. Reine Darstellung - gelesen wird
    //  der Stapel ohnehin nur.
    Q_PROPERTY(bool groupDigits READ groupDigits WRITE setGroupDigits NOTIFY sortChanged)
    Q_PROPERTY(bool slashDateMonthFirst READ slashDateMonthFirst WRITE setSlashDateMonthFirst
                                        NOTIFY sortChanged)

    Q_PROPERTY(QStringList warnings READ warnings NOTIFY stateChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY stateChanged)
    Q_PROPERTY(bool cp1252    READ cp1252    NOTIFY stateChanged)

public:
    explicit DatevController(QObject* parent = nullptr);
    ~DatevController() override;

    QString source() const { return m_source; }
    void    setSource(const QString& pathOrUrl);
    //  Liest dieselbe Datei neu - nach einer Aenderung im Rohtext.
    Q_INVOKABLE void reload();

    bool    busy() const  { return m_busy; }
    bool    ready() const { return m_datei && m_datei->ok; }
    QString error() const { return m_fehler; }

    QString identifier() const;
    int     version() const;
    QString formatName() const;
    QString createdAt() const;
    QVariantList headerFields() const;

    int rowCount() const;
    int totalRows() const { return m_datei ? int(m_datei->buchungen.size()) : 0; }
    int columnCount() const;
    QVariantList columns() const { return m_spalten; }
    bool showAllColumns() const  { return m_alleSpalten; }
    void setShowAllColumns(bool v);
    int  hiddenColumnCount() const { return int(m_versteckt.size()); }

    int  sortColumn() const    { return m_sortSpalte; }
    bool sortAscending() const { return m_sortRichtung == mg::table::SortRichtung::Auf; }
    bool sorting() const       { return m_sortLaeuft; }
    int  contentRevision() const { return m_inhaltRevision; }
    bool    filterActive() const { return m_filter.aktiv(); }
    int     filterColumn() const { return m_filter.spalte; }
    QString filterText() const   { return m_filter.text; }

    //  Aufsteigend -> absteigend -> Dateireihenfolge.
    Q_INVOKABLE void sortByColumn(int column);
    Q_INVOKABLE void clearSort();
    //  Wie in der CSV-Ansicht (s. `table/TableFilter.h`); die Datei bleibt, wie sie ist.
    Q_INVOKABLE void setFilter(int column, const QString& text, bool caseSensitive,
                               bool wholeCell);
    Q_INVOKABLE void clearFilter();

    Q_INVOKABLE void setColumnHidden(int column, bool hidden);
    Q_INVOKABLE bool columnHidden(int column) const { return m_versteckt.contains(column); }
    Q_INVOKABLE void showAllHiddenColumns();

    //  Eine ganze Zeile als Text, die GEZEIGTEN Spalten mit Tabulator getrennt.
    Q_INVOKABLE QString rowText(int row) const;
    //  Ein Bereich als Tabulatortext. Die Spalten sind STELLEN in der gezeigten
    //  Liste, nicht Feldnummern - ein Bereich spannt ueber das, was man sieht.
    //  Ueber dem Deckel kommt eine leere Zeichenkette zurueck: der Aufbau
    //  laeuft im GUI-Faden, und was niemand mehr einfuegt, wird nicht gebaut.
    Q_INVOKABLE QString rangeText(int row1, int col1, int row2, int col2) const;

    //  Von Hand gesetzte Spaltenbreite in Pixeln; 0 stellt sie wieder auf
    //  automatisch. Gehalten wird sie in der Beidatei neben der Datei.
    Q_INVOKABLE void setColumnWidth(int column, int px);

    //  Formatierung je SPALTE - wie die Breite in der Beidatei. Die Datei
    //  selbst bleibt unberuehrt; ein Buchungsstapel wird nie geschrieben.
    Q_INVOKABLE void setColumnBold(int column, bool bold);
    Q_INVOKABLE void setColumnColor(int column, const QString& color);
    Q_INVOKABLE void setColumnBackground(int column, const QString& color);
    Q_INVOKABLE void clearColumnFormat(int column);
    Q_INVOKABLE bool columnHasFormat(int column) const {
        return m_formate.contains(column);
    }

    double sumDebit() const  { return m_soll; }
    double sumCredit() const { return m_haben; }
    double sumDiff() const   { return m_soll - m_haben; }

    QStringList warnings() const { return m_warnungen; }
    bool truncated() const { return m_datei && m_datei->abgeschnitten; }
    bool cp1252() const    { return m_datei && m_datei->cp1252; }

    //  EIN Feld - der Weg der Anzeige. Eine ganze Zeile zurueckzugeben kopierte
    //  je sichtbarer Zeile 125 Zeichenketten statt der zehn gezeigten.
    Q_INVOKABLE QString cell(int row, int column) const;
    //  Mit Filter die Nummer der Buchung, damit die Luecken sichtbar bleiben.
    Q_INVOKABLE int rowNumber(int row) const;

    int  matchCount() const  { return m_suche.anzahl(); }
    int  matchIndex() const  { return m_suche.index(); }
    int  matchRow() const    { return m_suche.index() < 0 ? -1 : m_suche.aktuell().zeile; }
    int  matchColumn() const { return m_suche.index() < 0 ? -1 : m_suche.aktuell().spalte; }
    bool matchOverflow() const { return m_suche.mehr(); }
    bool searching() const   { return m_suchLaeuft; }
    int  searchRevision() const { return m_suche.revision(); }
    bool groupDigits() const { return m_gruppiert; }
    void setGroupDigits(bool v);
    bool slashDateMonthFirst() const { return m_monatZuerst; }
    void setSlashDateMonthFirst(bool v);

    //  Gesucht wird nur in den GEZEIGTEN Spalten - ein Treffer in einer
    //  ausgeblendeten waere ein Sprung ins Nichts.
    Q_INVOKABLE void search(const QString& text, bool caseSensitive,
                            bool wholeCell, int fromRow);
    Q_INVOKABLE void stepMatch(int delta);
    Q_INVOKABLE void clearSearch();
    //  Die Trefferspalten EINER Zeile - die Anzeige legt nur dafuer Marken an.
    Q_INVOKABLE QVariantList rowMatches(int row) const;
    Q_INVOKABLE bool cellMatches(int row, int column) const {
        return m_suche.trifft(row, column);
    }

    //  Buchungen sind nie leer - die Anzeige fragt es trotzdem, weil sie
    //  denselben Tabellenkoerper benutzt.
    Q_INVOKABLE bool rowEmpty(int) const { return false; }

    //  Laengstes Feld dieser Spalte in Zeichen (Ueberschrift eingerechnet).
    //  Die Anzeige rechnet daraus die Spaltenbreite; gemessen wird nur ueber
    //  die ersten Zeilen, weil eine Datei mit 100.000 Buchungen sonst je
    //  Spalte einmal komplett gelesen wuerde.
    Q_INVOKABLE int columnChars(int column) const;

signals:
    void sourceChanged();
    void stateChanged();
    void columnsChanged();
    void searchChanged();
    void sortChanged();
    void rowsChanged();

private:
    void ergebnisUebernehmen(std::shared_ptr<Datei> d, const QString& fehler);
    void spaltenNeuRechnen();
    //  Breiten und Formate in die Beidatei.
    void spaltenAblegen();
    void formatAendern(int column,
                       const std::function<void(mg::table::SpaltenFormat&)>& aendere);
    void sucheStarten();
    void suchErgebnis(QList<mg::table::Treffer> treffer, bool mehr);
    void ordnungNeuBauen();
    void ordnungErgebnis(QList<int> ordnung, bool aktiv, const QVariant& summen);
    //  Anzeigezeile -> Buchung. Ohne Sortierung sind beide dieselbe Zahl.
    int  rohZeile(int anzeige) const;
    //  Maske der gezeigten Spalten: der Schalter oben UND die einzeln
    //  ausgeblendeten zusammen.
    QList<bool> spaltenMaske() const;

    QString m_source;
    QString m_fehler;
    bool    m_busy = false;
    bool    m_alleSpalten = false;

    std::shared_ptr<Datei> m_datei;
    QVariantList m_spalten;
    QStringList  m_warnungen;
    QSet<int>    m_versteckt;
    QHash<int, int> m_breiten;      // Spalte -> Breite in Pixeln, von Hand gesetzt
    QHash<int, mg::table::SpaltenFormat> m_formate;

    int  m_sortSpalte = -1;
    mg::table::SortRichtung m_sortRichtung = mg::table::SortRichtung::Keine;
    bool m_sortLaeuft = false;
    int  m_inhaltRevision = 0;
    bool m_gruppiert = false;
    //  Buchungsnummern in Anzeigereihenfolge, gueltig bei `m_ordnungAktiv`.
    QList<int> m_ordnung;
    bool m_ordnungAktiv = false;
    mg::table::FilterRegel m_filter;
    std::shared_ptr<std::atomic<bool>> m_sortAbbruch;
    double m_soll = 0.0;
    double m_haben = 0.0;

    mg::table::Suchzustand  m_suche;
    QString                 m_suchText;
    mg::table::SuchOptionen m_suchOpt;
    int  m_suchAb = 0;
    bool m_monatZuerst = false;
    bool m_suchLaeuft = false;

    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_abbruch;
    std::shared_ptr<std::atomic<bool>> m_suchAbbruch;
};

}  // namespace mg::datev
