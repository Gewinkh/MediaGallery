#include "PdfEditCommands.h"
#include "PdfEditModel.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Hinzufügen
// ─────────────────────────────────────────────────────────────────────────────
PdfEditAddCommand::PdfEditAddCommand(PdfEditModel* model, const PdfEditBox& box, int row)
    : m_model(model), m_box(box), m_row(row) {}

void PdfEditAddCommand::redo() {
    m_model->insertBoxAt(m_row, m_box);
}

void PdfEditAddCommand::undo() {
    m_model->removeById(m_box.id);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Entfernen
// ─────────────────────────────────────────────────────────────────────────────
PdfEditRemoveCommand::PdfEditRemoveCommand(PdfEditModel* model, const PdfEditBox& box, int row)
    : m_model(model), m_box(box), m_row(row) {}

void PdfEditRemoveCommand::redo() {
    m_model->removeById(m_box.id);
}

void PdfEditRemoveCommand::undo() {
    m_model->insertBoxAt(m_row, m_box);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Geometrie
// ─────────────────────────────────────────────────────────────────────────────
PdfEditGeometryCommand::PdfEditGeometryCommand(PdfEditModel* model, int id,
                                               int oldPage, const QRectF& oldRect,
                                               const QVector<QPointF>& oldPts,
                                               int newPage, const QRectF& newRect,
                                               const QVector<QPointF>& newPts)
    : m_model(model), m_id(id)
    , m_oldPage(oldPage), m_old(oldRect), m_oldPts(oldPts)
    , m_newPage(newPage), m_new(newRect), m_newPts(newPts) {}

void PdfEditGeometryCommand::redo() {
    m_model->applyPlacementPoints(m_id, m_newPage, m_new, m_newPts);
}

void PdfEditGeometryCommand::undo() {
    m_model->applyPlacementPoints(m_id, m_oldPage, m_old, m_oldPts);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Text
// ─────────────────────────────────────────────────────────────────────────────
PdfEditTextCommand::PdfEditTextCommand(PdfEditModel* model, int id,
                                       const QString& oldText, const QString& newText)
    : m_model(model), m_id(id), m_old(oldText), m_new(newText) {}

void PdfEditTextCommand::redo() {
    m_model->applyText(m_id, m_new);
}

void PdfEditTextCommand::undo() {
    m_model->applyText(m_id, m_old);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Stil-/Formatfeld
// ─────────────────────────────────────────────────────────────────────────────
PdfEditFieldCommand::PdfEditFieldCommand(PdfEditModel* model, int id, PdfEditField field,
                                         const QVariant& oldValue, const QVariant& newValue)
    : m_model(model), m_id(id), m_field(field), m_old(oldValue), m_new(newValue) {}

void PdfEditFieldCommand::redo() {
    m_model->applyField(m_id, m_field, m_new);
}

void PdfEditFieldCommand::undo() {
    m_model->applyField(m_id, m_field, m_old);
}

bool PdfEditFieldCommand::mergeWith(const QUndoCommand* other) {
    if (other->id() != kCommandId)
        return false;
    const auto* o = static_cast<const PdfEditFieldCommand*>(other);
    if (o->m_id != m_id || o->m_field != m_field)
        return false;
    m_new = o->m_new;
    // Serie hebt sich exakt auf (z. B. Bold an→aus) → Kommando komplett
    // verwerfen, statt einen wirkungslosen Undo-Schritt zu hinterlassen.
    if (m_new == m_old)
        setObsolete(true);
    return true;
}
