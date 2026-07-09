#include "PdfEditController.h"
#include "PdfEditCommands.h"
#include "ISettings.h"
#include "AppSettings.h"      // AppSettings::instance() für den Default-Ctor (QML-Instanzen)
#include "PathUtils.h"

#include <QPdfDocument>
#include <QPdfWriter>
#include <QPageSize>
#include <QPageLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QTextLayout>
#include <QTextOption>
#include <QImage>
#include <QFont>
#include <QFontInfo>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRunnable>
#include <QMetaObject>
#include <QCoreApplication>
#include <utility>

// ─────────────────────────────────────────────────────────────────────────────
//  PdfExportTask — rendert Original + Overlay in ein NEUES PDF (Worker-Thread).
//
//  Öffnet eine EIGENE QPdfDocument-Instanz (der PDFium-Render-Mutex der
//  sichtbaren Anzeige bleibt unberührt) und schreibt über QPdfWriter in eine
//  QSaveFile → der Commit ist ATOMAR (bei Fehlern entsteht keine halbe Datei);
//  Ziel ist stets eine NEUE Kopie — das Original wird nie angefasst.
//
//  KOORDINATEN/WYSIWYG: writer.setResolution(72) → 1 Painter-Einheit = 1
//  PDF-Punkt. Schriftgrößen werden als Punktgrößen gesetzt (72-dpi-Gerät:
//  1 pt = 1 Einheit) — exakt die Skala, mit der QML die Boxen anzeigt
//  (fontSizePt · Pixel-je-Punkt).
//
//  RAM: Seiten werden SEQUENZIELL verarbeitet; zu jedem Zeitpunkt existiert
//  genau EIN Seitenbild (kExportRenderDpi) transient. Keine PDF-Kopie im RAM.
//
//  Reihenfolge am Ende (Windows-fest): painter.end() → doc.close() (Lese-
//  Handle frei) → out.commit() (Rename über das ggf. identische Original).
// ─────────────────────────────────────────────────────────────────────────────
namespace {
class PdfExportTask : public QRunnable {
public:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    PdfExportTask(PdfEditController* owner, QString sourcePath, QString targetPath,
                  QVector<PdfEditBox> boxes, int generation, CancelFlag cancel)
        : m_owner(owner)
        , m_source(std::move(sourcePath))
        , m_target(std::move(targetPath))
        , m_boxes(std::move(boxes))
        , m_gen(generation)
        , m_cancel(std::move(cancel)) {
        setAutoDelete(true);
    }

    void run() override {
        QString err;
        const bool ok = writePdf(&err);

        PdfEditController* owner = m_owner;
        const QString target = m_target;
        const int gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, ok, target, err, gen]() {
            owner->exportTaskFinished(ok, target, err, gen);
        }, Qt::QueuedConnection);
    }

private:
    bool cancelled() const {
        return m_cancel && m_cancel->load(std::memory_order_relaxed);
    }

    void reportProgress(int done, int total) {
        PdfEditController* owner = m_owner;
        const int gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, done, total, gen]() {
            owner->exportTaskProgress(done, total, gen);
        }, Qt::QueuedConnection);
    }

    bool writePdf(QString* err) {
        QPdfDocument doc;   // Affinität: dieser Pool-Thread (wie PdfThumbRenderTask)
        if (doc.load(m_source) != QPdfDocument::Error::None
            || doc.status() != QPdfDocument::Status::Ready) {
            *err = QStringLiteral("PDF");
            return false;
        }
        const int pageCount = doc.pageCount();
        if (pageCount <= 0) {
            *err = QStringLiteral("0");
            return false;
        }

        QSaveFile out(m_target);
        if (!out.open(QIODevice::WriteOnly)) {
            *err = out.errorString();
            return false;
        }

        QPdfWriter writer(&out);
        writer.setResolution(72);                       // 1 Einheit = 1 PDF-Punkt
        writer.setCreator(QStringLiteral("MediaGallery"));
        writer.setTitle(QFileInfo(m_source).completeBaseName());

        QPainter p;
        for (int page = 0; page < pageCount; ++page) {
            if (cancelled()) {
                if (p.isActive()) p.end();
                out.cancelWriting();
                *err = QStringLiteral("cancel");
                return false;
            }

            QSizeF pts = doc.pagePointSize(page);
            if (pts.isEmpty())
                pts = QSizeF(612.0, 792.0);             // Fallback: US Letter

            writer.setPageSize(QPageSize(pts, QPageSize::Point,
                                         QString(), QPageSize::ExactMatch));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));

            if (page == 0) {
                if (!p.begin(&writer)) {
                    out.cancelWriting();
                    *err = QStringLiteral("begin");
                    return false;
                }
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setRenderHint(QPainter::TextAntialiasing, true);
                p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            } else if (!writer.newPage()) {
                p.end();
                out.cancelWriting();
                *err = QStringLiteral("newPage");
                return false;
            }

            // ── Basisseite als Bild (genau EIN Bild transient im RAM) ─────────
            const QSize px(qMax(1, qRound(pts.width()  / 72.0 * PdfEditController::kExportRenderDpi)),
                           qMax(1, qRound(pts.height() / 72.0 * PdfEditController::kExportRenderDpi)));
            const QImage img = doc.render(page, px);
            if (!img.isNull())
                p.drawImage(QRectF(QPointF(0, 0), pts), img);
            else
                p.fillRect(QRectF(QPointF(0, 0), pts), Qt::white);

            // ── Overlay-Boxen dieser Seite darüber ────────────────────────────
            for (const PdfEditBox& b : std::as_const(m_boxes))
                if (b.page == page)
                    drawBox(p, b);

            reportProgress(page + 1, pageCount);
        }

        p.end();
        doc.close();            // Lese-Handle VOR dem Commit freigeben (Windows)
        if (!out.commit()) {
            *err = out.errorString();
            return false;
        }
        return true;
    }

    // Zeichnet eine Notiz exakt wie die QML-Anzeige: Post-it-Optik (versetzter
    // Schatten, Papierfläche = highlight inkl. Alpha, Eselsohr unten rechts),
    // dann der Text via QTextLayout (gleiche Text-Engine wie QML TextEdit;
    // Umbruch WrapAtWordBoundaryOrAnywhere = TextEdit.Wrap). Vertikal je
    // b.vAlign OBEN (Standard, wie Word-Textfelder) oder ZENTRIERT. KEIN
    // Clipping — QML zeigt Überlauf ebenfalls an (clip:false) → WYSIWYG; bei
    // zentriertem Überlauf wird der Offset negativ (symmetrisch wie Anzeige).
    void drawBox(QPainter& p, const PdfEditBox& b) {
        p.save();

        const bool paper = b.highlight.alpha() > 0;
        if (paper) {
            // Schatten (Qt-PDF-Engine schreibt Konstant-Alpha nativ).
            p.fillRect(b.rect.translated(PdfEditController::kNoteShadowDxPt,
                                         PdfEditController::kNoteShadowDyPt),
                       QColor(0, 0, 0, 52));
            // Papier.
            p.fillRect(b.rect, b.highlight);
            // Eselsohr: abgedunkeltes Dreieck + feine Faltlinie entlang der
            // Hypotenuse — identische Geometrie wie der QML-Canvas.
            const qreal fold = qMin(PdfEditController::kNoteFoldPt,
                                    qMin(b.rect.width(), b.rect.height()) / 3.0);
            if (fold > 2.0) {
                const QPointF pA(b.rect.right() - fold, b.rect.bottom());
                const QPointF pB(b.rect.right(),        b.rect.bottom() - fold);
                const QPointF pC(b.rect.right(),        b.rect.bottom());
                QPainterPath flap;
                flap.moveTo(pA);
                flap.lineTo(pB);
                flap.lineTo(pC);
                flap.closeSubpath();
                QColor flapCol = b.highlight.darker(118);
                flapCol.setAlpha(b.highlight.alpha());
                p.setPen(Qt::NoPen);
                p.fillPath(flap, flapCol);
                QColor lineCol = b.highlight.darker(150);
                lineCol.setAlpha(b.highlight.alpha());
                p.setPen(QPen(lineCol, 0.7));
                p.drawLine(pA, pB);
            }
        }

        QFont f(b.fontFamily);
        f.setPointSizeF(qMax(1.0, b.fontSizePt));       // 72-dpi-Gerät: 1 pt = 1 Einheit
        f.setBold(b.bold);
        f.setItalic(b.italic);
        f.setUnderline(b.underline);

        const qreal pad    = PdfEditController::kBoxPaddingPt;
        const qreal availW = qMax<qreal>(4.0, b.rect.width() - 2.0 * pad);

        QTextOption opt;
        opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        opt.setAlignment(b.alignment == 1 ? Qt::AlignHCenter
                       : b.alignment == 2 ? Qt::AlignRight
                                          : Qt::AlignLeft);

        QTextLayout layout(b.text, f);
        layout.setTextOption(opt);
        layout.beginLayout();
        qreal totalH = 0.0;
        for (;;) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(availW);
            line.setPosition(QPointF(0.0, totalH));
            totalH += line.height();
        }
        layout.endLayout();

        // Vertikale Ausrichtung je Box: 0 = OBEN (Word-Textfeld, Text startet
        // an der oberen Innenkante — kein Offset), 1 = zentriert (Offset bei
        // Überlauf bewusst NICHT geklemmt, symmetrisch wie die Anzeige).
        const qreal availH = b.rect.height() - 2.0 * pad;
        const qreal yOff   = (b.vAlign == 1) ? (availH - totalH) / 2.0 : 0.0;

        p.setPen(b.color);
        layout.draw(&p, QPointF(b.rect.left() + pad, b.rect.top() + pad + yOff));

        p.restore();
    }

    PdfEditController*  m_owner;
    QString             m_source;
    QString             m_target;
    QVector<PdfEditBox> m_boxes;
    int                 m_gen;
    CancelFlag          m_cancel;
};
} // namespace

// ═════════════════════════════════════════════════════════════════════════════
//  PdfEditController
// ═════════════════════════════════════════════════════════════════════════════
// Default-Ctor für die QML-Instanziierung PRO PdfSurface (dezentraler Editor je
// PDF-Kachel): delegiert an den ISettings&-Ctor mit der zentralen AppSettings-
// Instanz. So teilen alle Instanzen dieselbe persistierte Einstellung panelOnTop.
PdfEditController::PdfEditController(QObject* parent)
    : PdfEditController(AppSettings::instance(), parent) {}

PdfEditController::PdfEditController(ISettings& settings, QObject* parent)
    : QObject(parent), m_settings(settings) {
    m_stack.setUndoLimit(kUndoLimit);
    // 1 Worker: nie zwei Export-Läufe (je eine QPdfDocument-Instanz + Seitenbild)
    // gleichzeitig → RAM-Peak gedeckelt.
    m_pool.setMaxThreadCount(1);

    connect(&m_stack, &QUndoStack::canUndoChanged, this, [this] { emit undoStateChanged(); });
    connect(&m_stack, &QUndoStack::canRedoChanged, this, [this] { emit undoStateChanged(); });
    connect(&m_stack, &QUndoStack::cleanChanged,   this, [this] { emit dirtyChanged(); });
    connect(&m_model, &PdfEditModel::countChanged, this, [this] { emit boxCountChanged(); });

    // Datenänderung an der AUSGEWÄHLTEN Box → Toolbar/Panel rev-getrieben
    // neu binden lassen; verschwundene Auswahl (Undo eines Hinzufügens,
    // Sidecar-Reset) aufräumen.
    connect(&m_model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& tl, const QModelIndex&, const QList<int>&) {
                if (m_selectedId >= 0 && tl.isValid()
                    && tl.row() == m_model.indexOfId(m_selectedId))
                    bumpSelectionRev();
            });
    auto dropVanishedSelection = [this] {
        if (m_selectedId >= 0 && m_model.indexOfId(m_selectedId) < 0)
            setSelectedId(-1);
        else
            bumpSelectionRev();
    };
    connect(&m_model, &QAbstractItemModel::rowsRemoved,  this, dropVanishedSelection);
    connect(&m_model, &QAbstractItemModel::rowsInserted, this, dropVanishedSelection);
    connect(&m_model, &QAbstractItemModel::modelReset,   this, dropVanishedSelection);
}

PdfEditController::~PdfEditController() {
    // Laufenden Export kooperativ stoppen, bevor der Controller verschwindet
    // (der Task hält Referenzen nur über Werte + diesen Zeiger via invokeMethod-
    // Kontext — QueuedConnection auf ein zerstörtes Objekt wird verworfen).
    if (m_cancel)
        m_cancel->store(true, std::memory_order_relaxed);
    m_pool.clear();
    m_pool.waitForDone();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Modus / Auswahl
// ─────────────────────────────────────────────────────────────────────────────
void PdfEditController::setEditMode(bool on) {
    if (m_editMode == on)
        return;
    finishOpenSessions();
    m_editMode = on;
    if (!on)
        setSelectedId(-1);
    emit editModeChanged();
}

void PdfEditController::setSelectedId(int id) {
    if (m_selectedId == id)
        return;
    m_selectedId = id;
    emit selectedIdChanged();
    bumpSelectionRev();
}

void PdfEditController::bumpSelectionRev() {
    ++m_selectionRev;
    emit selectionRevChanged();
}

bool PdfEditController::panelOnTop() const {
    return m_settings.pdfEditPanelTop();
}

void PdfEditController::setPanelOnTop(bool v) {
    if (m_settings.pdfEditPanelTop() == v)
        return;
    m_settings.setPdfEditPanelTop(v);
    emit panelOnTopChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Dokument-Lebenszyklus
// ─────────────────────────────────────────────────────────────────────────────
void PdfEditController::setDocument(const QString& pathOrUrl) {
    const QString local = mg::toLocalPath(pathOrUrl);
    if (local == m_docPath)
        return;                                     // idempotent

    finishOpenSessions();
    // Auto-Sicherung: ungesicherte Änderungen des VORHERIGEN Dokuments landen
    // im Sidecar — Navigation verliert nie stillschweigend Bearbeitungen.
    if (!m_docPath.isEmpty() && !m_stack.isClean())
        saveOverlay();

    m_docPath = local;
    setSelectedId(-1);
    m_stack.clear();                                // setzt zugleich auf „clean"
    m_model.clearAll();
    m_nextId = 1;

    if (!m_docPath.isEmpty())
        loadOverlay(m_docPath);
}

void PdfEditController::releaseDocument() {
    if (m_docPath.isEmpty())
        return;
    finishOpenSessions();
    if (!m_stack.isClean())
        saveOverlay();
    m_docPath.clear();
    setSelectedId(-1);
    m_stack.clear();
    m_model.clearAll();
    m_nextId = 1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Boxen erzeugen / entfernen
// ─────────────────────────────────────────────────────────────────────────────
int PdfEditController::addTextBox(int page, qreal xPt, qreal yPt,
                                  qreal pageWPt, qreal pageHPt) {
    if (m_docPath.isEmpty() || page < 0)
        return -1;
    if (pageWPt <= 0.0) pageWPt = 612.0;
    if (pageHPt <= 0.0) pageHPt = 792.0;

    PdfEditBox b = seededBox();                     // Stil der letzten Notiz erben
    b.id   = m_nextId++;
    b.page = page;
    // Standardgröße: ~1/3 Seitenbreite, zwei Zeilenhöhen; in die Seite geklemmt.
    const qreal w = qMin(qMax(90.0, pageWPt * 0.32), qMax(90.0, pageWPt - 16.0));
    const qreal h = qMax(kMinBoxHPt, b.fontSizePt * 2.0 + 2.0 * kBoxPaddingPt);
    const qreal x = qMax(2.0, qMin(xPt, qMax(2.0, pageWPt - w - 2.0)));
    const qreal y = qMax(2.0, qMin(yPt, qMax(2.0, pageHPt - h - 2.0)));
    b.rect = QRectF(x, y, w, h);

    pushCommand(new PdfEditAddCommand(&m_model, b, m_model.count()));
    setSelectedId(b.id);
    return b.id;
}

int PdfEditController::addAnchoredTextBox(int page, qreal xPt, qreal yPt,
                                          qreal wPt, qreal hPt) {
    if (m_docPath.isEmpty() || page < 0)
        return -1;

    PdfEditBox b = seededBox();                     // Stil der letzten Notiz erben
    b.id       = m_nextId++;
    b.page     = page;
    b.anchored = true;
    b.rect     = QRectF(qMax(0.0, xPt), qMax(0.0, yPt),
                        qMax(kMinBoxWPt, wPt), qMax(kMinBoxHPt, hPt));
    // Schriftgröße aus der Zeilenhöhe ableiten (typografisch ≈ 72 % der Zeile).
    b.fontSizePt = qBound(6.0, hPt * 0.72, 72.0);

    pushCommand(new PdfEditAddCommand(&m_model, b, m_model.count()));
    setSelectedId(b.id);
    return b.id;
}

void PdfEditController::removeBox(int id) {
    const int row = m_model.indexOfId(id);
    const PdfEditBox* b = m_model.boxById(id);
    if (row < 0 || !b)
        return;
    const PdfEditBox copy = *b;                     // vor der Mutation kopieren
    finishOpenSessions();
    if (m_selectedId == id)
        setSelectedId(-1);
    pushCommand(new PdfEditRemoveCommand(&m_model, copy, row));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Copy / Paste + Stil-Vorlage
// ─────────────────────────────────────────────────────────────────────────────
PdfEditBox PdfEditController::seededBox() const {
    PdfEditBox b = m_textTpl;                        // Schrift/Farben/Deckkraft/Ausrichtung
    b.id       = 0;
    b.text.clear();                                  // aber OHNE Text
    b.anchored = false;
    return b;
}

void PdfEditController::mirrorToTemplate(PdfEditField f, const QVariant& v) {
    switch (f) {
    case PdfEditField::FontFamily: m_textTpl.fontFamily = v.toString();     break;
    case PdfEditField::FontSize:   m_textTpl.fontSizePt = v.toReal();       break;
    case PdfEditField::Bold:       m_textTpl.bold       = v.toBool();       break;
    case PdfEditField::Italic:     m_textTpl.italic     = v.toBool();       break;
    case PdfEditField::Underline:  m_textTpl.underline  = v.toBool();       break;
    case PdfEditField::Color:      m_textTpl.color      = v.value<QColor>();break;
    case PdfEditField::Highlight:  m_textTpl.highlight  = v.value<QColor>();break;
    case PdfEditField::Alignment:  m_textTpl.alignment  = v.toInt();        break;
    case PdfEditField::VAlign:     m_textTpl.vAlign     = v.toInt();        break;
    default: break;
    }
}

void PdfEditController::copySelected() {
    const PdfEditBox* b = m_model.boxById(m_selectedId);
    if (!b)
        return;
    m_clip = *b;
    if (!m_hasClip) { m_hasClip = true; emit clipboardChanged(); }
}

void PdfEditController::paste() {
    if (!m_hasClip || m_docPath.isEmpty())
        return;
    finishOpenSessions();
    PdfEditBox b = m_clip;                           // inkl. Text + allen Einstellungen
    b.id = m_nextId++;
    // Leicht versetzt auf derselben Seite, damit die Kopie sichtbar liegt.
    b.rect.translate(14.0, 14.0);
    pushCommand(new PdfEditAddCommand(&m_model, b, m_model.count()));
    setSelectedId(b.id);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Geometrie-Session
// ─────────────────────────────────────────────────────────────────────────────
void PdfEditController::beginGeometryEdit(int id) {
    finishOpenSessions();
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    m_geoEditId  = id;
    m_geoOldPage = b->page;
    m_geoOld     = b->rect;
}

void PdfEditController::updateGeometry(int id, qreal xPt, qreal yPt,
                                       qreal wPt, qreal hPt) {
    if (id != m_geoEditId)
        return;                                     // nur innerhalb einer Session
    QRectF r(xPt, yPt, qMax(kMinBoxWPt, wPt), qMax(kMinBoxHPt, hPt));
    if (r.x() < 0.0) r.moveLeft(0.0);
    if (r.y() < 0.0) r.moveTop(0.0);
    m_model.applyGeometry(id, r);                   // live, KEIN Kommando je Move
}

void PdfEditController::updatePlacement(int id, int page, qreal xPt, qreal yPt,
                                        qreal wPt, qreal hPt) {
    if (id != m_geoEditId)
        return;                                     // nur innerhalb einer Session
    QRectF r(xPt, yPt, qMax(kMinBoxWPt, wPt), qMax(kMinBoxHPt, hPt));
    if (r.x() < 0.0) r.moveLeft(0.0);
    // y bewusst NICHT klemmen: über den Seitenrand gezogene Zwischenzustände
    // (seitenübergreifendes Verschieben) brauchen y < 0 bzw. y > Seitenhöhe;
    // die Zielseite + finale Klemmung liefert QML beim Loslassen.
    m_model.applyPlacement(id, page, r);            // live, KEIN Kommando je Move
}

void PdfEditController::endGeometryEdit(int id) {
    if (id == m_geoEditId)
        finishGeometrySession();
}

void PdfEditController::finishGeometrySession() {
    if (m_geoEditId < 0)
        return;
    const int id = m_geoEditId;
    m_geoEditId = -1;
    const PdfEditBox* b = m_model.boxById(id);
    if (b && (b->rect != m_geoOld || b->page != m_geoOldPage))
        pushCommand(new PdfEditGeometryCommand(&m_model, id,
                                               m_geoOldPage, m_geoOld,
                                               b->page, b->rect));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Text-Session
// ─────────────────────────────────────────────────────────────────────────────
void PdfEditController::beginTextEdit(int id) {
    finishOpenSessions();
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    m_textEditId = id;
    m_textOld    = b->text;
    emit textEditingChanged();
}

void PdfEditController::updateText(int id, const QString& text) {
    if (id != m_textEditId)
        return;
    m_model.applyText(id, text);                    // live, KEIN Kommando je Taste
}

void PdfEditController::endTextEdit(int id) {
    if (id == m_textEditId)
        finishTextSession();
}

void PdfEditController::finishTextSession() {
    if (m_textEditId < 0)
        return;
    const int id = m_textEditId;
    m_textEditId = -1;
    emit textEditingChanged();
    const PdfEditBox* b = m_model.boxById(id);
    if (b && b->text != m_textOld)
        pushCommand(new PdfEditTextCommand(&m_model, id, m_textOld, b->text));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Stil/Format
// ─────────────────────────────────────────────────────────────────────────────
void PdfEditController::setBoxField(int id, PdfEditField f, const QVariant& v) {
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    QVariant old;
    switch (f) {
    case PdfEditField::FontFamily: old = b->fontFamily; break;
    case PdfEditField::FontSize:   old = b->fontSizePt; break;
    case PdfEditField::Bold:       old = b->bold;       break;
    case PdfEditField::Italic:     old = b->italic;     break;
    case PdfEditField::Underline:  old = b->underline;  break;
    case PdfEditField::Color:      old = b->color;      break;
    case PdfEditField::Highlight:  old = b->highlight;  break;
    case PdfEditField::Alignment:  old = b->alignment;  break;
    case PdfEditField::VAlign:     old = b->vAlign;     break;
    default: return;                                // Text/Geometry: eigene Wege
    }
    mirrorToTemplate(f, v);                          // neue Notizen erben diesen Stil
    if (old == v)
        return;
    pushCommand(new PdfEditFieldCommand(&m_model, id, f, old, v));
}

void PdfEditController::setBoxFont(int id, const QString& family) {
    setBoxField(id, PdfEditField::FontFamily, family);
}
void PdfEditController::setBoxFontSize(int id, qreal sizePt) {
    setBoxField(id, PdfEditField::FontSize, qBound(4.0, sizePt, 200.0));
}
void PdfEditController::setBoxBold(int id, bool v) {
    setBoxField(id, PdfEditField::Bold, v);
}
void PdfEditController::setBoxItalic(int id, bool v) {
    setBoxField(id, PdfEditField::Italic, v);
}
void PdfEditController::setBoxUnderline(int id, bool v) {
    setBoxField(id, PdfEditField::Underline, v);
}
void PdfEditController::setBoxColor(int id, const QColor& c) {
    if (c.isValid())
        setBoxField(id, PdfEditField::Color, QColor(c.red(), c.green(), c.blue()));
}
void PdfEditController::setBoxHighlight(int id, const QColor& c) {
    if (c.isValid())
        setBoxField(id, PdfEditField::Highlight, c);
}
void PdfEditController::setBoxAlignment(int id, int align) {
    setBoxField(id, PdfEditField::Alignment, qBound(0, align, 2));
}
void PdfEditController::setBoxVAlign(int id, int vAlign) {
    setBoxField(id, PdfEditField::VAlign, qBound(0, vAlign, 1));
}

// ─────────────────────────────────────────────────────────────────────────────
QVariantMap PdfEditController::boxInfo(int id) const {
    QVariantMap m;
    const PdfEditBox* b = m_model.boxById(id);
    m.insert(QStringLiteral("exists"), b != nullptr);
    if (!b)
        return m;
    m.insert(QStringLiteral("page"),           b->page);
    m.insert(QStringLiteral("xPt"),            b->rect.x());
    m.insert(QStringLiteral("yPt"),            b->rect.y());
    m.insert(QStringLiteral("wPt"),            b->rect.width());
    m.insert(QStringLiteral("hPt"),            b->rect.height());
    m.insert(QStringLiteral("text"),           b->text);
    m.insert(QStringLiteral("fontFamily"),     b->fontFamily);
    m.insert(QStringLiteral("fontSizePt"),     b->fontSizePt);
    m.insert(QStringLiteral("bold"),           b->bold);
    m.insert(QStringLiteral("italic"),         b->italic);
    m.insert(QStringLiteral("underline"),      b->underline);
    m.insert(QStringLiteral("textColor"),      b->color);
    m.insert(QStringLiteral("highlightColor"), b->highlight);
    m.insert(QStringLiteral("hasHighlight"),   b->highlight.alpha() > 0);
    m.insert(QStringLiteral("alignment"),      b->alignment);
    m.insert(QStringLiteral("vAlign"),         b->vAlign);
    m.insert(QStringLiteral("anchored"),       b->anchored);
    return m;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Undo/Redo
// ─────────────────────────────────────────────────────────────────────────────
void PdfEditController::pushCommand(QUndoCommand* cmd) {
    m_stack.push(cmd);                              // führt redo() sofort aus
}

void PdfEditController::undo() {
    finishOpenSessions();                           // deterministisch abschließen
    m_stack.undo();
}

void PdfEditController::redo() {
    finishOpenSessions();
    m_stack.redo();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Sidecar-Persistenz
// ─────────────────────────────────────────────────────────────────────────────
QString PdfEditController::sidecarPath(const QString& pdfPath) {
    return pdfPath + QStringLiteral(".mgedit.json");
}

bool PdfEditController::saveOverlay() {
    if (m_docPath.isEmpty())
        return false;
    finishOpenSessions();

    const QString sc = sidecarPath(m_docPath);
    bool ok = false;

    if (m_model.count() == 0) {
        // Leeres Overlay → kein Sidecar-Artefakt zurücklassen.
        ok = !QFile::exists(sc) || QFile::remove(sc);
    } else {
        QJsonArray arr;
        const QVector<PdfEditBox> boxes = m_model.boxes();
        for (const PdfEditBox& b : boxes)
            arr.append(b.toJson());
        QJsonObject rootObj;
        rootObj.insert(QStringLiteral("format"),  QStringLiteral("mediagallery-pdf-overlay"));
        rootObj.insert(QStringLiteral("version"), 1);
        rootObj.insert(QStringLiteral("boxes"),   arr);

        // Atomar (QSaveFile) — wie ViewerController::writeTextFile.
        QSaveFile f(sc);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QByteArray bytes = QJsonDocument(rootObj).toJson(QJsonDocument::Compact);
            if (f.write(bytes) == bytes.size())
                ok = f.commit();
            else
                f.cancelWriting();
        }
    }

    if (ok)
        m_stack.setClean();
    emit overlaySaved(ok);
    return ok;
}

bool PdfEditController::loadOverlay(const QString& pdfPath) {
    const QString sc = sidecarPath(pdfPath);
    QFile f(sc);
    if (!f.exists() || f.size() > kMaxSidecarBytes)
        return false;
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument jd = QJsonDocument::fromJson(f.readAll());
    if (!jd.isObject())
        return false;
    const QJsonObject o = jd.object();
    if (o.value(QStringLiteral("format")).toString()
        != QLatin1String("mediagallery-pdf-overlay"))
        return false;

    QVector<PdfEditBox> boxes;
    const QJsonArray arr = o.value(QStringLiteral("boxes")).toArray();
    boxes.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        if (!v.isObject())
            continue;
        PdfEditBox b = PdfEditBox::fromJson(v.toObject());
        b.id = m_nextId++;                          // IDs sind sitzungslokal
        boxes.append(b);
    }
    m_model.resetBoxes(boxes);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Export
// ─────────────────────────────────────────────────────────────────────────────
QString PdfEditController::uniqueCopyPath(const QString& pdfPath) {
    const QFileInfo fi(pdfPath);
    const QString dir  = fi.absolutePath();
    const QString base = fi.completeBaseName() + QStringLiteral("_bearbeitet");
    QString candidate = dir + QLatin1Char('/') + base + QStringLiteral(".pdf");
    int n = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QLatin1Char('/') + base
                    + QStringLiteral(" (%1).pdf").arg(n);
        ++n;
    }
    return candidate;
}

QString PdfEditController::exportTargetPath() const {
    if (m_docPath.isEmpty())
        return {};
    // IMMER eine Kopie neben dem Original — nie destruktiv (die Notizen
    // bleiben über das Sidecar dauerhaft editier- und entfernbar).
    return uniqueCopyPath(m_docPath);
}

void PdfEditController::exportPdf() {
    if (m_busy || m_docPath.isEmpty())
        return;
    finishOpenSessions();

    const QString target = exportTargetPath();
    if (target.isEmpty())
        return;

    m_busy = true;
    emit busyChanged();

    const int gen = ++m_exportGen;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    m_pool.start(new PdfExportTask(this, m_docPath, target,
                                   m_model.boxes(), gen, m_cancel));
}

void PdfEditController::exportTaskFinished(bool ok, const QString& target,
                                           const QString& error, int generation) {
    if (generation != m_exportGen)
        return;                                     // veralteter Lauf
    m_busy = false;
    emit busyChanged();
    emit exportFinished(ok, target, error);
}

void PdfEditController::exportTaskProgress(int done, int total, int generation) {
    if (generation != m_exportGen)
        return;
    emit exportProgress(done, total);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Schriften
// ─────────────────────────────────────────────────────────────────────────────
QStringList PdfEditController::standardFonts() const {
    // Feste Editor-Palette (Anforderung). Fehlt eine Familie im System,
    // substituiert Qt beim Rendern automatisch (z. B. Helvetica → DejaVu Sans
    // unter Linux) — identisch in Anzeige UND Export, da beide dieselbe
    // Font-Auflösung nutzen.
    return { QStringLiteral("Times New Roman"),
             QStringLiteral("Arial"),
             QStringLiteral("Calibri"),
             QStringLiteral("Helvetica"),
             QStringLiteral("Courier New") };
}

QString PdfEditController::resolvedFont(const QString& family) const {
    // Erkennung installierter Systemschriften: liefert die Familie, die die
    // Font-Datenbank für `family` tatsächlich auflöst (== family, wenn
    // installiert; sonst die Substitution).
    return QFontInfo(QFont(family)).family();
}
