#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  ImageEditCommands.h — Delta-Kommandos des Undo/Redo-Systems (Bild-Editor).
// ══════════════════════════════════════════════════════════════════════════════
//
//  RAM-EFFIZIENZ (analog PdfEditCommands): KEINE vollständigen Snapshots. Jedes
//  Kommando speichert nur das DELTA genau EINER Annotation:
//   • Add/Remove  → die eine Annotation (ihr minimales Delta) + Zeile
//   • Geometry    → id + altes/neues Rechteck + alte/neue Punkte (Striche)
//   • Text        → id + alter/neuer String
//   • Field       → id + Feld + alter/neuer QVariant (mergefähig)
//  Der QUndoStack (QtGui seit Qt 6 — kein Widgets-Bezug) hält damit selbst bei
//  langen Sitzungen nur Kilobytes. Kontinuierliche Gesten (Ziehen/Zeichnen/
//  Tippen) erzeugen über die Session-API des Controllers ohnehin nur EIN Kommando.
// ══════════════════════════════════════════════════════════════════════════════

#include <QUndoCommand>
#include <QVariant>
#include <QRectF>
#include <QPointF>
#include <QVector>
#include "ImageEditTypes.h"

class ImageEditModel;

// ─────────────────────────────────────────────────────────────────────────────
//  Annotation hinzufügen (undo entfernt sie wieder — Zeile bleibt stabil).
// ─────────────────────────────────────────────────────────────────────────────
class ImageEditAddCommand : public QUndoCommand {
public:
    ImageEditAddCommand(ImageEditModel* model, const ImageAnnotation& ann, int row);
    void redo() override;
    void undo() override;
private:
    ImageEditModel* m_model;
    ImageAnnotation m_ann;
    int             m_row;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Annotation entfernen (undo fügt sie an der alten Zeile wieder ein).
// ─────────────────────────────────────────────────────────────────────────────
class ImageEditRemoveCommand : public QUndoCommand {
public:
    ImageEditRemoveCommand(ImageEditModel* model, const ImageAnnotation& ann, int row);
    void redo() override;
    void undo() override;
private:
    ImageEditModel* m_model;
    ImageAnnotation m_ann;
    int             m_row;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Verschieben/Skalieren — EIN Kommando je Drag-Session (Delta alt→neu).
//  Trägt neben dem Rechteck auch die PUNKTE (Freihand/Pfeil werden beim
//  Verschieben/Skalieren mit-transformiert; Undo stellt beides wieder her).
// ─────────────────────────────────────────────────────────────────────────────
class ImageEditGeometryCommand : public QUndoCommand {
public:
    ImageEditGeometryCommand(ImageEditModel* model, int id,
                             const QRectF& oldRect, const QVector<QPointF>& oldPts,
                             const QRectF& newRect, const QVector<QPointF>& newPts);
    void redo() override;
    void undo() override;
private:
    ImageEditModel*     m_model;
    int                 m_id;
    QRectF              m_oldRect;
    QVector<QPointF>    m_oldPts;
    QRectF              m_newRect;
    QVector<QPointF>    m_newPts;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Textänderung — EIN Kommando je Bearbeitungs-Session (Delta alt→neu).
// ─────────────────────────────────────────────────────────────────────────────
class ImageEditTextCommand : public QUndoCommand {
public:
    ImageEditTextCommand(ImageEditModel* model, int id,
                         const QString& oldText, const QString& newText);
    void redo() override;
    void undo() override;
private:
    ImageEditModel* m_model;
    int             m_id;
    QString         m_old;
    QString         m_new;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Stil-/Formatfeld (Bold/Italic/Underline/Größe/Farbe/Hervorhebung/
//  Ausrichtung/Schriftart/Linienfarbe/-breite/Füllung). Aufeinanderfolgende
//  Änderungen DESSELBEN Feldes derselben Annotation verschmelzen (mergeWith) —
//  Slider-/SpinBox-Serien erzeugen so einen einzigen Undo-Schritt; hebt sich
//  eine Serie exakt auf, verwirft setObsolete() das Kommando ganz.
// ─────────────────────────────────────────────────────────────────────────────
class ImageEditFieldCommand : public QUndoCommand {
public:
    static constexpr int kCommandId = 4712;

    ImageEditFieldCommand(ImageEditModel* model, int id, ImageAnnField field,
                          const QVariant& oldValue, const QVariant& newValue);
    void redo() override;
    void undo() override;
    int  id() const override { return kCommandId; }
    bool mergeWith(const QUndoCommand* other) override;
private:
    ImageEditModel* m_model;
    int             m_id;
    ImageAnnField   m_field;
    QVariant        m_old;
    QVariant        m_new;
};
