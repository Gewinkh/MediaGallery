#include "docx/edit/DocxEditController.h"
#include "docx/DocxZip.h"
#include "docx/DocxPdfExporter.h"
#include "core/AppSettings.h"
#include "core/PathUtils.h"

#include <QThreadPool>
#include <QRunnable>
#include <QSaveFile>
#include <QFileInfo>
#include <QFile>
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QDataStream>
#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPdfDocument>
#include <QDir>
#include <QMetaObject>
#include <QPointer>

using namespace Docx;

// Kanonische Kind-Reihenfolgen (OOXML-Schema) für upsertProp — nur die im
// Editor relevanten Namen müssen enthalten sein; unbekannte Kinder bleiben
// ohnehin unangetastet an ihrem Platz.
static const QStringList kRPrOrder = {
    QStringLiteral("w:rStyle"), QStringLiteral("w:rFonts"), QStringLiteral("w:b"),
    QStringLiteral("w:bCs"), QStringLiteral("w:i"), QStringLiteral("w:iCs"),
    QStringLiteral("w:caps"), QStringLiteral("w:smallCaps"), QStringLiteral("w:strike"),
    QStringLiteral("w:color"), QStringLiteral("w:spacing"), QStringLiteral("w:kern"),
    QStringLiteral("w:position"), QStringLiteral("w:sz"), QStringLiteral("w:szCs"),
    QStringLiteral("w:highlight"), QStringLiteral("w:u"), QStringLiteral("w:vertAlign"),
    QStringLiteral("w:rtl"), QStringLiteral("w:lang")
};
static const QStringList kPPrOrder = {
    QStringLiteral("w:pStyle"), QStringLiteral("w:keepNext"), QStringLiteral("w:keepLines"),
    QStringLiteral("w:pageBreakBefore"), QStringLiteral("w:widowControl"),
    QStringLiteral("w:numPr"), QStringLiteral("w:pBdr"), QStringLiteral("w:shd"),
    QStringLiteral("w:tabs"), QStringLiteral("w:bidi"), QStringLiteral("w:spacing"),
    QStringLiteral("w:ind"), QStringLiteral("w:contextualSpacing"),
    QStringLiteral("w:jc"), QStringLiteral("w:outlineLvl"), QStringLiteral("w:rPr")
};

DocxEditController::DocxEditController(QObject* parent) : QObject(parent) {
    m_stack.setUndoLimit(200);
    connect(&m_stack, &QUndoStack::canUndoChanged, this, &DocxEditController::undoChanged);
    connect(&m_stack, &QUndoStack::canRedoChanged, this, &DocxEditController::undoChanged);
}

DocxEditController::~DocxEditController() = default;

void DocxEditController::setTranslit(QObject* t) {
    if (m_translit == t) return;
    m_translit = t;
    emit translitChanged();
}

void DocxEditController::bumpFormat() {
    ++m_formatRev;
    emit formatRevChanged();
}

void DocxEditController::setModified(bool m) {
    if (m_modified == m) return;
    m_modified = m;
    emit modifiedChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Laden — asynchron (QRunnable + Generationszähler; Muster PdfTextController)
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::setSource(const QString& s) {
    const QString path = mg::toLocalPath(s);
    if (m_source == path)
        return;
    m_source = path;
    emit sourceChanged();

    m_ready = false;
    m_loadError.clear();
    m_stack.clear();
    m_bakDone  = false;
    m_modified = false;
    m_cursor   = DocxCursor();
    //  Regionen zurücksetzen — die neue Datei hat eigene Kopf-/Fußzeilenteile.
    m_region = Body;
    for (RegionSlot& slot : m_slots) slot = RegionSlot();
    emit activeRegionChanged();
    emit regionsAvailable();
    emit readyChanged();
    emit modifiedChanged();
    if (path.isEmpty())
        return;

    const int gen = ++m_loadGen;
    QPointer<DocxEditController> self(this);

    class LoadTask : public QRunnable {
    public:
        LoadTask(QPointer<DocxEditController> c, QString p, int g)
            : m_c(c), m_path(std::move(p)), m_gen(g) { setAutoDelete(true); }
        void run() override {
            auto* doc = new Document();
            QString err;
            const bool ok = doc->load(m_path, &err);
            auto c = m_c;
            const int gen = m_gen;
            //  Übergabe an den GUI-Thread (QueuedConnection-Äquivalent).
            QMetaObject::invokeMethod(qApp, [c, gen, doc, ok, err]() {
                if (!c || gen != c->m_loadGen) { delete doc; return; }
                if (ok) {
                    c->m_doc = std::move(*doc);
                    c->m_ready = true;
                    //  Kopf-/Fußzeilen-Teile des Hauptabschnitts merken; geladen
                    //  werden sie erst beim Umschalten (lazy, RAM).
                    c->m_slots[Body].partPath  = c->m_doc.partPath();
                    c->m_slots[Body].available = true;
                    c->m_slots[Header].partPath = c->m_doc.headerFooterPart(false, false);
                    c->m_slots[Footer].partPath = c->m_doc.headerFooterPart(true, false);
                    c->m_slots[Header].available = !c->m_slots[Header].partPath.isEmpty();
                    c->m_slots[Footer].available = !c->m_slots[Footer].partPath.isEmpty();
                    emit c->regionsAvailable();
                } else {
                    c->m_loadError = err;
                }
                delete doc;
                emit c->blocksReplaced(0, 0, c->m_doc.blocks.size());
                emit c->readyChanged();
                emit c->cursorChanged();
                c->bumpFormat();
            }, Qt::QueuedConnection);
        }
    private:
        QPointer<DocxEditController> m_c;
        QString m_path;
        int     m_gen;
    };
    QThreadPool::globalInstance()->start(new LoadTask(self, path, gen));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Positions-Helfer
// ─────────────────────────────────────────────────────────────────────────────
QString DocxEditController::blockText(int i) const {
    if (i < 0 || i >= m_doc.blocks.size()) return {};
    return m_doc.blocks.at(i).plainText();
}
int DocxEditController::blockLen(int i) const {
    if (i < 0 || i >= m_doc.blocks.size()) return 0;
    return m_doc.blocks.at(i).textLength();
}
bool DocxEditController::isEditableParagraph(int i) const {
    if (i < 0 || i >= m_doc.blocks.size())
        return false;
    const Block& b = m_doc.blocks.at(i);
    if (b.kind != Block::Paragraph)
        return false;
    return true;
}

//  Gehören zwei Blöcke in DIESELBE Zelle (bzw. beide in den Rumpf)? Das ist die
//  Grenze, über die keine STRUKTUR-Änderung laufen darf: ein Absatz-Merge über
//  eine Zellgrenze würde eine Zelle auflösen und damit die Tabelle zerstören —
//  die Datei bliebe zwar wohlgeformt, aber der Inhalt wäre verschoben.
bool DocxEditController::sameCell(int i, int j) const {
    if (i == j) return true;
    if (i < 0 || j < 0 || i >= m_doc.blocks.size() || j >= m_doc.blocks.size())
        return false;
    const Block& a = m_doc.blocks.at(i);
    const Block& b = m_doc.blocks.at(j);
    if (a.tableId != b.tableId) return false;
    if (a.tableId < 0) return true;                  // beide im Rumpf
    return a.row == b.row && a.col == b.col;
}

//  Mehrblock-Operationen auf die Zelle des ANKERS begrenzen. Eine Selektion darf
//  quer über Zellen laufen (Kopieren ist harmlos); LÖSCHEN darf es nicht, sonst
//  verschwänden Zellgrenzen. Statt die Aktion zu verweigern, wird der Bereich
//  gekürzt — so bleibt Strg+A + Entf im Rumpf benutzbar.
void DocxEditController::clampRangeToCell(int b1, int& b2) const {
    while (b2 > b1 && !sameCell(b1, b2)) --b2;
}

void DocxEditController::orderedSelection(int& b1, int& p1, int& b2, int& p2) const {
    b1 = m_cursor.aBlock; p1 = m_cursor.aPos;
    b2 = m_cursor.block;  p2 = m_cursor.pos;
    if (b1 > b2 || (b1 == b2 && p1 > p2)) {
        std::swap(b1, b2);
        std::swap(p1, p2);
    }
}

void DocxEditController::runAt(const Block& b, int pos, int* runIdx, int* runOfs) const {
    int acc = 0;
    for (int i = 0; i < b.runs.size(); ++i) {
        const int len = b.runs.at(i).text.size();
        if (pos < acc + len || i == b.runs.size() - 1) {
            *runIdx = i;
            *runOfs = qBound(0, pos - acc, len);
            return;
        }
        acc += len;
    }
    *runIdx = -1;
    *runOfs = 0;
}

int DocxEditController::ensureRunBoundary(Block& b, int pos) const {
    int acc = 0;
    for (int i = 0; i < b.runs.size(); ++i) {
        Run& r = b.runs[i];
        const int len = r.text.size();
        if (pos == acc)
            return i;                                   // Grenze existiert
        if (pos < acc + len) {
            //  Run teilen: beide Hälften erben rPr (Original-Span bzw. schon
            //  materialisiertes Fragment) und werden dirty (Neuaufbau nötig).
            Run right = r;
            right.text = r.text.mid(pos - acc);
            right.dirty = true;
            r.text.truncate(pos - acc);
            r.dirty = true;
            //  rPr der rechten Hälfte materialisieren (Span zeigt weiterhin
            //  auf dasselbe Original-Fragment — verbatim geteilt ist ok).
            b.runs.insert(i + 1, right);
            return i + 1;
        }
        acc += len;
    }
    return b.runs.size();
}

void DocxEditController::removeRangeInBlock(Block& b, int p1, int p2) const {
    if (p1 >= p2) return;
    const int i2 = ensureRunBoundary(b, p2);
    const int i1 = ensureRunBoundary(b, p1);
    //  Runs [i1, i2') entfernen — i2 nach dem zweiten Split neu bestimmen.
    int acc = 0, end = b.runs.size();
    for (int i = 0; i < b.runs.size(); ++i) {
        if (acc == p2 && i >= i1) { end = i; break; }
        acc += b.runs.at(i).text.size();
    }
    if (acc == p2) end = qMin(end, b.runs.size());
    Q_UNUSED(i2)
    for (int i = end - 1; i >= i1; --i)
        b.runs.removeAt(i);
    b.dirty = true;
}

void DocxEditController::applyPendingTo(Run& r) const {
    QString rpr = r.currentRpr(m_doc.docXml());
    auto upsert = [&](const QString& name, const QString& xml) {
        rpr = Document::upsertProp(rpr, QStringLiteral("w:rPr"), name, xml, kRPrOrder);
    };
    if (m_pending.set & RunFmt::FBold) {
        r.fmt.bold = m_pending.bold; r.fmt.set |= RunFmt::FBold;
        upsert(QStringLiteral("w:b"),
               m_pending.bold ? QStringLiteral("<w:b/>") : QStringLiteral("<w:b w:val=\"0\"/>"));
    }
    if (m_pending.set & RunFmt::FItalic) {
        r.fmt.italic = m_pending.italic; r.fmt.set |= RunFmt::FItalic;
        upsert(QStringLiteral("w:i"),
               m_pending.italic ? QStringLiteral("<w:i/>") : QStringLiteral("<w:i w:val=\"0\"/>"));
    }
    if (m_pending.set & RunFmt::FUnderline) {
        r.fmt.underline = m_pending.underline; r.fmt.set |= RunFmt::FUnderline;
        upsert(QStringLiteral("w:u"), m_pending.underline
                   ? QStringLiteral("<w:u w:val=\"single\"/>")
                   : QStringLiteral("<w:u w:val=\"none\"/>"));
    }
    if (m_pending.set & RunFmt::FFont) {
        r.fmt.font = m_pending.font; r.fmt.set |= RunFmt::FFont;
        upsert(QStringLiteral("w:rFonts"),
               QStringLiteral("<w:rFonts w:ascii=\"%1\" w:hAnsi=\"%1\" w:cs=\"%1\"/>")
                   .arg(Document::xmlEscape(m_pending.font)));
    }
    if (m_pending.set & RunFmt::FSize) {
        r.fmt.sizePt = m_pending.sizePt; r.fmt.set |= RunFmt::FSize;
        const int hp = qRound(m_pending.sizePt * 2.0);
        upsert(QStringLiteral("w:sz"),   QStringLiteral("<w:sz w:val=\"%1\"/>").arg(hp));
        upsert(QStringLiteral("w:szCs"), QStringLiteral("<w:szCs w:val=\"%1\"/>").arg(hp));
    }
    if (m_pending.set & RunFmt::FColor) {
        r.fmt.color = m_pending.color; r.fmt.set |= RunFmt::FColor;
        upsert(QStringLiteral("w:color"),
               QStringLiteral("<w:color w:val=\"%1\"/>")
                   .arg(m_pending.color.name(QColor::HexRgb).mid(1).toUpper()));
    }
    r.rprXml = rpr;
    r.rprMaterialized = true;
    r.dirty = true;
}

void DocxEditController::clearPending() {
    m_pending      = RunFmt();
    m_pendingBlock = -1;
    m_pendingPos   = -1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Word-Verhalten: Wird das LETZTE Zeichen einer Zeile gelöscht, behält die
//  Zeile ihr eigenes Zeichenformat, statt auf ihr Absatz-/Stil-Format
//  zurückzufallen (Nutzerbefund: eine per Enter aus einer 20-pt-Zeile
//  entstandene Zeile sprang nach dem Leerlöschen wieder auf 20 pt).
//
//  WO das Format lebt — der TRÄGER-RUN:
//  Ein Enter am Zeilenende legt im neuen Absatz einen LEEREN Run an, der das
//  rPr der alten Zeile erbt. Genau dieser Run ist das Gegenstück zur
//  Absatzmarke in Word: Er trägt kein Zeichen, bestimmt aber, wie der Absatz
//  aussieht und womit das nächste Tippen fortsetzt. Das ist gewollt — ohne ihn
//  würde ein Enter die Formatierung der Vorzeile NICHT fortführen.
//  Der Fehler war, dass dieser Träger nach dem Leerlöschen weiter das ALTE
//  Format (20 pt) trug: Solange das Pending-Format lebte, überdeckte es das
//  zwar — verließ der Cursor die Zeile aber einmal, kam wieder 20 pt zurück.
//  Deshalb wird der Träger jetzt UMGESCHRIEBEN statt nur überdeckt; das Format
//  überlebt damit Cursor-Wechsel, Speichern und Neuladen.
//
//  Gesetzt werden NUR die Felder, die vom Stil-Format des leeren Absatzes
//  abweichen: minimales rPr (Verlusterhaltungs-/RAM-Prinzip), gleiches Bild.
//  Gibt es keinen Träger (der Absatz hatte nie einen), wird einer angelegt —
//  ein leerer `<w:r>` mit rPr ist gültiges OOXML und genau das, was Word für
//  eine formatierte, leere Zeile schreibt.
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::keepFormatOnEmptiedBlock(int bi, const RunFmt& had) {
    if (!isEditableParagraph(bi))
        return;
    Block& blk = m_doc.blocks[bi];
    if (blk.textLength() != 0)
        return;                                   // Zeile ist nicht leer geworden
    const RunFmt base = m_doc.resolveRun(blk, Run());
    RunFmt p;
    if (had.bold      != base.bold)      { p.bold      = had.bold;      p.set |= RunFmt::FBold; }
    if (had.italic    != base.italic)    { p.italic    = had.italic;    p.set |= RunFmt::FItalic; }
    if (had.underline != base.underline) { p.underline = had.underline; p.set |= RunFmt::FUnderline; }
    if (!qFuzzyCompare(had.sizePt + 1, base.sizePt + 1)) { p.sizePt = had.sizePt; p.set |= RunFmt::FSize; }
    if (had.font != base.font && !had.font.isEmpty())    { p.font   = had.font;   p.set |= RunFmt::FFont; }
    if (had.color.isValid() && had.color != base.color)  { p.color  = had.color;  p.set |= RunFmt::FColor; }
    clearPending();

    //  Träger suchen (erster nicht-opaker Run des leeren Absatzes).
    int carrier = -1;
    for (int i = 0; i < blk.runs.size(); ++i) {
        if (!blk.runs.at(i).opaque) { carrier = i; break; }
    }

    if (p.set == 0) {
        //  Nichts weicht vom Stil ab: einen vorhandenen Träger von seinem
        //  ALTEN (geerbten) rPr befreien, damit er nicht weiter ein fremdes
        //  Format festhält. Ohne Träger ist ohnehin nichts zu tun.
        if (carrier >= 0) {
            Run& r = blk.runs[carrier];
            r.rprXml.clear();
            r.rprMaterialized = true;
            r.fmt = RunFmt();
            r.dirty = true;
            blk.dirty = true;
        }
        return;
    }

    if (carrier < 0) {
        //  Kein Träger vorhanden → einen leeren anlegen (Word-Äquivalent der
        //  formatierten Absatzmarke).
        Run r;
        r.rprMaterialized = true;                 // startet ohne rPr
        r.dirty = true;
        blk.runs.append(r);
        carrier = blk.runs.size() - 1;
    } else {
        //  Vorhandenen Träger auf ein FRISCHES rPr setzen: sonst bliebe das
        //  geerbte Format (20 pt) unter den neu gesetzten Feldern erhalten.
        Run& r = blk.runs[carrier];
        r.rprXml.clear();
        r.rprMaterialized = true;
        r.fmt = RunFmt();
    }

    //  Felder über denselben Weg schreiben wie das Pending-Format beim Tippen
    //  (applyPendingTo pflegt rPr-XML UND das geparste RunFmt konsistent).
    const RunFmt save = m_pending;
    m_pending = p;
    applyPendingTo(blk.runs[carrier]);
    m_pending = save;
    blk.dirty = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Cursor & Selektion
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::setCursor(int block, int pos, bool keepAnchor) {
    if (m_doc.blocks.isEmpty()) return;
    block = qBound(0, block, int(m_doc.blocks.size()) - 1);
    pos   = qBound(0, pos, blockLen(block));
    if (m_cursor.block == block && m_cursor.pos == pos
        && (keepAnchor || !m_cursor.hasSelection()))
        return;
    m_cursor.block = block;
    m_cursor.pos   = pos;
    if (!keepAnchor)
        m_cursor.collapse();
    //  Pending-Format erlischt, sobald der Cursor die Stelle verlässt.
    if (m_pendingBlock != block || m_pendingPos != pos) {
        m_pending = RunFmt();
        m_pendingBlock = -1;
    }
    emit cursorChanged();
    bumpFormat();
}

void DocxEditController::selectAll() {
    if (m_doc.blocks.isEmpty()) return;
    m_cursor.aBlock = 0;
    m_cursor.aPos   = 0;
    m_cursor.block  = m_doc.blocks.size() - 1;
    m_cursor.pos    = blockLen(m_cursor.block);
    emit cursorChanged();
    bumpFormat();
}

void DocxEditController::selectWordAt(int block, int pos) {
    if (!isEditableParagraph(block)) return;
    const QString t = blockText(block);
    pos = qBound(0, pos, t.size());
    int a = pos, b = pos;
    auto wordChar = [](QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); };
    while (a > 0 && wordChar(t.at(a - 1))) --a;
    while (b < t.size() && wordChar(t.at(b))) ++b;
    if (a == b && pos < t.size()) b = pos + 1;           // Einzelzeichen
    m_cursor = { block, b, block, a };
    emit cursorChanged();
    bumpFormat();
}

void DocxEditController::clearSelection() {
    if (!m_cursor.hasSelection()) return;
    m_cursor.collapse();
    emit cursorChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Regionen: Körper / Kopfzeile / Fußzeile
//
//  Die aktive Region liegt in m_doc/m_cursor — der gesamte übrige Editor-Code
//  bleibt dadurch unverändert. Umschalten heißt: den aktuellen Zustand in
//  seinen Slot PARKEN und den anderen holen (move, O(1) je Member).
// ─────────────────────────────────────────────────────────────────────────────
bool DocxEditController::ensureRegionLoaded(Region r) {
    RegionSlot& s = m_slots[r];
    if (s.loaded) return true;
    if (!s.available || s.partPath.isEmpty()) return false;

    Docx::Document d;
    QString err;
    if (!d.loadPart(m_source, s.partPath, &err)) {
        //  Nicht editierbar (Selbstprüfung/Encoding) → Region bleibt gesperrt.
        s.available = false;
        emit regionsAvailable();
        return false;
    }
    s.doc    = std::move(d);
    s.cursor = DocxCursor();
    s.loaded = true;
    return true;
}

bool DocxEditController::setRegion(int rInt) {
    if (!m_ready) return false;
    const Region r = Region(qBound(0, rInt, 2));
    if (r == m_region) return true;
    if (r != Body && !ensureRegionLoaded(r)) return false;

    //  Aktiven Zustand parken …
    m_slots[m_region].doc    = std::move(m_doc);
    m_slots[m_region].cursor = m_cursor;
    m_slots[m_region].loaded = true;
    //  … und den neuen aktivieren.
    m_doc    = std::move(m_slots[r].doc);
    m_cursor = m_slots[r].cursor;
    m_region = r;
    clearPending();

    //  Anderes Dokument ⇒ die Fläche legt auf activeRegionChanged ALLES neu aus.
    emit activeRegionChanged();
    emit cursorChanged();
    bumpFormat();
    return true;
}

void DocxEditController::setActiveRegionInt(int r) { setRegion(r); }

void DocxEditController::activateRegionForCommand(int r) {
    if (int(m_region) != r) setRegion(r);
}

//  Alle geladenen Regionen fürs Speichern zusammentragen: je Region ihr
//  Teil-XML plus ihre Ersatzteile (Medien/Beziehungen liegen je Teil in einer
//  EIGENEN .rels-Datei, kollidieren also nicht). Der Körper wird zuletzt
//  eingetragen und gewinnt bei gemeinsamen Dateien ([Content_Types].xml).
QHash<QString, QByteArray> DocxEditController::allRegionParts() const {
    QHash<QString, QByteArray> parts;
    for (int r = 2; r >= 0; --r) {
        const bool active = (int(m_region) == r);
        if (!active && !m_slots[r].loaded) continue;
        const Docx::Document& d = active ? m_doc : m_slots[r].doc;
        if (d.partPath().isEmpty()) continue;
        const QHash<QString, QByteArray> own = d.replacementParts();
        for (auto it = own.constBegin(); it != own.constEnd(); ++it)
            parts.insert(it.key(), it.value());
        parts.insert(d.partPath(), d.newDocumentXml().toUtf8());
    }
    return parts;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Kommando-Anwendung (Undo/Redo-Pfad)
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::applyBlocks(int first, int oldCount,
                                     const QList<Block>& blocks,
                                     const DocxCursor& cur) {
    for (int i = 0; i < oldCount; ++i)
        m_doc.blocks.removeAt(first);
    for (int i = 0; i < blocks.size(); ++i)
        m_doc.blocks.insert(first + i, blocks.at(i));
    m_cursor = cur;
    setModified(true);
    emit blocksReplaced(first, oldCount, blocks.size());
    emit cursorChanged();
    bumpFormat();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Edit-Scope: Bereich kopieren → mutieren → Kommando mit Vorher/Nachher.
// ─────────────────────────────────────────────────────────────────────────────
struct DocxEditController::EditScope {
    DocxEditController* c;
    int first;
    int oldCount;
    QList<Block> before;
    DocxCursor   curBefore;
    int          mergeKind;
    //  Struktur-Änderungen an einer Tabelle betreffen NEBEN den Blöcken auch
    //  das Gerüst — beides muss zusammen zurückgenommen werden.
    int          tableId = -1;
    Docx::TableDef tblBefore;

    EditScope(DocxEditController* ctl, int firstIdx, int count, int merge = -1)
        : c(ctl), first(firstIdx), oldCount(count), mergeKind(merge) {
        curBefore = c->m_cursor;
        before.reserve(count);
        for (int i = 0; i < count; ++i)
            before.append(c->m_doc.blocks.at(first + i));
    }
    //  VOR der Mutation aufrufen.
    void watchTable(int tid) {
        tableId = tid;
        tblBefore = c->m_doc.tableDef(tid);
    }
    //  newCount = Blockzahl des Bereichs NACH der Mutation; Cursor ist vom
    //  Aufrufer bereits gesetzt.
    void commit(int newCount) {
        QList<Block> after;
        after.reserve(newCount);
        for (int i = 0; i < newCount; ++i)
            after.append(c->m_doc.blocks.at(first + i));
        c->setModified(true);
        emit c->blocksReplaced(first, oldCount, newCount);
        emit c->cursorChanged();
        c->bumpFormat();
        auto* cmd = new DocxReplaceBlocksCommand(
            c, first, std::move(before), std::move(after),
            curBefore, c->m_cursor, mergeKind);
        cmd->setRegion(int(c->m_region));
        if (tableId >= 0)
            cmd->snapshotTable(tableId, tblBefore, c->m_doc.tableDef(tableId));
        c->m_stack.push(cmd);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Text-Operationen
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::insertText(const QString& raw) {
    if (!m_ready || raw.isEmpty()) return;
    QString text = raw;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    if (!isEditableParagraph(b1) || !isEditableParagraph(b2))
        return;
    //  Selektion über Zellgrenzen: nur den Teil in der Zelle des Ankers ersetzen.
    if (b2 != b1) {
        const int wanted = b2;
        clampRangeToCell(b1, b2);
        if (b2 != wanted) p2 = blockLen(b2);
    }

    const bool oneChar = !m_cursor.hasSelection() && text.size() == 1
                         && !text.contains(QLatin1Char('\n'));
    EditScope scope(this, b1, b2 - b1 + 1, oneChar ? 0 : -1);

    //  1) Selektion entfernen: Bereich zu EINEM Block verschmelzen.
    Block& head = m_doc.blocks[b1];
    if (m_cursor.hasSelection()) {
        if (b1 == b2) {
            removeRangeInBlock(head, p1, p2);
        } else {
            removeRangeInBlock(head, p1, blockLen(b1));
            Block& tail = m_doc.blocks[b2];
            removeRangeInBlock(tail, 0, p2);
            const int k = ensureRunBoundary(head, p1);
            Q_UNUSED(k)
            head.runs += tail.runs;
            head.dirty = true;
            for (int i = b2; i > b1; --i)
                m_doc.blocks.removeAt(i);
        }
    }

    //  2) Text einfügen (\n teilt Absätze).
    const QStringList parts = text.split(QLatin1Char('\n'));
    const bool pendingHere = (m_pendingBlock == b1 && m_pendingPos == p1
                              && m_pending.set != 0);
    int curBlock = b1, curPos = p1;
    {
        Block& blk = m_doc.blocks[curBlock];
        const int at = ensureRunBoundary(blk, curPos);
        Run nr;
        //  Format erben: linker Nachbar-Run (nicht-opak), sonst rechter.
        int inherit = -1;
        for (int i = at - 1; i >= 0; --i)
            if (!blk.runs.at(i).opaque) { inherit = i; break; }
        if (inherit < 0)
            for (int i = at; i < blk.runs.size(); ++i)
                if (!blk.runs.at(i).opaque) { inherit = i; break; }
        if (inherit >= 0) {
            const Run& src = blk.runs.at(inherit);
            nr.rprSpan = src.rprSpan;
            nr.rprXml = src.rprXml;
            nr.rprMaterialized = src.rprMaterialized;
            nr.fmt = src.fmt;
        }
        nr.text  = parts.at(0);
        nr.dirty = true;
        if (pendingHere)
            applyPendingTo(nr);
        blk.runs.insert(at, nr);
        blk.dirty = true;
        curPos += parts.at(0).size();
    }
    for (int pi = 1; pi < parts.size(); ++pi) {
        //  Absatz-Split an curPos: Rest wandert in einen NEUEN Absatz.
        Block& blk = m_doc.blocks[curBlock];
        const int k = ensureRunBoundary(blk, curPos);
        Block nb;
        nb.kind = Block::Paragraph;
        //  Ein neuer Absatz bleibt in DERSELBEN Zelle — sonst stünde er nach dem
        //  Speichern außerhalb der Tabelle (die Gruppen-Emission ordnet Blöcke
        //  über tableId/row/col ihren Zellen zu).
        nb.tableId = blk.tableId;
        nb.row = blk.row;
        nb.col = blk.col;
        const QString ppr = blk.currentPpr(m_doc.docXml());
        if (!ppr.isEmpty()) { nb.pprXml = ppr; nb.pprMaterialized = true; }
        nb.pfmt = blk.pfmt;
        while (blk.runs.size() > k)
            nb.runs.append(blk.runs.takeAt(k));
        blk.dirty = true;
        nb.dirty  = true;
        if (!parts.at(pi).isEmpty()) {
            Run nr;
            if (!blk.runs.isEmpty()) {
                const Run& src = blk.runs.constLast();
                nr.rprSpan = src.rprSpan; nr.rprXml = src.rprXml;
                nr.rprMaterialized = src.rprMaterialized; nr.fmt = src.fmt;
            }
            nr.text = parts.at(pi);
            nr.dirty = true;
            if (pendingHere) applyPendingTo(nr);
            nb.runs.prepend(nr);
        }
        ++curBlock;
        m_doc.blocks.insert(curBlock, nb);
        curPos = parts.at(pi).size();
    }

    m_cursor = { curBlock, curPos, curBlock, curPos };
    if (pendingHere) { m_pendingBlock = curBlock; m_pendingPos = curPos; }
    scope.commit(curBlock - b1 + 1);
    if (text.size() == 1)
        runTranslit();
}

void DocxEditController::insertParagraphBreak() {
    //  Word-Verhalten: Enter in einem LEEREN Listenabsatz beendet die Liste,
    //  statt einen weiteren leeren Punkt anzulegen — der Cursor bleibt in
    //  DERSELBEN Zeile, nur ohne Aufzählung/Nummerierung. (Der erste Enter
    //  nach einem befüllten Punkt führt die Liste normal fort: der neue
    //  Absatz erbt das pPr inkl. w:numPr.)
    if (m_ready && !m_cursor.hasSelection() && isEditableParagraph(m_cursor.block)) {
        const Block& b = m_doc.blocks.at(m_cursor.block);
        if (b.textLength() == 0 && m_doc.resolvePar(b).numId > 0) {
            applyParProp(QStringLiteral("w:numPr"), QString(),
                         [](ParFmt& p) { p.numId = -1; p.ilvl = 0;
                                         p.set &= ~ParFmt::FNum; });
            return;
        }
    }
    insertText(QStringLiteral("\n"));
}
void DocxEditController::insertLineBreak()      { insertText(QString(kLineBreak)); }

void DocxEditController::deleteBackward() {
    if (!m_ready) return;
    if (m_cursor.hasSelection()) {
        //  Selektion löschen = leeren Text „einfügen" über dieselbe Mechanik.
        int b1, p1, b2, p2;
        orderedSelection(b1, p1, b2, p2);
        if (b2 != b1) {
            const int wanted = b2;
            clampRangeToCell(b1, b2);
            if (b2 != wanted) p2 = blockLen(b2);
        }
        //  Format der gelöschten Stelle merken (s. keepFormatOnEmptiedBlock).
        const RunFmt had = resolvedFormatAt(b1, p1);
        EditScope scope(this, b1, b2 - b1 + 1);
        Block& head = m_doc.blocks[b1];
        if (b1 == b2) {
            removeRangeInBlock(head, p1, p2);
        } else {
            removeRangeInBlock(head, p1, blockLen(b1));
            Block& tail = m_doc.blocks[b2];
            removeRangeInBlock(tail, 0, p2);
            head.runs += tail.runs;
            head.dirty = true;
            for (int i = b2; i > b1; --i)
                m_doc.blocks.removeAt(i);
        }
        m_cursor = { b1, p1, b1, p1 };
        clearPending();
        keepFormatOnEmptiedBlock(b1, had);
        scope.commit(1);
        return;
    }
    const int bi = m_cursor.block;
    if (!isEditableParagraph(bi)) return;
    if (m_cursor.pos > 0) {
        //  Ein Zeichen rückwärts (Surrogat-Paare als Einheit; endet das
        //  Zeichen einen opaken atomaren Run, wird der GANZE Run entfernt).
        const QString t = blockText(bi);
        int p1 = m_cursor.pos - 1;
        if (p1 > 0 && t.at(p1).isLowSurrogate() && t.at(p1 - 1).isHighSurrogate())
            --p1;
        int ri, ro;
        runAt(m_doc.blocks[bi], p1, &ri, &ro);
        //  Format des gelöschten Zeichens merken (s. keepFormatOnEmptiedBlock).
        const RunFmt had = resolvedFormatAt(bi, p1);
        EditScope scope(this, bi, 1, 1);
        Block& blk = m_doc.blocks[bi];
        if (ri >= 0 && blk.runs.at(ri).opaque && !blk.runs.at(ri).text.isEmpty()) {
            //  Atomar: kompletter opaker Run fällt (Hyperlink/Zeichnung).
            int acc = 0;
            for (int i = 0; i < ri; ++i) acc += blk.runs.at(i).text.size();
            p1 = acc;
            const int p2 = acc + blk.runs.at(ri).text.size();
            removeRangeInBlock(blk, p1, p2);
        } else {
            removeRangeInBlock(blk, p1, m_cursor.pos);
        }
        m_cursor = { bi, p1, bi, p1 };
        clearPending();
        keepFormatOnEmptiedBlock(bi, had);
        scope.commit(1);
        return;
    }
    //  Am Absatzanfang: mit dem VORHERIGEN Absatz verschmelzen.
    int prev = bi - 1;
    if (prev < 0) return;                           // nichts zu tun → Pending hält
    //  ZELLGRENZE: der vorherige Absatz steht in einer anderen Zelle (oder im
    //  Rumpf) → NICHT verschmelzen. Sonst fiele eine Zellgrenze weg und die
    //  Tabelle verlöre eine Zelle. Der Cursor bleibt einfach stehen.
    if (!sameCell(bi, prev))
        return;
    if (!isEditableParagraph(prev)) {
        //  Opaker Block davor (Tabelle/sectPr): nichts löschen, nur Cursor
        //  vor den Block bewegen (Schutz vor versehentlichem Strukturverlust).
        for (int i = prev; i >= 0; --i) {
            if (isEditableParagraph(i)) { setCursor(i, blockLen(i), false); return; }
        }
        return;
    }
    //  Der Cursor wechselt jetzt die Zeile → das für die geleerte Zeile
    //  gemerkte Format erlischt (genau das vom Nutzer gewünschte Verhalten:
    //  „erst beim nächsten Backspace gilt die Überschriftformatierung").
    clearPending();
    EditScope scope(this, prev, 2);
    Block& a = m_doc.blocks[prev];
    Block& b = m_doc.blocks[bi];
    const int joinPos = a.textLength();
    a.runs += b.runs;
    a.dirty = true;
    m_doc.blocks.removeAt(bi);
    m_cursor = { prev, joinPos, prev, joinPos };
    scope.commit(1);
}

void DocxEditController::deleteForward() {
    if (!m_ready) return;
    if (m_cursor.hasSelection()) { deleteBackward(); return; }
    const int bi = m_cursor.block;
    if (!isEditableParagraph(bi)) return;
    const int len = blockLen(bi);
    if (m_cursor.pos < len) {
        const QString t = blockText(bi);
        int p2 = m_cursor.pos + 1;
        if (p2 < len && t.at(m_cursor.pos).isHighSurrogate() && t.at(p2).isLowSurrogate())
            ++p2;
        int ri, ro;
        runAt(m_doc.blocks[bi], m_cursor.pos, &ri, &ro);
        //  Format des gelöschten Zeichens merken (s. keepFormatOnEmptiedBlock);
        //  bei Entf steht es RECHTS vom Cursor.
        const RunFmt had = resolvedFormatAt(bi, m_cursor.pos);
        EditScope scope(this, bi, 1);
        Block& blk = m_doc.blocks[bi];
        if (ri >= 0 && blk.runs.at(ri).opaque && !blk.runs.at(ri).text.isEmpty()) {
            int acc = 0;
            for (int i = 0; i < ri; ++i) acc += blk.runs.at(i).text.size();
            removeRangeInBlock(blk, acc, acc + blk.runs.at(ri).text.size());
            m_cursor = { bi, acc, bi, acc };
        } else {
            removeRangeInBlock(blk, m_cursor.pos, p2);
        }
        clearPending();
        keepFormatOnEmptiedBlock(bi, had);
        scope.commit(1);
        return;
    }
    //  Am Absatzende: mit dem NÄCHSTEN Absatz verschmelzen.
    const int nxt = bi + 1;
    //  ZELLGRENZE (s. deleteBackward): kein Verschmelzen aus einer anderen Zelle.
    if (nxt < m_doc.blocks.size() && !sameCell(bi, nxt))
        return;
    if (nxt >= m_doc.blocks.size() || !isEditableParagraph(nxt))
        return;
    EditScope scope(this, bi, 2);
    Block& a = m_doc.blocks[bi];
    Block& b = m_doc.blocks[nxt];
    a.runs += b.runs;
    a.dirty = true;
    m_doc.blocks.removeAt(nxt);
    scope.commit(1);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Zeichenformat
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::applyCharFormat(int field, const QVariant& value) {
    if (!m_ready) return;
    if (!m_cursor.hasSelection()) {
        //  Pending-Format (fürs nächste Tippen an genau dieser Stelle).
        switch (field) {
        case RunFmt::FBold:      m_pending.bold      = value.toBool(); break;
        case RunFmt::FItalic:    m_pending.italic    = value.toBool(); break;
        case RunFmt::FUnderline: m_pending.underline = value.toBool(); break;
        case RunFmt::FFont:      m_pending.font      = value.toString(); break;
        case RunFmt::FSize:      m_pending.sizePt    = value.toReal(); break;
        case RunFmt::FColor:     m_pending.color     = value.value<QColor>(); break;
        }
        m_pending.set |= field;
        m_pendingBlock = m_cursor.block;
        m_pendingPos   = m_cursor.pos;
        bumpFormat();
        return;
    }
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    EditScope scope(this, b1, b2 - b1 + 1);
    const RunFmt save = m_pending;
    m_pending = RunFmt();
    m_pending.set = field;
    switch (field) {
    case RunFmt::FBold:      m_pending.bold      = value.toBool(); break;
    case RunFmt::FItalic:    m_pending.italic    = value.toBool(); break;
    case RunFmt::FUnderline: m_pending.underline = value.toBool(); break;
    case RunFmt::FFont:      m_pending.font      = value.toString(); break;
    case RunFmt::FSize:      m_pending.sizePt    = value.toReal(); break;
    case RunFmt::FColor:     m_pending.color     = value.value<QColor>(); break;
    }
    for (int i = b1; i <= b2; ++i) {
        if (!isEditableParagraph(i)) continue;
        Block& blk = m_doc.blocks[i];
        const int from = (i == b1) ? p1 : 0;
        const int to   = (i == b2) ? p2 : blockLen(i);
        if (from >= to) continue;
        ensureRunBoundary(blk, to);
        const int k1 = ensureRunBoundary(blk, from);
        int acc = 0;
        for (int r = 0; r < k1; ++r) acc += blk.runs.at(r).text.size();
        for (int r = k1; r < blk.runs.size() && acc < to; ++r) {
            Run& rn = blk.runs[r];
            if (!rn.opaque && !rn.text.isEmpty())
                applyPendingTo(rn);                       // nutzt m_pending
            acc += rn.text.size();
        }
        blk.dirty = true;
    }
    m_pending = save;
    scope.commit(b2 - b1 + 1);
}

//  Toggle-Zustand: „alles in der Selektion bereits X?" → aus, sonst an.
static bool allRunsHave(const Document& d, const DocxCursor& cur,
                        const std::function<bool(const RunFmt&)>& pred,
                        int b1, int p1, int b2, int p2) {
    bool any = false;
    for (int i = b1; i <= b2; ++i) {
        if (i < 0 || i >= d.blocks.size()) continue;
        const Block& blk = d.blocks.at(i);
        if (blk.kind != Block::Paragraph) continue;
        const int from = (i == b1) ? p1 : 0;
        const int to   = (i == b2) ? p2 : blk.textLength();
        int acc = 0;
        for (const Run& r : blk.runs) {
            const int rs = acc, re = acc + r.text.size();
            acc = re;
            if (r.opaque || r.text.isEmpty()) continue;
            if (re <= from || rs >= to) continue;
            any = true;
            if (!pred(d.resolveRun(blk, r)))
                return false;
        }
    }
    Q_UNUSED(cur)
    return any;
}

void DocxEditController::toggleBold() {
    int b1, p1, b2, p2; orderedSelection(b1, p1, b2, p2);
    const bool on = m_cursor.hasSelection()
        ? !allRunsHave(m_doc, m_cursor, [](const RunFmt& f){ return f.bold; }, b1, p1, b2, p2)
        : !currentFormat().value(QStringLiteral("bold")).toBool();
    applyCharFormat(RunFmt::FBold, on);
}
void DocxEditController::toggleItalic() {
    int b1, p1, b2, p2; orderedSelection(b1, p1, b2, p2);
    const bool on = m_cursor.hasSelection()
        ? !allRunsHave(m_doc, m_cursor, [](const RunFmt& f){ return f.italic; }, b1, p1, b2, p2)
        : !currentFormat().value(QStringLiteral("italic")).toBool();
    applyCharFormat(RunFmt::FItalic, on);
}
void DocxEditController::toggleUnderline() {
    int b1, p1, b2, p2; orderedSelection(b1, p1, b2, p2);
    const bool on = m_cursor.hasSelection()
        ? !allRunsHave(m_doc, m_cursor, [](const RunFmt& f){ return f.underline; }, b1, p1, b2, p2)
        : !currentFormat().value(QStringLiteral("underline")).toBool();
    applyCharFormat(RunFmt::FUnderline, on);
}
void DocxEditController::setFontFamily(const QString& f) { applyCharFormat(RunFmt::FFont, f); }
void DocxEditController::setFontSizePt(qreal pt) {
    applyCharFormat(RunFmt::FSize, qBound<qreal>(6.0, pt, 96.0));
}
void DocxEditController::setTextColor(const QColor& c) { applyCharFormat(RunFmt::FColor, c); }

// ─────────────────────────────────────────────────────────────────────────────
//  Absatzformat
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::applyParProp(const QString& propName, const QString& newXml,
                                      const std::function<void(ParFmt&)>& mut) {
    if (!m_ready) return;
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    Q_UNUSED(p1) Q_UNUSED(p2)
    EditScope scope(this, b1, b2 - b1 + 1);
    for (int i = b1; i <= b2; ++i) {
        if (!isEditableParagraph(i)) continue;
        Block& blk = m_doc.blocks[i];
        //  Für w:spacing hängt das fertige Element vom (mutierten) ParFmt des
        //  Blocks ab → mut() zuerst, dann Fragment bauen (s. Aufrufer).
        mut(blk.pfmt);
        QString xml = newXml;
        if (propName == QLatin1String("w:spacing")) {
            //  Nur DIREKT gesetzte Werte schreiben (Stil-Werte bleiben Stil).
            QString attrs;
            if (blk.pfmt.set & ParFmt::FBefore)
                attrs += QStringLiteral(" w:before=\"%1\"").arg(qRound(blk.pfmt.beforePt * 20));
            if (blk.pfmt.set & ParFmt::FAfter)
                attrs += QStringLiteral(" w:after=\"%1\"").arg(qRound(blk.pfmt.afterPt * 20));
            if (blk.pfmt.set & ParFmt::FLine)
                attrs += QStringLiteral(" w:line=\"%1\" w:lineRule=\"auto\"")
                             .arg(qRound(blk.pfmt.lineSpacing * 240));
            xml = attrs.isEmpty() ? QString()
                                  : QStringLiteral("<w:spacing%1/>").arg(attrs);
        }
        blk.pprXml = Document::upsertProp(blk.currentPpr(m_doc.docXml()),
                                          QStringLiteral("w:pPr"), propName, xml, kPPrOrder);
        blk.pprMaterialized = true;
    }
    scope.commit(b2 - b1 + 1);
}

void DocxEditController::setAlignment(int align) {
    align = qBound(0, align, 3);
    static const char* vals[] = { "left", "center", "right", "both" };
    applyParProp(QStringLiteral("w:jc"),
                 QStringLiteral("<w:jc w:val=\"%1\"/>").arg(QLatin1String(vals[align])),
                 [align](ParFmt& p) { p.align = align; p.set |= ParFmt::FAlign; });
}
void DocxEditController::setLineSpacing(qreal m) {
    m = qBound<qreal>(0.5, m, 4.0);
    applyParProp(QStringLiteral("w:spacing"), QString(),
                 [m](ParFmt& p) { p.lineSpacing = m; p.set |= ParFmt::FLine; });
}
void DocxEditController::setSpaceBeforePt(qreal pt) {
    pt = qBound<qreal>(0.0, pt, 200.0);
    applyParProp(QStringLiteral("w:spacing"), QString(),
                 [pt](ParFmt& p) { p.beforePt = pt; p.set |= ParFmt::FBefore; });
}
void DocxEditController::setSpaceAfterPt(qreal pt) {
    pt = qBound<qreal>(0.0, pt, 200.0);
    applyParProp(QStringLiteral("w:spacing"), QString(),
                 [pt](ParFmt& p) { p.afterPt = pt; p.set |= ParFmt::FAfter; });
}

QVariantList DocxEditController::paragraphStyles() const {
    QVariantList out;
    if (!m_ready) return out;
    const QList<Docx::StyleInfo>& styles = m_doc.paragraphStyles();
    out.reserve(styles.size());
    for (const Docx::StyleInfo& s : styles) {
        QVariantMap m;
        m.insert(QStringLiteral("id"),        s.id);
        m.insert(QStringLiteral("name"),      s.name);
        m.insert(QStringLiteral("isDefault"), s.isDefault);
        out.append(m);
    }
    return out;
}

void DocxEditController::setParagraphStyle(const QString& styleId) {
    if (!m_ready) return;
    //  Standardvorlage = KEIN w:pStyle: Word schreibt sie nie in den Absatz,
    //  und ein explizites w:pStyle="Standard" würde beim Vorlagenwechsel im
    //  Zieldokument anders auflösen als der Absatz ohne Angabe.
    const bool toDefault = styleId.isEmpty()
                           || styleId == m_doc.defaultParagraphStyleId();
    const QString id = toDefault ? QString() : styleId;
    applyParProp(QStringLiteral("w:pStyle"),
                 id.isEmpty() ? QString()
                              : QStringLiteral("<w:pStyle w:val=\"%1\"/>")
                                    .arg(Document::xmlEscape(id)),
                 [id](ParFmt& p) { p.styleId = id; });
}

void DocxEditController::insertTable(int rows, int cols) {
    if (!m_ready) return;
    rows = qBound(1, rows, 100);
    cols = qBound(1, cols, 32);

    //  Einfügestelle: hinter dem Cursor-Absatz. Steht er in einer Zelle, hinter
    //  der ganzen Tabelle (keine verschachtelten Tabellen).
    int at = qBound(0, m_cursor.block, m_doc.blocks.size() - 1) + 1;
    if (!m_doc.blocks.isEmpty()) {
        const Block& cur = m_doc.blocks.at(qBound(0, m_cursor.block,
                                                  m_doc.blocks.size() - 1));
        if (cur.tableId >= 0) {
            const int last = m_doc.tableLastBlock(cur.tableId);
            if (last >= 0) at = last + 1;
        }
    }
    at = qBound(0, at, m_doc.blocks.size());

    //  Der Undo-Schnappschuss deckt einen LEEREN Bereich ab (reines Einfügen):
    //  vorher 0 Blöcke, nachher rows*cols. Das Kommando kann das bereits.
    //  Die TableDef bleibt beim Rückgängig in m_tables stehen — sie ist ohne
    //  zugehörige Blöcke inert (die Emission läuft über die Blöcke) und wird
    //  beim Wiederherstellen mit derselben tableId wieder benutzt.
    EditScope scope(this, at, 0);
    const int first = m_doc.insertTable(at, rows, cols);
    if (first < 0) return;
    m_cursor = { first, 0, first, 0 };
    clearPending();
    scope.commit(rows * cols);
}

void DocxEditController::insertTableOfContents() {
    if (!m_ready) return;
    //  Wie beim Einfügen einer Tabelle: hinter dem Cursor-Absatz, bei einer
    //  Tabelle hinter der GANZEN Tabelle.
    int at = qBound(0, m_cursor.block, qMax(0, m_doc.blocks.size() - 1)) + 1;
    if (!m_doc.blocks.isEmpty()) {
        const Block& cur = m_doc.blocks.at(qBound(0, m_cursor.block,
                                                  m_doc.blocks.size() - 1));
        if (cur.tableId >= 0) {
            const int last = m_doc.tableLastBlock(cur.tableId);
            if (last >= 0) at = last + 1;
        }
    }
    at = qBound(0, at, m_doc.blocks.size());

    EditScope scope(this, at, 0);
    const int idx = m_doc.insertToc(at);
    if (idx < 0) return;
    m_cursor = { idx, 0, idx, 0 };
    clearPending();
    scope.commit(1);
}

void DocxEditController::insertImage(const QString& fileUrl) {
    if (!m_ready) return;
    const QString path = mg::toLocalPath(fileUrl);
    if (path.isEmpty()) return;

    int at = qBound(0, m_cursor.block, qMax(0, m_doc.blocks.size() - 1)) + 1;
    if (!m_doc.blocks.isEmpty()) {
        const Block& cur = m_doc.blocks.at(qBound(0, m_cursor.block,
                                                  m_doc.blocks.size() - 1));
        //  In einer Zelle: das Bild landet IN der Zelle (dafür muss der neue
        //  Block ihre Koordinaten erben) — sonst hinter dem Absatz.
        Q_UNUSED(cur)
    }
    at = qBound(0, at, m_doc.blocks.size());

    EditScope scope(this, at, 0);
    QString err;
    const int idx = m_doc.insertImage(at, path, &err);
    if (idx < 0) {
        emit imageInsertFailed(err);
        return;                                   // Scope ohne commit = kein Kommando
    }
    //  Steht der Cursor in einer Zelle, gehört das Bild in dieselbe Zelle.
    if (idx > 0 && idx - 1 < m_doc.blocks.size()) {
        const Block& prev = m_doc.blocks.at(idx - 1);
        if (prev.tableId >= 0) {
            Block& nb = m_doc.blocks[idx];
            nb.tableId = prev.tableId;
            nb.row = prev.row;
            nb.col = prev.col;
        }
    }
    m_cursor = { idx, 0, idx, 0 };
    clearPending();
    scope.commit(1);
}

void DocxEditController::insertImageData(const QByteArray& bytes,
                                         const QString& ext) {
    if (!m_ready || bytes.isEmpty()) return;
    int at = qBound(0, m_cursor.block, qMax(0, m_doc.blocks.size() - 1)) + 1;
    at = qBound(0, at, m_doc.blocks.size());

    EditScope scope(this, at, 0);
    QString err;
    const int idx = m_doc.insertImageData(at, bytes, ext, &err);
    if (idx < 0) {
        emit imageInsertFailed(err);
        return;
    }
    if (idx > 0 && idx - 1 < m_doc.blocks.size()) {
        const Block& prev = m_doc.blocks.at(idx - 1);
        if (prev.tableId >= 0) {
            Block& nb = m_doc.blocks[idx];
            nb.tableId = prev.tableId;
            nb.row = prev.row;
            nb.col = prev.col;
        }
    }
    m_cursor = { idx, 0, idx, 0 };
    clearPending();
    scope.commit(1);
}

QVariantList DocxEditController::folderImages() const {
    QVariantList out;
    const QString dirPath = QFileInfo(m_source).absolutePath();
    if (dirPath.isEmpty()) return out;

    QStringList filters;
    const auto fmts = QImageReader::supportedImageFormats();
    filters.reserve(fmts.size());
    for (const QByteArray& f : fmts)
        filters << QStringLiteral("*.") + QString::fromLatin1(f).toLower();

    QDir d(dirPath);
    const QFileInfoList files = d.entryInfoList(filters, QDir::Files | QDir::Readable,
                                                QDir::Name | QDir::IgnoreCase);
    //  Deckel: das Popup zeigt Miniaturen; ein Ordner mit tausenden Bildern
    //  soll die Kachel nicht lahmlegen (RAM/Reaktionszeit).
    constexpr int kMax = 300;
    for (const QFileInfo& fi : files) {
        if (out.size() >= kMax) break;
        QVariantMap m;
        m.insert(QStringLiteral("name"), fi.fileName());
        m.insert(QStringLiteral("url"), QUrl::fromLocalFile(fi.absoluteFilePath()).toString());
        out.append(m);
    }
    return out;
}

int DocxEditController::pdfPageCount(const QString& fileUrl) const {
    const QString path = mg::toLocalPath(fileUrl);
    if (path.isEmpty()) return 0;
    QPdfDocument d;
    if (d.load(path) != QPdfDocument::Error::None) return 0;
    return qMax(0, d.pageCount());
}

void DocxEditController::insertPdfPage(const QString& fileUrl, int page) {
    if (!m_ready) return;
    const QString path = mg::toLocalPath(fileUrl);
    if (path.isEmpty()) return;
    QPdfDocument d;
    if (d.load(path) != QPdfDocument::Error::None || d.pageCount() <= 0) {
        emit imageInsertFailed(QStringLiteral("PDF nicht lesbar."));
        return;
    }
    page = qBound(0, page, d.pageCount() - 1);

    //  150 dpi: Druckqualität, ohne den Container zu sprengen. Die Punktgröße
    //  der Seite ist 1/72 Zoll — daraus folgt die Pixelgröße direkt.
    constexpr qreal kDpi = 150.0;
    const QSizeF ptSize = d.pagePointSize(page);
    const QSize px(qMax(1, qRound(ptSize.width()  / 72.0 * kDpi)),
                   qMax(1, qRound(ptSize.height() / 72.0 * kDpi)));
    const QImage raw = d.render(page, px);
    if (raw.isNull()) {
        emit imageInsertFailed(QStringLiteral("Seite konnte nicht gerendert werden."));
        return;
    }
    //  Auf WEISS komponieren — der Renderer liefert einen transparenten Grund,
    //  im Dokument sähe die Seite sonst je nach Betrachter unterschiedlich aus.
    QImage flat(raw.size(), QImage::Format_RGB32);
    flat.fill(Qt::white);
    { QPainter p(&flat); p.drawImage(0, 0, raw); }

    QByteArray bytes;
    {
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        if (!flat.save(&buf, "PNG")) {
            emit imageInsertFailed(QStringLiteral("Seite konnte nicht gewandelt werden."));
            return;
        }
    }
    insertImageData(bytes, QStringLiteral("png"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tabellen-Struktur (Kontextmenü)
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::applyTableDef(int tableId, const Docx::TableDef& def) {
    m_doc.setTableDef(tableId, def);
}

QVariantMap DocxEditController::tableInfoAt(int block) const {
    QVariantMap m;
    m.insert(QStringLiteral("table"), false);
    if (!m_ready || m_doc.blocks.isEmpty()) return m;
    const int bi = qBound(0, block < 0 ? m_cursor.block : block,
                          m_doc.blocks.size() - 1);
    const Block& b = m_doc.blocks.at(bi);
    if (b.tableId < 0) return m;

    const int tid = b.tableId;
    const bool editable = m_doc.tableStructEditable(tid);
    m.insert(QStringLiteral("table"), true);
    m.insert(QStringLiteral("tableId"), tid);
    m.insert(QStringLiteral("row"), b.row);
    m.insert(QStringLiteral("col"), b.col);
    m.insert(QStringLiteral("rows"), m_doc.tableRowCount(tid));
    m.insert(QStringLiteral("cols"), m_doc.tableColumnCount(tid));
    m.insert(QStringLiteral("editable"), editable);
    //  Breiten für den Dialog in MILLIMETERN (Twips sind keine Nutzergröße).
    QVariantList mm;
    if (editable)
        for (int tw : m_doc.tableColumnWidths(tid))
            mm.append(qRound(tw / 56.6929 * 10.0) / 10.0);
    m.insert(QStringLiteral("widths"), mm);
    return m;
}

//  Gemeinsamer Rahmen aller Struktur-Operationen: der Undo-Bereich ist die
//  GANZE Tabelle (Blöcke) plus ihr Gerüst-Schnappschuss.
bool DocxEditController::tableStructOp(int tableId,
                                       const std::function<bool()>& op) {
    if (!m_ready) return false;
    const int first = m_doc.tableFirstBlock(tableId);
    const int last  = m_doc.tableLastBlock(tableId);
    if (first < 0 || last < first) return false;

    EditScope scope(this, first, last - first + 1);
    scope.watchTable(tableId);
    if (!op()) return false;                       // ohne commit = kein Kommando

    const int newLast = m_doc.tableLastBlock(tableId);
    const int newCount = (newLast < first) ? 0 : newLast - first + 1;
    //  Der Cursor kann in einem entfallenen Block gestanden haben.
    const int nb = m_doc.blocks.size();
    m_cursor.block = qBound(0, m_cursor.block, qMax(0, nb - 1));
    m_cursor.pos   = qBound(0, m_cursor.pos, blockLen(m_cursor.block));
    m_cursor.collapse();
    clearPending();
    scope.commit(newCount);
    return true;
}

void DocxEditController::tableInsertRow(int tableId, int atRow) {
    tableStructOp(tableId, [&] { return m_doc.tableInsertRow(tableId, atRow); });
}
void DocxEditController::tableDeleteRow(int tableId, int row) {
    tableStructOp(tableId, [&] { return m_doc.tableDeleteRow(tableId, row); });
}
void DocxEditController::tableInsertColumn(int tableId, int atCol) {
    tableStructOp(tableId, [&] { return m_doc.tableInsertColumn(tableId, atCol); });
}
void DocxEditController::tableDeleteColumn(int tableId, int col) {
    tableStructOp(tableId, [&] { return m_doc.tableDeleteColumn(tableId, col); });
}

void DocxEditController::tableSetColumnWidthsMm(int tableId,
                                                const QVariantList& mm) {
    QVector<int> tw;
    for (const QVariant& v : mm)
        tw.append(qRound(v.toDouble() * 56.6929));      // mm → Twips
    tableStructOp(tableId, [&] { return m_doc.tableSetColumnWidths(tableId, tw); });
}

void DocxEditController::scaleTableWidths(int tableId, qreal factor) {
    if (!m_ready) return;
    factor = qBound(0.1, factor, 10.0);
    QVector<int> tw = m_doc.tableColumnWidths(tableId);
    if (tw.isEmpty()) return;
    for (int& w : tw) w = qBound(200, int(qRound(w * factor)), 100000);
    tableStructOp(tableId, [&] { return m_doc.tableSetColumnWidths(tableId, tw); });
}

void DocxEditController::deleteTable(int tableId) {
    if (!m_ready) return;
    const int first = m_doc.tableFirstBlock(tableId);
    const int last  = m_doc.tableLastBlock(tableId);
    if (first < 0 || last < first) return;

    //  Die TableDef bleibt stehen — ohne Blöcke ist sie inert (die Emission
    //  läuft über die Blöcke), und ein Undo benutzt sie unverändert wieder.
    EditScope scope(this, first, last - first + 1);
    for (int i = last; i >= first; --i)
        m_doc.blocks.removeAt(i);
    const int nb = m_doc.blocks.size();
    m_cursor.block = qBound(0, first, qMax(0, nb - 1));
    m_cursor.pos   = 0;
    m_cursor.collapse();
    clearPending();
    scope.commit(0);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Bildgröße (A2)
// ─────────────────────────────────────────────────────────────────────────────
QVariantMap DocxEditController::imageInfoAt(int block) const {
    QVariantMap m;
    m.insert(QStringLiteral("image"), false);
    if (!m_ready || m_doc.blocks.isEmpty()) return m;
    const int bi = qBound(0, block < 0 ? m_cursor.block : block,
                          m_doc.blocks.size() - 1);
    Docx::InlineImage info;
    if (!m_doc.paragraphImage(m_doc.blocks.at(bi), &info)) return m;
    m.insert(QStringLiteral("image"), true);
    m.insert(QStringLiteral("block"), bi);
    //  Größe in Millimetern (1 mm = 36000 EMU).
    m.insert(QStringLiteral("widthMm"),  qRound(info.cxEmu / 36000.0 * 10.0) / 10.0);
    m.insert(QStringLiteral("heightMm"), qRound(info.cyEmu / 36000.0 * 10.0) / 10.0);
    return m;
}

void DocxEditController::setImageSizeMm(int block, qreal widthMm, qreal heightMm) {
    if (!m_ready || m_doc.blocks.isEmpty()) return;
    const int bi = qBound(0, block < 0 ? m_cursor.block : block,
                          m_doc.blocks.size() - 1);
    const qint64 cx = qint64(qRound(qBound(1.0, widthMm,  5000.0) * 36000.0));
    const qint64 cy = qint64(qRound(qBound(1.0, heightMm, 5000.0) * 36000.0));

    EditScope scope(this, bi, 1);
    if (!m_doc.setImageSizeEmu(bi, cx, cy)) return;
    clearPending();
    scope.commit(1);
}

void DocxEditController::toggleList(bool bullet) {
    if (!m_ready) return;
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    Q_UNUSED(p1) Q_UNUSED(p2)
    //  Zustand: sind ALLE selektierten Absätze bereits eine Liste dieses Typs?
    bool all = true, any = false;
    for (int i = b1; i <= b2; ++i) {
        if (!isEditableParagraph(i)) continue;
        any = true;
        const ParFmt pf = m_doc.resolvePar(m_doc.blocks.at(i));
        const bool isType = pf.numId > 0
            && (m_doc.numLevel(pf.numId, pf.ilvl).numFmt == QLatin1String("bullet")) == bullet;
        all = all && isType;
    }
    if (!any) return;
    if (all) {
        //  Ausschalten: numPr entfernen.
        applyParProp(QStringLiteral("w:numPr"), QString(),
                     [](ParFmt& p) { p.numId = -1; p.set &= ~ParFmt::FNum; });
        return;
    }
    //  Einschalten: numId des Vorgänger-Absatzes fortführen (gleicher Typ),
    //  sonst neue Listen-Instanz anlegen (Word-Verhalten).
    int numId = -1;
    if (b1 > 0 && isEditableParagraph(b1 - 1)) {
        const ParFmt prev = m_doc.resolvePar(m_doc.blocks.at(b1 - 1));
        if (prev.numId > 0
            && (m_doc.numLevel(prev.numId, 0).numFmt == QLatin1String("bullet")) == bullet)
            numId = prev.numId;
    }
    if (numId < 0)
        numId = m_doc.newListNum(bullet);
    applyParProp(QStringLiteral("w:numPr"),
                 QStringLiteral("<w:numPr><w:ilvl w:val=\"0\"/>"
                                "<w:numId w:val=\"%1\"/></w:numPr>").arg(numId),
                 [numId](ParFmt& p) { p.numId = numId; p.ilvl = 0; p.set |= ParFmt::FNum; });
}
void DocxEditController::toggleBullets()   { toggleList(true); }
void DocxEditController::toggleNumbering() { toggleList(false); }

// ─────────────────────────────────────────────────────────────────────────────
//  Zwischenablage / Undo
// ─────────────────────────────────────────────────────────────────────────────
//  Eigener Zwischenablage-Typ: trägt die Runs MIT ihrem rPr-Fragment, damit
//  Kopieren/Einfügen innerhalb der App (und zwischen zwei DOCX-Kacheln)
//  Schriftgröße/-art/Stil/Farbe verlustfrei erhält. Reiner Text (setText)
//  konnte das nicht — daher der Nutzerbefund „Kopieren verliert Formatierung".
static const char* const kDocxMime = "application/x-mediagallery-docx-runs";
static constexpr quint32 kClipMagic   = 0x4D474458u;   // "MGDX"
static constexpr quint16 kClipVersion = 1;

// ─────────────────────────────────────────────────────────────────────────────
//  Selektion → Blob. Kopiert werden AUSSCHLIESSLICH normale Runs; opake
//  (Zeichnung/Feld/Hyperlink-Konstrukt) hängen an ihrem Original-XML im
//  Quelldokument und dürfen nicht in ein anderes verpflanzt werden — sie
//  entfallen wie bisher schon in der Klartext-Fassung. Die Sentinels
//  U+FFFC/U+E000 werden mitentfernt (sonst wanderten Platzhalter mit).
// ─────────────────────────────────────────────────────────────────────────────
QByteArray DocxEditController::serializeSelection() const {
    QByteArray blob;
    QDataStream ds(&blob, QIODevice::WriteOnly);
    ds.setVersion(QDataStream::Qt_6_0);
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);

    QList<QList<Run>> paras;
    for (int i = b1; i <= b2; ++i) {
        QList<Run> out;
        if (isEditableParagraph(i)) {
            const Block& blk = m_doc.blocks.at(i);
            const int from = (i == b1) ? p1 : 0;
            const int to   = (i == b2) ? p2 : blk.textLength();
            int acc = 0;
            for (const Run& r : blk.runs) {
                const int rs = acc, re = acc + r.text.size();
                acc = re;
                if (r.opaque || r.text.isEmpty()) continue;
                if (re <= from || rs >= to) continue;
                Run c;
                c.text = r.text.mid(qMax(0, from - rs),
                                    qMin(re, to) - qMax(rs, from));
                c.text.remove(kObjectChar);
                c.text.remove(kPageBreak);
                if (c.text.isEmpty()) continue;
                c.rprXml = r.currentRpr(m_doc.docXml());
                c.fmt    = r.fmt;
                out.append(c);
            }
        }
        paras.append(out);
    }

    ds << kClipMagic << kClipVersion << quint32(paras.size());
    for (const QList<Run>& p : paras) {
        ds << quint32(p.size());
        for (const Run& r : p) {
            ds << r.rprXml << r.text << qint32(r.fmt.set)
               << r.fmt.bold << r.fmt.italic << r.fmt.underline
               << double(r.fmt.sizePt) << r.fmt.font << r.fmt.color;
        }
    }
    return blob;
}

bool DocxEditController::deserializeRuns(const QByteArray& blob,
                                         QList<QList<Run>>* out) {
    if (!out) return false;
    QDataStream ds(blob);
    ds.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0; quint16 ver = 0; quint32 paraCount = 0;
    ds >> magic >> ver >> paraCount;
    if (ds.status() != QDataStream::Ok || magic != kClipMagic || ver != kClipVersion)
        return false;
    if (paraCount == 0 || paraCount > 100000u)
        return false;
    out->clear();
    out->reserve(int(paraCount));
    for (quint32 i = 0; i < paraCount; ++i) {
        quint32 runCount = 0;
        ds >> runCount;
        if (ds.status() != QDataStream::Ok || runCount > 1000000u)
            return false;
        QList<Run> runs;
        runs.reserve(int(runCount));
        for (quint32 k = 0; k < runCount; ++k) {
            Run r;
            qint32 set = 0; double sz = 11.0;
            ds >> r.rprXml >> r.text >> set
               >> r.fmt.bold >> r.fmt.italic >> r.fmt.underline
               >> sz >> r.fmt.font >> r.fmt.color;
            if (ds.status() != QDataStream::Ok)
                return false;
            r.fmt.set    = int(set);
            r.fmt.sizePt = sz;
            //  Frische Runs OHNE Herkunfts-Spans: das rPr-Fragment ist bereits
            //  materialisiert, der Text wird neu serialisiert.
            r.rprMaterialized = true;
            r.dirty = true;
            runs.append(r);
        }
        out->append(runs);
    }
    return !out->isEmpty();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Selektion als HTML-Fragment — damit auch Word/LibreOffice/Browser die
//  Formatierung übernehmen (die interne MIME-Form kennen sie nicht).
// ─────────────────────────────────────────────────────────────────────────────
QString DocxEditController::selectionAsHtml() const {
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    static const char* const kAlign[] = { "left", "center", "right", "justify" };
    QString html = QStringLiteral("<!--StartFragment--><div>");
    for (int i = b1; i <= b2; ++i) {
        if (!isEditableParagraph(i)) continue;
        const Block& blk = m_doc.blocks.at(i);
        const ParFmt pf  = m_doc.resolvePar(blk);
        html += QStringLiteral("<p style=\"margin:0;text-align:%1\">")
                    .arg(QLatin1String(kAlign[qBound(0, pf.align, 3)]));
        const int from = (i == b1) ? p1 : 0;
        const int to   = (i == b2) ? p2 : blk.textLength();
        int acc = 0;
        bool any = false;
        for (const Run& r : blk.runs) {
            const int rs = acc, re = acc + r.text.size();
            acc = re;
            if (r.opaque || r.text.isEmpty()) continue;
            if (re <= from || rs >= to) continue;
            QString t = r.text.mid(qMax(0, from - rs), qMin(re, to) - qMax(rs, from));
            t.remove(kObjectChar);
            t.remove(kPageBreak);
            if (t.isEmpty()) continue;
            const RunFmt f = m_doc.resolveRun(blk, r);
            QString style = QStringLiteral("font-family:'%1';font-size:%2pt;")
                                .arg(Document::xmlEscape(f.font))
                                .arg(f.sizePt, 0, 'f', 1);
            if (f.bold)      style += QStringLiteral("font-weight:bold;");
            if (f.italic)    style += QStringLiteral("font-style:italic;");
            if (f.underline) style += QStringLiteral("text-decoration:underline;");
            if (f.color.isValid())
                style += QStringLiteral("color:%1;").arg(f.color.name(QColor::HexRgb));
            html += QStringLiteral("<span style=\"%1\">%2</span>")
                        .arg(style,
                             Document::xmlEscape(t)
                                 .replace(QLatin1Char('\t'), QLatin1String("&#9;"))
                                 .replace(kLineBreak, QLatin1String("<br/>")));
            any = true;
        }
        if (!any) html += QStringLiteral("<br/>");
        html += QStringLiteral("</p>");
    }
    html += QStringLiteral("</div><!--EndFragment-->");
    return html;
}

//  Bild in die Zwischenablage: als BILD, nicht als Modell-Ausschnitt. Damit
//  landet es auch in anderen Programmen, und `paste()` fügt es über die
//  bestehende Bild-Kette wieder ein — die Beziehung (rId) wandert also nicht
//  mit, was sie in einem FREMDEN Dokument ohnehin nicht dürfte. Preis: die
//  Pixel werden neu als PNG kodiert, das Original bleibt aber unangetastet
//  (kopiert wird ja nur).
bool DocxEditController::copyImageAtCursor() {
    if (!m_ready || m_doc.blocks.isEmpty()) return false;
    const int bi = qBound(0, m_cursor.block, m_doc.blocks.size() - 1);
    Docx::InlineImage info;
    if (!m_doc.paragraphImage(m_doc.blocks.at(bi), &info)) return false;
    const QByteArray bytes = m_doc.imageBytes(info.relId);
    QImage img;
    if (bytes.isEmpty() || !img.loadFromData(bytes) || img.isNull()) return false;
    QGuiApplication::clipboard()->setImage(img);
    return true;
}

void DocxEditController::deleteImageAtCursor() {
    if (!m_ready || m_doc.blocks.isEmpty()) return;
    const int bi = qBound(0, m_cursor.block, m_doc.blocks.size() - 1);
    Docx::InlineImage info;
    if (!m_doc.paragraphImage(m_doc.blocks.at(bi), &info)) return;

    EditScope scope(this, bi, 1);
    m_doc.blocks.removeAt(bi);
    const int nb = m_doc.blocks.size();
    m_cursor.block = qBound(0, bi > 0 ? bi - 1 : 0, qMax(0, nb - 1));
    m_cursor.pos   = 0;
    m_cursor.collapse();
    clearPending();
    scope.commit(0);
}

void DocxEditController::copy() {
    //  Kein Text markiert, aber ein Bild ausgewählt → das Bild kopieren.
    if (!m_cursor.hasSelection() && copyImageAtCursor()) return;
    if (!m_cursor.hasSelection()) return;
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    QString out;
    for (int i = b1; i <= b2; ++i) {
        const QString t = blockText(i);
        const int from = (i == b1) ? p1 : 0;
        const int to   = (i == b2) ? p2 : t.size();
        out += t.mid(from, to - from);
        if (i < b2) out += QLatin1Char('\n');
    }
    out.remove(kObjectChar);
    out.replace(kPageBreak, QLatin1Char('\n'));
    //  DREI Darstellungen in EINEM QMimeData: intern verlustfrei (eigener Typ),
    //  HTML für Fremdprogramme, Klartext als kleinster gemeinsamer Nenner.
    //  Das QMimeData geht in den Besitz der Zwischenablage über.
    auto* md = new QMimeData;
    md->setText(out);
    md->setHtml(selectionAsHtml());
    md->setData(QLatin1String(kDocxMime), serializeSelection());
    QGuiApplication::clipboard()->setMimeData(md);
}
void DocxEditController::cut() {
    if (!m_cursor.hasSelection()) {
        if (copyImageAtCursor()) deleteImageAtCursor();
        return;
    }
    copy();
    deleteBackward();
}
void DocxEditController::paste() {
    const QMimeData* md = QGuiApplication::clipboard()->mimeData();
    if (!md) return;
    //  Eigener Typ zuerst: Runs samt rPr → Formatierung bleibt erhalten.
    if (md->hasFormat(QLatin1String(kDocxMime))) {
        QList<QList<Run>> paras;
        if (deserializeRuns(md->data(QLatin1String(kDocxMime)), &paras)) {
            insertRunParagraphs(paras);
            return;
        }
    }
    //  BILD aus der Zwischenablage (Bildschirmfoto, Browser, Bildbetrachter):
    //  vor dem Klartext prüfen — viele Quellen legen zusätzlich einen
    //  Dateinamen als Text ab, der sonst gewönne. PNG behält Transparenz.
    if (md->hasImage()) {
        const QImage img = qvariant_cast<QImage>(md->imageData());
        if (!img.isNull()) {
            QByteArray bytes;
            QBuffer buf(&bytes);
            buf.open(QIODevice::WriteOnly);
            if (img.save(&buf, "PNG")) {
                buf.close();
                insertImageData(bytes, QStringLiteral("png"));
                return;
            }
        }
    }
    //  Fremdinhalt: wie bisher als Klartext (HTML-Import ist bewusst NICHT
    //  Teil dieses Editors — s. README „Planned").
    const QString t = md->text();
    if (!t.isEmpty()) insertText(t);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Einfügen fertiger Runs (Zwischenablage) — Ablauf identisch zu insertText,
//  nur dass die Runs ihr Format MITBRINGEN (kein Erben vom Nachbarn, kein
//  Pending). Absatz-Eigenschaften (pPr) übernimmt der Zielabsatz — genau wie
//  beim Tippen von Enter; damit können keine Listen-/Stil-Verweise (numId,
//  pStyle) aus einem fremden Dokument ins Ziel lecken.
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::insertRunParagraphs(const QList<QList<Run>>& paras) {
    if (!m_ready || paras.isEmpty()) return;
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    if (!isEditableParagraph(b1) || !isEditableParagraph(b2))
        return;

    EditScope scope(this, b1, b2 - b1 + 1);

    //  1) Selektion entfernen: Bereich zu EINEM Block verschmelzen.
    Block& head = m_doc.blocks[b1];
    if (m_cursor.hasSelection()) {
        if (b1 == b2) {
            removeRangeInBlock(head, p1, p2);
        } else {
            removeRangeInBlock(head, p1, blockLen(b1));
            Block& tail = m_doc.blocks[b2];
            removeRangeInBlock(tail, 0, p2);
            const int k = ensureRunBoundary(head, p1);
            Q_UNUSED(k)
            head.runs += tail.runs;
            head.dirty = true;
            for (int i = b2; i > b1; --i)
                m_doc.blocks.removeAt(i);
        }
    }

    //  2) Absätze einfügen.
    int curBlock = b1, curPos = p1;
    {
        Block& blk = m_doc.blocks[curBlock];
        int at = ensureRunBoundary(blk, curPos);
        for (const Run& r : paras.at(0)) {
            blk.runs.insert(at++, r);
            curPos += r.text.size();
        }
        blk.dirty = true;
    }
    for (int pi = 1; pi < paras.size(); ++pi) {
        //  Absatz-Split an curPos: Rest wandert in einen NEUEN Absatz.
        Block& blk = m_doc.blocks[curBlock];
        const int k = ensureRunBoundary(blk, curPos);
        Block nb;
        nb.kind = Block::Paragraph;
        const QString ppr = blk.currentPpr(m_doc.docXml());
        if (!ppr.isEmpty()) { nb.pprXml = ppr; nb.pprMaterialized = true; }
        nb.pfmt = blk.pfmt;
        while (blk.runs.size() > k)
            nb.runs.append(blk.runs.takeAt(k));
        blk.dirty = true;
        nb.dirty  = true;
        int at = 0;
        curPos = 0;
        for (const Run& r : paras.at(pi)) {
            nb.runs.insert(at++, r);
            curPos += r.text.size();
        }
        ++curBlock;
        m_doc.blocks.insert(curBlock, nb);
    }

    m_cursor = { curBlock, curPos, curBlock, curPos };
    clearPending();
    scope.commit(curBlock - b1 + 1);
}
void DocxEditController::undo() { if (m_stack.canUndo()) m_stack.undo(); }
void DocxEditController::redo() { if (m_stack.canRedo()) m_stack.redo(); }

// ─────────────────────────────────────────────────────────────────────────────
//  Suchen & Ersetzen
// ─────────────────────────────────────────────────────────────────────────────
QString DocxEditController::selectionPlainText() const {
    if (!m_cursor.hasSelection()) return {};
    int b1, p1, b2, p2; orderedSelection(b1, p1, b2, p2);
    if (b1 == b2)
        return blockText(b1).mid(p1, p2 - p1);
    QString out;
    for (int i = b1; i <= b2; ++i) {
        const QString t = blockText(i);
        const int s = (i == b1) ? p1 : 0;
        const int e = (i == b2) ? p2 : t.size();
        out += t.mid(s, e - s);
        if (i < b2) out += QLatin1Char('\n');
    }
    return out;
}

void DocxEditController::selectRange(int bi, int start, int len) {
    setCursor(bi, start, false);          // Anker + Position auf den Anfang
    setCursor(bi, start + len, true);     // Position ans Ende ziehen (Selektion)
}

QVariantMap DocxEditController::findNext(const QString& needle, bool caseSensitive,
                                         bool backward) {
    QVariantMap r;
    r.insert(QStringLiteral("found"), false);
    const int n = m_doc.blocks.size();
    if (!m_ready || needle.isEmpty() || n == 0)
        return r;
    const auto sens = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    //  Startgrenze: vorwärts ab Selektionsende, rückwärts ab Selektionsanfang.
    int sb, sp;
    { int b1, p1, b2, p2; orderedSelection(b1, p1, b2, p2);
      if (backward) { sb = b1; sp = p1; } else { sb = b2; sp = p2; } }

    //  EINMAL umlaufend durch alle Blöcke; der Wrap-Durchgang (k==n) landet
    //  wieder im Startblock und akzeptiert nur Treffer VOR der Startgrenze
    //  (die dahinter wurden bei k==0 bereits abgedeckt).
    for (int k = 0; k <= n; ++k) {
        const int bi = backward ? ((sb - k) % n + n) % n
                                : (sb + k) % n;
        if (!isEditableParagraph(bi))
            continue;
        const QString t = blockText(bi);
        int idx;
        if (!backward) {
            const int from = (k == 0) ? sp : 0;
            idx = t.indexOf(needle, from, sens);
            if (k == n) {                         // Wrap zurück in den Startblock
                if (idx >= 0 && idx < sp) { selectRange(bi, idx, needle.size());
                    r.insert(QStringLiteral("found"), true);
                    r.insert(QStringLiteral("wrapped"), true);
                    r.insert(QStringLiteral("block"), bi);
                    r.insert(QStringLiteral("pos"), idx); return r; }
                continue;
            }
            if (idx >= 0) { selectRange(bi, idx, needle.size());
                r.insert(QStringLiteral("found"), true);
                r.insert(QStringLiteral("wrapped"), false);
                r.insert(QStringLiteral("block"), bi);
                r.insert(QStringLiteral("pos"), idx); return r; }
        } else {
            const int fromEnd = (k == 0) ? sp - needle.size() : t.size();
            idx = t.lastIndexOf(needle, fromEnd, sens);
            if (k == n) {                         // Wrap zurück in den Startblock
                if (idx >= 0 && idx >= sp) { selectRange(bi, idx, needle.size());
                    r.insert(QStringLiteral("found"), true);
                    r.insert(QStringLiteral("wrapped"), true);
                    r.insert(QStringLiteral("block"), bi);
                    r.insert(QStringLiteral("pos"), idx); return r; }
                continue;
            }
            if (idx >= 0) { selectRange(bi, idx, needle.size());
                r.insert(QStringLiteral("found"), true);
                r.insert(QStringLiteral("wrapped"), false);
                r.insert(QStringLiteral("block"), bi);
                r.insert(QStringLiteral("pos"), idx); return r; }
        }
    }
    return r;
}

QVariantMap DocxEditController::replaceAndFind(const QString& needle,
                                               const QString& replacement,
                                               bool caseSensitive) {
    //  Suchen&Ersetzen ist absatzintern → Ersatztext ohne Umbrüche (sonst
    //  spaltete insertText den Absatz und verschöbe die Trefferpositionen).
    QString rep = replacement; rep.remove(QLatin1Char('\n')).remove(QLatin1Char('\r'));
    if (m_ready && !needle.isEmpty() && m_cursor.hasSelection()) {
        const auto sens = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        if (selectionPlainText().compare(needle, sens) == 0) {
            //  Aktuelle Selektion IST der Treffer → ersetzen (undo-fähig).
            if (rep.isEmpty()) deleteBackward();   // löscht die Selektion
            else               insertText(rep);
        }
    }
    return findNext(needle, caseSensitive, false);
}

int DocxEditController::replaceAll(const QString& needle, const QString& replacement,
                                   bool caseSensitive) {
    if (!m_ready || needle.isEmpty())
        return 0;
    //  Absatzintern → Ersatztext ohne Umbrüche (s. replaceAndFind).
    QString rep = replacement; rep.remove(QLatin1Char('\n')).remove(QLatin1Char('\r'));
    const auto sens = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    int count = 0;
    m_stack.beginMacro(QStringLiteral("Alle ersetzen"));
    //  Deterministisch von vorne: je Block alle Vorkommen, danach der nächste.
    //  Beim Ersetzen ändert sich nur der eigene Block (Absatzgrenzen bleiben,
    //  da needle/replacement absatzintern sind), daher Suche im selben Block
    //  ab dem Ende des Ersatzes fortsetzen — keine Endlosschleife (Deckel).
    const int hardCap = 5'000'000;
    for (int bi = 0; bi < m_doc.blocks.size(); ++bi) {
        if (!isEditableParagraph(bi))
            continue;
        int from = 0;
        while (true) {
            const QString t = blockText(bi);
            const int idx = t.indexOf(needle, from, sens);
            if (idx < 0) break;
            selectRange(bi, idx, needle.size());
            if (rep.isEmpty()) deleteBackward();
            else               insertText(rep);
            from = idx + rep.size();               // hinter dem Ersatz weitersuchen
            if (++count >= hardCap) break;
        }
        if (count >= hardCap) break;
    }
    m_stack.endMacro();
    if (count > 0)
        setModified(true);
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Live-Transliteration (Latein → Arabisch/Kana) am Cursor-Absatz — Muster
//  TextSurface._applyTranslit, nur controllerseitig: liveApply liefert
//  {changed,start,end,replacement,cursor}; die Ersetzung koalesziert (merge-
//  Kind 0) mit dem auslösenden Tastendruck zu EINEM Undo-Schritt.
// ─────────────────────────────────────────────────────────────────────────────
void DocxEditController::runTranslit() {
    if (!m_translit || m_cursor.hasSelection()) return;
    const int bi = m_cursor.block;
    if (!isEditableParagraph(bi)) return;
    QVariantMap res;
    const bool ok = QMetaObject::invokeMethod(
        m_translit, "liveApply", Qt::DirectConnection,
        Q_RETURN_ARG(QVariantMap, res),
        Q_ARG(QString, blockText(bi)), Q_ARG(int, m_cursor.pos));
    if (!ok || !res.value(QStringLiteral("changed")).toBool())
        return;
    const int start = res.value(QStringLiteral("start")).toInt();
    const int end   = res.value(QStringLiteral("end")).toInt();
    const QString repl = res.value(QStringLiteral("replacement")).toString();
    const int newCur   = res.value(QStringLiteral("cursor")).toInt();
    if (start < 0 || end < start || end > blockLen(bi))
        return;
    EditScope scope(this, bi, 1, 0);                    // koalesziert mit Tippen
    Block& blk = m_doc.blocks[bi];
    removeRangeInBlock(blk, start, end);
    if (!repl.isEmpty()) {
        const int at = ensureRunBoundary(blk, start);
        Run nr;
        int inherit = -1;
        for (int i = at - 1; i >= 0; --i)
            if (!blk.runs.at(i).opaque) { inherit = i; break; }
        if (inherit < 0)
            for (int i = at; i < blk.runs.size(); ++i)
                if (!blk.runs.at(i).opaque) { inherit = i; break; }
        if (inherit >= 0) {
            const Run& src = blk.runs.at(inherit);
            nr.rprSpan = src.rprSpan; nr.rprXml = src.rprXml;
            nr.rprMaterialized = src.rprMaterialized; nr.fmt = src.fmt;
        }
        nr.text = repl;
        nr.dirty = true;
        blk.runs.insert(at, nr);
        blk.dirty = true;
    }
    m_cursor = { bi, qBound(0, newCur, blockLen(bi)), bi, 0 };
    m_cursor.collapse();
    scope.commit(1);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Toolbar-Format
// ─────────────────────────────────────────────────────────────────────────────
//  Aufgelöstes Zeichenformat EINER Stelle. Leerer Absatz (alle Runs gelöscht):
//  NICHT die docDefaults, sondern das Stil-Format des Absatzes selbst
//  (resolveRun mit leerem Run läuft die pStyle-Kette ab) — sonst zeigte die
//  Leiste in einer leergelöschten Überschrift-Zeile ein drittes, mit nichts
//  übereinstimmendes Format (docDefaults) an.
RunFmt DocxEditController::resolvedFormatAt(int block, int pos) const {
    if (!isEditableParagraph(block))
        return m_doc.defaultRun();
    const Block& blk = m_doc.blocks.at(block);
    int ri = -1, ro = 0;
    runAt(blk, pos, &ri, &ro);
    if (ri >= 0 && ri < blk.runs.size())
        return m_doc.resolveRun(blk, blk.runs.at(ri));
    return m_doc.resolveRun(blk, Run());
}

RunFmt DocxEditController::caretFormat() const {
    if (!m_ready || m_doc.blocks.isEmpty())
        return m_doc.defaultRun();
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    //  Format des Zeichens LINKS vom Cursor (Editor-Konvention).
    RunFmt rf = resolvedFormatAt(b1, m_cursor.hasSelection() ? p1 : qMax(0, p1 - 1));
    //  Pending-Format überlagert an der Pending-Stelle.
    if (m_pendingBlock == m_cursor.block && m_pendingPos == m_cursor.pos) {
        if (m_pending.set & RunFmt::FBold)      rf.bold      = m_pending.bold;
        if (m_pending.set & RunFmt::FItalic)    rf.italic    = m_pending.italic;
        if (m_pending.set & RunFmt::FUnderline) rf.underline = m_pending.underline;
        if (m_pending.set & RunFmt::FFont)      rf.font      = m_pending.font;
        if (m_pending.set & RunFmt::FSize)      rf.sizePt    = m_pending.sizePt;
        if (m_pending.set & RunFmt::FColor)     rf.color     = m_pending.color;
    }
    return rf;
}

QVariantMap DocxEditController::currentFormat() const {
    QVariantMap m;
    if (m_doc.blocks.isEmpty() || !m_ready) {
        m.insert(QStringLiteral("ready"), false);
        return m;
    }
    int b1, p1, b2, p2;
    orderedSelection(b1, p1, b2, p2);
    const int bi = b1;
    const RunFmt rf = caretFormat();
    ParFmt pf;
    if (isEditableParagraph(bi))
        pf = m_doc.resolvePar(m_doc.blocks.at(bi));
    int list = 0;
    if (pf.numId > 0)
        list = (m_doc.numLevel(pf.numId, pf.ilvl).numFmt == QLatin1String("bullet")) ? 1 : 2;
    m.insert(QStringLiteral("ready"),       true);
    m.insert(QStringLiteral("bold"),        rf.bold);
    m.insert(QStringLiteral("italic"),      rf.italic);
    m.insert(QStringLiteral("underline"),   rf.underline);
    m.insert(QStringLiteral("font"),        rf.font);
    m.insert(QStringLiteral("sizePt"),      rf.sizePt);
    m.insert(QStringLiteral("color"),       rf.color);
    m.insert(QStringLiteral("align"),       pf.align);
    m.insert(QStringLiteral("lineSpacing"), pf.lineSpacing);
    m.insert(QStringLiteral("beforePt"),    pf.beforePt);
    m.insert(QStringLiteral("afterPt"),     pf.afterPt);
    m.insert(QStringLiteral("list"),        list);
    //  Absatzvorlage am Cursor. Leer = Standardvorlage (kein w:pStyle); die
    //  Auswahlliste zeigt dafür ihren ersten Eintrag.
    m.insert(QStringLiteral("styleId"),     pf.styleId);
    m.insert(QStringLiteral("hasSelection"), m_cursor.hasSelection());
    return m;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Speichern (asynchron; GUI baut die Ersatzteile, der Worker schreibt)
// ─────────────────────────────────────────────────────────────────────────────
QString DocxEditController::exportTargetPath() const {
    const QFileInfo fi(m_source);
    const QString dir  = fi.absolutePath();
    const QString base = fi.completeBaseName() + QStringLiteral("_edited");
    QString candidate = dir + QLatin1Char('/') + base + QStringLiteral(".docx");
    int n = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QLatin1Char('/') + base
                    + QStringLiteral(" (%1).docx").arg(n);
        ++n;
    }
    return candidate;
}

void DocxEditController::save() {
    //  Primäre Speicheraktion (Button/Strg+S) — folgt der globalen
    //  Einstellung: Direkt speichern ODER Kopie exportieren.
    if (AppSettings::instance().docxSaveDirect()) {
        if (!m_ready || m_busy || m_source.isEmpty() || !m_modified)
            return;
        //  Einmalig je Sitzung: .bak-Sicherung NEBEN dem Original.
        if (!m_bakDone) {
            const QString bak = m_source + QStringLiteral(".bak");
            QFile::remove(bak);
            QFile::copy(m_source, bak);
            m_bakDone = true;
        }
        startSaveWorker(m_source, true);
    } else {
        exportCopy();
    }
}

void DocxEditController::exportCopy() {
    if (!m_ready || m_busy || m_source.isEmpty())
        return;
    startSaveWorker(exportTargetPath(), false);
}

QString DocxEditController::pdfExportTargetPath() const {
    const QFileInfo fi(m_source);
    const QString dir  = fi.absolutePath();
    const QString base = fi.completeBaseName();
    QString candidate = dir + QLatin1Char('/') + base + QStringLiteral(".pdf");
    int n = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QLatin1Char('/') + base
                    + QStringLiteral(" (%1).pdf").arg(n);
        ++n;
    }
    return candidate;
}

void DocxEditController::exportToPdf(const QString& tablePlaceholder,
                                     const QString& pageBreakLabel) {
    //  Aufgabe 2 „→ PDF": Original-.docx bleibt unangetastet, es entsteht
    //  <Name>.pdf daneben. Der Aufbau des QTextDocument + das Paginieren nach
    //  A4 laufen im Worker (potenziell länger → UI-Thread bleibt responsiv,
    //  Regel 17). Der Worker arbeitet auf einer COW-Kopie des Modells.
    if (!m_ready || m_busy || m_source.isEmpty())
        return;
    m_busy = true;
    emit busyChanged();

    const QString target = pdfExportTargetPath();
    //  IMMER der Körper — steht der Editor gerade in der Kopfzeile, wäre
    //  sonst diese das exportierte "Dokument".
    Docx::Document docCopy = bodyDoc();   // implizit geteilte Kopie
    QPointer<DocxEditController> self(this);

    class PdfTask : public QRunnable {
    public:
        PdfTask(QPointer<DocxEditController> c, Docx::Document d, QString tgt,
                QString tbl, QString pb)
            : m_c(c), m_doc(std::move(d)), m_tgt(std::move(tgt)),
              m_tbl(std::move(tbl)), m_pb(std::move(pb)) { setAutoDelete(true); }
        void run() override {
            QString err;
            const bool ok = DocxPdf::exportToPdf(m_doc, m_tgt, m_tbl, m_pb, &err);
            auto c = m_c;
            const QString tgt = m_tgt;
            QMetaObject::invokeMethod(qApp, [c, ok, tgt, err]() {
                if (!c) return;
                c->m_busy = false;
                emit c->busyChanged();
                emit c->pdfExportFinished(ok, tgt, err);
            }, Qt::QueuedConnection);
        }
    private:
        QPointer<DocxEditController> m_c;
        Docx::Document m_doc;
        QString m_tgt, m_tbl, m_pb;
    };
    QThreadPool::globalInstance()->start(
        new PdfTask(self, std::move(docCopy), target, tablePlaceholder, pageBreakLabel));
}

void DocxEditController::startSaveWorker(const QString& targetPath, bool direct) {
    m_busy = true;
    emit busyChanged();

    //  Schnappschuss der Ersatzteile auf dem GUI-Thread (reine Strings) —
    //  über ALLE geladenen Regionen, damit eine bearbeitete Kopf-/Fußzeile
    //  mitgeschrieben wird (s. allRegionParts).
    QHash<QString, QByteArray> parts = allRegionParts();
    const QString srcPath = m_source;
    QPointer<DocxEditController> self(this);

    class SaveTask : public QRunnable {
    public:
        SaveTask(QPointer<DocxEditController> c, QString src, QString tgt,
                 QHash<QString, QByteArray> parts, bool direct)
            : m_c(c), m_src(std::move(src)), m_tgt(std::move(tgt)),
              m_parts(std::move(parts)), m_direct(direct) { setAutoDelete(true); }
        void run() override {
            QString err;
            bool ok = false;
            //  Quelle KOMPLETT in den Speicher lesen und schließen — im
            //  Direkt-Modus ersetzt QSaveFile::commit die Quelldatei (unter
            //  Windows scheitert das Umbenennen sonst am offenen Handle).
            QList<QPair<DocxZip::Entry, QByteArray>> raws;
            {
                DocxZip::Reader zip;
                if (zip.open(m_src, &err)) {
                    ok = true;
                    raws.reserve(zip.entries().size());
                    for (int i = 0; i < zip.entries().size(); ++i) {
                        bool rok = false;
                        raws.append({ zip.entries().at(i), zip.rawData(i, &rok) });
                        if (!rok) { ok = false; err = QStringLiteral("Eintrag nicht lesbar."); break; }
                    }
                }
            }
            if (ok) {
                QSaveFile out(m_tgt);
                ok = out.open(QIODevice::WriteOnly);
                if (!ok) err = QStringLiteral("Ziel nicht beschreibbar.");
                if (ok) {
                    DocxZip::Writer w(&out);
                    auto parts = m_parts;
                    for (const auto& pr : std::as_const(raws)) {
                        auto it = parts.find(pr.first.name);
                        if (it != parts.end()) {
                            ok = w.addFile(pr.first.name, it.value(), &pr.first, &err);
                            parts.erase(it);
                        } else {
                            ok = w.addRaw(pr.first, pr.second, &err);
                        }
                        if (!ok) break;
                    }
                    if (ok) {
                        for (auto it = parts.constBegin(); ok && it != parts.constEnd(); ++it)
                            ok = w.addFile(it.key(), it.value(), nullptr, &err);
                    }
                    ok = ok && w.finish(&err) && out.commit();
                    if (!ok && err.isEmpty())
                        err = QStringLiteral("Schreiben fehlgeschlagen.");
                    if (!ok) out.cancelWriting();
                }
            }
            auto c = m_c;
            const QString tgt = m_tgt;
            const bool direct = m_direct;
            QMetaObject::invokeMethod(qApp, [c, ok, tgt, err, direct]() {
                if (!c) return;
                c->m_busy = false;
                emit c->busyChanged();
                if (ok && direct)
                    c->setModified(false);
                emit c->saveFinished(ok, tgt, err);
            }, Qt::QueuedConnection);
        }
    private:
        QPointer<DocxEditController> m_c;
        QString m_src, m_tgt;
        QHash<QString, QByteArray> m_parts;
        bool m_direct;
    };
    QThreadPool::globalInstance()->start(new SaveTask(self, srcPath, targetPath, parts, direct));
}

void DocxEditController::release() {
    //  Beim Verlassen der Kachel automatisch sichern (Muster TextSurface —
    //  kein Datenverlust); der Speicherweg folgt der globalen Einstellung.
    if (m_ready && m_modified && !m_busy)
        save();
}
