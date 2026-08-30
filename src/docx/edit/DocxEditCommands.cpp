#include "docx/edit/DocxEditCommands.h"
#include "docx/edit/DocxEditController.h"

DocxReplaceBlocksCommand::DocxReplaceBlocksCommand(
    DocxEditController* ctl, int first,
    QList<Docx::Block> before, QList<Docx::Block> after,
    const DocxCursor& curBefore, const DocxCursor& curAfter, int mergeKind)
    : m_ctl(ctl), m_first(first),
      m_before(std::move(before)), m_after(std::move(after)),
      m_curBefore(curBefore), m_curAfter(curAfter), m_mergeKind(mergeKind) {}

void DocxReplaceBlocksCommand::snapshotTable(int tableId,
                                             const Docx::TableDef& before,
                                             const Docx::TableDef& after) {
    m_tableId  = tableId;
    m_tblBefore = before;
    m_tblAfter  = after;
}

void DocxReplaceBlocksCommand::redo() {
    //  QUndoStack::push ruft redo() sofort - die Mutation ist zu diesem
    //  Zeitpunkt aber bereits am Dokument vollzogen (Scope-Muster des
    //  Controllers). Erst ECHTE Redos wenden den Nachher-Zustand an.
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }
    //  Gerüst VOR den Blöcken: die Anzeige baut das Gitter aus beidem, und
    //  applyBlocks stößt das Neu-Auslegen an.
    if (m_tableId >= 0) m_ctl->applyTableDef(m_tableId, m_tblAfter);
    m_ctl->applyBlocks(m_first, m_before.size(), m_after, m_curAfter);
}

void DocxReplaceBlocksCommand::undo() {
    if (m_tableId >= 0) m_ctl->applyTableDef(m_tableId, m_tblBefore);
    m_ctl->applyBlocks(m_first, m_after.size(), m_before, m_curBefore);
}

// ─── Seitenränder ────────────────────────────────────────────────────────────
DocxSectionCommand::DocxSectionCommand(DocxEditController* ctl,
                                       Docx::Document::SectionState before,
                                       Docx::Document::SectionState after)
    : m_ctl(ctl), m_before(std::move(before)), m_after(std::move(after)) {
    setText(QStringLiteral("Seitenränder"));
}

void DocxSectionCommand::redo() {
    //  Wie bei den Blöcken: `push()` ruft redo() sofort, die Änderung ist zu
    //  diesem Zeitpunkt aber schon am Dokument vollzogen.
    if (m_firstRedo) { m_firstRedo = false; return; }
    m_ctl->applySectionState(m_after);
}

void DocxSectionCommand::undo() {
    m_ctl->applySectionState(m_before);
}

bool DocxSectionCommand::mergeWith(const QUndoCommand* other) {
    const auto* o = dynamic_cast<const DocxSectionCommand*>(other);
    if (!o) return false;
    m_after = o->m_after;
    return true;
}

bool DocxReplaceBlocksCommand::mergeWith(const QUndoCommand* other) {
    const auto* o = dynamic_cast<const DocxReplaceBlocksCommand*>(other);
    //  Verschmelzen nur: gleiche Koaleszenz-Klasse, derselbe EINE Block,
    //  1:1-Ersetzung auf beiden Seiten (reines Tippen/Löschen im Absatz).
    if (!o || o->m_mergeKind != m_mergeKind || m_mergeKind < 0
        || o->m_first != m_first
        || m_before.size() != 1 || m_after.size() != 1
        || o->m_before.size() != 1 || o->m_after.size() != 1)
        return false;
    m_after    = o->m_after;
    m_curAfter = o->m_curAfter;
    return true;
}
