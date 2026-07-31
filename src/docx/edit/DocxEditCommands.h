#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  DocxEditCommands — Undo/Redo des DOCX-Editors.
//
//  Ein EINZIGER Kommandotyp deckt alle Bearbeitungen ab (Muster „Zustand des
//  betroffenen Bereichs vorher/nachher"): ReplaceBlockRangeCommand ersetzt
//  einen zusammenhängenden Block-Bereich [first, first+oldCount) durch die
//  gespeicherten Nachher-Blöcke. Das trägt Tippen, Löschen, Absatz-Split/
//  Merge, Zeichen-/Absatzformatierung und Listen gleichermaßen — Blöcke sind
//  klein (Runs = Spans + kurze Strings), daher RAM-freundlich (Regel 3).
//
//  Tipp-Koaleszenz: aufeinanderfolgende Zeichen-Eingaben im selben Absatz
//  verschmelzen über mergeWith() zu EINEM Undo-Schritt (Word-üblich), Cursor-
//  Positionen wandern mit.
// ─────────────────────────────────────────────────────────────────────────────

#include "docx/DocxDocument.h"
#include <QUndoCommand>

class DocxEditController;

//  Cursor als (Blockindex, Zeichenposition im Absatz-Klartext) + Anker für
//  Selektionen (Anker == Position ⇒ keine Selektion).
struct DocxCursor {
    int block = 0;
    int pos   = 0;
    int aBlock = 0;
    int aPos   = 0;
    bool hasSelection() const { return block != aBlock || pos != aPos; }
    void collapse() { aBlock = block; aPos = pos; }
};

class DocxReplaceBlocksCommand : public QUndoCommand {
public:
    //  mergeKind: −1 = nie verschmelzen; ≥0 = Koaleszenz-Klasse (0 = Tippen,
    //  1 = Rückwärtslöschen) — verschmolzen wird nur gleiche Klasse + gleicher
    //  Block + 1:1-Ersetzung.
    DocxReplaceBlocksCommand(DocxEditController* ctl, int first,
                             QList<Docx::Block> before, QList<Docx::Block> after,
                             const DocxCursor& curBefore, const DocxCursor& curAfter,
                             int mergeKind = -1);

    //  Struktur-Änderungen an einer Tabelle (Zeile/Spalte/Breite) mutieren
    //  NEBEN den Blöcken auch das Gerüst (TableDef). Ohne diesen Schnappschuss
    //  liefe Undo auseinander: die Blöcke kämen zurück, das Gerüst behielte die
    //  zusätzliche Zeile — und beim Speichern entstünde eine leere Geisterzeile.
    void snapshotTable(int tableId, const Docx::TableDef& before,
                       const Docx::TableDef& after);
    //  Region (Körper/Kopf-/Fußzeile), in der dieses Kommando gilt. Undo/Redo
    //  schalten VOR dem Anwenden dorthin um — die Ansicht folgt, genau wie in
    //  Word. Deshalb genügt EIN Undo-Stapel für alle Regionen.
    void setRegion(int r) { m_regionId = r; }

    void undo() override;
    void redo() override;
    int  id() const override { return m_mergeKind >= 0 ? 0xD0C : -1; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    DocxEditController* m_ctl;
    int                 m_first;
    QList<Docx::Block>  m_before;
    QList<Docx::Block>  m_after;
    DocxCursor          m_curBefore;
    DocxCursor          m_curAfter;
    int                 m_mergeKind;
    bool                m_firstRedo = true;   // push() ruft redo(); Mutation lief schon
    int                 m_regionId = 0;       // DocxEditController::Region
    int                 m_tableId = -1;       // −1 = kein Gerüst betroffen
    Docx::TableDef      m_tblBefore, m_tblAfter;
};
