#include "image/edit/ImageEditCommands.h"
#include "image/edit/ImageEditModel.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Hinzufügen
// ─────────────────────────────────────────────────────────────────────────────
ImageEditAddCommand::ImageEditAddCommand(ImageEditModel* model, const ImageAnnotation& ann, int row)
    : m_model(model), m_ann(ann), m_row(row) {}

void ImageEditAddCommand::redo() {
    m_model->insertAnnAt(m_row, m_ann);
}

void ImageEditAddCommand::undo() {
    m_model->removeById(m_ann.id);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Entfernen
// ─────────────────────────────────────────────────────────────────────────────
ImageEditRemoveCommand::ImageEditRemoveCommand(ImageEditModel* model, const ImageAnnotation& ann, int row)
    : m_model(model), m_ann(ann), m_row(row) {}

void ImageEditRemoveCommand::redo() {
    m_model->removeById(m_ann.id);
}

void ImageEditRemoveCommand::undo() {
    m_model->insertAnnAt(m_row, m_ann);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Geometrie (Rechteck + Punkte)
// ─────────────────────────────────────────────────────────────────────────────
ImageEditGeometryCommand::ImageEditGeometryCommand(ImageEditModel* model, int id,
                                                   const QRectF& oldRect, const QVector<QPointF>& oldPts,
                                                   const QRectF& newRect, const QVector<QPointF>& newPts)
    : m_model(model), m_id(id)
    , m_oldRect(oldRect), m_oldPts(oldPts)
    , m_newRect(newRect), m_newPts(newPts) {}

void ImageEditGeometryCommand::redo() {
    m_model->applyGeometryPoints(m_id, m_newRect, m_newPts);
}

void ImageEditGeometryCommand::undo() {
    m_model->applyGeometryPoints(m_id, m_oldRect, m_oldPts);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Text
// ─────────────────────────────────────────────────────────────────────────────
ImageEditTextCommand::ImageEditTextCommand(ImageEditModel* model, int id,
                                           const QString& oldText, const QString& newText)
    : m_model(model), m_id(id), m_old(oldText), m_new(newText) {}

void ImageEditTextCommand::redo() {
    m_model->applyText(m_id, m_new);
}

void ImageEditTextCommand::undo() {
    m_model->applyText(m_id, m_old);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Stil-/Formatfeld
// ─────────────────────────────────────────────────────────────────────────────
ImageEditFieldCommand::ImageEditFieldCommand(ImageEditModel* model, int id, ImageAnnField field,
                                             const QVariant& oldValue, const QVariant& newValue)
    : m_model(model), m_id(id), m_field(field), m_old(oldValue), m_new(newValue) {}

void ImageEditFieldCommand::redo() {
    m_model->applyField(m_id, m_field, m_new);
}

void ImageEditFieldCommand::undo() {
    m_model->applyField(m_id, m_field, m_old);
}

bool ImageEditFieldCommand::mergeWith(const QUndoCommand* other) {
    if (other->id() != kCommandId)
        return false;
    const auto* o = static_cast<const ImageEditFieldCommand*>(other);
    if (o->m_id != m_id || o->m_field != m_field)
        return false;
    m_new = o->m_new;
    // Serie hebt sich exakt auf (z. B. Bold an->aus) -> Kommando komplett
    // verwerfen, statt einen wirkungslosen Undo-Schritt zu hinterlassen.
    if (m_new == m_old)
        setObsolete(true);
    return true;
}
