#include "PdfEditController.h"
#include "PdfEditCommands.h"
#include "ISettings.h"
#include "AppSettings.h"      // AppSettings::instance() für den Default-Ctor (QML-Instanzen)
#include "PathUtils.h"
#include "PdfPageCopier.h"   // PdfAssembler — destruktiver Seiten-Neuschrieb

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
#include <QtMath>
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
                  QVector<PdfEditBox> boxes, QVector<int> plan,
                  int generation, CancelFlag cancel)
        : m_owner(owner)
        , m_source(std::move(sourcePath))
        , m_target(std::move(targetPath))
        , m_boxes(std::move(boxes))
        , m_plan(std::move(plan))
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

        // Aufgabe 3: der Seiten-Plan bestimmt Reihenfolge/Anzahl der Ausgabe.
        // Leer = Identität (Rückwärtskompatibilität). ≥0 = Quellseite, −1 = A4-Leer.
        const int viewCount = m_plan.isEmpty() ? pageCount : m_plan.size();

        QPainter p;
        for (int vi = 0; vi < viewCount; ++vi) {
            if (cancelled()) {
                if (p.isActive()) p.end();
                out.cancelWriting();
                *err = QStringLiteral("cancel");
                return false;
            }

            const int  src   = m_plan.isEmpty() ? vi : m_plan.at(vi);
            const bool blank = (src < 0 || src >= pageCount);

            QSizeF pts = blank ? QSizeF(595.276, 841.890)   // A4-Leerseite
                               : doc.pagePointSize(src);
            if (pts.isEmpty())
                pts = QSizeF(612.0, 792.0);                 // Fallback: US Letter

            writer.setPageSize(QPageSize(pts, QPageSize::Point,
                                         QString(), QPageSize::ExactMatch));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));

            if (vi == 0) {
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

            if (blank) {
                p.fillRect(QRectF(QPointF(0, 0), pts), Qt::white);  // Leerseite
            } else {
                // ── Basisseite als Bild (genau EIN Bild transient im RAM) ─────
                const QSize px(qMax(1, qRound(pts.width()  / 72.0 * PdfEditController::kExportRenderDpi)),
                               qMax(1, qRound(pts.height() / 72.0 * PdfEditController::kExportRenderDpi)));
                const QImage img = doc.render(src, px);
                if (!img.isNull())
                    p.drawImage(QRectF(QPointF(0, 0), pts), img);
                else
                    p.fillRect(QRectF(QPointF(0, 0), pts), Qt::white);

                // ── Overlay-Boxen dieser Quellseite darüber ───────────────────
                for (const PdfEditBox& b : std::as_const(m_boxes))
                    if (b.page == src)
                        drawBox(p, b);
            }

            reportProgress(vi + 1, viewCount);
        }

        p.end();
        doc.close();            // Lese-Handle VOR dem Commit freigeben (Windows)
        if (!out.commit()) {
            *err = out.errorString();
            return false;
        }
        return true;
    }

    // Pfeilspitze: zwei kurze Schenkel am Endpunkt, Länge/Winkel aus der
    // Linienbreite abgeleitet (identische Geometrie im QML-Delegate; Maße in
    // PDF-Punkten).
    static void drawArrowHead(QPainter& p, const QPointF& from, const QPointF& to,
                              qreal lineWidth) {
        const qreal ang = std::atan2(to.y() - from.y(), to.x() - from.x());
        const qreal len = qMax<qreal>(10.0, lineWidth * 4.0);
        const qreal spread = M_PI / 7.0;                 // ~25.7°
        const QPointF a(to.x() - len * std::cos(ang - spread),
                        to.y() - len * std::sin(ang - spread));
        const QPointF b(to.x() - len * std::cos(ang + spread),
                        to.y() - len * std::sin(ang + spread));
        p.drawLine(to, a);
        p.drawLine(to, b);
    }

    // Zeichnet eine Annotation exakt wie die QML-Anzeige. Text-Notizen:
    // Post-it-Optik (versetzter Schatten, Papierfläche = highlight inkl. Alpha,
    // Eselsohr unten rechts), dann der Text via QTextLayout (gleiche Text-
    // Engine wie QML TextEdit; Umbruch WrapAtWordBoundaryOrAnywhere =
    // TextEdit.Wrap). Vertikal je b.vAlign OBEN (Standard, wie Word-Text-
    // felder) oder ZENTRIERT. KEIN Clipping — QML zeigt Überlauf ebenfalls an
    // (clip:false) → WYSIWYG; bei zentriertem Überlauf wird der Offset negativ
    // (symmetrisch wie Anzeige). Zeichnungen (Freihand/Pfeil/Rechteck/Ellipse)
    // laufen 1:1 in PDF-Punkten — dieselbe Geometrie wie der Canvas im
    // PdfEditBox-Delegate (Bild-Editor-Muster).
    void drawBox(QPainter& p, const PdfEditBox& b) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);

        switch (b.kind) {
        case PdfAnnKind::Freehand: {
            if (b.points.size() >= 2) {
                QPen pen(b.stroke, qMax<qreal>(0.2, b.lineWidth));
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);
                p.setPen(pen);
                QPainterPath path(b.points.first());
                for (int i = 1; i < b.points.size(); ++i)
                    path.lineTo(b.points.at(i));
                p.drawPath(path);
            } else if (b.points.size() == 1) {
                p.setPen(Qt::NoPen);
                p.setBrush(b.stroke);
                const qreal r = qMax<qreal>(0.2, b.lineWidth) * 0.5;
                p.drawEllipse(b.points.first(), r, r);
            }
            p.restore();
            return;
        }
        case PdfAnnKind::Arrow: {
            if (b.points.size() >= 2) {
                QPen pen(b.stroke, qMax<qreal>(0.2, b.lineWidth));
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);
                p.setPen(pen);
                const QPointF from = b.points.first();
                const QPointF to   = b.points.at(1);
                p.drawLine(from, to);
                drawArrowHead(p, from, to, b.lineWidth);
            }
            p.restore();
            return;
        }
        case PdfAnnKind::Rect: {
            if (b.fill.alpha() > 0) p.setBrush(b.fill); else p.setBrush(Qt::NoBrush);
            p.setPen(QPen(b.stroke, qMax<qreal>(0.2, b.lineWidth)));
            p.drawRect(b.rect);
            p.restore();
            return;
        }
        case PdfAnnKind::Ellipse: {
            if (b.fill.alpha() > 0) p.setBrush(b.fill); else p.setBrush(Qt::NoBrush);
            p.setPen(QPen(b.stroke, qMax<qreal>(0.2, b.lineWidth)));
            p.drawEllipse(b.rect);
            p.restore();
            return;
        }
        case PdfAnnKind::Replace:
            // „Text ersetzen": deckende, fix weiße Fläche EXAKT über dem
            // Box-Rechteck — bewusst OHNE Post-it-Optik (kein Schatten, kein
            // Eselsohr); danach derselbe Text-Pfad wie die Notizen.
            p.fillRect(b.rect, b.highlight);
            break;
        case PdfAnnKind::Text:
            break;                                   // fällt in die Notiz-Zeichnung
        }

        // Post-it-Optik (Schatten/Papier/Eselsohr) NUR für klassische Notizen —
        // die Replace-Deckfläche wurde oben bereits flach gezeichnet.
        const bool paper = b.kind == PdfAnnKind::Text && b.highlight.alpha() > 0;
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
    QVector<int>        m_plan;    // Seiten-Plan (Aufgabe 3); leer = Identität
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
    finishDrawSession();
    m_editMode = on;
    if (!on) { setTool(Select); setSelectedId(-1); }
    emit editModeChanged();
}

void PdfEditController::setTool(int t) {
    if (t < Select || t > ReplaceTool || m_tool == t)
        return;
    finishOpenSessions();
    finishDrawSession();
    m_tool = t;
    // Beim Wechsel auf ein Zeichen-/Text-Werkzeug die Auswahl aufheben (keine
    // schwebende Toolbar über einer alten Auswahl während des Zeichnens).
    if (t != Select)
        setSelectedId(-1);
    emit toolChanged();
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
//  Seiten hinzufügen/entfernen (Aufgabe 3)
// ─────────────────────────────────────────────────────────────────────────────
bool PdfEditController::pageEditDestructive() const {
    return m_settings.pdfPageEditDestructive();
}
void PdfEditController::setPageEditDestructive(bool v) {
    if (m_settings.pdfPageEditDestructive() == v)
        return;
    m_settings.setPdfPageEditDestructive(v);
    emit pageEditDestructiveChanged();
    // Modewechsel bei bereits geändertem Plan → Arbeitsdatei neu am richtigen Ort.
    if (!m_docPath.isEmpty() && !planIsIdentity())
        bakeWorking();
}

QString PdfEditController::backupPath(const QString& pdfPath) {
    return pdfPath + QStringLiteral(".mgorig");
}
QString PdfEditController::previewPath(const QString& pdfPath) {
    return pdfPath + QStringLiteral(".mgpreview.pdf");
}

QString PdfEditController::pristinePath() const {
    // Quelle mit den UNVERÄNDERTEN Seiten: destruktiv liegt sie in der
    // einmaligen .mgorig-Sicherung, sonst ist das Original selbst pristine.
    if (m_settings.pdfPageEditDestructive()) {
        const QString bak = backupPath(m_docPath);
        if (QFile::exists(bak))
            return bak;
    }
    return m_docPath;
}

QString PdfEditController::renderSourcePath() const {
    // Datei, die die ANZEIGE rendert: bei geändertem Plan die gebackene
    // Arbeitsdatei (destruktiv = die .pdf selbst, sonst die temporäre
    // .mgpreview.pdf), sonst die pristine Quelle. So bleibt „Ansichts-Index ==
    // Seitenindex der gerenderten Datei" erhalten — die Ansicht muss den Plan
    // NICHT selbst anwenden.
    if (m_docPath.isEmpty() || planIsIdentity())
        return pristinePath();
    return m_settings.pdfPageEditDestructive() ? m_docPath
                                               : previewPath(m_docPath);
}

bool PdfEditController::planIsIdentity() const {
    if (m_plan.size() != m_srcPageCount)
        return false;
    for (int i = 0; i < m_plan.size(); ++i)
        if (m_plan.at(i) != i)
            return false;
    return true;
}

void PdfEditController::setSourcePageCount(int n) {
    if (n < 0) n = 0;
    m_srcPageCount = n;
    if (m_plan.isEmpty()) {
        m_plan.resize(n);
        for (int i = 0; i < n; ++i)
            m_plan[i] = i;                      // Identitäts-Plan
    } else {
        // Geladenen Sidecar-Plan absichern: Quellindizes außerhalb [0,n)
        // verwerfen, Leerseiten (−1) behalten; leert sich alles → Identität.
        QVector<int> v;
        v.reserve(m_plan.size());
        for (int e : std::as_const(m_plan))
            if (e == -1 || (e >= 0 && e < n))
                v.append(e);
        if (v.isEmpty())
            for (int i = 0; i < n; ++i)
                v.append(i);
        m_plan = v;
    }
    emit planChanged();
    // Geladener Nicht-Identitäts-Plan (Sidecar) → Arbeitsdatei erzeugen, damit
    // die Anzeige sie sofort rendern kann (renderSourcePath()).
    if (!planIsIdentity())
        bakeWorking();
}

int PdfEditController::viewSourcePage(int viewIndex) const {
    return (viewIndex >= 0 && viewIndex < m_plan.size())
               ? m_plan.at(viewIndex) : -1;
}

void PdfEditController::addBlankPageAfter(int viewIndex) {
    if (m_docPath.isEmpty())
        return;
    int pos = viewIndex + 1;
    if (pos < 0)              pos = 0;
    if (pos > m_plan.size())  pos = m_plan.size();
    QVector<int> next = m_plan;
    next.insert(pos, -1);
    pushCommand(new PdfEditPagePlanCommand(this, m_plan, next));
}

void PdfEditController::removePage(int viewIndex) {
    if (m_docPath.isEmpty() || viewIndex < 0 || viewIndex >= m_plan.size())
        return;
    if (m_plan.size() <= 1)
        return;                                 // mindestens eine Seite bleibt
    QVector<int> next = m_plan;
    next.removeAt(viewIndex);
    pushCommand(new PdfEditPagePlanCommand(this, m_plan, next));
}

void PdfEditController::applyPlan(const QVector<int>& plan) {
    m_plan = plan;
    emit planChanged();
    bakeWorking();                              // Arbeitsdatei + Reload (beide Modi)
}

void PdfEditController::bakeWorking() {
    if (m_docPath.isEmpty())
        return;
    const bool destructive = m_settings.pdfPageEditDestructive();

    if (planIsIdentity()) {
        // Kein Plan (mehr) → Arbeitsdatei = pristine.
        if (destructive) {
            const QString bak = backupPath(m_docPath);
            if (QFile::exists(bak)) {           // gebackene .pdf aufs Original zurück
                QFile::remove(m_docPath);
                QFile::copy(bak, m_docPath);
            }
        } else {
            QFile::remove(previewPath(m_docPath));   // temporäre Vorschau aufräumen
        }
        emit documentRewritten();
        return;
    }

    // Pristine bestimmen (destruktiv: einmalige Sicherung anlegen).
    QString src;
    if (destructive) {
        const QString bak = backupPath(m_docPath);
        if (!QFile::exists(bak) && !QFile::copy(m_docPath, bak))
            return;                             // ohne Sicherung nicht schreiben
        src = bak;
    } else {
        src = m_docPath;                        // Original bleibt unverändert
    }
    const QString target = destructive ? m_docPath : previewPath(m_docPath);

    const QSizeF a4(595.276, 841.890);
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly))
        return;
    QString err;
    PdfAssembler asmbl(&out);
    bool ok = asmbl.begin(&err);
    for (int i = 0; ok && i < m_plan.size(); ++i) {
        const int e = m_plan.at(i);
        ok = (e >= 0) ? asmbl.addSourcePages(src, { e }, &err)
                      : asmbl.addBlankPage(a4, &err);
    }
    ok = ok && asmbl.finish(&err);
    if (ok && out.commit())
        emit documentRewritten();
    else
        out.cancelWriting();                    // pristine bleibt unversehrt
}

// ─────────────────────────────────────────────────────────────────────────────
//  Dokument-Lebenszyklus
// ─────────────────────────────────────────────────────────────────────────────
void PdfEditController::setDocument(const QString& pathOrUrl) {
    const QString local = mg::toLocalPath(pathOrUrl);
    if (local == m_docPath)
        return;                                     // idempotent

    finishOpenSessions();
    finishDrawSession();
    // Auto-Sicherung: ungesicherte Änderungen des VORHERIGEN Dokuments landen
    // im Sidecar — Navigation verliert nie stillschweigend Bearbeitungen.
    if (!m_docPath.isEmpty() && !m_stack.isClean())
        saveOverlay();

    if (!m_docPath.isEmpty())
        QFile::remove(previewPath(m_docPath));  // Vorschau des VORIGEN Dokuments

    m_docPath = local;
    setSelectedId(-1);
    setTool(Select);
    m_stack.clear();                                // setzt zugleich auf „clean"
    m_model.clearAll();
    m_nextId = 1;
    m_plan.clear();                                 // Seiten-Plan (Aufgabe 3)
    m_srcPageCount = 0;

    if (!m_docPath.isEmpty())
        loadOverlay(m_docPath);
}

void PdfEditController::releaseDocument() {
    if (m_docPath.isEmpty())
        return;
    finishOpenSessions();
    finishDrawSession();
    if (!m_stack.isClean())
        saveOverlay();
    QFile::remove(previewPath(m_docPath));      // temporäre Vorschau verwerfen
    m_docPath.clear();
    setSelectedId(-1);
    setTool(Select);
    m_stack.clear();
    m_model.clearAll();
    m_nextId = 1;
    m_plan.clear();
    m_srcPageCount = 0;
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

// ── Zeichen-Session (Freihand/Pfeil/Rechteck/Ellipse — analog Bild-Editor) ───
int PdfEditController::beginDraw(int kind, int page, qreal xPt, qreal yPt) {
    if (m_docPath.isEmpty() || page < 0
        || kind < static_cast<int>(PdfAnnKind::Freehand)
        || kind > static_cast<int>(PdfAnnKind::Replace))
        return -1;
    finishOpenSessions();
    finishDrawSession();

    // „Text ersetzen" zieht wie Rechteck/Ellipse auf (Live-Vorschau = weiße
    // Fläche), erbt aber die eigene Replace-Vorlage statt der Zeichen-Defaults.
    PdfEditBox b = (kind == static_cast<int>(PdfAnnKind::Replace))
                       ? seededReplace()
                       : seededDraw(static_cast<PdfAnnKind>(kind));
    b.id   = m_nextId++;
    b.page = page;
    m_drawStart = QPointF(xPt, yPt);
    m_drawPage  = page;
    switch (b.kind) {
    case PdfAnnKind::Freehand:
        b.points = { QPointF(xPt, yPt) };
        b.recomputeBounds();
        break;
    case PdfAnnKind::Arrow:
        b.points = { QPointF(xPt, yPt), QPointF(xPt, yPt) };
        b.recomputeBounds();
        break;
    default:                                        // Rect / Ellipse
        b.rect = QRectF(xPt, yPt, 0.0, 0.0);
        break;
    }
    // LIVE einfügen (Vorschau) — KEIN Kommando; das kommt erst bei endDraw().
    m_model.insertBoxAt(m_model.count(), b);
    m_drawId = b.id;
    return b.id;
}

void PdfEditController::updateDraw(int id, qreal xPt, qreal yPt) {
    if (id != m_drawId)
        return;
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    switch (b->kind) {
    case PdfAnnKind::Freehand: {
        QVector<QPointF> pts = b->points;
        pts.append(QPointF(xPt, yPt));
        m_model.applyPoints(id, pts);
        break;
    }
    case PdfAnnKind::Arrow: {
        QVector<QPointF> pts = b->points;
        if (pts.size() < 2) pts.resize(2);
        pts[0] = m_drawStart;
        pts[1] = QPointF(xPt, yPt);
        m_model.applyPoints(id, pts);
        break;
    }
    default: {                                      // Rect / Ellipse
        const QRectF r = QRectF(m_drawStart, QPointF(xPt, yPt)).normalized();
        m_model.applyGeometry(id, r);
        break;
    }
    }
}

void PdfEditController::endDraw(int id) {
    if (id != m_drawId)
        return;
    m_drawId = -1;
    const PdfEditBox* b = m_model.boxById(id);
    if (!b) return;
    PdfEditBox copy = *b;

    // Entartete Zeichnungen verwerfen (reiner Klick ohne Zug).
    const bool tooSmall =
        (copy.isStroke() && copy.points.size() < 2)
        || (!copy.isStroke() && (copy.rect.width() < 1.5 && copy.rect.height() < 1.5));
    // Die Live-Instanz wieder entfernen (ohne Kommando) …
    m_model.removeById(id);
    if (tooSmall)
        return;
    // … und als EIN Add-Kommando neu einsetzen (Undo entfernt die Zeichnung).
    pushCommand(new PdfEditAddCommand(&m_model, copy, m_model.count()));
    // Vorlage nachziehen (Stil-Erben): Replace pflegt die EIGENE Vorlage —
    // dieser Pfad läuft nur, wenn eine Replace-Session unerwartet über das
    // generische endDraw endet (z. B. finishDrawSession bei Moduswechsel);
    // der reguläre Abschluss ist endReplaceDraw.
    if (copy.kind == PdfAnnKind::Replace) {
        m_replaceTpl = copy;
        m_replaceTpl.text.clear();
        m_replaceTpl.points.clear();
    } else {
        m_defStroke = copy.stroke; m_defLineWidth = copy.lineWidth; m_defFill = copy.fill;
    }
    ++m_defaultRev; emit defaultRevChanged();
    setSelectedId(copy.id);
}

// ── „Text ersetzen": Session-Abschluss mit Zeilen-Einschnappen + Vorbefüllung ─
int PdfEditController::endReplaceDraw(int id, bool snapped,
                                      qreal xPt, qreal yPt, qreal wPt, qreal hPt,
                                      qreal lineHPt, const QString& text) {
    if (id != m_drawId)
        return -1;
    m_drawId = -1;
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return -1;
    PdfEditBox copy = *b;
    // Die Live-Instanz wieder entfernen (ohne Kommando) …
    m_model.removeById(id);
    if (copy.kind != PdfAnnKind::Replace)
        return -1;                                   // defensiv: falscher Aufruf

    if (snapped) {
        // Auf die erkannten Zeilen-Bounds einschnappen (weiße Fläche deckt
        // die Zeile(n) exakt); Schriftgröße aus der Ø-Zeilenhöhe — dieselbe
        // Typografie-Faustregel wie addAnchoredTextBox (≈ 72 % der Zeile).
        copy.rect = QRectF(qMax(0.0, xPt), qMax(0.0, yPt),
                           qMax(kMinBoxWPt, wPt), qMax(kMinBoxHPt, hPt));
        copy.anchored = true;
        if (lineHPt > 0.0)
            copy.fontSizePt = qBound(6.0, lineHPt * 0.72, 72.0);
        copy.text = text;                            // Vorbefüllung: erkannter Text
    } else if (copy.rect.width() < 1.5 && copy.rect.height() < 1.5) {
        return -1;                                   // entarteter Klick ohne Zug
    } else {
        // Keine Texterkennung (gescannte PDF/Stelle ohne Text): Box bleibt
        // exakt der aufgezogene Bereich, Stil aus der Vorlage — bewusst STILL,
        // ohne Hinweis-Dialog/Toast (konsistent zu den Post-its).
        copy.rect.setWidth(qMax(kMinBoxWPt,  copy.rect.width()));
        copy.rect.setHeight(qMax(kMinBoxHPt, copy.rect.height()));
    }
    // Invariante des Werkzeugs: Deckfläche fix deckendes Weiß.
    copy.highlight = QColor(255, 255, 255, 255);
    // Vorbefüllter Text muss vollständig in die Box passen (feste Breite,
    // Umbruch, Höhe wächst mit dem Inhalt — Muster der Post-it-Reflow-Logik).
    if (!copy.text.isEmpty()) {
        const qreal need = requiredHeightPt(copy);
        if (need > copy.rect.height())
            copy.rect.setHeight(need);
    }

    pushCommand(new PdfEditAddCommand(&m_model, copy, m_model.count()));
    // Replace-Vorlage nachziehen (Stil-Erben für die nächste Box, OHNE Text).
    m_replaceTpl = copy;
    m_replaceTpl.text.clear();
    m_replaceTpl.points.clear();
    ++m_defaultRev; emit defaultRevChanged();
    setSelectedId(copy.id);
    return copy.id;
}

void PdfEditController::finishDrawSession() {
    if (m_drawId >= 0)
        endDraw(m_drawId);
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
    b.kind     = PdfAnnKind::Text;
    b.text.clear();                                  // aber OHNE Text
    b.points.clear();
    b.anchored = false;
    return b;
}

PdfEditBox PdfEditController::seededDraw(PdfAnnKind kind) const {
    PdfEditBox b;
    b.kind      = kind;
    b.stroke    = m_defStroke;
    b.lineWidth = m_defLineWidth;
    b.fill      = m_defFill;
    return b;
}

PdfEditBox PdfEditController::seededReplace() const {
    PdfEditBox b = m_replaceTpl;                     // eigene Replace-Vorlage
    b.id       = 0;
    b.kind     = PdfAnnKind::Replace;
    b.text.clear();                                  // aber OHNE Text
    b.points.clear();
    b.anchored = false;
    b.highlight = QColor(255, 255, 255, 255);        // Deckfläche fix Weiß
    return b;
}

PdfEditBox PdfEditController::makeReplaceTpl() {
    // Startwerte des „Text ersetzen"-Werkzeugs: schwarzer Text auf deckendem
    // Weiß (ersetzt gedruckten Text — kein Post-it-Gelb), oben-links.
    PdfEditBox b;
    b.kind      = PdfAnnKind::Replace;
    b.color     = QColor(0, 0, 0);
    b.highlight = QColor(255, 255, 255, 255);
    b.alignment = 0;
    b.vAlign    = 0;
    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Benötigte Boxhöhe für den Textinhalt (feste Breite, Umbruch) — dieselbe
//  QTextLayout-Mathematik wie der Export-Zeichner (drawBox), nur ohne Gerät:
//  QFont::setPixelSize nimmt Ganzzahlen, daher wird mit Faktor k skaliert
//  (Sub-Punkt-Präzision); PreferNoHinting hält die Metriken linear skalierbar.
// ─────────────────────────────────────────────────────────────────────────────
qreal PdfEditController::requiredHeightPt(const PdfEditBox& b) {
    if (b.text.isEmpty())
        return b.rect.height();
    const qreal k = 8.0;
    QFont f(b.fontFamily);
    f.setPixelSize(qMax(1, qRound(qMax(1.0, b.fontSizePt) * k)));
    f.setBold(b.bold);
    f.setItalic(b.italic);
    f.setUnderline(b.underline);
    f.setHintingPreference(QFont::PreferNoHinting);

    const qreal availW = qMax<qreal>(4.0, b.rect.width() - 2.0 * kBoxPaddingPt) * k;

    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

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
    return totalH / k + 2.0 * kBoxPaddingPt;
}

void PdfEditController::mirrorToTemplate(PdfEditField f, const QVariant& v, PdfAnnKind kind) {
    if (kind == PdfAnnKind::Text || kind == PdfAnnKind::Replace) {
        PdfEditBox& tpl = (kind == PdfAnnKind::Replace) ? m_replaceTpl : m_textTpl;
        switch (f) {
        case PdfEditField::FontFamily: tpl.fontFamily = v.toString();     break;
        case PdfEditField::FontSize:   tpl.fontSizePt = v.toReal();       break;
        case PdfEditField::Bold:       tpl.bold       = v.toBool();       break;
        case PdfEditField::Italic:     tpl.italic     = v.toBool();       break;
        case PdfEditField::Underline:  tpl.underline  = v.toBool();       break;
        case PdfEditField::Color:      tpl.color      = v.value<QColor>();break;
        case PdfEditField::Highlight:
            // Replace: Deckfläche bleibt fix Weiß — kein Vorlagen-Nachzug.
            if (kind == PdfAnnKind::Text)
                tpl.highlight = v.value<QColor>();
            break;
        case PdfEditField::Alignment:  tpl.alignment  = v.toInt();        break;
        case PdfEditField::VAlign:     tpl.vAlign     = v.toInt();        break;
        default: break;
        }
    } else {
        switch (f) {
        case PdfEditField::Stroke:    m_defStroke    = v.value<QColor>(); break;
        case PdfEditField::LineWidth: m_defLineWidth = v.toReal();        break;
        case PdfEditField::Fill:      m_defFill      = v.value<QColor>(); break;
        default: break;
        }
    }
    ++m_defaultRev;
    emit defaultRevChanged();
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
    finishDrawSession();
    PdfEditBox b = m_clip;                           // inkl. Text + allen Einstellungen
    b.id = m_nextId++;
    // Leicht versetzt auf derselben Seite, damit die Kopie sichtbar liegt;
    // Strich-Punkte wandern synchron mit.
    b.rect.translate(14.0, 14.0);
    for (QPointF& p : b.points)
        p += QPointF(14.0, 14.0);
    setTool(Select);
    pushCommand(new PdfEditAddCommand(&m_model, b, m_model.count()));
    setSelectedId(b.id);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Geometrie-Session
// ─────────────────────────────────────────────────────────────────────────────
void PdfEditController::beginGeometryEdit(int id) {
    finishOpenSessions();
    finishDrawSession();
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    m_geoEditId  = id;
    m_geoOldPage = b->page;
    m_geoOld     = b->rect;
    m_geoOldPts  = b->points;
}

//  Striche: Punkte proportional vom Session-Basis- in das neue Rechteck
//  abbilden (Verschieben = Translation, Skalieren = Streckung) — identische
//  Mathematik wie ImageEditController::updateGeometry.
static QVector<QPointF> transformPoints(const QVector<QPointF>& oldPts,
                                        const QRectF& oldRect, const QRectF& r) {
    QVector<QPointF> pts;
    if (oldPts.isEmpty() || oldRect.width() <= 0.0 || oldRect.height() <= 0.0)
        return pts;
    const qreal sx = r.width()  / oldRect.width();
    const qreal sy = r.height() / oldRect.height();
    pts.reserve(oldPts.size());
    for (const QPointF& p : oldPts)
        pts.append(QPointF(r.x() + (p.x() - oldRect.x()) * sx,
                           r.y() + (p.y() - oldRect.y()) * sy));
    return pts;
}

void PdfEditController::updateGeometry(int id, qreal xPt, qreal yPt,
                                       qreal wPt, qreal hPt) {
    if (id != m_geoEditId)
        return;                                     // nur innerhalb einer Session
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    // Zeichnungen dürfen deutlich kleiner werden als Textboxen.
    const qreal minW = b->hasText() ? kMinBoxWPt : kMinDrawPt;
    const qreal minH = b->hasText() ? kMinBoxHPt : kMinDrawPt;
    QRectF r(xPt, yPt, qMax(minW, wPt), qMax(minH, hPt));
    if (r.x() < 0.0) r.moveLeft(0.0);
    if (r.y() < 0.0) r.moveTop(0.0);
    if (!m_geoOldPts.isEmpty())
        m_model.applyPlacementPoints(id, b->page, r,
                                     transformPoints(m_geoOldPts, m_geoOld, r));
    else
        m_model.applyGeometry(id, r);               // live, KEIN Kommando je Move
}

void PdfEditController::updatePlacement(int id, int page, qreal xPt, qreal yPt,
                                        qreal wPt, qreal hPt) {
    if (id != m_geoEditId)
        return;                                     // nur innerhalb einer Session
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    const qreal minW = b->hasText() ? kMinBoxWPt : kMinDrawPt;
    const qreal minH = b->hasText() ? kMinBoxHPt : kMinDrawPt;
    QRectF r(xPt, yPt, qMax(minW, wPt), qMax(minH, hPt));
    if (r.x() < 0.0) r.moveLeft(0.0);
    // y bewusst NICHT klemmen: über den Seitenrand gezogene Zwischenzustände
    // (seitenübergreifendes Verschieben) brauchen y < 0 bzw. y > Seitenhöhe;
    // die Zielseite + finale Klemmung liefert QML beim Loslassen. Strich-
    // Punkte wandern synchron mit (Translation/Streckung aus der Session-Basis).
    if (!m_geoOldPts.isEmpty())
        m_model.applyPlacementPoints(id, page, r,
                                     transformPoints(m_geoOldPts, m_geoOld, r));
    else
        m_model.applyPlacement(id, page, r);        // live, KEIN Kommando je Move
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
    if (b && (b->rect != m_geoOld || b->page != m_geoOldPage
              || b->points != m_geoOldPts))
        pushCommand(new PdfEditGeometryCommand(&m_model, id,
                                               m_geoOldPage, m_geoOld, m_geoOldPts,
                                               b->page, b->rect, b->points));
    m_geoOldPts.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Text-Session
// ─────────────────────────────────────────────────────────────────────────────
void PdfEditController::beginTextEdit(int id) {
    finishOpenSessions();
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    m_textEditId  = id;
    m_textOld     = b->text;
    m_textOldRect = b->rect;      // Basis des Höhenwachstums („Text ersetzen")
    emit textEditingChanged();
}

void PdfEditController::updateText(int id, const QString& text) {
    if (id != m_textEditId)
        return;
    m_model.applyText(id, text);                    // live, KEIN Kommando je Taste

    // „Text ersetzen"-Boxen wachsen mit dem Inhalt: feste Breite, automatischer
    // Umbruch, Höhe folgt dem Text (nie automatisches Schrumpfen — der Nutzer
    // kann über die Handles jederzeit selbst verkleinern). Live ohne Kommando;
    // die Höhenänderung wird am Session-Ende mit dem Text-Delta zu EINEM
    // Undo-Schritt zusammengefasst (finishTextSession-Makro).
    const PdfEditBox* b = m_model.boxById(id);
    if (b && b->kind == PdfAnnKind::Replace) {
        const qreal need = requiredHeightPt(*b);
        if (need > b->rect.height() + 0.5) {
            QRectF r = b->rect;
            r.setHeight(need);
            m_model.applyGeometry(id, r);
        }
    }
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
    if (!b)
        return;
    const bool textChanged = b->text != m_textOld;
    const bool rectChanged = b->rect != m_textOldRect;
    if (textChanged && rectChanged) {
        // Automatisches Höhenwachstum („Text ersetzen"): Text- und Geometrie-
        // Delta zu EINEM Undo-Schritt zusammenfassen — Undo stellt Text UND
        // ursprüngliche Boxhöhe gemeinsam wieder her.
        m_stack.beginMacro(QStringLiteral("text"));
        m_stack.push(new PdfEditTextCommand(&m_model, id, m_textOld, b->text));
        m_stack.push(new PdfEditGeometryCommand(&m_model, id,
                                                b->page, m_textOldRect, {},
                                                b->page, b->rect, {}));
        m_stack.endMacro();
    } else if (textChanged) {
        pushCommand(new PdfEditTextCommand(&m_model, id, m_textOld, b->text));
    } else if (rectChanged) {
        pushCommand(new PdfEditGeometryCommand(&m_model, id,
                                               b->page, m_textOldRect, {},
                                               b->page, b->rect, {}));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Stil/Format
// ─────────────────────────────────────────────────────────────────────────────
void PdfEditController::setBoxField(int id, PdfEditField f, const QVariant& v) {
    if (id < 0) {
        // Nur die Vorlage/Default für NEUE Annotationen setzen (kein Kommando):
        // Zeichen-Felder → Zeichen-Defaults; Text-Felder → Vorlage des AKTIVEN
        // Werkzeugs (Text ersetzen pflegt seine eigene, sonst die Post-its).
        const bool draw = (f == PdfEditField::Stroke || f == PdfEditField::LineWidth
                           || f == PdfEditField::Fill);
        const PdfAnnKind tplKind = draw ? PdfAnnKind::Freehand
                                 : (m_tool == ReplaceTool ? PdfAnnKind::Replace
                                                          : PdfAnnKind::Text);
        mirrorToTemplate(f, v, tplKind);
        return;
    }
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    // Invariante „Text ersetzen": Die Deckfläche ist fix deckendes Weiß — es
    // gibt in dieser Phase bewusst keine Farbwahl (UI ausgeblendet, hier
    // zusätzlich defensiv gesperrt).
    if (f == PdfEditField::Highlight && b->kind == PdfAnnKind::Replace)
        return;
    QVariant old;
    switch (f) {
    case PdfEditField::Stroke:     old = b->stroke;     break;
    case PdfEditField::LineWidth:  old = b->lineWidth;  break;
    case PdfEditField::Fill:       old = b->fill;       break;
    case PdfEditField::FontFamily: old = b->fontFamily; break;
    case PdfEditField::FontSize:   old = b->fontSizePt; break;
    case PdfEditField::Bold:       old = b->bold;       break;
    case PdfEditField::Italic:     old = b->italic;     break;
    case PdfEditField::Underline:  old = b->underline;  break;
    case PdfEditField::Color:      old = b->color;      break;
    case PdfEditField::Highlight:  old = b->highlight;  break;
    case PdfEditField::Alignment:  old = b->alignment;  break;
    case PdfEditField::VAlign:     old = b->vAlign;     break;
    default: return;                                // Text/Geometry/Points: eigene Wege
    }
    // Vorlage nachziehen (Stil-Erben), passend zur Annotationsart.
    mirrorToTemplate(f, v, b->kind);
    if (old == v)
        return;
    pushCommand(new PdfEditFieldCommand(&m_model, id, f, old, v));
}

void PdfEditController::setBoxStroke(int id, const QColor& c) {
    if (c.isValid())
        setBoxField(id, PdfEditField::Stroke, c);
}
void PdfEditController::setBoxLineWidth(int id, qreal wPt) {
    setBoxField(id, PdfEditField::LineWidth, qBound(0.2, wPt, 72.0));
}
void PdfEditController::setBoxFill(int id, const QColor& c) {
    if (c.isValid())
        setBoxField(id, PdfEditField::Fill, c);
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
    m.insert(QStringLiteral("kind"),           static_cast<int>(b->kind));
    m.insert(QStringLiteral("isText"),         b->kind == PdfAnnKind::Text);
    m.insert(QStringLiteral("isReplace"),      b->kind == PdfAnnKind::Replace);
    m.insert(QStringLiteral("isStroke"),       b->isStroke());
    m.insert(QStringLiteral("isShape"),        b->kind == PdfAnnKind::Rect
                                               || b->kind == PdfAnnKind::Ellipse);
    m.insert(QStringLiteral("xPt"),            b->rect.x());
    m.insert(QStringLiteral("yPt"),            b->rect.y());
    m.insert(QStringLiteral("wPt"),            b->rect.width());
    m.insert(QStringLiteral("hPt"),            b->rect.height());
    m.insert(QStringLiteral("strokeColor"),    b->stroke);
    m.insert(QStringLiteral("lineWidth"),      b->lineWidth);
    m.insert(QStringLiteral("fillColor"),      b->fill);
    m.insert(QStringLiteral("hasFill"),        b->fill.alpha() > 0);
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

QVariantMap PdfEditController::defaultInfo() const {
    // Vorlagen-Defaults für neue Annotationen (Panel liest sie rev-getrieben
    // über defaultRev, wenn nichts ausgewählt ist — Muster wie Bild-Editor).
    // Die Text-Felder kommen aus der Vorlage des AKTIVEN Werkzeugs: „Text
    // ersetzen" pflegt eine eigene (schwarz auf fix Weiß), sonst die Post-its.
    const PdfEditBox& tpl = (m_tool == ReplaceTool) ? m_replaceTpl : m_textTpl;
    QVariantMap m;
    m.insert(QStringLiteral("strokeColor"),    m_defStroke);
    m.insert(QStringLiteral("lineWidth"),      m_defLineWidth);
    m.insert(QStringLiteral("fillColor"),      m_defFill);
    m.insert(QStringLiteral("hasFill"),        m_defFill.alpha() > 0);
    m.insert(QStringLiteral("fontFamily"),     tpl.fontFamily);
    m.insert(QStringLiteral("fontSizePt"),     tpl.fontSizePt);
    m.insert(QStringLiteral("bold"),           tpl.bold);
    m.insert(QStringLiteral("italic"),         tpl.italic);
    m.insert(QStringLiteral("underline"),      tpl.underline);
    m.insert(QStringLiteral("textColor"),      tpl.color);
    m.insert(QStringLiteral("highlightColor"), tpl.highlight);
    m.insert(QStringLiteral("hasHighlight"),   tpl.highlight.alpha() > 0);
    m.insert(QStringLiteral("alignment"),      tpl.alignment);
    m.insert(QStringLiteral("vAlign"),         tpl.vAlign);
    m.insert(QStringLiteral("isReplace"),      m_tool == ReplaceTool);
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
    finishDrawSession();
    m_stack.undo();
}

void PdfEditController::redo() {
    finishOpenSessions();
    finishDrawSession();
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
    finishDrawSession();

    const QString sc = sidecarPath(m_docPath);
    bool ok = false;

    const bool hasBoxes = m_model.count() > 0;
    const bool hasPlan  = !planIsIdentity();

    if (!hasBoxes && !hasPlan) {
        // Leeres Overlay UND unveränderter Seiten-Plan → kein Sidecar zurücklassen.
        ok = !QFile::exists(sc) || QFile::remove(sc);
    } else {
        QJsonObject rootObj;
        rootObj.insert(QStringLiteral("format"),  QStringLiteral("mediagallery-pdf-overlay"));
        rootObj.insert(QStringLiteral("version"), 1);
        if (hasBoxes) {
            QJsonArray arr;
            const QVector<PdfEditBox> boxes = m_model.boxes();
            for (const PdfEditBox& b : boxes)
                arr.append(b.toJson());
            rootObj.insert(QStringLiteral("boxes"), arr);
        }
        if (hasPlan) {
            // Seiten-Plan (Aufgabe 3): ≥0 Quellseite, −1 Leerseite.
            QJsonArray parr;
            for (int e : std::as_const(m_plan))
                parr.append(e);
            rootObj.insert(QStringLiteral("pageplan"), parr);
        }

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

    // Seiten-Plan (Aufgabe 3), falls vorhanden — die Validierung gegen die echte
    // Seitenzahl erfolgt in setSourcePageCount(), sobald QML sie meldet.
    m_plan.clear();
    const QJsonArray parr = o.value(QStringLiteral("pageplan")).toArray();
    for (const QJsonValue& v : parr)
        m_plan.append(v.toInt(-1));

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
    finishDrawSession();

    const QString target = exportTargetPath();
    if (target.isEmpty())
        return;

    m_busy = true;
    emit busyChanged();

    const int gen = ++m_exportGen;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    // Export rendert PRISTINE + Plan (einmalige Anwendung) — NICHT die bereits
    // gebackene Arbeitsdatei (renderSourcePath()), sonst doppelte Anwendung.
    m_pool.start(new PdfExportTask(this, pristinePath(), target,
                                   m_model.boxes(), m_plan, gen, m_cancel));
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
