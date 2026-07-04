#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfEditCommands.h — Delta-Kommandos des Undo/Redo-Systems.
// ══════════════════════════════════════════════════════════════════════════════
//
//  RAM-EFFIZIENZ (Anforderung: KEINE vollständigen Snapshots)
//  ──────────────────────────────────────────────────────────
//  Jedes Kommando speichert nur das DELTA genau EINER Box:
//   • Add/Remove  → die eine Box (das ist ihr minimales Delta) + Zeile
//   • Geometry    → id + altes/neues Rechteck
//   • Text        → id + alter/neuer String
//   • Field       → id + Feld + alter/neuer QVariant
//  Der QUndoStack (QtGui seit Qt 6 — kein Widgets-Bezug) hält damit selbst bei
//  langen Sitzungen nur Kilobytes. Kontinuierliche Gesten (Ziehen/Tippen)
//  erzeugen über die Session-API des Controllers ohnehin nur EIN Kommando.
//
//  Hinweis Erst-redo(): push() führt redo() sofort aus. Bei Session-Kommandos
//  (Geometry/Text) trägt das Modell den neuen Wert bereits → das erneute
//  Anwenden ist ein idempotentes No-Op (applyX prüft auf Gleichheit).
// ══════════════════════════════════════════════════════════════════════════════

#include <QUndoCommand>
#include <QVariant>
#include "PdfEditTypes.h"

class PdfEditModel;

// ─────────────────────────────────────────────────────────────────────────────
//  Textbox hinzufügen (undo entfernt sie wieder — Zeile bleibt stabil).
// ─────────────────────────────────────────────────────────────────────────────
class PdfEditAddCommand : public QUndoCommand {
public:
    PdfEditAddCommand(PdfEditModel* model, const PdfEditBox& box, int row);
    void redo() override;
    void undo() override;
private:
    PdfEditModel* m_model;
    PdfEditBox    m_box;
    int           m_row;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Textbox entfernen (undo fügt sie an der alten Zeile wieder ein).
// ─────────────────────────────────────────────────────────────────────────────
class PdfEditRemoveCommand : public QUndoCommand {
public:
    PdfEditRemoveCommand(PdfEditModel* model, const PdfEditBox& box, int row);
    void redo() override;
    void undo() override;
private:
    PdfEditModel* m_model;
    PdfEditBox    m_box;
    int           m_row;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Verschieben/Skalieren — EIN Kommando je Drag-Session (Delta alt→neu).
//  Trägt neben dem Rechteck auch die SEITE (seitenübergreifendes Verschieben:
//  Undo bringt die Box auf die alte Seite zurück).
// ─────────────────────────────────────────────────────────────────────────────
class PdfEditGeometryCommand : public QUndoCommand {
public:
    PdfEditGeometryCommand(PdfEditModel* model, int id,
                           int oldPage, const QRectF& oldRect,
                           int newPage, const QRectF& newRect);
    void redo() override;
    void undo() override;
private:
    PdfEditModel* m_model;
    int           m_id;
    int           m_oldPage;
    QRectF        m_old;
    int           m_newPage;
    QRectF        m_new;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Textänderung — EIN Kommando je Bearbeitungs-Session (Delta alt→neu).
// ─────────────────────────────────────────────────────────────────────────────
class PdfEditTextCommand : public QUndoCommand {
public:
    PdfEditTextCommand(PdfEditModel* model, int id,
                       const QString& oldText, const QString& newText);
    void redo() override;
    void undo() override;
private:
    PdfEditModel* m_model;
    int           m_id;
    QString       m_old;
    QString       m_new;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Stil-/Formatfeld (Bold/Italic/Underline/Größe/Farbe/Hervorhebung/
//  Ausrichtung/Schriftart). Aufeinanderfolgende Änderungen DESSELBEN Feldes
//  derselben Box verschmelzen (mergeWith) — SpinBox-Klickserien erzeugen so
//  einen einzigen Undo-Schritt; hebt sich eine Serie exakt auf (alt == neu),
//  verwirft setObsolete() das Kommando ganz.
// ─────────────────────────────────────────────────────────────────────────────
class PdfEditFieldCommand : public QUndoCommand {
public:
    static constexpr int kCommandId = 4711;

    PdfEditFieldCommand(PdfEditModel* model, int id, PdfEditField field,
                        const QVariant& oldValue, const QVariant& newValue);
    void redo() override;
    void undo() override;
    int  id() const override { return kCommandId; }
    bool mergeWith(const QUndoCommand* other) override;
private:
    PdfEditModel* m_model;
    int           m_id;
    PdfEditField  m_field;
    QVariant      m_old;
    QVariant      m_new;
};
