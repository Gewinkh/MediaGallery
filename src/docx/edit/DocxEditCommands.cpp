#include "docx/edit/DocxEditCommands.h"
#include "docx/edit/DocxEditController.h"

DocxReplaceBlocksCommand::DocxReplaceBlocksCommand(
    DocxEditController* ctl, int first,
    QList<Docx::Block> before, QList<Docx::Block> after,
    const DocxCursor& curBefore, const DocxCursor& curAfter, int mergeKind)
    : m_ctl(ctl), m_first(first),
      m_before(std::move(before)), m_after(std::move(after)),
      m_curBefore(curBefore), m_curAfter(curAfter), m_mergeKind(mergeKind) {}

void DocxReplaceBlocksCommand::redo() {
    //  QUndoStack::push ruft redo() sofort — die Mutation ist zu diesem
    //  Zeitpunkt aber bereits am Dokument vollzogen (Scope-Muster des
    //  Controllers). Erst ECHTE Redos wenden den Nachher-Zustand an.
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }
    m_ctl->applyBlocks(m_first, m_before.size(), m_after, m_curAfter);
}

void DocxReplaceBlocksCommand::undo() {
    m_ctl->applyBlocks(m_first, m_after.size(), m_before, m_curBefore);
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
