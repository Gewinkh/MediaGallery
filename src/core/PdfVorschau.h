#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// PdfVorschau - welche Seiten ein PDF-Export schreibt, und die Vorschau-Datei, an der man sie auswaehlt.
namespace mg {

// Die Seiten (1-basiert) in Schreibfolge: die Liste, wenn eine da ist - in IHRER Reihenfolge, ohne Doppel, nur
// gueltige -, sonst der Bereich `von`..`bis` (`bis` 0 = bis zum Ende). Leer nur bei `gesamt` 0.
QList<int> seitenFolge(int gesamt, int von, int bis, const QList<int>& liste);

// Haelt die Vorschau-Dateien EINES Besitzers im Temp-Ordner. Eine neue wird angelegt, bevor sie geschrieben
// wird; geloescht wird die alte erst, wenn die neue uebernommen ist - ein noch schreibender Lauf legte eine
// zu frueh geloeschte Datei sonst wieder an. Alles, was noch liegt, faellt mit dem Besitzer.
class PdfVorschau {
public:
    PdfVorschau() = default;
    PdfVorschau(const PdfVorschau&) = delete;
    PdfVorschau& operator=(const PdfVorschau&) = delete;
    ~PdfVorschau();

    QString neu();                           // leere Datei mit eindeutigem Namen, leer bei Fehler
    void    uebernehme(const QString& pfad);  // wird die aktuelle; die vorige faellt
    void    verwerfe(const QString& pfad);    // ein veralteter oder abgebrochener Lauf
    void    leere();                          // alles weg, etwa beim Freigeben der Flaeche
    QString aktuell() const { return m_aktuell; }

private:
    QString     m_aktuell;
    QStringList m_offen;
};

}  // namespace mg
