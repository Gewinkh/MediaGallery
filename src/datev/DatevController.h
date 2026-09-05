#pragma once
//  DatevController - der Zustand EINER geoeffneten DATEV-Datei.
//  Je Kachel eine Instanz (wie PdfEditController), damit zwei Haelften
//  verschiedene Dateien zeigen koennen. Schreibt nie: in eine Buchungsdatei
//  zurueckzuschreiben waere ein Schaden, den keine Bequemlichkeit aufwiegt.
#include "datev/DatevCsv.h"

#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <atomic>
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

    Q_PROPERTY(int rowCount    READ rowCount    NOTIFY stateChanged)
    Q_PROPERTY(int columnCount READ columnCount NOTIFY stateChanged)
    //  Sichtbare Spalten als { index, title }. 125 Spalten sind nicht lesbar -
    //  vorgegeben sind deshalb nur die, die in mindestens einer Buchung etwas
    //  enthalten (in der Vorlage 12 statt 125).
    Q_PROPERTY(QVariantList columns READ columns NOTIFY columnsChanged)
    Q_PROPERTY(bool showAllColumns READ showAllColumns WRITE setShowAllColumns
                                   NOTIFY columnsChanged)

    Q_PROPERTY(double sumDebit  READ sumDebit  NOTIFY stateChanged)
    Q_PROPERTY(double sumCredit READ sumCredit NOTIFY stateChanged)
    Q_PROPERTY(double sumDiff   READ sumDiff   NOTIFY stateChanged)

    Q_PROPERTY(QStringList warnings READ warnings NOTIFY stateChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY stateChanged)
    Q_PROPERTY(bool cp1252    READ cp1252    NOTIFY stateChanged)

public:
    explicit DatevController(QObject* parent = nullptr);
    ~DatevController() override;

    QString source() const { return m_source; }
    void    setSource(const QString& pathOrUrl);

    bool    busy() const  { return m_busy; }
    bool    ready() const { return m_datei && m_datei->ok; }
    QString error() const { return m_fehler; }

    QString identifier() const;
    int     version() const;
    QString formatName() const;
    QString createdAt() const;
    QVariantList headerFields() const;

    int rowCount() const;
    int columnCount() const;
    QVariantList columns() const { return m_spalten; }
    bool showAllColumns() const  { return m_alleSpalten; }
    void setShowAllColumns(bool v);

    double sumDebit() const  { return m_soll; }
    double sumCredit() const { return m_haben; }
    double sumDiff() const   { return m_soll - m_haben; }

    QStringList warnings() const { return m_warnungen; }
    bool truncated() const { return m_datei && m_datei->abgeschnitten; }
    bool cp1252() const    { return m_datei && m_datei->cp1252; }

    //  EIN Feld - der Weg der Anzeige. Eine ganze Zeile zurueckzugeben kopierte
    //  je sichtbarer Zeile 125 Zeichenketten statt der zehn gezeigten.
    Q_INVOKABLE QString cell(int row, int column) const;

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

private:
    void ergebnisUebernehmen(std::shared_ptr<Datei> d, const QString& fehler);
    void spaltenNeuRechnen();

    QString m_source;
    QString m_fehler;
    bool    m_busy = false;
    bool    m_alleSpalten = false;

    std::shared_ptr<Datei> m_datei;
    QVariantList m_spalten;
    QStringList  m_warnungen;
    double m_soll = 0.0;
    double m_haben = 0.0;

    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_abbruch;
};

}  // namespace mg::datev
