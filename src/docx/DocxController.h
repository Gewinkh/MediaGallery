#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  DocxController — QML-Singleton „Docx" des DOCX-Editors (Muster PdfEdit-
//  Panel-Einstellung): trägt die GLOBALE Speicherverhalten-Einstellung
//  (persistiert via ISettings) — der eigentliche Editor-Zustand lebt bewusst
//  DEZENTRAL in den DocxEditController-Instanzen je Kachel (Split-View!).
//
//   saveDirect = true  (Standard): „Direkt speichern" — Speichern-Button/
//                Strg+S schreibt auf die Originaldatei; einmalig je Sitzung
//                entsteht vorher eine .bak-Sicherungskopie daneben.
//   saveDirect = false: „Kopie exportieren" — das Original bleibt unangetastet,
//                beim Speichern entsteht <Name>_edited(.n).docx.
// ─────────────────────────────────────────────────────────────────────────────

#include <QObject>

class ISettings;

class DocxController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool saveDirect READ saveDirect WRITE setSaveDirect NOTIFY saveDirectChanged)

public:
    explicit DocxController(ISettings& settings, QObject* parent = nullptr);

    bool saveDirect() const;
    void setSaveDirect(bool v);

signals:
    void saveDirectChanged();

private:
    ISettings& m_settings;
};
