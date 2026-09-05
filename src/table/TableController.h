#pragma once
//  TableController - der Zustand EINER geoeffneten Tabellendatei (CSV/TSV).
//  Je Kachel eine Instanz, damit zwei Haelften verschiedene Dateien zeigen.
//  Zeigt nur an; Bearbeiten und Zurueckschreiben sind noch nicht gebaut.
#include "table/DelimitedText.h"

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

    //  Ist die Zeile eine Leerzeile der Datei? Die Anzeige laesst sie dann
    //  ohne Streifen stehen, damit die Luecke als Luecke zu sehen ist.
    Q_INVOKABLE bool rowEmpty(int row) const;

signals:
    void sourceChanged();
    void stateChanged();
    void blockChanged();

private:
    void neuLesen();
    void ergebnisUebernehmen(std::shared_ptr<Datei> d, const QString& fehler);
    void spaltenNeuRechnen();
    void bloeckeNeuBauen();
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

    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_abbruch;
};

}  // namespace mg::table
