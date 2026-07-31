#include "docx/edit/DocxTextArea.h"
#include "docx/edit/DocxEditController.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QStyleHints>
#include <QRegularExpression>
#include <QCursor>

using namespace Docx;

namespace {
constexpr qreal kPtToPx     = 96.0 / 72.0;    // Punkt → logische Pixel
constexpr qreal kTwipToPx   = kPtToPx / 20.0; // Twips (1/1440") → logische Pixel
constexpr qreal kPadV       = 28.0;           // Rand über/unter dem Seitenstapel
constexpr qreal kPageGap    = 20.0;           // Lücke zwischen zwei Seiten
constexpr qreal kSideMargin = 14.0;           // Mindestrand neben der Seite
constexpr qreal kListIndent = 28.0;           // Einzug je Listenebene
constexpr int   kChunk      = 300;            // Blöcke je Initial-Layout-Tick

//  Layout-Fenster (RAM): Ein vermessener QTextLayout haelt die komplette
//  Glyphen-/Zeilenstruktur seines Absatzes — bei einem 400-Seiten-Dokument mit
//  ~10.000 Absaetzen summiert sich das auf zweistellige Megabyte, obwohl nie
//  mehr als ein Bildschirm davon gebraucht wird. Ab kLayoutCap Bloecken werden
//  daher Layouts ausserhalb des Fensters (sichtbarer Bereich ± kKeepMargin)
//  freigegeben; die bereits vermessene HOEHE bleibt erhalten (Bildlaufleiste
//  und Offsets bleiben exakt), ensureLaid() baut das Layout bei Bedarf
//  identisch neu auf.
constexpr int   kLayoutCap  = 600;
constexpr int   kKeepMargin = 150;
}

DocxTextArea::DocxTextArea(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(false);
    setFlag(ItemAcceptsInputMethod, true);
    setCursor(QCursor(Qt::IBeamCursor));
    setOpaquePainting(false);

    m_chunkTimer.setInterval(0);
    m_chunkTimer.setSingleShot(false);
    connect(&m_chunkTimer, &QTimer::timeout, this, &DocxTextArea::layoutChunk);

    //  Der Blinktakt laeuft NUR, solange der Caret ueberhaupt sichtbar sein kann
    //  (Fokus, keine Selektion). Jedes update() dieses QQuickPaintedItem malt den
    //  gesamten Viewport samt aller sichtbaren QTextLayouts neu und laedt die
    //  Textur wieder zur GPU — vorher geschah das zweimal je Sekunde dauerhaft,
    //  auch bei fokusloser, unsichtbarer oder gar nicht editierter Ansicht.
    m_blinkTimer.setInterval(530);
    connect(&m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_caretOn = !m_caretOn;
        update();
    });
    connect(this, &QQuickItem::activeFocusChanged, this, &DocxTextArea::syncCaretBlink);
    syncCaretBlink();
}

//  Blinken exakt dann laufen lassen, wenn paint() den Caret auch zeichnen wuerde
//  (s. Bedingung am Ende von paint()).
void DocxTextArea::syncCaretBlink() {
    const bool want = hasActiveFocus() && m_ctl && m_ctl->ready()
                      && !m_ctl->cursor().hasSelection();
    if (want == m_blinkTimer.isActive())
        return;
    if (want) {
        m_caretOn = true;
        m_blinkTimer.start();
    } else {
        m_blinkTimer.stop();
    }
    update();
}

DocxTextArea::~DocxTextArea() = default;

void DocxTextArea::setCtl(DocxEditController* c) {
    if (m_ctl == c) return;
    if (m_ctl) m_ctl->disconnect(this);
    m_ctl = c;
    if (m_ctl) {
        connect(m_ctl, &DocxEditController::readyChanged, this, [this]() {
            rebuildAll();
            syncCaretBlink();
        });
        connect(m_ctl, &DocxEditController::blocksReplaced, this,
                [this](int first, int oldCount, int newCount) {
                    invalidateFrom(first, oldCount, newCount);
                });
        connect(m_ctl, &DocxEditController::cursorChanged, this, [this]() {
            m_caretOn = true;
            //  Der zuletzt besuchte LEERE Absatz wurde mit dem Cursor-Format
            //  vermessen — verlässt der Cursor ihn, gilt wieder sein Stil-Format.
            const int now = m_ctl->cursor().block;
            if (m_lastCursorBlock != now) {
                invalidateEmptyBlock(m_lastCursorBlock);
                invalidateEmptyBlock(now);
                m_lastCursorBlock = now;
            }
            updateCursorRect();
            syncCaretBlink();   // Selektion begonnen/aufgehoben → Blinken an/aus
            update();
        });
        //  Format-Änderung OHNE Selektion existiert nur als Pending-Format im
        //  Controller — sie erzeugt weder blocksReplaced noch cursorChanged.
        //  Ohne diese Verbindung blieb der Caret (und die Höhe einer leeren
        //  Zeile) auf der alten Schriftgröße stehen (Nutzerbefund).
        //  Regionswechsel (Körper ↔ Kopf-/Fußzeile) = ANDERES Dokument:
        //  Layout-Cache, Offsets und laufende Teile sind komplett ungültig.
        connect(m_ctl, &DocxEditController::activeRegionChanged, this, [this]() {
            m_header = RunningPart();
            m_footer = RunningPart();
            rebuildAll();
        });
        connect(m_ctl, &DocxEditController::formatRevChanged, this, [this]() {
            m_caretOn = true;
            invalidateEmptyBlock(m_ctl->cursor().block);
            updateCursorRect();
            update();
        });
    }
    emit ctlChanged();
    rebuildAll();
    syncCaretBlink();
}

void DocxTextArea::setContentY(qreal y) {
    y = qMax(0.0, qMin(y, qMax(0.0, m_contentHeight - height())));
    if (qFuzzyCompare(m_contentY + 1.0, y + 1.0)) return;
    m_contentY = y;
    emit contentYChanged();
    emit imageSelectionChanged();   // Ziehpunkte scrollen mit
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Seitengeometrie — alles in DOKUMENT-Pixeln aus der Seiteneinrichtung des
//  Dokuments (w:sectPr). Bewusst UNABHÄNGIG von der Fensterbreite: die
//  Zeilenbreite ist die echte Textbreite der Seite, damit der Umbruch dort
//  fällt, wo er auch in Word fällt. Zu schmale Kacheln werden beim ZEICHNEN
//  verkleinert (m_scale), nicht neu umbrochen.
// ─────────────────────────────────────────────────────────────────────────────
const SectionProps& DocxTextArea::sect() const {
    static const SectionProps fallback;               // A4, falls kein Dokument
    //  IMMER die Seiteneinrichtung des KÖRPERS — ein Kopfzeilen-Teil hat keine
    //  eigene und die Seite dürfte beim Umschalten nicht die Größe wechseln.
    return (m_ctl && m_ctl->ready()) ? m_ctl->section() : fallback;
}
qreal DocxTextArea::pageWpx() const   { return sect().pageW * kTwipToPx; }
qreal DocxTextArea::pageHpx() const   { return sect().pageH * kTwipToPx; }
qreal DocxTextArea::marLpx() const    { return sect().marLeft * kTwipToPx; }
qreal DocxTextArea::marTpx() const    { return sect().marTop * kTwipToPx; }
int   DocxTextArea::colCount() const  { return qMax(1, sect().cols); }
qreal DocxTextArea::colSpacePx() const { return sect().colSpace * kTwipToPx; }

qreal DocxTextArea::contentWidth() const {
    const SectionProps& s = sect();
    const qreal textW = qMax(40.0, (s.pageW - s.marLeft - s.marRight) * kTwipToPx);
    const int n = colCount();
    return qMax(40.0, (textW - (n - 1) * colSpacePx()) / n);
}
qreal DocxTextArea::slotHeight() const {
    const SectionProps& s = sect();
    return qMax(40.0, (s.pageH - s.marTop - s.marBottom) * kTwipToPx);
}
qreal DocxTextArea::slotDocX(int slot) const {
    const int c = ((slot % colCount()) + colCount()) % colCount();
    return marLpx() + c * (contentWidth() + colSpacePx());
}
qreal DocxTextArea::pageDocY(int page) const {
    return kPadV + page * (pageHpx() + kPageGap);
}
qreal DocxTextArea::slotDocY(int slot) const {
    return pageDocY(slot / colCount()) + marTpx();
}
qreal DocxTextArea::docHeight() const {
    return pageDocY(qMax(0, m_pageCount - 1)) + pageHpx() + kPadV;
}

//  Maßstab: eine A4-Seite ist ~794 px breit — in einer halben Split-View-Kachel
//  hätte sie keinen Platz. Statt neu umzubrechen (dann wäre die Ansicht nicht
//  mehr seitengenau) wird die Seite VERKLEINERT gezeichnet.
void DocxTextArea::updateScale() {
    const qreal avail = width() - 2 * kSideMargin;
    const qreal s = (avail > 20.0 && pageWpx() > 1.0)
                        ? qMin(1.0, avail / pageWpx()) : 1.0;
    if (qFuzzyCompare(s + 1.0, m_scale + 1.0))
        return;
    m_scale = s;
    updateContentHeight();
    updateCursorRect();
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Segmente: welche Zeilen eines Absatzes liegen in welchem Slot?
// ─────────────────────────────────────────────────────────────────────────────
const DocxTextArea::PageSeg& DocxTextArea::segAt(int i, int lineIdx) const {
    static const PageSeg zero;
    const BlockLayout& L = m_lay[size_t(i)];
    if (L.segs.isEmpty()) return zero;
    int k = 0;
    for (int j = 1; j < L.segs.size(); ++j) {
        if (L.segs.at(j).firstLine > lineIdx) break;
        k = j;
    }
    return L.segs.at(k);
}

//  Dokument-y AUS DEM FLUSS (Segmente) — ohne Zell-Sonderfall. Für den Anker
//  ist das die Oberkante der TABELLE; jede Zelle rechnet von dort weiter. Der
//  Zell-Zweig darf deshalb NICHT docYForLine(anchor) rufen (der Anker ist selbst
//  eine Zelle → Endlosrekursion).
qreal DocxTextArea::flowDocYForLine(int i, int lineIdx) {
    if (i < 0 || i >= int(m_lay.size())) return slotDocY(0);
    ensureOffsetsTo(i + 1);
    const BlockLayout& L = m_lay[size_t(i)];
    if (L.segs.isEmpty()) return slotDocY(0);
    const PageSeg& s = segAt(i, lineIdx);
    qreal segLineY = 0.0, lineY = 0.0;
    if (L.layout) {
        const int n = L.layout->lineCount();
        if (s.firstLine > 0 && s.firstLine < n)
            segLineY = L.layout->lineAt(s.firstLine).y();
        if (lineIdx >= 0 && lineIdx < n)
            lineY = L.layout->lineAt(lineIdx).y();
    }
    return slotDocY(s.slot) + s.yInSlot + (lineY - segLineY);
}

qreal DocxTextArea::flowDocXForBlock(int i, int lineIdx) {
    if (i < 0 || i >= int(m_lay.size())) return slotDocX(0);
    ensureOffsetsTo(i + 1);
    return slotDocX(segAt(i, lineIdx).slot) + m_lay[size_t(i)].indentPx;
}

//  Dokument-Lage eines Blocks. Zellblöcke (auch der Anker!) liegen explizit
//  relativ zur Tabellen-Oberkante, alle anderen im Fluss.
qreal DocxTextArea::docYForLine(int i, int lineIdx) {
    if (i < 0 || i >= int(m_lay.size())) return slotDocY(0);
    const int anchor = tableAnchorOf(i);
    if (anchor >= 0 && m_lay[size_t(i)].isCell) {
        const BlockLayout& C = m_lay[size_t(i)];
        qreal ly = 0.0;
        if (C.layout && lineIdx > 0 && lineIdx < C.layout->lineCount())
            ly = C.layout->lineAt(lineIdx).y();
        return flowDocYForLine(anchor, 0) + C.cellRelY + ly;
    }
    return flowDocYForLine(i, lineIdx);
}

qreal DocxTextArea::docXForBlock(int i, int lineIdx) {
    if (i < 0 || i >= int(m_lay.size())) return slotDocX(0);
    const int anchor = tableAnchorOf(i);
    if (anchor >= 0 && m_lay[size_t(i)].isCell)
        return flowDocXForBlock(anchor, 0) + m_lay[size_t(i)].cellRelX;
    return flowDocXForBlock(i, lineIdx);
}

int DocxTextArea::currentPage() const {
    if (!m_ctl || !m_ctl->ready() || m_lay.empty()) return 0;
    const int bi = qBound(0, m_ctl->cursor().block, int(m_lay.size()) - 1);
    const BlockLayout& L = m_lay[size_t(bi)];
    if (L.segs.isEmpty()) return 0;
    return L.segs.constFirst().slot / colCount();
}

qreal DocxTextArea::pageTop(int page) {
    return pageDocY(qBound(0, page, qMax(0, m_pageCount - 1))) * m_scale;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layout-Verwaltung
// ─────────────────────────────────────────────────────────────────────────────
void DocxTextArea::rebuildAll() {
    m_lay.clear();
    m_offsets.clear();
    m_offsetsValidTo = 0;
    m_layChunkAt = 0;
    m_trimLo = m_trimHi = -1;   // Indizes verschoben → Layout-Fenster neu bestimmen
    m_contentHeight = 0;
    m_header = RunningPart();
    m_footer = RunningPart();
    m_lastCursorBlock = (m_ctl && m_ctl->ready()) ? m_ctl->cursor().block : -1;
    if (m_ctl && m_ctl->ready()) {
        m_lay.resize(size_t(m_ctl->doc().blocks.size()));
        //  Schätzhöhen (eine Zeile je ~90 Zeichen) — vom Chunk-Layout ersetzt.
        for (int i = 0; i < int(m_lay.size()); ++i) {
            const Block& b = m_ctl->doc().blocks.at(i);
            if (b.kind == Block::OpaqueHidden)      m_lay[i].height = 0;
            else if (b.kind == Block::OpaqueVisible) m_lay[i].height = 34;
            else m_lay[i].height = 22.0 * qMax(1, b.textLength() / 90 + 1);
        }
        rebuildMarkers();
        m_chunkTimer.start();
    }
    //  Seitengröße kommt aus dem Dokument → Einpass-Maßstab neu bestimmen.
    updateScale();
    updateContentHeight();
    updateCursorRect();
    emit documentChanged();
    update();
}

void DocxTextArea::invalidateFrom(int first, int oldCount, int newCount) {
    if (!m_ctl || !m_ctl->ready()) { rebuildAll(); return; }
    if (m_lay.empty() && newCount > 0) { rebuildAll(); return; }
    first = qBound(0, first, int(m_lay.size()));
    //  Wird in einer Zelle getippt, ändert sich Zeilen- und damit Tabellenhöhe.
    //  Die trägt der ANKER — er muss also mit invalidiert werden, sonst bliebe
    //  die Tabelle auf ihrer alten Höhe stehen und alle Umbrüche danach falsch.
    {
        const int anchor = tableAnchorOf(qMin(first, int(m_lay.size()) - 1));
        if (anchor >= 0 && anchor < int(m_lay.size())) {
            m_lay[size_t(anchor)].laid = false;
            m_lay[size_t(anchor)].table.reset();
            first = qMin(first, anchor);
        }
    }
    for (int i = 0; i < oldCount && first < int(m_lay.size()); ++i)
        m_lay.erase(m_lay.begin() + first);
    for (int i = 0; i < newCount; ++i)
        m_lay.insert(m_lay.begin() + first + i, BlockLayout());
    for (int i = 0; i < newCount; ++i)
        ensureLaid(first + i);
    m_offsetsValidTo = qMin(m_offsetsValidTo, first);
    m_trimLo = m_trimHi = -1;   // Indizes verschoben → Layout-Fenster neu bestimmen
    //  Ein Inhaltsverzeichnis hängt am GANZEN Dokument, nicht nur an den
    //  Blöcken hinter der Änderung — es steht ja meist davor. Also immer
    //  verwerfen; der Neuaufbau kostet einen Lauf über die Blöcke, genau wie
    //  rebuildMarkers direkt darunter.
    for (size_t k = 0; k < m_lay.size(); ++k) {
        if (!m_lay[k].isToc) continue;
        m_lay[k].laid = false;
        m_lay[k].tocEntries.clear();
        m_offsetsValidTo = qMin(m_offsetsValidTo, int(k));
    }
    rebuildMarkers();
    //  Marker können sich hinter der Änderung geändert haben (Listenzähler) —
    //  betroffene Layouts dort NICHT wegwerfen (nur Marker-Strings neu).
    updateContentHeight();
    updateCursorRect();
    emit documentChanged();
    update();
}

//  Läuft über das GESAMTE Dokument und wird bei JEDER Blockänderung (also bei
//  jedem Tastendruck) aufgerufen — die Listenzähler sind global. Der teure Teil
//  war bisher resolvePar() je Absatz: Vorlagenkette ablaufen, ParFmt bauen.
//  Kann laut Vorlagen keine Nummerierung entstehen (Normalfall), genügt ein
//  Bit-Test am Absatz selbst und die Schleife wird pro Block O(1) ohne
//  Allokation — bei einem 400-Seiten-Dokument der Unterschied zwischen
//  spürbarem Tipp-Ruckeln und gar nicht messbar.
void DocxTextArea::rebuildMarkers() {
    if (!m_ctl) return;
    const Document& d = m_ctl->doc();
    const bool mayNumber = d.stylesMayNumber();
    QHash<int, int> counters;                       // numId → laufende Nummer
    for (int i = 0; i < d.blocks.size() && i < int(m_lay.size()); ++i) {
        const Block& b = d.blocks.at(i);
        QString marker;
        qreal indent = 0;
        if (b.kind == Block::Paragraph
            && (mayNumber || (b.pfmt.set & ParFmt::FNum))) {
            const ParFmt pf = d.resolvePar(b);
            if (pf.numId > 0) {
                const NumLevel lv = d.numLevel(pf.numId, pf.ilvl);
                indent = kListIndent * (pf.ilvl + 1);
                if (lv.numFmt == QLatin1String("bullet")) {
                    marker = lv.lvlText.isEmpty() ? QStringLiteral("\u2022")
                                                  : lv.lvlText;
                    if (marker.contains(QLatin1Char('%')))
                        marker = QStringLiteral("\u2022");
                } else {
                    const int n = ++counters[pf.numId];
                    marker = lv.lvlText;
                    if (marker.isEmpty()) marker = QStringLiteral("%1.");
                    marker.replace(QStringLiteral("%1"), QString::number(n));
                    //  Fremde Mehr-Ebenen-Muster (%2…) pragmatisch kappen.
                    //  static: sonst würde das Muster je Listenabsatz und je
                    //  Tastendruck neu übersetzt.
                    static const QRegularExpression reLvlRest(QStringLiteral("%\\d"));
                    marker.remove(reLvlRest);
                }
                marker += QLatin1Char(' ');
            }
        }
        if (m_lay[i].marker != marker || !qFuzzyCompare(m_lay[i].indentPx + 1, indent + 1)) {
            m_lay[i].marker   = marker;
            m_lay[i].indentPx = indent;
            if (m_lay[i].laid) { m_lay[i].laid = false; m_lay[i].layout.reset(); }
        }
    }
}

void DocxTextArea::buildLayout(int i) {
    const Document& d = m_ctl->doc();
    const Block& b = d.blocks.at(i);
    BlockLayout& L = m_lay[i];
    L.layout.reset();
    L.beforePx = 0;

    if (b.kind == Block::OpaqueHidden) {
        L.height = 0; L.laid = true; return;
    }
    if (b.kind == Block::OpaqueVisible) {
        //  Tabellen werden als echtes Gitter ausgelegt (richtige HÖHE → richtige
        //  Seitenumbrüche danach); alles andere bleibt eine Platzhalterzeile.
        //  Dieser Zweig gilt nur noch für Tabellen, die NICHT flach zerlegt
        //  werden konnten (verschachtelt, sdt …) — sie bleiben ein opaker Block.
        if (b.opaqueName == QLatin1String("w:tbl")) { buildTableLayout(i); return; }
        L.height = 34; L.laid = true; return;
    }

    //  Flach zerlegte Tabelle: der ERSTE Zellblock ist der Anker und trägt das
    //  Gitter samt Gesamthöhe; alle weiteren Zellblöcke sind 0 hoch. Damit bleibt
    //  der Fluss streng monoton (nebeneinanderliegende Zellen teilen sonst
    //  dasselbe y-Band) — die Auflösung dieser Vereinfachung ist Schritt 1A.2.
    if (b.tableId >= 0) {
        const int anchor = d.tableFirstBlock(b.tableId);
        if (anchor == i) { buildFlatTableLayout(i); return; }
        //  Zellblöcke werden VOM ANKER mitausgelegt (die Zeilenhöhe hängt von
        //  allen Zellen der Zeile ab) — hier nur sicherstellen, dass er es tat.
        if (anchor >= 0 && anchor < int(m_lay.size()) && !m_lay[size_t(anchor)].table)
            buildFlatTableLayout(anchor);
        L.laid = true;
        return;
    }

    //  Inhaltsverzeichnis-Feld → eine Zeile je Überschrift.
    if (buildTocLayout(i))
        return;

    //  Absatz, der NUR ein Bild enthält → als Bild auslegen.
    if (buildImageLayout(i))
        return;

    const ParFmt pf = d.resolvePar(b);
    const QString text = b.plainText();

    //  Grundschrift des Absatzes = SEIN aufgelöstes Stil-Format (nicht die
    //  docDefaults): nur so ist eine leere Überschriftzeile auch so hoch wie
    //  eine Überschrift. Steht der Cursor in dieser (leeren) Zeile, gilt das am
    //  Cursor wirksame Format inkl. Pending — sonst hätte eine Schriftgrößen-
    //  Änderung in einer leeren Zeile sichtbar keinerlei Wirkung.
    const RunFmt def = d.defaultRun();
    RunFmt bf = d.resolveRun(b, Run());
    if (text.isEmpty() && m_ctl->cursor().block == i)
        bf = m_ctl->caretFormat();
    QFont base;
    base.setFamily(bf.font.isEmpty() ? def.font : bf.font);
    base.setPointSizeF(bf.sizePt > 0 ? bf.sizePt : def.sizePt);
    base.setBold(bf.bold);
    base.setItalic(bf.italic);

    auto* lay = new QTextLayout(text, base);
    QTextOption opt;
    switch (pf.align) {
    case 1:  opt.setAlignment(Qt::AlignHCenter); break;
    case 2:  opt.setAlignment(Qt::AlignRight);   break;
    case 3:  opt.setAlignment(Qt::AlignJustify); break;
    default: opt.setAlignment(Qt::AlignLeft);    break;
    }
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    opt.setTextDirection(Qt::LayoutDirectionAuto);   // Arabisch: RTL automatisch
    lay->setTextOption(opt);
    lay->setCacheEnabled(true);

    //  Format-Bereiche aus den Runs (aufgelöst über docDefaults + Vorlagen).
    QList<QTextLayout::FormatRange> fmts;
    int acc = 0;
    for (const Run& r : b.runs) {
        if (r.text.isEmpty()) continue;
        const RunFmt rf = d.resolveRun(b, r);
        QTextCharFormat cf;
        QFont f;
        f.setFamily(rf.font.isEmpty() ? def.font : rf.font);
        f.setPointSizeF(rf.sizePt > 0 ? rf.sizePt : def.sizePt);
        f.setBold(rf.bold);
        f.setItalic(rf.italic);
        f.setUnderline(rf.underline);
        cf.setFont(f);
        cf.setForeground(rf.color.isValid() ? rf.color : QColor(0, 0, 0));
        if (r.opaque) {
            //  Atomare Fremdinhalte dezent hinterlegen (Hyperlink-Blau wäre
            //  irreführend — es sind auch Felder/Zeichnungen).
            cf.setBackground(QColor(0, 0, 0, 14));
        }
        QTextLayout::FormatRange fr;
        fr.start  = acc;
        fr.length = r.text.size();
        fr.format = cf;
        fmts.append(fr);
        acc += r.text.size();
    }
    //  Seitenumbruch-Sentinel unsichtbar machen (der Marker wird gemalt).
    for (int p = text.indexOf(kPageBreak); p >= 0; p = text.indexOf(kPageBreak, p + 1)) {
        QTextLayout::FormatRange fr;
        fr.start = p; fr.length = 1;
        fr.format.setForeground(Qt::transparent);
        fmts.append(fr);
    }
    lay->setFormats(fmts);

    //  Zeilen manuell positionieren (Zeilenabstand = Vielfaches der Zeilenhöhe).
    const qreal w = contentWidth() - L.indentPx;
    const qreal spacing = qMax(0.5, pf.lineSpacing);
    L.beforePx = pf.beforePt * kPtToPx;
    qreal y = L.beforePx;
    lay->beginLayout();
    for (;;) {
        QTextLine line = lay->createLine();
        if (!line.isValid()) break;
        line.setLineWidth(w);
        line.setPosition(QPointF(0, y));
        y += line.height() * spacing;
    }
    lay->endLayout();
    if (lay->lineCount() == 0)                       // leerer Absatz: eine Zeile hoch
        y += QFontMetricsF(base).height() * spacing;
    L.height = y + pf.afterPt * kPtToPx;
    L.layout.reset(lay);
    L.laid = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tabellen-Anzeige (read-only): Gitter auslegen und zeichnen
// ─────────────────────────────────────────────────────────────────────────────
//  Einen Absatz vermessen, ohne die Block-Layout-Verwaltung zu berühren — wird
//  für ZELL-Absätze gebraucht (dieselben Schrift-/Ausrichtungsregeln wie
//  buildLayout, nur ohne Listenmarker und Absatzabstände).
std::unique_ptr<QTextLayout> DocxTextArea::layoutParagraph(const Block& b, qreal width,
                                                           qreal* heightOut) const {
    const Document& d = m_ctl->doc();
    const RunFmt def = d.defaultRun();
    const RunFmt bf  = d.resolveRun(b, Run());
    const ParFmt pf  = d.resolvePar(b);

    QFont base;
    base.setFamily(bf.font.isEmpty() ? def.font : bf.font);
    base.setPointSizeF(bf.sizePt > 0 ? bf.sizePt : def.sizePt);
    base.setBold(bf.bold);
    base.setItalic(bf.italic);

    auto lay = std::make_unique<QTextLayout>(b.plainText(), base);
    QTextOption opt;
    switch (pf.align) {
    case 1:  opt.setAlignment(Qt::AlignHCenter); break;
    case 2:  opt.setAlignment(Qt::AlignRight);   break;
    case 3:  opt.setAlignment(Qt::AlignJustify); break;
    default: opt.setAlignment(Qt::AlignLeft);    break;
    }
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    opt.setTextDirection(Qt::LayoutDirectionAuto);
    lay->setTextOption(opt);

    QList<QTextLayout::FormatRange> fmts;
    int acc = 0;
    for (const Run& r : b.runs) {
        if (r.text.isEmpty()) continue;
        const RunFmt rf = d.resolveRun(b, r);
        QTextCharFormat cf;
        QFont f;
        f.setFamily(rf.font.isEmpty() ? def.font : rf.font);
        f.setPointSizeF(rf.sizePt > 0 ? rf.sizePt : def.sizePt);
        f.setBold(rf.bold);
        f.setItalic(rf.italic);
        f.setUnderline(rf.underline);
        cf.setFont(f);
        cf.setForeground(rf.color.isValid() ? rf.color : QColor(0, 0, 0));
        QTextLayout::FormatRange fr;
        fr.start = acc; fr.length = r.text.size(); fr.format = cf;
        fmts.append(fr);
        acc += r.text.size();
    }
    lay->setFormats(fmts);

    const qreal spacing = qMax(0.5, pf.lineSpacing);
    qreal y = 0.0;
    lay->beginLayout();
    for (;;) {
        QTextLine line = lay->createLine();
        if (!line.isValid()) break;
        line.setLineWidth(qMax(8.0, width));
        line.setPosition(QPointF(0, y));
        y += line.height() * spacing;
    }
    lay->endLayout();
    if (lay->lineCount() == 0)
        y += QFontMetricsF(base).height() * spacing;
    if (heightOut) *heightOut = y;
    return lay;
}

//  Bild-Absatz: ein Absatz, der NUR ein eingebettetes Bild enthält, wird als
//  Bild ausgelegt statt als grauer Platzhalter. Rückgabe: true = war einer.
//  Bilder MITTEN im Text bleiben der Platzhalter — QTextLayout kann Inline-
//  Objekte ohne QTextDocument nicht vermessen.
bool DocxTextArea::buildImageLayout(int i, qreal availWidth) {
    BlockLayout& L = m_lay[size_t(i)];
    const Document& d = m_ctl->doc();
    const Block& b = d.blocks.at(i);

    InlineImage info;
    if (!d.paragraphImage(b, &info)) {
        L.isImage = false;
        L.image = QImage();
        return false;
    }
    L.isImage = true;

    //  Sollmaß aus wp:extent (EMU → Punkt → Pixel), auf die verfügbare Breite
    //  begrenzt — das ist die Textspalte, in einer Tabellenzelle deren Breite.
    constexpr qreal kEmuToPx = kPtToPx / 12700.0;     // 1 pt = 12700 EMU
    const qreal avail = availWidth > 0.0 ? availWidth : contentWidth();
    qreal w = info.cxEmu > 0 ? info.cxEmu * kEmuToPx : 0.0;
    qreal h = info.cyEmu > 0 ? info.cyEmu * kEmuToPx : 0.0;

    const QByteArray bytes = d.imageBytes(info.relId);
    QImage src;
    if (!bytes.isEmpty()) src.loadFromData(bytes);

    if (src.isNull()) {
        //  Beziehung/Format unbekannt → Rahmen in Sollgröße statt Fehlanzeige.
        if (w <= 0.0) w = qMin(avail, 160.0);
        if (h <= 0.0) h = 90.0;
    } else {
        if (w <= 0.0 || h <= 0.0) {                  // kein extent → native Größe
            w = src.width()  * (96.0 / qMax(1, src.dotsPerMeterX() > 0
                                                ? qRound(src.dotsPerMeterX() * 0.0254) : 96));
            h = src.height() * (96.0 / qMax(1, src.dotsPerMeterY() > 0
                                                ? qRound(src.dotsPerMeterY() * 0.0254) : 96));
            if (w <= 0.0 || h <= 0.0) { w = src.width(); h = src.height(); }
        }
    }
    //  Auf die Textbreite einpassen (Seitenverhältnis halten).
    if (w > avail && w > 0.0) { h *= avail / w; w = avail; }
    w = qBound(8.0, w, qMax(8.0, avail));
    h = qBound(8.0, h, 4000.0);

    //  NUR das eingepasste Bild halten, nicht das Original (RAM = Priorität 1);
    //  freigegeben wird es mit dem Layout-Fenster (trimLayouts).
    if (!src.isNull()) {
        const QSize target(qMax(1, qRound(w)), qMax(1, qRound(h)));
        L.image = src.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        L.image = QImage();
    }
    L.imageBoxW = w;
    L.imageBoxH = h;
    L.height    = h + 6.0;                           // kleiner Abstand darunter
    L.indentPx  = 0.0;                               // Bild sitzt am Textrand
    L.marker.clear();
    L.laid      = true;
    L.layout.reset();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Inhaltsverzeichnis
//
//  Das FELD in der Datei bleibt deklarativ (keine eingebackenen Seitenzahlen).
//  Hier wird je Überschrift eine Zeile reserviert; die SEITENZAHL trägt erst
//  `paintToc` ein. Das geht auf, weil die Höhe nur an der ANZAHL der Einträge
//  hängt, nicht an den Zahlen — es braucht also keinen zweiten Layout-Pass.
// ─────────────────────────────────────────────────────────────────────────────
bool DocxTextArea::buildTocLayout(int i) {
    BlockLayout& L = m_lay[size_t(i)];
    const Document& d = m_ctl->doc();
    const Block& b = d.blocks.at(i);

    if (!d.isTocParagraph(b)) {
        L.isToc = false;
        L.tocEntries.clear();
        return false;
    }
    L.isToc = true;
    L.tocEntries = d.tocEntries();
    L.layout.reset();

    const RunFmt def = d.defaultRun();
    QFont f;
    f.setFamily(def.font);
    f.setPointSizeF(def.sizePt > 0 ? def.sizePt : 11.0);
    const QFontMetricsF fm(f);
    L.tocLineH = fm.height() + 4.0;
    //  Auch ein LEERES Verzeichnis braucht Höhe, sonst wäre der Absatz
    //  unsichtbar und nicht mehr anwählbar (zum Löschen).
    const int n = qMax(1, int(L.tocEntries.size()));
    L.height   = n * L.tocLineH + 8.0;
    L.beforePx = 0.0;
    L.indentPx = 0.0;
    L.marker.clear();
    L.laid     = true;
    return true;
}

//  1-basierte Seitenzahl eines Blocks (Verzeichnis-Einträge).
int DocxTextArea::pageOfBlock(int i) {
    if (i < 0 || i >= int(m_lay.size())) return 1;
    ensureOffsetsTo(i + 1);
    const BlockLayout& L = m_lay[size_t(i)];
    if (L.segs.isEmpty()) return 1;
    return L.segs.constFirst().slot / qMax(1, colCount()) + 1;
}

void DocxTextArea::paintToc(QPainter* p, const BlockLayout& L, qreal left,
                            qreal y, qreal width) {
    const Document& d = m_ctl->doc();
    const RunFmt def = d.defaultRun();
    QFont f;
    f.setFamily(def.font);
    f.setPointSizeF(def.sizePt > 0 ? def.sizePt : 11.0);
    p->setFont(f);
    const QFontMetricsF fm(f);

    if (L.tocEntries.isEmpty()) {
        p->setPen(QColor(140, 140, 140));
        QFont it = f; it.setItalic(true);
        p->setFont(it);
        p->drawText(QRectF(left, y + 4, width, L.tocLineH),
                    Qt::AlignLeft | Qt::AlignVCenter, m_tocEmptyLabel);
        return;
    }

    qreal ly = y + 4.0;
    for (const Docx::TocEntry& e : L.tocEntries) {
        const qreal indent = (e.level - 1) * 18.0;
        const QString page = QString::number(pageOfBlock(e.block));
        const qreal pw = fm.horizontalAdvance(page);
        //  Text links (bei Bedarf gekürzt), Seitenzahl rechtsbündig, dazwischen
        //  eine Punktreihe — wie in Word.
        const qreal availText = qMax(20.0, width - indent - pw - 24.0);
        const QString text = fm.elidedText(e.text, Qt::ElideRight, availText);
        const qreal tw = fm.horizontalAdvance(text);
        p->setPen(QColor(30, 30, 30));
        p->drawText(QPointF(left + indent, ly + fm.ascent()), text);
        p->drawText(QPointF(left + width - pw, ly + fm.ascent()), page);

        const qreal dotFrom = left + indent + tw + 4.0;
        const qreal dotTo   = left + width - pw - 4.0;
        if (dotTo > dotFrom) {
            p->setPen(QPen(QColor(150, 150, 150), 1, Qt::DotLine));
            const qreal by = ly + fm.ascent() - fm.xHeight() / 2.0;
            p->drawLine(QPointF(dotFrom, by), QPointF(dotTo, by));
        }
        ly += L.tocLineH;
    }
}

//  Kopf-/Fußzeile EINMAL auslegen (auf allen Seiten dieselbe) und für jede Seite
//  wiederverwenden — sie hängt nicht am Fluss und darf ihn nicht verschieben.
void DocxTextArea::ensureHeaderFooter() {
    if (!m_ctl || !m_ctl->ready()) return;
    //  Wird die Kopf-/Fußzeile SELBST bearbeitet, steht sie schon als Inhalt
    //  auf der Seite — sie dann zusätzlich in den Rand zu malen wäre doppelt.
    if (m_ctl->activeRegionInt() != DocxEditController::Body) return;
    const Document& d = m_ctl->doc();
    const qreal w = contentWidth() * colCount()
                    + colSpacePx() * (colCount() - 1);
    for (int pass = 0; pass < 2; ++pass) {
        RunningPart& part = (pass == 0) ? m_header : m_footer;
        if (part.built) continue;
        part.built = true;
        const HeaderFooter hf = d.headerFooter(pass == 1, false);
        if (!hf.ok) continue;
        qreal total = 0.0;
        for (const Block& p : hf.paragraphs) {
            qreal ph = 0.0;
            part.paras.push_back(layoutParagraph(p, w, &ph));
            total += ph;
        }
        part.height = total;
    }
}

void DocxTextArea::paintHeaderFooter(QPainter* p, int page) {
    ensureHeaderFooter();
    const qreal x = marLpx();
    //  Kopfzeile SITZT IM oberen Rand (über dem Textbereich), Fußzeile im
    //  unteren — genau dafür ist der Rand da. Läuft der Teil höher als der
    //  Rand, wird er oben/unten abgeschnitten statt in den Text zu laufen.
    if (m_header.height > 0.0) {
        p->save();
        p->setClipRect(QRectF(x, pageDocY(page), pageWpx() - marLpx(),
                              qMax(0.0, marTpx() - 4.0)), Qt::IntersectClip);
        qreal y = pageDocY(page) + qMax(4.0, marTpx() - m_header.height - 8.0);
        for (const auto& lay : m_header.paras) {
            if (!lay) continue;
            lay->draw(p, QPointF(x, y));
            qreal h = 0.0;
            for (int ln = 0; ln < lay->lineCount(); ++ln)
                h = qMax(h, lay->lineAt(ln).y() + lay->lineAt(ln).height());
            y += h;
        }
        p->restore();
    }
    if (m_footer.height > 0.0) {
        const qreal top = pageDocY(page) + pageHpx() - sect().marBottom * kTwipToPx + 4.0;
        p->save();
        p->setClipRect(QRectF(x, top, pageWpx() - marLpx(),
                              qMax(0.0, sect().marBottom * kTwipToPx - 4.0)),
                       Qt::IntersectClip);
        qreal y = top;
        for (const auto& lay : m_footer.paras) {
            if (!lay) continue;
            lay->draw(p, QPointF(x, y));
            qreal h = 0.0;
            for (int ln = 0; ln < lay->lineCount(); ++ln)
                h = qMax(h, lay->lineAt(ln).y() + lay->lineAt(ln).height());
            y += h;
        }
        p->restore();
    }
}

int DocxTextArea::tableAnchorOf(int i) const {
    if (!m_ctl || !m_ctl->ready() || i < 0 || i >= m_ctl->doc().blocks.size())
        return -1;
    const Block& b = m_ctl->doc().blocks.at(i);
    if (b.tableId < 0) return -1;
    return m_ctl->doc().tableFirstBlock(b.tableId);
}

//  Gitter einer FLACH zerlegten Tabelle aus ihren lebenden Zellblöcken bauen.
//  Der Anker trägt die Gesamthöhe (Fluss bleibt monoton, die Tabelle ist damit
//  für die Paginierung ein Stück); jeder Zellblock bekommt seine Lage RELATIV
//  zum Anker. Bearbeiten einer Zelle ändert deren Höhe → Zeilenhöhe →
//  Tabellenhöhe; deshalb wird bei jeder Änderung der ANKER neu ausgelegt
//  (s. invalidateFrom).
void DocxTextArea::buildFlatTableLayout(int anchor) {
    const Document& d = m_ctl->doc();
    BlockLayout& A = m_lay[size_t(anchor)];
    const int tid = d.blocks.at(anchor).tableId;
    const TableDef& def = d.tables().at(tid);

    A.isTable = true;
    A.table.reset();
    A.layout.reset();
    A.isCell = false;

    //  Spaltenbreiten wie im Anzeige-Pfad: Gitter, sonst gleichmäßig; zu breite
    //  Tabellen werden proportional in die Textspalte eingepasst.
    const qreal avail = contentWidth();
    int maxCells = 1;
    for (int r = 0; r < def.rowSpans.size(); ++r) {
        const int firstCell = def.rowFirstCell.at(r);
        const int lastCell = (r + 1 < def.rowFirstCell.size())
                                 ? def.rowFirstCell.at(r + 1) - 1
                                 : def.cellSpans.size() - 1;
        int n = 0;
        for (int c = firstCell; c <= lastCell; ++c)
            n += qMax(1, def.cellGridSpan.value(c, 1));
        maxCells = qMax(maxCells, n);
    }
    QVector<qreal> colW;
    qreal sum = 0;
    for (int w : def.gridTw) { colW.append(w * kTwipToPx); sum += w * kTwipToPx; }
    if (sum > avail && sum > 0) {
        const qreal f = avail / sum;
        for (qreal& w : colW) w *= f;
    }
    while (colW.size() < maxCells)
        colW.append(avail / maxCells);

    auto tl = std::make_unique<TableLayout>();
    const qreal pad = 108.0 * kTwipToPx;
    qreal yRow = 0.0;
    int blockAt = anchor;

    for (int r = 0; r < def.rowSpans.size(); ++r) {
        RowLayout rl;
        rl.y = yRow;
        const int firstCell = def.rowFirstCell.at(r);
        const int lastCell = (r + 1 < def.rowFirstCell.size())
                                 ? def.rowFirstCell.at(r + 1) - 1
                                 : def.cellSpans.size() - 1;
        int colIdx = 0;
        qreal x = 0.0;
        for (int c = firstCell; c <= lastCell; ++c) {
            const int span = qBound(1, def.cellGridSpan.value(c, 1),
                                    qMax(1, colW.size() - colIdx));
            qreal w = 0.0;
            for (int k = 0; k < span && colIdx + k < colW.size(); ++k)
                w += colW.at(colIdx + k);
            if (w <= 0.0) w = avail / maxCells;

            CellLayout cl;
            cl.x = x;
            cl.w = w;

            //  Absätze DIESER Zelle sind die nächsten Blöcke mit (row,col).
            const int colOfCell = c - firstCell;
            qreal yInCell = pad;
            while (blockAt < d.blocks.size()
                   && d.blocks.at(blockAt).tableId == tid
                   && d.blocks.at(blockAt).row == r
                   && d.blocks.at(blockAt).col == colOfCell) {
                BlockLayout& CB = m_lay[size_t(blockAt)];
                qreal ph = 0.0;
                //  Ein Zell-Absatz, der NUR ein Bild enthält, wird auch in der
                //  Zelle als Bild ausgelegt (auf die Zellbreite eingepasst) —
                //  sonst stünde dort das nackte Objekt-Zeichen und die Zelle
                //  hätte die falsche Höhe.
                if (buildImageLayout(blockAt, w - 2 * pad)) {
                    ph = CB.height;                        // Bildhöhe + Abstand
                } else {
                    CB.layout = layoutParagraph(d.blocks.at(blockAt), w - 2 * pad, &ph);
                }
                CB.isCell   = true;
                CB.isTable  = false;
                CB.cellRelX = x + pad;
                CB.cellRelY = yRow + yInCell;
                CB.cellW    = w - 2 * pad;
                CB.height   = 0.0;         // Höhe trägt der Anker
                CB.beforePx = 0.0;
                CB.indentPx = 0.0;
                CB.laid     = true;
                yInCell += ph;
                ++blockAt;
            }
            cl.h = yInCell + pad;
            rl.cells.push_back(std::move(cl));
            x += w;
            colIdx += span;
        }
        qreal rowH = 20.0;
        for (const CellLayout& c2 : rl.cells) rowH = qMax(rowH, c2.h);
        rl.h = rowH;
        tl->width = qMax(tl->width, x);
        yRow += rowH;
        tl->rows.push_back(std::move(rl));
    }

    //  Der Anker hat ZWEI Rollen: er trägt das Gitter (isTable) UND ist der
    //  erste Absatz der ersten Zelle (isCell) — die Zellschleife oben hat seine
    //  Flags dabei überschrieben, deshalb hier wiederherstellen. Genau das war
    //  der Grund, warum jeder Klick in der Tabelle im Anker landete.
    A.isTable = true;
    A.height  = yRow + 6.0;
    A.table   = std::move(tl);
    A.laid    = true;
}

void DocxTextArea::buildTableLayout(int i) {
    BlockLayout& L = m_lay[size_t(i)];
    L.table.reset();
    const Block& b = m_ctl->doc().blocks.at(i);
    //  Gezeichnet wird immer aus dem ROH-Bereich der Tabelle — egal ob sie flach
    //  zerlegt wurde (dann liefert das Gerüst den Bereich) oder als opaker Block
    //  vorliegt. So gibt es genau EINEN Tabellen-Renderer.
    Block src = b;
    if (b.tableId >= 0 && b.tableId < m_ctl->doc().tables().size()) {
        src = Block();
        src.kind = Block::OpaqueVisible;
        src.opaqueName = QStringLiteral("w:tbl");
        src.rawSpan = m_ctl->doc().tables().at(b.tableId).rawSpan();
    }
    const TableView tv = m_ctl->doc().parseTableForDisplay(src);
    if (!tv.ok) {
        //  Nicht deutbar → wie bisher eine Platzhalterzeile.
        L.isTable = false;
        L.height  = 34;
        L.laid    = true;
        return;
    }
    L.isTable = true;

    //  Spaltenbreiten: Gitter aus w:tblGrid, sonst gleichmäßig. Ist die Tabelle
    //  breiter als die Textspalte, wird sie proportional eingepasst — eine
    //  Tabelle, die über den Seitenrand hinausläuft, wäre schlimmer als eine
    //  leicht gestauchte.
    const qreal avail = contentWidth();
    int maxCells = 1;
    for (const TableRow& r : tv.rows) {
        int n = 0;
        for (const TableCell& c : r.cells) n += qMax(1, c.gridSpan);
        maxCells = qMax(maxCells, n);
    }
    QVector<qreal> colW;
    qreal totalTw = 0;
    for (int w : tv.gridTw) totalTw += w;
    if (!tv.gridTw.isEmpty() && totalTw > 0) {
        qreal sum = 0;
        for (int w : tv.gridTw) { colW.append(w * kTwipToPx); sum += w * kTwipToPx; }
        if (sum > avail && sum > 0) {
            const qreal f = avail / sum;
            for (qreal& w : colW) w *= f;
        }
    }
    while (colW.size() < maxCells)
        colW.append(avail / maxCells);

    auto tl = std::make_unique<TableLayout>();
    const qreal pad = 108.0 * kTwipToPx;          // Word-Standard-Zellrand
    qreal yRow = 0.0;
    for (const TableRow& row : tv.rows) {
        RowLayout rl;
        rl.y = yRow;
        int colIdx = 0;
        qreal x = 0.0;
        for (const TableCell& cell : row.cells) {
            const int span = qBound(1, cell.gridSpan, qMax(1, colW.size() - colIdx));
            qreal w = 0.0;
            for (int k = 0; k < span && colIdx + k < colW.size(); ++k)
                w += colW.at(colIdx + k);
            if (w <= 0.0) w = avail / maxCells;

            CellLayout cl;
            cl.x = x;
            cl.w = w;
            qreal h = pad;
            for (const Block& para : cell.paragraphs) {
                if (para.kind == Block::OpaqueVisible) {
                    //  Verschachtelte Tabelle: Platzhalterzeile.
                    cl.paras.push_back(nullptr);
                    cl.paraIsPlaceholder.push_back(true);
                    h += 22.0;
                    continue;
                }
                qreal ph = 0.0;
                cl.paras.push_back(layoutParagraph(para, w - 2 * pad, &ph));
                cl.paraIsPlaceholder.push_back(false);
                h += ph;
            }
            cl.h = h + pad;
            rl.cells.push_back(std::move(cl));
            x += w;
            colIdx += span;
        }
        //  Zeilenhöhe = höchste Zelle (mindestens eine leere Textzeile).
        qreal rowH = 20.0;
        for (const CellLayout& c : rl.cells) rowH = qMax(rowH, c.h);
        rl.h = rowH;
        tl->width = qMax(tl->width, x);
        yRow += rowH;
        tl->rows.push_back(std::move(rl));
    }

    L.height = yRow + 6.0;          // kleiner Abstand nach der Tabelle
    L.table  = std::move(tl);
    L.laid   = true;
}

void DocxTextArea::paintTable(QPainter* p, const BlockLayout& L, qreal left, qreal y) {
    if (!L.table) return;
    const qreal pad = 108.0 * kTwipToPx;
    p->save();
    for (const RowLayout& row : L.table->rows) {
        for (const CellLayout& cell : row.cells) {
            const QRectF cr(left + cell.x, y + row.y, cell.w, row.h);
            //  Gitterlinien (Word-Standardtabelle). Eigene Rahmen-/Schattierungs-
            //  Angaben aus w:tcBorders/w:shd werden bewusst nicht gedeutet.
            p->setPen(QPen(QColor(140, 140, 148), 0.8));
            p->setBrush(Qt::NoBrush);
            p->drawRect(cr);

            qreal py = cr.y() + pad;
            //  Bei flach zerlegten Tabellen ist `paras` leer — der Zelltext kommt
            //  dann aus den echten Blöcken (s. paintSlot).
            for (size_t k = 0; k < cell.paras.size(); ++k) {
                if (k < cell.paraIsPlaceholder.size() && cell.paraIsPlaceholder[k]) {
                    p->setPen(QColor(120, 120, 128));
                    QFont f; f.setPointSizeF(8.5); f.setItalic(true);
                    p->setFont(f);
                    p->drawText(QPointF(cr.x() + pad, py + 14.0), m_tablePlaceholder);
                    py += 22.0;
                    continue;
                }
                const QTextLayout* lay = cell.paras[k].get();
                if (!lay) continue;
                lay->draw(p, QPointF(cr.x() + pad, py));
                qreal h = 0.0;
                for (int ln = 0; ln < lay->lineCount(); ++ln)
                    h = qMax(h, lay->lineAt(ln).y() + lay->lineAt(ln).height());
                py += h;
            }
        }
    }
    p->restore();
}

void DocxTextArea::ensureLaid(int i) {
    if (!m_ctl || i < 0 || i >= int(m_lay.size())) return;
    //  Ein Zellblock ohne Layout heißt: der Anker seiner Tabelle ist nicht (mehr)
    //  ausgelegt. Der Anker baut ALLE Zellen — einzeln ginge es nicht, weil die
    //  Zeilenhöhe von den Nachbarzellen abhängt.
    {
        const int anchor = tableAnchorOf(i);
        if (anchor >= 0 && anchor != i) {
            //  Ein BILD-Absatz in der Zelle hat bewusst kein QTextLayout — ohne
            //  diese Ausnahme würde hier bei jedem Aufruf die ganze Tabelle neu
            //  ausgelegt (paint ruft ensureLaid je Block).
            if (!m_lay[size_t(anchor)].table
                || (!m_lay[size_t(i)].layout && !m_lay[size_t(i)].isImage))
                ensureLaid(anchor);
            return;
        }
    }
    if (!m_lay[i].laid
        || (!m_lay[i].layout && !m_lay[i].isImage && !m_lay[i].isToc
            && m_ctl->doc().blocks.at(i).kind == Block::Paragraph)
        || (m_lay[i].isTable && !m_lay[i].table)
        || (m_lay[i].isImage && m_lay[i].image.isNull() && m_lay[i].imageBoxW > 0.0)) {
        const qreal oldH = m_lay[i].height;
        buildLayout(i);
        if (!qFuzzyCompare(oldH + 1, m_lay[i].height + 1))
            m_offsetsValidTo = qMin(m_offsetsValidTo, i);
    }
}

//  Verteilt EINEN Block auf Spalten-Slots. Rückgabe: Fluss-y direkt nach ihm.
//  Ein Absatz, der die Slot-Grenze überschreitet, wird ZEILENWEISE getrennt
//  (mehrere Segmente) — genau das macht die Ansicht seitengenau. Ist er nicht
//  trennbar (Platzhalter, noch nicht vermessen, einzeilig), wandert er
//  vollständig in den nächsten Slot; ist er höher als ein Slot, läuft er über
//  (der Fluss bleibt dabei monoton, s. Koordinaten-Kommentar im Header).
qreal DocxTextArea::paginateBlock(int idx, qreal flowStart, qreal slotH) {
    //  Ein Block, der die Slot-Grenze überschreitet, muss zeilenweise getrennt
    //  werden KÖNNEN — dafür braucht er sein Layout. `trimLayouts` gibt Layouts
    //  außerhalb des Fensters frei; hinge die Trennung daran, ob eines gerade
    //  da ist, wäre die Seitenzahl von Lauf zu Lauf eine andere (im Prüfstand
    //  beobachtet: 18 vs. 19 Seiten fürs selbe Dokument). Deshalb wird hier
    //  notfalls nachvermessen — das trifft ~einen Block je Seitengrenze, nicht
    //  das Dokument.
    {
        const BlockLayout& L0 = m_lay[size_t(idx)];
        const int slot0 = int(flowStart / slotH);
        const qreal yInSlot0 = flowStart - slot0 * slotH;
        if (L0.height > 0.0 && !L0.layout && yInSlot0 + L0.height > slotH
            && m_ctl && m_ctl->ready()
            && m_ctl->doc().blocks.at(idx).kind == Block::Paragraph)
            ensureLaid(idx);
    }
    BlockLayout& L = m_lay[size_t(idx)];
    L.segs.clear();
    const int nCols = colCount();
    int   slot = int(flowStart / slotH);
    qreal y    = flowStart - slot * slotH;
    const qreal h = L.height;

    //  Unsichtbare Blöcke (w:sectPr, Kommentare) belegen keinen Platz.
    if (h <= 0.0) {
        L.segs.append({ slot, 0, y });
        return flowStart;
    }

    const int nLines = L.layout ? L.layout->lineCount() : 0;
    const QString text = L.layout ? L.layout->text() : QString();
    const bool hasExplicitBreak = text.contains(kPageBreak);

    //  Einfachster und häufigster Fall: passt komplett, kein erzwungener Umbruch.
    if (y + h <= slotH && !hasExplicitBreak) {
        L.segs.append({ slot, 0, y });
        return slot * slotH + y + h;
    }

    //  Nicht trennbar → ganz in den nächsten Slot.
    if (nLines <= 1) {
        if (y > 0.0) { ++slot; y = 0.0; }
        L.segs.append({ slot, 0, y });
        return slot * slotH + y + h;
    }

    //  Zeilenweise verteilen. `segFirstY` ist die Layout-y der ersten Zeile des
    //  laufenden Segments — daraus ergibt sich die Slot-y jeder Folgezeile.
    L.segs.append({ slot, 0, y });
    qreal segTop = y;
    qreal segFirstY = 0.0;                 // Zeile 0 liegt bei y = beforePx
    for (int li = 0; li < nLines; ++li) {
        const QTextLine ln = L.layout->lineAt(li);
        const qreal lnTop = segTop + (ln.y() - segFirstY);
        const qreal lnBot = lnTop + ln.height();
        const bool first  = (li == L.segs.constLast().firstLine);
        if (lnBot > slotH && !(first && segTop <= 0.0)) {
            //  Zeile passt nicht mehr: neues Segment im nächsten Slot. Steht das
            //  laufende Segment noch bei seiner ersten Zeile, wird es komplett
            //  verschoben statt ein leeres zurückzulassen.
            ++slot;
            segTop = 0.0;
            segFirstY = ln.y();
            if (first) L.segs.last() = { slot, li, segTop };
            else       L.segs.append({ slot, li, segTop });
        }
        //  Erzwungener Seitenumbruch: alles NACH dieser Zeile beginnt auf der
        //  nächsten SEITE (nicht nur in der nächsten Spalte).
        if (hasExplicitBreak && li + 1 < nLines) {
            const int st = ln.textStart(), len = ln.textLength();
            if (text.indexOf(kPageBreak, st) >= 0
                && text.indexOf(kPageBreak, st) < st + len) {
                slot = (slot / nCols + 1) * nCols;
                segTop = 0.0;
                const QTextLine nxt = L.layout->lineAt(li + 1);
                segFirstY = nxt.y();
                L.segs.append({ slot, li + 1, segTop });
            }
        }
    }

    //  Fluss-y hinter dem letzten Segment: verbrauchte Layout-Höhe plus der
    //  Abstand DANACH (der steckt in L.height, nicht in den Zeilen).
    const QTextLine lastLine = L.layout->lineAt(nLines - 1);
    const qreal layoutBottom = lastLine.y() + lastLine.height();
    const qreal trailing = qMax(0.0, h - layoutBottom);
    const PageSeg& last = L.segs.constLast();
    qreal lastFirstY = 0.0;
    if (last.firstLine > 0 && last.firstLine < nLines)
        lastFirstY = L.layout->lineAt(last.firstLine).y();
    return last.slot * slotH + last.yInSlot + (layoutBottom - lastFirstY) + trailing;
}

void DocxTextArea::ensureOffsetsTo(int i) {
    i = qMin(i, int(m_lay.size()));
    if (m_offsets.size() != int(m_lay.size()) + 1) {
        m_offsets.resize(int(m_lay.size()) + 1);
        m_offsetsValidTo = 0;
    }
    if (m_offsets.isEmpty()) return;
    const qreal slotH = slotHeight();
    if (m_offsetsValidTo == 0) {
        m_offsets[0] = 0.0;
        if (!m_lay.empty()) m_offsets[1] = paginateBlock(0, 0.0, slotH);
        m_offsetsValidTo = qMin(1, int(m_lay.size()));
    }
    for (int k = m_offsetsValidTo + 1; k <= i; ++k)
        m_offsets[k] = paginateBlock(k - 1, m_offsets[k - 1], slotH);
    m_offsetsValidTo = qMax(m_offsetsValidTo, i);

    //  Seitenzahl steht erst fest, wenn der ganze Fluss paginiert ist.
    if (m_offsetsValidTo >= int(m_lay.size()) && !m_lay.empty()) {
        const PageSeg last = m_lay.back().segs.isEmpty()
                                 ? PageSeg() : m_lay.back().segs.constLast();
        const int pages = qMax(1, last.slot / colCount() + 1);
        if (pages != m_pageCount) {
            m_pageCount = pages;
            emit pageCountChanged();
        }
    }
}

qreal DocxTextArea::blockTop(int i) {
    ensureOffsetsTo(i);
    return (i >= 0 && i < m_offsets.size()) ? m_offsets[i] : kPadV;
}

int DocxTextArea::blockAtY(qreal y) {
    if (m_lay.empty()) return -1;
    ensureOffsetsTo(int(m_lay.size()));
    //  Binäre Suche über die Präfix-Offsets.
    int lo = 0, hi = int(m_lay.size()) - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (m_offsets[mid] <= y) lo = mid; else hi = mid - 1;
    }
    return lo;
}

void DocxTextArea::layoutChunk() {
    if (!m_ctl || !m_ctl->ready()) { m_chunkTimer.stop(); return; }
    int done = 0;
    while (m_layChunkAt < int(m_lay.size()) && done < kChunk) {
        if (!m_lay[m_layChunkAt].laid) { buildLayout(m_layChunkAt); ++done; }
        ++m_layChunkAt;
    }
    updateContentHeight();
    if (m_layChunkAt >= int(m_lay.size())) {
        m_chunkTimer.stop();
        updateCursorRect();
    }
    //  Schon WAEHREND des Initial-Layouts trimmen: sonst laegen bei einem
    //  400-Seiten-Dokument kurzzeitig alle Layouts gleichzeitig im Speicher
    //  (genau der Spitzenwert, den das Fenster vermeiden soll). m_trimLo wird
    //  zurueckgesetzt, damit die gerade neu gebauten Layouts erfasst werden.
    m_trimLo = -1;
    trimLayouts(blockAtY(m_contentY), blockAtY(m_contentY + height()));
    update();
}

void DocxTextArea::trimLayouts(int firstVisible, int lastVisible) {
    const int n = int(m_lay.size());
    if (n <= kLayoutCap)
        return;                       // kleine Dokumente zahlen hier gar nichts

    const int lo = qMax(0, firstVisible - kKeepMargin);
    const int hi = qMin(n - 1, lastVisible + kKeepMargin);
    if (lo == m_trimLo && hi == m_trimHi)
        return;                       // Fenster unveraendert → nichts zu tun
    m_trimLo = lo;
    m_trimHi = hi;

    //  Cursor-Block ausnehmen: updateCursorRect/moveCursorVertical brauchen sein
    //  Layout bei jedem Tastendruck — ihn wegzuwerfen kostete mehr, als er belegt.
    const int cursorBlock = (m_ctl && m_ctl->ready()) ? m_ctl->cursor().block : -1;
    for (int i = 0; i < n; ++i) {
        if ((i >= lo && i <= hi) || i == cursorBlock)
            continue;
        //  NUR das Layout freigeben — `laid`/`height` bleiben gueltig, damit
        //  weder Praefix-Offsets noch die Inhaltshoehe neu berechnet werden.
        //  Fuer Tabellen gilt dasselbe: das Gitter-Layout haelt je Zelle
        //  QTextLayouts und ist damit der groesste Einzelposten im Fenster.
        m_lay[i].layout.reset();
        m_lay[i].table.reset();
        //  Das eingepasste Bild ist der groesste Einzelposten je Block.
        //  `imageBoxW/H` bleiben stehen, die Hoehe aendert sich also nicht.
        if (m_lay[i].isImage) m_lay[i].image = QImage();
    }
}

void DocxTextArea::updateContentHeight() {
    ensureOffsetsTo(int(m_lay.size()));
    //  Scrollbare Höhe = SEITENSTAPEL in Item-Pixeln (nicht die Fluss-Höhe:
    //  die kennt weder Seitenränder noch die Lücken zwischen den Seiten).
    const qreal h = docHeight() * m_scale;
    if (!qFuzzyCompare(m_contentHeight + 1, h + 1)) {
        m_contentHeight = h;
        emit contentHeightChanged();
        //  Klemmen, falls der Inhalt geschrumpft ist.
        setContentY(m_contentY);
    }
}

//  Ein leerer Absatz wird mit dem am Cursor geltenden Format vermessen
//  (s. buildLayout) — ändert sich dieser Bezug, muss sein Layout neu entstehen.
void DocxTextArea::invalidateEmptyBlock(int i) {
    if (!m_ctl || !m_ctl->ready() || i < 0 || i >= int(m_lay.size()))
        return;
    const Block& b = m_ctl->doc().blocks.at(i);
    if (b.kind != Block::Paragraph || b.textLength() != 0)
        return;
    m_lay[i].laid = false;
    m_lay[i].layout.reset();
    ensureLaid(i);
    updateContentHeight();
}

void DocxTextArea::updateCursorRect() {
    if (!m_ctl || !m_ctl->ready() || m_lay.empty()) {
        m_cursorRect = QRectF();
        emit cursorRectChanged();
        return;
    }
    const DocxCursor& c = m_ctl->cursor();
    const int bi = qBound(0, c.block, int(m_lay.size()) - 1);
    ensureLaid(bi);
    const BlockLayout& L = m_lay[bi];

    //  Höhe/Grundlinie aus dem am Cursor WIRKSAMEN Zeichenformat (inkl.
    //  Pending) statt aus der Zeilenhöhe: Die Zeile ist so hoch wie ihr
    //  größtes Zeichen — der Caret muss aber die Größe zeigen, in der das
    //  NÄCHSTE Zeichen erscheint. Genau das fehlte (Nutzerbefund: „Caret wird
    //  bei Schriftgrößen-Wechsel nicht aktualisiert").
    const RunFmt cf = m_ctl->caretFormat();
    QFont f;
    f.setFamily(cf.font.isEmpty() ? m_ctl->doc().defaultRun().font : cf.font);
    f.setPointSizeF(cf.sizePt > 0 ? cf.sizePt : m_ctl->doc().defaultRun().sizePt);
    f.setBold(cf.bold);
    f.setItalic(cf.italic);
    const QFontMetricsF fm(f);

    //  Caret in DOKUMENT-Koordinaten aufbauen (Seite + Spalte des Cursors, s.
    //  Segmente) und erst am Ende in Item-Pixel umrechnen — die Property wird
    //  von QML zum Mitscrollen benutzt und muss dieselbe Einheit wie contentY
    //  haben.
    const int lineIdx0 = (L.layout && L.layout->lineCount() > 0)
                             ? qMax(0, L.layout->lineForTextPosition(
                                            qBound(0, c.pos, L.layout->text().size()))
                                            .lineNumber())
                             : 0;
    qreal x = docXForBlock(bi, lineIdx0);
    qreal y = docYForLine(bi, 0) + L.beforePx;
    qreal h = fm.height();
    if (L.layout && L.layout->lineCount() > 0) {
        QTextLine line = L.layout->lineForTextPosition(
            qBound(0, c.pos, L.layout->text().size()));
        if (!line.isValid()) line = L.layout->lineAt(L.layout->lineCount() - 1);
        x += line.cursorToX(qBound(0, c.pos, L.layout->text().size()));
        //  An der GRUNDLINIE der Zeile ausrichten (nicht an der Zeilenoberkante)
        //  — sonst „schwebt" ein kleiner Caret in einer hohen Mischzeile.
        y = docYForLine(bi, line.lineNumber()) + line.ascent() - fm.ascent();
    }
    //  Gespeichert in DOKUMENT-Pixeln (paint zeichnet darin); die Properties
    //  cursorY/cursorH rechnen für QML in Item-Pixel um.
    m_cursorRect = QRectF(x, y, 1.6, h);
    emit cursorRectChanged();
    updateImageSelection();
    if (currentPage() != m_lastPage) {
        m_lastPage = currentPage();
        emit currentPageChanged();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Bild-Auswahl (A2): der Cursorblock IST die Auswahl, wenn er ein reiner
//  Bild-Absatz ist. Das Rechteck wird in DOKUMENT-Pixeln gemerkt und erst in
//  den Gettern auf Item-Pixel umgerechnet — genau wie beim Caret, damit
//  Scrollen und Maßstabswechsel keinen Neuaufbau brauchen.
// ─────────────────────────────────────────────────────────────────────────────
void DocxTextArea::updateImageSelection() {
    int b = -1;
    QRectF r;
    if (m_ctl && m_ctl->ready() && !m_lay.empty()) {
        const int bi = qBound(0, m_ctl->cursor().block, int(m_lay.size()) - 1);
        ensureLaid(bi);
        const BlockLayout& L = m_lay[size_t(bi)];
        if (L.isImage && L.imageBoxW > 0.0 && L.imageBoxH > 0.0) {
            b = bi;
            r = QRectF(docXForBlock(bi, 0), docYForLine(bi, 0),
                       L.imageBoxW, L.imageBoxH);
        }
    }
    //  Tabelle: Rechteck des ANKERS (er trägt Gitter und Gesamthöhe).
    int tid = -1;
    QRectF tr;
    if (m_ctl && m_ctl->ready() && !m_lay.empty()) {
        const int bi = qBound(0, m_ctl->cursor().block, int(m_lay.size()) - 1);
        const int anchor = tableAnchorOf(bi);
        if (anchor >= 0 && anchor < int(m_lay.size())) {
            ensureLaid(anchor);
            const BlockLayout& A = m_lay[size_t(anchor)];
            if (A.isTable && A.table && A.table->width > 0.0) {
                tid = m_ctl->doc().blocks.at(anchor).tableId;
                tr = QRectF(flowDocXForBlock(anchor, 0), flowDocYForLine(anchor, 0),
                            A.table->width, qMax(1.0, A.height - 6.0));
            }
        }
    }
    if (b == m_imgSelBlock && r == m_imgSelDoc && tid == m_tblSelId && tr == m_tblSelDoc)
        return;
    m_imgSelBlock = b;
    m_imgSelDoc   = r;
    m_tblSelId    = tid;
    m_tblSelDoc   = tr;
    emit imageSelectionChanged();
}

int   DocxTextArea::selTableId() const { return m_tblSelId; }
qreal DocxTextArea::selTableX() const {
    return itemOffsetX() + m_tblSelDoc.x() * m_scale;
}
qreal DocxTextArea::selTableY() const {
    return m_tblSelDoc.y() * m_scale - m_contentY;
}
qreal DocxTextArea::selTableW() const { return m_tblSelDoc.width() * m_scale; }
qreal DocxTextArea::selTableH() const { return m_tblSelDoc.height() * m_scale; }

//  Waagerechter Versatz der Seite im Item — identisch zu paint().
qreal DocxTextArea::itemOffsetX() const {
    return qMax(kSideMargin * m_scale, (width() - pageWpx() * m_scale) / 2.0);
}

int   DocxTextArea::selImageBlock() const { return m_imgSelBlock; }
qreal DocxTextArea::selImageX() const {
    return itemOffsetX() + m_imgSelDoc.x() * m_scale;
}
qreal DocxTextArea::selImageY() const {
    return m_imgSelDoc.y() * m_scale - m_contentY;
}
qreal DocxTextArea::selImageW() const { return m_imgSelDoc.width() * m_scale; }
qreal DocxTextArea::selImageH() const { return m_imgSelDoc.height() * m_scale; }

// ─────────────────────────────────────────────────────────────────────────────
//  Zeichnen
// ─────────────────────────────────────────────────────────────────────────────
void DocxTextArea::paint(QPainter* p) {
    if (!m_ctl || !m_ctl->ready())
        return;
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::TextAntialiasing);
    p->fillRect(QRectF(0, 0, width(), height()), m_surroundColor);

    ensureOffsetsTo(int(m_lay.size()));

    //  Item-Pixel → Dokument-Pixel: waagerecht zentrieren, senkrecht scrollen,
    //  dann auf den Einpass-Maßstab stellen. Alles Weitere rechnet in
    //  Dokument-Pixeln (Seitengeometrie).
    const qreal s = m_scale;
    const qreal offX = qMax(kSideMargin * s, (width() - pageWpx() * s) / 2.0);
    p->save();
    p->translate(offX, -m_contentY);
    p->scale(s, s);

    //  Sichtbarer Bereich in Dokument-y.
    const qreal viewTop = m_contentY / s;
    const qreal viewBot = (m_contentY + height()) / s;

    const int firstPage = qBound(0, int((viewTop - kPadV) / (pageHpx() + kPageGap)),
                                 qMax(0, m_pageCount - 1));
    const int nCols = colCount();
    for (int page = firstPage; page < m_pageCount; ++page) {
        const qreal pTop = pageDocY(page);
        if (pTop > viewBot) break;
        if (pTop + pageHpx() < viewTop) continue;

        //  Papier mit Schattenkante — jetzt als EINZELNE Seite.
        const QRectF paper(0, pTop, pageWpx(), pageHpx());
        p->fillRect(paper.translated(3.0 / s, 3.0 / s), QColor(0, 0, 0, 45));
        p->fillRect(paper, QColor(255, 255, 255));
        p->setPen(QColor(0, 0, 0, 45));
        p->drawRect(paper);

        paintHeaderFooter(p, page);
        for (int c = 0; c < nCols; ++c)
            paintSlot(p, page * nCols + c, true);
    }
    p->restore();

    //  Layouts weit ausserhalb des Viewports freigeben (nur grosse Dokumente).
    const int fv = blockAtY(qMax(0.0, viewTop - pageDocY(firstPage)) + firstPage * nCols * slotHeight());
    trimLayouts(qMax(0, fv - nCols * 2), qMin(int(m_lay.size()) - 1, fv + nCols * 8));
}

//  Zeichnet die Blöcke EINES Spalten-Slots. Der Painter steht auf
//  Dokument-Pixeln; geclippt wird auf den Textbereich des Slots, damit ein
//  Absatz, der auf der nächsten Seite weiterläuft, hier nicht überstehen kann.
void DocxTextArea::paintSlot(QPainter* p, int slot, bool withCaret) {
    const Document& d = m_ctl->doc();
    const qreal slotH = slotHeight();
    const qreal sx = slotDocX(slot);
    const qreal sy = slotDocY(slot);
    const qreal cw = contentWidth();

    p->save();
    p->setClipRect(QRectF(sx - marLpx(), sy, pageWpx(), slotH), Qt::IntersectClip);

    //  Blöcke dieses Slots über den FLUSS finden (dort ist die Reihenfolge
    //  monoton, s. Koordinaten-Kommentar im Header).
    const qreal flowLo = slot * slotH;
    const qreal flowHi = flowLo + slotH;
    int i = blockAtY(flowLo);
    if (i < 0) { p->restore(); return; }

    int b1, p1, b2, p2;
    const DocxCursor& cur = m_ctl->cursor();
    b1 = cur.aBlock; p1 = cur.aPos; b2 = cur.block; p2 = cur.pos;
    if (b1 > b2 || (b1 == b2 && p1 > p2)) { std::swap(b1, b2); std::swap(p1, p2); }
    const bool hasSel = cur.hasSelection();
    const QColor selBg(38, 118, 216, 110);

    for (; i < int(m_lay.size()); ++i) {
        ensureLaid(i);
        const qreal flowTop = blockTop(i);
        if (flowTop >= flowHi) break;
        const BlockLayout& L = m_lay[i];
        if (L.height <= 0)
            continue;
        if (flowTop + L.height <= flowLo && L.segs.size() <= 1)
            continue;                       // liegt komplett vor diesem Slot
        //  Hat der Block in diesem Slot überhaupt ein Stück?
        int segIdx = -1;
        for (int k = 0; k < L.segs.size(); ++k)
            if (L.segs.at(k).slot == slot) { segIdx = k; break; }
        if (segIdx < 0)
            continue;
        const PageSeg& seg = L.segs.at(segIdx);
        const Block& b = d.blocks.at(i);
        //  Ursprung so setzen, dass die erste Zeile des Stücks an seiner
        //  Slot-y landet; ältere/spätere Zeilen fallen ins Clipping.
        qreal segLineY = 0.0;
        if (L.layout && seg.firstLine > 0 && seg.firstLine < L.layout->lineCount())
            segLineY = L.layout->lineAt(seg.firstLine).y();
        const qreal y = sy + seg.yInSlot - segLineY;
        const qreal left = sx;

        //  Reine Zellblöcke zeichnet der Anker mit (sie haben keine Fluss-Lage);
        //  der Anker selbst ist AUCH ein Zellblock und muss hier durch.
        if (L.isCell && !L.isTable)
            continue;
        //  Tabellen-Anker zeichnet Gitter UND — bei flach zerlegten Tabellen —
        //  den Text seiner Zellblöcke an deren expliziten Lagen.
        if (L.isTable && L.table) {
            paintTable(p, L, left, y);
            const Block& ab = d.blocks.at(i);
            if (ab.tableId >= 0) {
                const int tid = ab.tableId;
                for (int k = i; k < int(m_lay.size())
                                && k < d.blocks.size()
                                && d.blocks.at(k).tableId == tid; ++k) {
                    const BlockLayout& CB = m_lay[size_t(k)];
                    if (!CB.isCell) continue;
                    //  Bild-Absatz IN der Zelle (hat kein QTextLayout).
                    if (CB.isImage) {
                        const QRectF box(left + CB.cellRelX, y + CB.cellRelY,
                                         CB.imageBoxW, CB.imageBoxH);
                        if (!CB.image.isNull()) {
                            p->drawImage(box.topLeft(), CB.image);
                        } else {
                            p->setPen(QPen(QColor(150, 150, 150), 1, Qt::DashLine));
                            p->setBrush(QColor(0, 0, 0, 8));
                            p->drawRect(box);
                        }
                        continue;
                    }
                    if (!CB.layout) continue;
                    QList<QTextLayout::FormatRange> csel;
                    if (hasSel && k >= b1 && k <= b2) {
                        QTextLayout::FormatRange fr;
                        fr.start  = (k == b1) ? p1 : 0;
                        const int end = (k == b2) ? p2 : CB.layout->text().size();
                        fr.length = qMax(0, end - fr.start);
                        fr.format.setBackground(selBg);
                        if (fr.length > 0) csel.append(fr);
                    }
                    CB.layout->draw(p, QPointF(left + CB.cellRelX, y + CB.cellRelY),
                                    csel);
                }
            }
            continue;
        }
        if (b.kind == Block::OpaqueVisible) {
            //  Platzhalter (nicht deutbare Fremdblöcke) — Inhalt bleibt intakt.
            const QRectF r(left, y + 5, cw, 24);
            p->setPen(QPen(QColor(150, 150, 150), 1, Qt::DashLine));
            p->setBrush(QColor(0, 0, 0, 8));
            p->drawRoundedRect(r, 5, 5);
            p->setPen(QColor(110, 110, 110));
            QFont f; f.setPointSizeF(9.5); f.setItalic(true);
            p->setFont(f);
            p->drawText(r, Qt::AlignCenter, m_tablePlaceholder);
            continue;
        }
        //  Inhaltsverzeichnis: Einträge mit AKTUELLER Seitenzahl.
        if (L.isToc) {
            paintToc(p, L, left, y, cw);
            continue;
        }
        //  Bild-Absatz: das eingepasste QImage (oder ein Rahmen, wenn die
        //  Beziehung/das Format unbekannt ist).
        if (L.isImage) {
            const QRectF box(left, y, L.imageBoxW, L.imageBoxH);
            if (!L.image.isNull()) {
                p->drawImage(box.topLeft(), L.image);
            } else {
                p->setPen(QPen(QColor(150, 150, 150), 1, Qt::DashLine));
                p->setBrush(QColor(0, 0, 0, 8));
                p->drawRect(box);
            }
            continue;
        }
        if (!L.layout)
            continue;

        //  Listenmarker.
        if (!L.marker.isEmpty() && L.layout->lineCount() > 0) {
            const RunFmt def = d.defaultRun();
            QFont mf; mf.setFamily(def.font); mf.setPointSizeF(def.sizePt);
            p->setFont(mf);
            p->setPen(QColor(30, 30, 30));
            const QTextLine l0 = L.layout->lineAt(0);
            p->drawText(QPointF(left + L.indentPx - QFontMetricsF(mf).horizontalAdvance(L.marker),
                                y + l0.y() + l0.ascent()),
                        L.marker);
        }

        //  Selektion des Blocks als FormatRange.
        QList<QTextLayout::FormatRange> sel;
        if (hasSel && i >= b1 && i <= b2) {
            QTextLayout::FormatRange fr;
            fr.start  = (i == b1) ? p1 : 0;
            const int end = (i == b2) ? p2 : L.layout->text().size();
            fr.length = qMax(0, end - fr.start);
            fr.format.setBackground(selBg);
            if (fr.length > 0) sel.append(fr);
        }
        L.layout->draw(p, QPointF(left + L.indentPx, y), sel);

        //  Seitenumbruch-Marker: gestrichelte Linie in der Zeile des Sentinels.
        const QString t = L.layout->text();
        for (int pb = t.indexOf(kPageBreak); pb >= 0; pb = t.indexOf(kPageBreak, pb + 1)) {
            const QTextLine line = L.layout->lineForTextPosition(pb);
            if (!line.isValid()) continue;
            const qreal ly = y + line.y() + line.height() + 2;
            p->setPen(QPen(QColor(140, 140, 150), 1, Qt::DashLine));
            p->drawLine(QPointF(left, ly), QPointF(left + cw, ly));
            QFont f; f.setPointSizeF(8.0);
            p->setFont(f);
            p->setPen(QColor(130, 130, 140));
            p->drawText(QPointF(left + cw / 2 - 30, ly - 3), m_pageBreakLabel);
        }
    }

    //  Caret — nur in dem Slot, in dem der Cursor auch steht (m_cursorRect
    //  liegt in Dokument-Pixeln, der Painter ebenfalls).
    if (withCaret && m_caretOn && hasActiveFocus() && !hasSel
        && !m_cursorRect.isNull()
        && m_cursorRect.y() >= sy - 1.0 && m_cursorRect.y() < sy + slotH
        && m_cursorRect.x() >= sx - marLpx()) {
        p->fillRect(m_cursorRect, QColor(20, 20, 20));
    }
    p->restore();
}

//  Eine EINZELNE Seite in ein Zielrechteck malen — der Weg der Miniaturen.
//  Bewusst derselbe Zeichencode wie paint() (keine zweite Darstellung, kein
//  Bild-Cache): das Zielrechteck bestimmt nur den Maßstab.
void DocxTextArea::paintPageInto(QPainter* p, int page, const QRectF& target) {
    if (!m_ctl || !m_ctl->ready() || pageWpx() <= 0.0 || pageHpx() <= 0.0)
        return;
    ensureOffsetsTo(int(m_lay.size()));
    if (page < 0 || page >= m_pageCount)
        return;

    const qreal s = qMin(target.width() / pageWpx(), target.height() / pageHpx());
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::TextAntialiasing);
    p->translate(target.x(), target.y());
    p->scale(s, s);
    p->translate(0, -pageDocY(page));

    const QRectF paper(0, pageDocY(page), pageWpx(), pageHpx());
    p->fillRect(paper, QColor(255, 255, 255));
    p->setPen(QColor(0, 0, 0, 60));
    p->drawRect(paper);

    paintHeaderFooter(p, page);
    const int nCols = colCount();
    for (int c = 0; c < nCols; ++c)
        paintSlot(p, page * nCols + c, false);   // Miniatur ohne Caret
    p->restore();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Maus
// ─────────────────────────────────────────────────────────────────────────────
void DocxTextArea::hitTest(const QPointF& itemPos, int* block, int* pos) {
    *block = -1; *pos = 0;
    if (!m_ctl || m_lay.empty()) return;
    ensureOffsetsTo(int(m_lay.size()));

    //  Item-Pixel → Dokument-Pixel (Umkehrung der Transformation in paint()).
    const qreal s = qMax(0.05, m_scale);
    const qreal offX = qMax(kSideMargin * s, (width() - pageWpx() * s) / 2.0);
    const qreal docX = (itemPos.x() - offX) / s;
    const qreal docY = (itemPos.y() + m_contentY) / s;

    //  Seite und Spalte aus der Dokument-Position ableiten, daraus den Slot —
    //  erst der Slot macht aus dem Klick eine Stelle im FLUSS.
    const qreal pageStride = pageHpx() + kPageGap;
    const int page = qBound(0, int((docY - kPadV) / pageStride), qMax(0, m_pageCount - 1));
    const int nCols = colCount();
    int col = 0;
    if (nCols > 1) {
        const qreal colStride = contentWidth() + colSpacePx();
        col = qBound(0, int((docX - marLpx() + colSpacePx() / 2.0) / colStride), nCols - 1);
    }
    const int slot = page * nCols + col;
    const qreal slotH = slotHeight();

    //  ── ZWEITE Verfeinerungsstufe: liegt der Klick IN einer Tabelle? ─────────
    //  Zellblöcke haben keine eigene Fluss-Lage (die Tabelle hängt am Anker), ihre
    //  Dokument-Lage steht aber explizit. Es genügt also, die Zellen der
    //  sichtbaren Tabelle direkt zu prüfen: Zeile über y, Zelle über x — und
    //  zwar BEVOR die Fluss-Suche greift, die hier nur den Anker fände.
    for (int t = 0; t < m_ctl->doc().tables().size(); ++t) {
        const int anchor = m_ctl->doc().tableFirstBlock(t);
        if (anchor < 0 || anchor >= int(m_lay.size())) continue;
        const BlockLayout& A = m_lay[size_t(anchor)];
        if (!A.isTable || !A.table) continue;
        const qreal ax = flowDocXForBlock(anchor, 0);
        const qreal ay = flowDocYForLine(anchor, 0);
        if (docY < ay || docY > ay + A.height) continue;
        //  Nächstgelegener Zellblock dieser Tabelle (rechteckige Trefferprüfung,
        //  sonst der mit dem kleinsten Abstand — ein Klick in den Zellrand soll
        //  nicht ins Leere gehen).
        int best = -1;
        qreal bestDist = 0.0;
        const int tid = t;
        for (int k = anchor; k < int(m_lay.size())
                             && k < m_ctl->doc().blocks.size()
                             && m_ctl->doc().blocks.at(k).tableId == tid; ++k) {
            const BlockLayout& CB = m_lay[size_t(k)];
            if (!CB.isCell || (!CB.layout && !CB.isImage)) continue;
            qreal h = 0.0;
            if (CB.isImage) {
                h = CB.imageBoxH;            // Bild-Absatz hat kein QTextLayout
            } else {
                for (int ln = 0; ln < CB.layout->lineCount(); ++ln)
                    h = qMax(h, CB.layout->lineAt(ln).y() + CB.layout->lineAt(ln).height());
            }
            const QRectF r(ax + CB.cellRelX, ay + CB.cellRelY,
                           qMax(4.0, CB.cellW), qMax(4.0, h));
            const qreal dx = qMax(qMax(r.left() - docX, docX - r.right()), 0.0);
            const qreal dy = qMax(qMax(r.top() - docY, docY - r.bottom()), 0.0);
            const qreal dist = dx * dx + dy * dy;
            if (best < 0 || dist < bestDist) { best = k; bestDist = dist; }
        }
        if (best >= 0) {
            const BlockLayout& CB = m_lay[size_t(best)];
            *block = best;
            *pos = 0;
            if (CB.layout && CB.layout->lineCount() > 0) {
                const qreal localY = docY - (ay + CB.cellRelY);
                QTextLine line = CB.layout->lineAt(0);
                for (int li = 0; li < CB.layout->lineCount(); ++li) {
                    const QTextLine l = CB.layout->lineAt(li);
                    line = l;
                    if (localY < l.y() + l.height()) break;
                }
                *pos = line.xToCursor(docX - (ax + CB.cellRelX));
            }
            return;
        }
    }
    //  y im Textbereich des Slots, geklemmt: ein Klick in den Seitenrand
    //  landet auf der nächstgelegenen Zeile statt ins Leere.
    const qreal yInSlot = qBound(0.0, docY - slotDocY(slot), slotH);
    const qreal cy = slot * slotH + yInSlot;
    int bi = blockAtY(cy);
    //  Nur editierbare Absätze sind Cursor-Ziele; zum nächsten suchen.
    const Document& d = m_ctl->doc();
    auto editable = [&](int i) {
        return i >= 0 && i < d.blocks.size()
               && d.blocks.at(i).kind == Block::Paragraph;
    };
    if (!editable(bi)) {
        int up = bi, down = bi;
        while (up >= 0 && !editable(up)) --up;
        while (down < d.blocks.size() && !editable(down)) ++down;
        bi = editable(down) ? down : up;
        if (!editable(bi)) return;
    }
    ensureLaid(bi);
    const BlockLayout& L = m_lay[bi];
    *block = bi;
    if (!L.layout || L.layout->lineCount() == 0) { *pos = 0; return; }
    //  Dokument-y → Layout-y des Blocks: über das Segment, das an dieser Stelle
    //  liegt (bei einem über die Seitengrenze getrennten Absatz gehört dieselbe
    //  Layout-Zeile zu einer anderen Slot-y).
    int segIdx = 0;
    for (int k = 0; k < L.segs.size(); ++k)
        if (L.segs.at(k).slot <= slot) segIdx = k;
    const PageSeg seg = L.segs.isEmpty() ? PageSeg() : L.segs.at(segIdx);
    qreal segLineY = 0.0;
    if (seg.firstLine > 0 && seg.firstLine < L.layout->lineCount())
        segLineY = L.layout->lineAt(seg.firstLine).y();
    const qreal localY = (docY - (slotDocY(seg.slot) + seg.yInSlot)) + segLineY;

    QTextLine line = L.layout->lineAt(0);
    for (int li = 0; li < L.layout->lineCount(); ++li) {
        const QTextLine l = L.layout->lineAt(li);
        line = l;
        if (localY < l.y() + l.height())
            break;
    }
    *pos = line.xToCursor(docX - slotDocX(seg.slot) - L.indentPx);
}

void DocxTextArea::mousePressEvent(QMouseEvent* e) {
    forceActiveFocus();
    int b, p;
    hitTest(e->position(), &b, &p);

    if (e->button() == Qt::RightButton) {
        //  Wie in Word: der Rechtsklick verschiebt den Cursor NUR, wenn er
        //  außerhalb der bestehenden Selektion liegt — sonst bliebe das Menü
        //  ohne die Auswahl, auf die es sich beziehen soll.
        if (b >= 0) {
            const DocxCursor& c = m_ctl->cursor();
            int b1 = c.aBlock, b2 = c.block;
            if (b1 > b2) std::swap(b1, b2);
            if (!c.hasSelection() || b < b1 || b > b2)
                m_ctl->setCursor(b, p, false);
        }
        emit contextMenuRequested(e->position().x(), e->position().y(), b);
        e->accept();
        return;
    }

    if (b >= 0) {
        m_ctl->setCursor(b, p, e->modifiers() & Qt::ShiftModifier);
        m_selecting = true;
        m_goalX = -1;
    }
    e->accept();
}
void DocxTextArea::mouseMoveEvent(QMouseEvent* e) {
    if (!m_selecting) return;
    int b, p;
    hitTest(e->position(), &b, &p);
    if (b >= 0)
        m_ctl->setCursor(b, p, true);
    e->accept();
}
void DocxTextArea::mouseReleaseEvent(QMouseEvent* e) {
    m_selecting = false;
    e->accept();
}
void DocxTextArea::mouseDoubleClickEvent(QMouseEvent* e) {
    int b, p;
    hitTest(e->position(), &b, &p);
    if (b >= 0)
        m_ctl->selectWordAt(b, p);
    e->accept();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tastatur
// ─────────────────────────────────────────────────────────────────────────────
void DocxTextArea::moveCursorVertical(int dir, bool keepAnchor) {
    const DocxCursor& c = m_ctl->cursor();
    const int bi = c.block;
    ensureLaid(bi);
    const BlockLayout& L = m_lay[bi];
    if (!L.layout) return;
    QTextLine line = L.layout->lineForTextPosition(c.pos);
    if (!line.isValid() && L.layout->lineCount() > 0)
        line = L.layout->lineAt(0);
    if (m_goalX < 0 && line.isValid())
        m_goalX = line.cursorToX(c.pos);
    const int li = line.isValid() ? line.lineNumber() : 0;
    const Document& d = m_ctl->doc();
    auto editable = [&](int i) {
        return i >= 0 && i < d.blocks.size()
               && d.blocks.at(i).kind == Block::Paragraph;
    };
    if (dir < 0) {
        if (li > 0) {
            const QTextLine up = L.layout->lineAt(li - 1);
            m_ctl->setCursor(bi, up.xToCursor(m_goalX), keepAnchor);
            return;
        }
        int pb = bi - 1;
        while (pb >= 0 && !editable(pb)) --pb;
        if (pb < 0) return;
        ensureLaid(pb);
        const BlockLayout& P = m_lay[pb];
        int pos = m_ctl->doc().blocks.at(pb).textLength();
        if (P.layout && P.layout->lineCount() > 0)
            pos = P.layout->lineAt(P.layout->lineCount() - 1).xToCursor(m_goalX);
        m_ctl->setCursor(pb, pos, keepAnchor);
    } else {
        if (L.layout && li < L.layout->lineCount() - 1) {
            const QTextLine dn = L.layout->lineAt(li + 1);
            m_ctl->setCursor(bi, dn.xToCursor(m_goalX), keepAnchor);
            return;
        }
        int nb = bi + 1;
        while (nb < d.blocks.size() && !editable(nb)) ++nb;
        if (nb >= d.blocks.size()) {
            //  Letzte Zeile des letzten Absatzes: ↓ springt ans ZEILENENDE
            //  (statt wirkungslos zu bleiben) — einheitlich mit den übrigen
            //  Editoren der App.
            m_ctl->setCursor(bi, INT_MAX, keepAnchor);
            return;
        }
        ensureLaid(nb);
        const BlockLayout& N = m_lay[nb];
        int pos = 0;
        if (N.layout && N.layout->lineCount() > 0)
            pos = N.layout->lineAt(0).xToCursor(m_goalX);
        m_ctl->setCursor(nb, pos, keepAnchor);
    }
}

void DocxTextArea::keyPressEvent(QKeyEvent* e) {
    if (!m_ctl || !m_ctl->ready()) { e->ignore(); return; }
    const bool ctrl  = e->modifiers() & Qt::ControlModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const DocxCursor& c = m_ctl->cursor();

    //  Esc verlässt die Kopf-/Fußzeilen-Bearbeitung (wie in Word). Im Körper
    //  wird die Taste NICHT verbraucht — dort schließt sie den Viewer.
    if (e->key() == Qt::Key_Escape
        && m_ctl->activeRegionInt() != DocxEditController::Body) {
        m_ctl->setRegion(DocxEditController::Body);
        e->accept();
        return;
    }

    if (ctrl) {
        switch (e->key()) {
        case Qt::Key_S: emit saveRequested();          e->accept(); return;
        case Qt::Key_A: m_ctl->selectAll();            e->accept(); return;
        case Qt::Key_C: m_ctl->copy();                 e->accept(); return;
        case Qt::Key_X: m_ctl->cut();                  e->accept(); return;
        case Qt::Key_V: m_ctl->paste();                e->accept(); return;
        case Qt::Key_Z: shift ? m_ctl->redo() : m_ctl->undo(); e->accept(); return;
        case Qt::Key_Y: m_ctl->redo();                 e->accept(); return;
        case Qt::Key_B: m_ctl->toggleBold();           e->accept(); return;
        case Qt::Key_I: m_ctl->toggleItalic();         e->accept(); return;
        case Qt::Key_U: m_ctl->toggleUnderline();      e->accept(); return;
        default: break;
        }
    }
    m_goalX = (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) ? m_goalX : -1;
    switch (e->key()) {
    case Qt::Key_Left:
        if (c.pos > 0) m_ctl->setCursor(c.block, c.pos - 1, shift);
        else if (c.block > 0) m_ctl->setCursor(c.block - 1, INT_MAX, shift);
        e->accept(); return;
    case Qt::Key_Right: {
        const int len = m_ctl->doc().blocks.at(c.block).textLength();
        if (c.pos < len) m_ctl->setCursor(c.block, c.pos + 1, shift);
        else if (c.block < m_ctl->doc().blocks.size() - 1)
            m_ctl->setCursor(c.block + 1, 0, shift);
        e->accept(); return;
    }
    case Qt::Key_Up:   moveCursorVertical(-1, shift); e->accept(); return;
    case Qt::Key_Down: moveCursorVertical(+1, shift); e->accept(); return;
    case Qt::Key_Home: m_ctl->setCursor(c.block, 0, shift);       e->accept(); return;
    case Qt::Key_End:  m_ctl->setCursor(c.block, INT_MAX, shift); e->accept(); return;
    case Qt::Key_Backspace: m_ctl->deleteBackward();   e->accept(); return;
    case Qt::Key_Delete:    m_ctl->deleteForward();    e->accept(); return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        shift ? m_ctl->insertLineBreak() : m_ctl->insertParagraphBreak();
        e->accept(); return;
    case Qt::Key_Tab:
        m_ctl->insertText(QStringLiteral("\t"));
        e->accept(); return;
    default: break;
    }
    const QString t = e->text();
    if (!t.isEmpty() && t.at(0).isPrint()) {
        m_ctl->insertText(t);
        e->accept();
        return;
    }
    e->ignore();
}

void DocxTextArea::inputMethodEvent(QInputMethodEvent* e) {
    //  Minimal: Commit-Strings (tote Tasten/IME) einfügen; Preedit wird nicht
    //  gesondert dargestellt (Grundgerüst).
    if (!e->commitString().isEmpty() && m_ctl && m_ctl->ready())
        m_ctl->insertText(e->commitString());
    e->accept();
}

QVariant DocxTextArea::inputMethodQuery(Qt::InputMethodQuery q) const {
    switch (q) {
    case Qt::ImEnabled:        return true;
    case Qt::ImCursorRectangle:
        return m_cursorRect.translated(0, -m_contentY);
    case Qt::ImHints:          return int(Qt::ImhMultiLine);
    default:                   return QQuickPaintedItem::inputMethodQuery(q);
    }
}

//  Solange diese Fläche den Fokus hat, gehört JEDE Taste, die Text erzeugt,
//  dem Editor — auch ohne Modifikator. Ohne das öffnete ein Tippen von „d" den
//  Datum-Editor des Viewers, statt ein „d" zu schreiben (Nutzerbefund).
//  Modifizierte Kürzel (Strg/Alt/Meta) bleiben unangetastet, damit Strg+S,
//  Alt+Q & Co. weiterhin die App erreichen.
bool DocxTextArea::event(QEvent* e) {
    if (e->type() == QEvent::ShortcutOverride && hasActiveFocus()) {
        auto* ke = static_cast<QKeyEvent*>(e);
        const bool modified = ke->modifiers()
                              & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        if (!modified && !ke->text().isEmpty() && ke->text().at(0).isPrint()) {
            e->accept();
            return true;
        }
    }
    return QQuickPaintedItem::event(e);
}

void DocxTextArea::geometryChange(const QRectF& n, const QRectF& o) {
    QQuickPaintedItem::geometryChange(n, o);
    if (!qFuzzyCompare(n.width() + 1, o.width() + 1)) {
        //  Die Zeilenbreite ist jetzt die TEXTBREITE DER SEITE und damit von der
        //  Kachelbreite unabhängig — ein Breitenwechsel ändert also nur noch den
        //  Einpass-Maßstab, KEIN Umbruch und kein Neu-Vermessen (früher wurde
        //  hier das komplette Dokument neu ausgelegt).
        updateScale();
    }
    emit imageSelectionChanged();     // Seite wandert waagerecht/skaliert
    update();
}

// ─────────────────────────────────────────────────────────────────────────────
//  DocxPageThumb
// ─────────────────────────────────────────────────────────────────────────────
DocxPageThumb::DocxPageThumb(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setOpaquePainting(false);
}

void DocxPageThumb::setArea(DocxTextArea* a) {
    if (m_area == a) return;
    if (m_area) disconnect(m_area, nullptr, this, nullptr);
    m_area = a;
    if (m_area) {
        //  Inhalt/Seitenzahl geändert → Miniatur neu zeichnen.
        connect(m_area, &DocxTextArea::documentChanged, this, [this]() { update(); });
        connect(m_area, &DocxTextArea::pageCountChanged, this, [this]() { update(); });
    }
    emit areaChanged();
    update();
}

void DocxPageThumb::setPage(int p) {
    if (m_page == p) return;
    m_page = p;
    emit pageChanged();
    update();
}

void DocxPageThumb::paint(QPainter* p) {
    if (!m_area || width() <= 1.0 || height() <= 1.0)
        return;
    m_area->paintPageInto(p, m_page, QRectF(0, 0, width(), height()));
}
