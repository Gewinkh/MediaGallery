#pragma once
// Ein einziger Kommandotyp deckt alles ab: ReplaceBlockRangeCommand ersetzt einen
// Blockbereich durch die gespeicherten Nachher-Bloecke. Aufeinanderfolgende Eingaben
// im selben Absatz verschmelzen ueber mergeWith zu einem Undo-Schritt.

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
    //  1 = Rückwärtslöschen) - verschmolzen wird nur gleiche Klasse + gleicher
    //  Block + 1:1-Ersetzung.
    DocxReplaceBlocksCommand(DocxEditController* ctl, int first,
                             QList<Docx::Block> before, QList<Docx::Block> after,
                             const DocxCursor& curBefore, const DocxCursor& curAfter,
                             int mergeKind = -1);

    // Struktur-Änderungen an einer Tabelle mutieren NEBEN den Blöcken auch das Gerüst (`TableDef`). Ohne diesen
    // Schnappschuss liefe Undo auseinander - beim Speichern entstünde eine leere Geisterzeile.
    void snapshotTable(int tableId, const Docx::TableDef& before,
                       const Docx::TableDef& after);

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
    int                 m_tableId = -1;       // −1 = kein Gerüst betroffen
    Docx::TableDef      m_tblBefore, m_tblAfter;
};

// Eigener Kommandotyp statt Block-Schnappschuss: die Ränder ändern keinen Block, sondern die Seiteneinrichtung.
// Gesichert werden die Werte UND das umgeschriebene `w:sectPr` - das eine treibt die Anzeige, das andere das Speichern.
class DocxSectionCommand : public QUndoCommand {
public:
    DocxSectionCommand(DocxEditController* ctl,
                       Docx::Document::SectionState before,
                       Docx::Document::SectionState after);

    void undo() override;
    void redo() override;
    //  Aufeinanderfolgende Züge AM SELBEN Rand verschmelzen zu EINEM Schritt -
    //  sonst hinterließe ein einziges Ziehen des Lineals Dutzende von
    //  Rückgängig-Schritten (das Lineal meldet je Mausbewegung).
    int  id() const override { return 0xD05; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    DocxEditController*          m_ctl;
    Docx::Document::SectionState m_before;
    Docx::Document::SectionState m_after;
    bool                         m_firstRedo = true;
};
