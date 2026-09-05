#pragma once
// QML-Singleton Docx: traegt nur die globalen Einstellungen (Speicherverhalten, Rand
// und Seitenzahl fuer DOCX -> PDF). Der Editor-Zustand lebt dezentral je Kachel.
// saveDirect: true schreibt aufs Original mit einmaliger .bak, false exportiert eine Kopie.

#include <QObject>

class ISettings;

class DocxController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool saveDirect READ saveDirect WRITE setSaveDirect NOTIFY saveDirectChanged)
    // Zusätzlicher Rand beim PDF-Export in Millimetern (0-40, Vorgabe 0); das Papierformat bleibt das aus Word, der
    // Inhalt wird maßstäblich kleiner gemalt. Seitenzahl unten: 0 aus, 1 links, 2 mittig, 3 rechts.
    Q_PROPERTY(int pdfPageNumberPos READ pdfPageNumberPos WRITE setPdfPageNumberPos
               NOTIFY pdfOptionsChanged)
    //  Form: 0 = nur die Seite („3") · 1 = mit Gesamtzahl („3 / 12").
    Q_PROPERTY(int pdfPageNumberStyle READ pdfPageNumberStyle WRITE setPdfPageNumberStyle
               NOTIFY pdfOptionsChanged)

public:
    explicit DocxController(ISettings& settings, QObject* parent = nullptr);

    bool saveDirect() const;
    void setSaveDirect(bool v);
    int  pdfPageNumberPos() const;
    void setPdfPageNumberPos(int pos);
    int  pdfPageNumberStyle() const;
    void setPdfPageNumberStyle(int style);

signals:
    void saveDirectChanged();
    void pdfOptionsChanged();

private:
    ISettings& m_settings;
};
