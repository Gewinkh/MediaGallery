#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  DocxController - QML-Singleton „Docx" des DOCX-Editors (Muster PdfEdit-
//  Panel-Einstellung): trägt die GLOBALE Speicherverhalten-Einstellung
//  (persistiert via ISettings) - der eigentliche Editor-Zustand lebt bewusst
//  DEZENTRAL in den DocxEditController-Instanzen je Kachel (Split-View!).
//
//   saveDirect = true  (Standard): „Direkt speichern" - Speichern-Button/
//                Strg+S schreibt auf die Originaldatei; einmalig je Sitzung
//                entsteht vorher eine .bak-Sicherungskopie daneben.
//   saveDirect = false: „Kopie exportieren" - das Original bleibt unangetastet,
//                beim Speichern entsteht <Name>_edited(.n).docx.
//
//  Dazu kommt, was beim Weg „DOCX -> PDF" gilt: der zusätzliche RAND und die
//  SEITENZAHL. Beides ist global und überlebt den Programmstart; die Seitenzahl
//  wird in der Werkzeugleiste des Dokuments gewählt (dort arbeitet man daran),
//  der Rand in den Einstellungen (er wird einmal gesetzt, wie die Kachelgröße).
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>

class ISettings;

class DocxController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool saveDirect READ saveDirect WRITE setSaveDirect NOTIFY saveDirectChanged)
    //  Zusätzlicher Rand beim PDF-Export, in Millimetern (0…40, Vorgabe 0).
    //  Das Papierformat bleibt das aus Word; der Inhalt wird maßstäblich
    //  kleiner gemalt (Festlegung des Nutzers - A4 bleibt A4).
    //  Seitenzahl unten: 0 = aus · 1 = links · 2 = mittig · 3 = rechts.
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
