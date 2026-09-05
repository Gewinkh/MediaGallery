#include "image/edit/ImageEditController.h"
#include "image/edit/ImageEditCommands.h"
#include "core/PathUtils.h"

#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QFontInfo>
#include <QTextLayout>
#include <QTextOption>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRunnable>
#include <QMetaObject>
#include <QtMath>
#include <utility>

//  Gemeinsames Overlay-Rendering (Anzeige-Delegate UND Export nutzen dieselbe
//  Geometrie -> WYSIWYG). Als freie Funktion, damit der Worker sie ohne
//  Controller-Instanz aufrufen kann; die QML-Delegates spiegeln sie 1:1.
namespace {

void drawArrowHead(QPainter& p, const QPointF& from, const QPointF& to, qreal lineWidth) {
    const qreal ang = std::atan2(to.y() - from.y(), to.x() - from.x());
    const qreal len = qMax<qreal>(14.0, lineWidth * 4.0);
    const qreal spread = M_PI / 7.0;                 // ~25.7°
    const QPointF a(to.x() - len * std::cos(ang - spread),
                    to.y() - len * std::sin(ang - spread));
    const QPointF b(to.x() - len * std::cos(ang + spread),
                    to.y() - len * std::sin(ang + spread));
    p.drawLine(to, a);
    p.drawLine(to, b);
}

void drawAnnotation(QPainter& p, const ImageAnnotation& a) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    switch (a.kind) {
    case ImageAnnKind::Freehand: {
        if (a.points.size() >= 2) {
            QPen pen(a.stroke, qMax<qreal>(0.5, a.lineWidth));
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            p.setPen(pen);
            QPainterPath path(a.points.first());
            for (int i = 1; i < a.points.size(); ++i)
                path.lineTo(a.points.at(i));
            p.drawPath(path);
        } else if (a.points.size() == 1) {
            p.setPen(Qt::NoPen);
            p.setBrush(a.stroke);
            const qreal r = qMax<qreal>(0.5, a.lineWidth) * 0.5;
            p.drawEllipse(a.points.first(), r, r);
        }
        break;
    }
    case ImageAnnKind::Arrow: {
        if (a.points.size() >= 2) {
            QPen pen(a.stroke, qMax<qreal>(0.5, a.lineWidth));
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            p.setPen(pen);
            const QPointF from = a.points.first();
            const QPointF to   = a.points.at(1);
            p.drawLine(from, to);
            drawArrowHead(p, from, to, a.lineWidth);
        }
        break;
    }
    case ImageAnnKind::Rect: {
        if (a.fill.alpha() > 0) p.setBrush(a.fill); else p.setBrush(Qt::NoBrush);
        p.setPen(QPen(a.stroke, qMax<qreal>(0.5, a.lineWidth)));
        p.drawRect(a.rect);
        break;
    }
    case ImageAnnKind::Ellipse: {
        if (a.fill.alpha() > 0) p.setBrush(a.fill); else p.setBrush(Qt::NoBrush);
        p.setPen(QPen(a.stroke, qMax<qreal>(0.5, a.lineWidth)));
        p.drawEllipse(a.rect);
        break;
    }
    case ImageAnnKind::Text: {
        const bool paper = a.highlight.alpha() > 0;
        if (paper) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 52));
            p.drawRect(a.rect.translated(ImageEditController::kNoteShadowDxPx,
                                         ImageEditController::kNoteShadowDyPx));
            p.setBrush(a.highlight);
            p.drawRect(a.rect);
            const qreal fold = qMin(ImageEditController::kNoteFoldPx,
                                    qMin(a.rect.width(), a.rect.height()) / 3.0);
            if (fold > 2.0) {
                const QPointF pA(a.rect.right() - fold, a.rect.bottom());
                const QPointF pB(a.rect.right(),        a.rect.bottom() - fold);
                const QPointF pC(a.rect.right(),        a.rect.bottom());
                QPainterPath flap;
                flap.moveTo(pA); flap.lineTo(pB); flap.lineTo(pC); flap.closeSubpath();
                QColor flapCol = a.highlight.darker(118);
                flapCol.setAlpha(a.highlight.alpha());
                p.setPen(Qt::NoPen);
                p.fillPath(flap, flapCol);
                QColor lineCol = a.highlight.darker(150);
                lineCol.setAlpha(a.highlight.alpha());
                p.setPen(QPen(lineCol, qMax<qreal>(1.0, a.lineWidth * 0.0 + 1.0)));
                p.drawLine(pA, pB);
            }
        }

        QFont f(a.fontFamily);
        f.setPixelSize(qMax<int>(1, qRound(a.fontSizePx)));
        f.setBold(a.bold);
        f.setItalic(a.italic);
        f.setUnderline(a.underline);

        const qreal pad    = ImageEditController::kBoxPaddingPx;
        const qreal availW = qMax<qreal>(4.0, a.rect.width() - 2.0 * pad);

        QTextOption opt;
        opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        opt.setAlignment(a.alignment == 1 ? Qt::AlignHCenter
                       : a.alignment == 2 ? Qt::AlignRight
                                          : Qt::AlignLeft);

        QTextLayout layout(a.text, f);
        layout.setTextOption(opt);
        layout.beginLayout();
        qreal totalH = 0.0;
        for (;;) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(availW);
            line.setPosition(QPointF(0.0, totalH));
            totalH += line.height();
        }
        layout.endLayout();

        const qreal availH = a.rect.height() - 2.0 * pad;
        const qreal yOff   = (a.vAlign == 1) ? (availH - totalH) / 2.0 : 0.0;
        p.setPen(a.color);
        layout.draw(&p, QPointF(a.rect.left() + pad, a.rect.top() + pad + yOff));
        break;
    }
    }
    p.restore();
}

//  ImageExportTask - rendert Original + Overlay in eine NEUE Bildkopie.
//  Lädt eine EIGENE QImage (kein geteiltes Handle), zeichnet die Annotationen
//  1:1 in Bild-Pixeln darüber und schreibt atomar via QSaveFile im Quellformat.
class ImageExportTask : public QRunnable {
public:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    ImageExportTask(ImageEditController* owner, QString sourcePath, QString targetPath,
                    QByteArray format, int quality,
                    QVector<ImageAnnotation> anns, int generation, CancelFlag cancel)
        : m_owner(owner)
        , m_source(std::move(sourcePath))
        , m_target(std::move(targetPath))
        , m_format(std::move(format))
        , m_quality(quality)
        , m_anns(std::move(anns))
        , m_gen(generation)
        , m_cancel(std::move(cancel)) {
        setAutoDelete(true);
    }

    void run() override {
        QString err;
        const bool ok = writeImage(&err);
        ImageEditController* owner = m_owner;
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

    bool writeImage(QString* err) {
        QImageReader reader(m_source);
        reader.setAutoTransform(false);              // stored-Orientierung = Anzeige = WYSIWYG
        QImage img = reader.read();
        if (img.isNull()) {
            *err = reader.errorString();
            return false;
        }
        if (cancelled()) { *err = QStringLiteral("cancel"); return false; }

        // In ein Format mit Alpha wandeln, falls Overlay-Transparenz sonst
        // gegen einen undefinierten Kanal liefe (JPEG wird später ohnehin
        // deckend gespeichert; PNG behält Alpha des Originals).
        if (img.format() != QImage::Format_ARGB32_Premultiplied
            && img.format() != QImage::Format_RGB32
            && img.format() != QImage::Format_ARGB32)
            img = img.convertToFormat(QImage::Format_ARGB32);

        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setRenderHint(QPainter::TextAntialiasing, true);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            for (const ImageAnnotation& a : std::as_const(m_anns)) {
                if (cancelled()) { p.end(); *err = QStringLiteral("cancel"); return false; }
                drawAnnotation(p, a);
            }
        }

        QSaveFile out(m_target);
        if (!out.open(QIODevice::WriteOnly)) {
            *err = out.errorString();
            return false;
        }
        if (!img.save(&out, m_format.constData(), m_quality)) {
            out.cancelWriting();
            *err = QStringLiteral("save");
            return false;
        }
        if (!out.commit()) {
            *err = out.errorString();
            return false;
        }
        return true;
    }

    ImageEditController*     m_owner;
    QString                  m_source;
    QString                  m_target;
    QByteArray               m_format;
    int                      m_quality;
    QVector<ImageAnnotation> m_anns;
    int                      m_gen;
    CancelFlag               m_cancel;
};

} // namespace

ImageEditController::ImageEditController(QObject* parent) : QObject(parent) {
    m_stack.setUndoLimit(kUndoLimit);
    m_pool.setMaxThreadCount(1);

    m_textTpl.kind = ImageAnnKind::Text;

    connect(&m_stack, &QUndoStack::canUndoChanged, this, [this] { emit undoStateChanged(); });
    connect(&m_stack, &QUndoStack::canRedoChanged, this, [this] { emit undoStateChanged(); });
    connect(&m_stack, &QUndoStack::cleanChanged,   this, [this] { emit dirtyChanged(); });
    connect(&m_model, &ImageEditModel::countChanged, this, [this] { emit annCountChanged(); });
    connect(&m_model, &QAbstractItemModel::dataChanged,  this, [this] { emit trackedChanged(); });
    connect(&m_model, &QAbstractItemModel::rowsInserted, this, [this] { emit trackedChanged(); });
    connect(&m_model, &QAbstractItemModel::rowsRemoved,  this, [this] { emit trackedChanged(); });
    connect(&m_model, &QAbstractItemModel::modelReset,   this, [this] { emit trackedChanged(); });

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

ImageEditController::~ImageEditController() {
    if (m_cancel)
        m_cancel->store(true, std::memory_order_relaxed);
    m_pool.clear();
    m_pool.waitForDone();
}

void ImageEditController::setEditMode(bool on) {
    if (m_editMode == on)
        return;
    finishOpenSessions();
    m_editMode = on;
    if (!on) { setTool(Select); setSelectedId(-1); }
    emit editModeChanged();
}

void ImageEditController::setTool(int t) {
    if (t < Select || t > EllipseTool || m_tool == t)
        return;
    finishOpenSessions();
    m_tool = t;
    if (t != Select)
        setSelectedId(-1);
    emit toolChanged();
}

void ImageEditController::setSelectedId(int id) {
    if (m_selectedId == id)
        return;
    m_selectedId = id;
    emit selectedIdChanged();
    bumpSelectionRev();
}

void ImageEditController::bumpSelectionRev() {
    ++m_selectionRev;
    emit selectionRevChanged();
}

void ImageEditController::setDocument(const QString& pathOrUrl) {
    const QString local = mg::toLocalPath(pathOrUrl);
    if (local == m_docPath)
        return;                                     // idempotent

    finishOpenSessions();
    if (!m_docPath.isEmpty() && !m_stack.isClean())
        saveOverlay();

    m_docPath = local;
    setSelectedId(-1);
    setTool(Select);
    if (m_recording) { m_recording = false; emit recordingChanged(); }
    m_stack.clear();
    m_model.clearAll();
    m_nextId = 1;

    m_imgW = 0; m_imgH = 0;
    if (!m_docPath.isEmpty()) {
        QImageReader reader(m_docPath);
        reader.setAutoTransform(false);
        const QSize s = reader.size();
        if (s.isValid()) { m_imgW = s.width(); m_imgH = s.height(); }
    }
    emit imageSizeChanged();

    if (!m_docPath.isEmpty())
        loadOverlay(m_docPath);
}

void ImageEditController::releaseDocument() {
    if (m_docPath.isEmpty())
        return;
    finishOpenSessions();
    if (!m_stack.isClean())
        saveOverlay();
    m_docPath.clear();
    setSelectedId(-1);
    setTool(Select);
    m_stack.clear();
    m_model.clearAll();
    m_nextId = 1;
    m_imgW = 0; m_imgH = 0;
    emit imageSizeChanged();
}

void ImageEditController::setImageSize(int w, int h) {
    if (w <= 0 || h <= 0 || (m_imgW == w && m_imgH == h))
        return;
    if (m_imgW > 0 && m_imgH > 0)
        return;                                     // bereits aus dem Header bekannt
    m_imgW = w; m_imgH = h;
    emit imageSizeChanged();
}

ImageAnnotation ImageEditController::seededText() const {
    ImageAnnotation a = m_textTpl;                  // Schrift/Farben/Deckkraft/Ausrichtung
    a.kind = ImageAnnKind::Text;
    a.text.clear();                                 // aber OHNE Text
    a.points.clear();
    return a;
}

ImageAnnotation ImageEditController::seededDraw(ImageAnnKind kind) const {
    ImageAnnotation a;
    a.kind      = kind;
    a.stroke    = m_defStroke;
    a.lineWidth = m_defLineWidth;
    a.fill      = m_defFill;
    return a;
}

void ImageEditController::mirrorToTemplate(ImageAnnField f, const QVariant& v, bool textKind) {
    if (textKind) {
        switch (f) {
        case ImageAnnField::FontFamily: m_textTpl.fontFamily = v.toString();   break;
        case ImageAnnField::FontSize:   m_textTpl.fontSizePx = v.toReal();     break;
        case ImageAnnField::Bold:       m_textTpl.bold       = v.toBool();     break;
        case ImageAnnField::Italic:     m_textTpl.italic     = v.toBool();     break;
        case ImageAnnField::Underline:  m_textTpl.underline  = v.toBool();     break;
        case ImageAnnField::Color:      m_textTpl.color      = v.value<QColor>(); break;
        case ImageAnnField::Highlight:  m_textTpl.highlight  = v.value<QColor>(); break;
        case ImageAnnField::Alignment:  m_textTpl.alignment  = v.toInt();      break;
        case ImageAnnField::VAlign:     m_textTpl.vAlign     = v.toInt();      break;
        default: break;
        }
    } else {
        switch (f) {
        case ImageAnnField::Stroke:    m_defStroke    = v.value<QColor>(); break;
        case ImageAnnField::LineWidth: m_defLineWidth = v.toReal();        break;
        case ImageAnnField::Fill:      m_defFill      = v.value<QColor>(); break;
        default: break;
        }
    }
    ++m_defaultRev;
    emit defaultRevChanged();
}

int ImageEditController::addText(qreal xPx, qreal yPx) {
    if (m_docPath.isEmpty())
        return -1;
    const qreal imgW = m_imgW > 0 ? m_imgW : 2000.0;
    const qreal imgH = m_imgH > 0 ? m_imgH : 2000.0;

    ImageAnnotation a = seededText();
    a.id = m_nextId++;
    const qreal w = qMin(qMax(140.0, imgW * 0.28), qMax(140.0, imgW - 8.0));
    const qreal h = qMax(kMinAnnPx, a.fontSizePx * 2.0 + 2.0 * kBoxPaddingPx);
    const qreal x = qMax(0.0, qMin(xPx, qMax(0.0, imgW - w)));
    const qreal y = qMax(0.0, qMin(yPx, qMax(0.0, imgH - h)));
    a.rect = QRectF(x, y, w, h);

    pushAdd(a);
    setSelectedId(a.id);
    return a.id;
}

int ImageEditController::beginDraw(int kind, qreal xPx, qreal yPx) {
    if (m_docPath.isEmpty() || kind < static_cast<int>(ImageAnnKind::Freehand)
        || kind > static_cast<int>(ImageAnnKind::Ellipse))
        return -1;
    finishOpenSessions();

    ImageAnnotation a = seededDraw(static_cast<ImageAnnKind>(kind));
    a.id = m_nextId++;
    m_drawStart = QPointF(xPx, yPx);
    switch (a.kind) {
    case ImageAnnKind::Freehand:
        a.points = { QPointF(xPx, yPx) };
        a.recomputeBounds();
        break;
    case ImageAnnKind::Arrow:
        a.points = { QPointF(xPx, yPx), QPointF(xPx, yPx) };
        a.recomputeBounds();
        break;
    default:                                        // Rect / Ellipse
        a.rect = QRectF(xPx, yPx, 0.0, 0.0);
        break;
    }
    m_model.insertAnnAt(m_model.count(), a);
    m_drawId = a.id;
    return a.id;
}

void ImageEditController::updateDraw(int id, qreal xPx, qreal yPx) {
    if (id != m_drawId)
        return;
    const ImageAnnotation* a = m_model.annById(id);
    if (!a)
        return;
    switch (a->kind) {
    case ImageAnnKind::Freehand: {
        QVector<QPointF> pts = a->points;
        pts.append(QPointF(xPx, yPx));
        m_model.applyPoints(id, pts);
        break;
    }
    case ImageAnnKind::Arrow: {
        QVector<QPointF> pts = a->points;
        if (pts.size() < 2) pts.resize(2);
        pts[0] = m_drawStart;
        pts[1] = QPointF(xPx, yPx);
        m_model.applyPoints(id, pts);
        break;
    }
    default: {                                      // Rect / Ellipse
        const QRectF r = QRectF(m_drawStart, QPointF(xPx, yPx)).normalized();
        m_model.applyGeometry(id, r);
        break;
    }
    }
}

void ImageEditController::endDraw(int id) {
    if (id != m_drawId)
        return;
    m_drawId = -1;
    const ImageAnnotation* a = m_model.annById(id);
    if (!a) return;
    ImageAnnotation copy = *a;

    const bool tooSmall =
        (copy.isStroke() && copy.points.size() < 2)
        || (!copy.isStroke() && (copy.rect.width() < 3.0 && copy.rect.height() < 3.0));
    m_model.removeById(id);
    if (tooSmall)
        return;
    pushAdd(copy);
    m_defStroke = copy.stroke; m_defLineWidth = copy.lineWidth; m_defFill = copy.fill;
    ++m_defaultRev; emit defaultRevChanged();
    setSelectedId(copy.id);
}

void ImageEditController::pushAdd(ImageAnnotation& a) {
    if (m_recording)
        a.track = ImageTrackState::Added;
    pushCommand(new ImageEditAddCommand(&m_model, a, m_model.count()));
}

void ImageEditController::setTrack(int id, ImageTrackState st) {
    const ImageAnnotation* a = m_model.annById(id);
    if (!a || a->track == st)
        return;
    pushCommand(new ImageEditFieldCommand(&m_model, id, ImageAnnField::Track,
                                          static_cast<int>(a->track),
                                          static_cast<int>(st)));
}

void ImageEditController::setRecording(bool on) {
    if (m_recording == on)
        return;
    m_recording = on;
    emit recordingChanged();
    saveOverlay();                 // der Schalter gehört zum Bild, nicht zur Sitzung
}

int ImageEditController::trackedCount() const {
    int n = 0;
    const QVector<ImageAnnotation> anns = m_model.annotations();
    for (const ImageAnnotation& a : anns)
        if (a.track != ImageTrackState::None) ++n;
    return n;
}

void ImageEditController::discardAllAnnotations() {
    if (m_model.count() == 0)
        return;
    finishOpenSessions();
    setSelectedId(-1);
    m_stack.beginMacro(QStringLiteral("discardAllAnnotations"));
    const QVector<ImageAnnotation> anns = m_model.annotations();
    for (const ImageAnnotation& a : anns) {
        const int row = m_model.indexOfId(a.id);
        if (row >= 0)
            pushCommand(new ImageEditRemoveCommand(&m_model, a, row));
    }
    m_stack.endMacro();
    saveOverlay();
}

void ImageEditController::acceptChange(int id) {
    const ImageAnnotation* a = m_model.annById(id);
    if (!a || a->track == ImageTrackState::None)
        return;
    if (a->track == ImageTrackState::Deleted) {
        const int row = m_model.indexOfId(id);
        const ImageAnnotation copy = *a;
        if (m_selectedId == id)
            setSelectedId(-1);
        pushCommand(new ImageEditRemoveCommand(&m_model, copy, row));
        return;
    }
    setTrack(id, ImageTrackState::None);
}

void ImageEditController::rejectChange(int id) {
    const ImageAnnotation* a = m_model.annById(id);
    if (!a || a->track == ImageTrackState::None)
        return;
    if (a->track == ImageTrackState::Added) {
        const int row = m_model.indexOfId(id);
        const ImageAnnotation copy = *a;
        if (m_selectedId == id)
            setSelectedId(-1);
        pushCommand(new ImageEditRemoveCommand(&m_model, copy, row));
        return;
    }
    setTrack(id, ImageTrackState::None);
}

void ImageEditController::acceptAllChanges() {
    if (trackedCount() == 0)
        return;
    finishOpenSessions();
    m_stack.beginMacro(QStringLiteral("acceptAllChanges"));
    const QVector<ImageAnnotation> anns = m_model.annotations();
    for (const ImageAnnotation& a : anns)
        if (a.track != ImageTrackState::None) acceptChange(a.id);
    m_stack.endMacro();
}

void ImageEditController::rejectAllChanges() {
    if (trackedCount() == 0)
        return;
    finishOpenSessions();
    m_stack.beginMacro(QStringLiteral("rejectAllChanges"));
    const QVector<ImageAnnotation> anns = m_model.annotations();
    for (const ImageAnnotation& a : anns)
        if (a.track != ImageTrackState::None) rejectChange(a.id);
    m_stack.endMacro();
}

void ImageEditController::removeAnn(int id) {
    const int row = m_model.indexOfId(id);
    const ImageAnnotation* a = m_model.annById(id);
    if (row < 0 || !a)
        return;
    const ImageAnnotation copy = *a;
    finishOpenSessions();
    if (m_selectedId == id)
        setSelectedId(-1);
    //  Wie im PDF-Editor: bei laufender Aufzeichnung wird MARKIERT statt
    //  entfernt (sonst ließe sich das Verwerfen nicht zurücknehmen); eine in
    //  derselben Aufzeichnung entstandene Annotation verschwindet ganz.
    if (m_recording && copy.track != ImageTrackState::Added) {
        setTrack(id, ImageTrackState::Deleted);
        return;
    }
    pushCommand(new ImageEditRemoveCommand(&m_model, copy, row));
}

void ImageEditController::copySelected() {
    const ImageAnnotation* a = m_model.annById(m_selectedId);
    if (!a)
        return;
    m_clip = *a;
    if (!m_hasClip) { m_hasClip = true; emit clipboardChanged(); }
}

void ImageEditController::paste() {
    if (!m_hasClip || m_docPath.isEmpty())
        return;
    finishOpenSessions();
    ImageAnnotation a = m_clip;
    a.id = m_nextId++;
    const qreal imgW = m_imgW > 0 ? m_imgW : (a.rect.right() + 200.0);
    const qreal imgH = m_imgH > 0 ? m_imgH : (a.rect.bottom() + 200.0);
    qreal dx = qMax<qreal>(16.0, imgW * 0.03);
    qreal dy = qMax<qreal>(16.0, imgH * 0.03);
    if (a.rect.right()  + dx > imgW) dx = qMax<qreal>(0.0, imgW - a.rect.right());
    if (a.rect.bottom() + dy > imgH) dy = qMax<qreal>(0.0, imgH - a.rect.bottom());
    a.rect.translate(dx, dy);
    for (QPointF& p : a.points)
        p += QPointF(dx, dy);

    setTool(Select);
    pushAdd(a);
    setSelectedId(a.id);
}

void ImageEditController::beginGeometryEdit(int id) {
    finishOpenSessions();
    const ImageAnnotation* a = m_model.annById(id);
    if (!a)
        return;
    m_geoEditId  = id;
    m_geoOldRect = a->rect;
    m_geoOldPts  = a->points;
}

void ImageEditController::updateGeometry(int id, qreal xPx, qreal yPx, qreal wPx, qreal hPx) {
    if (id != m_geoEditId)
        return;
    QRectF r(xPx, yPx, qMax(kMinAnnPx, wPx), qMax(kMinAnnPx, hPx));
    if (r.x() < 0.0) r.moveLeft(0.0);
    if (r.y() < 0.0) r.moveTop(0.0);

    if (!m_geoOldPts.isEmpty() && m_geoOldRect.width() > 0.0 && m_geoOldRect.height() > 0.0) {
        // Striche: Punkte proportional vom alten in das neue Rechteck abbilden
        // (Verschieben = Translation, Skalieren = Streckung).
        const qreal sx = r.width()  / m_geoOldRect.width();
        const qreal sy = r.height() / m_geoOldRect.height();
        QVector<QPointF> pts;
        pts.reserve(m_geoOldPts.size());
        for (const QPointF& p : m_geoOldPts)
            pts.append(QPointF(r.x() + (p.x() - m_geoOldRect.x()) * sx,
                               r.y() + (p.y() - m_geoOldRect.y()) * sy));
        m_model.applyGeometryPoints(id, r, pts);
    } else {
        m_model.applyGeometry(id, r);
    }
}

void ImageEditController::endGeometryEdit(int id) {
    if (id == m_geoEditId)
        finishGeometrySession();
}

void ImageEditController::finishGeometrySession() {
    if (m_geoEditId < 0)
        return;
    const int id = m_geoEditId;
    m_geoEditId = -1;
    const ImageAnnotation* a = m_model.annById(id);
    if (a && (a->rect != m_geoOldRect || a->points != m_geoOldPts))
        pushCommand(new ImageEditGeometryCommand(&m_model, id,
                                                 m_geoOldRect, m_geoOldPts,
                                                 a->rect, a->points));
    m_geoOldPts.clear();
}

void ImageEditController::finishDrawSession() {
    if (m_drawId >= 0)
        endDraw(m_drawId);
}

void ImageEditController::beginTextEdit(int id) {
    finishOpenSessions();
    const ImageAnnotation* a = m_model.annById(id);
    if (!a)
        return;
    m_textEditId = id;
    m_textOld    = a->text;
    emit textEditingChanged();
}

void ImageEditController::updateText(int id, const QString& text) {
    if (id != m_textEditId)
        return;
    m_model.applyText(id, text);
}

void ImageEditController::endTextEdit(int id) {
    if (id == m_textEditId)
        finishTextSession();
}

void ImageEditController::finishTextSession() {
    if (m_textEditId < 0)
        return;
    const int id = m_textEditId;
    m_textEditId = -1;
    emit textEditingChanged();
    const ImageAnnotation* a = m_model.annById(id);
    if (a && a->text != m_textOld)
        pushCommand(new ImageEditTextCommand(&m_model, id, m_textOld, a->text));
}

void ImageEditController::setAnnField(int id, ImageAnnField f, const QVariant& v) {
    if (id < 0) {
        const bool draw = (f == ImageAnnField::Stroke || f == ImageAnnField::LineWidth
                           || f == ImageAnnField::Fill);
        mirrorToTemplate(f, v, !draw);
        return;
    }
    const ImageAnnotation* a = m_model.annById(id);
    if (!a)
        return;
    QVariant old;
    switch (f) {
    case ImageAnnField::Stroke:    old = a->stroke;     break;
    case ImageAnnField::LineWidth: old = a->lineWidth;  break;
    case ImageAnnField::Fill:      old = a->fill;       break;
    case ImageAnnField::FontFamily:old = a->fontFamily; break;
    case ImageAnnField::FontSize:  old = a->fontSizePx; break;
    case ImageAnnField::Bold:      old = a->bold;       break;
    case ImageAnnField::Italic:    old = a->italic;     break;
    case ImageAnnField::Underline: old = a->underline;  break;
    case ImageAnnField::Color:     old = a->color;      break;
    case ImageAnnField::Highlight: old = a->highlight;  break;
    case ImageAnnField::Alignment: old = a->alignment;  break;
    case ImageAnnField::VAlign:    old = a->vAlign;     break;
    default: return;
    }
    mirrorToTemplate(f, v, a->kind == ImageAnnKind::Text);
    if (old == v)
        return;
    pushCommand(new ImageEditFieldCommand(&m_model, id, f, old, v));
}

void ImageEditController::setAnnStroke(int id, const QColor& c) {
    if (c.isValid()) setAnnField(id, ImageAnnField::Stroke, c);
}
void ImageEditController::setAnnLineWidth(int id, qreal w) {
    setAnnField(id, ImageAnnField::LineWidth, qBound(0.5, w, 200.0));
}
void ImageEditController::setAnnFill(int id, const QColor& c) {
    if (c.isValid()) setAnnField(id, ImageAnnField::Fill, c);
}
void ImageEditController::setAnnFont(int id, const QString& family) {
    setAnnField(id, ImageAnnField::FontFamily, family);
}
void ImageEditController::setAnnFontSize(int id, qreal sizePx) {
    setAnnField(id, ImageAnnField::FontSize, qBound(4.0, sizePx, 800.0));
}
void ImageEditController::setAnnBold(int id, bool v)      { setAnnField(id, ImageAnnField::Bold, v); }
void ImageEditController::setAnnItalic(int id, bool v)    { setAnnField(id, ImageAnnField::Italic, v); }
void ImageEditController::setAnnUnderline(int id, bool v) { setAnnField(id, ImageAnnField::Underline, v); }
void ImageEditController::setAnnColor(int id, const QColor& c) {
    if (c.isValid()) setAnnField(id, ImageAnnField::Color, QColor(c.red(), c.green(), c.blue()));
}
void ImageEditController::setAnnHighlight(int id, const QColor& c) {
    if (c.isValid()) setAnnField(id, ImageAnnField::Highlight, c);
}
void ImageEditController::setAnnAlignment(int id, int align) {
    setAnnField(id, ImageAnnField::Alignment, qBound(0, align, 2));
}
void ImageEditController::setAnnVAlign(int id, int vAlign) {
    setAnnField(id, ImageAnnField::VAlign, qBound(0, vAlign, 1));
}

QVariantMap ImageEditController::annInfo(int id) const {
    QVariantMap m;
    const ImageAnnotation* a = m_model.annById(id);
    m.insert(QStringLiteral("exists"), a != nullptr);
    if (!a)
        return m;
    m.insert(QStringLiteral("kind"),           static_cast<int>(a->kind));
    m.insert(QStringLiteral("track"),          static_cast<int>(a->track));
    m.insert(QStringLiteral("isStroke"),       a->isStroke());
    m.insert(QStringLiteral("isText"),         a->kind == ImageAnnKind::Text);
    m.insert(QStringLiteral("isShape"),        a->kind == ImageAnnKind::Rect
                                               || a->kind == ImageAnnKind::Ellipse);
    m.insert(QStringLiteral("xPx"),            a->rect.x());
    m.insert(QStringLiteral("yPx"),            a->rect.y());
    m.insert(QStringLiteral("wPx"),            a->rect.width());
    m.insert(QStringLiteral("hPx"),            a->rect.height());
    m.insert(QStringLiteral("strokeColor"),    a->stroke);
    m.insert(QStringLiteral("lineWidth"),      a->lineWidth);
    m.insert(QStringLiteral("fillColor"),      a->fill);
    m.insert(QStringLiteral("hasFill"),        a->fill.alpha() > 0);
    m.insert(QStringLiteral("text"),           a->text);
    m.insert(QStringLiteral("fontFamily"),     a->fontFamily);
    m.insert(QStringLiteral("fontSizePx"),     a->fontSizePx);
    m.insert(QStringLiteral("bold"),           a->bold);
    m.insert(QStringLiteral("italic"),         a->italic);
    m.insert(QStringLiteral("underline"),      a->underline);
    m.insert(QStringLiteral("textColor"),      a->color);
    m.insert(QStringLiteral("highlightColor"), a->highlight);
    m.insert(QStringLiteral("hasHighlight"),   a->highlight.alpha() > 0);
    m.insert(QStringLiteral("alignment"),      a->alignment);
    m.insert(QStringLiteral("vAlign"),         a->vAlign);
    return m;
}

QVariantMap ImageEditController::defaultInfo() const {
    QVariantMap m;
    m.insert(QStringLiteral("strokeColor"),    m_defStroke);
    m.insert(QStringLiteral("lineWidth"),      m_defLineWidth);
    m.insert(QStringLiteral("fillColor"),      m_defFill);
    m.insert(QStringLiteral("hasFill"),        m_defFill.alpha() > 0);
    m.insert(QStringLiteral("fontFamily"),     m_textTpl.fontFamily);
    m.insert(QStringLiteral("fontSizePx"),     m_textTpl.fontSizePx);
    m.insert(QStringLiteral("bold"),           m_textTpl.bold);
    m.insert(QStringLiteral("italic"),         m_textTpl.italic);
    m.insert(QStringLiteral("underline"),      m_textTpl.underline);
    m.insert(QStringLiteral("textColor"),      m_textTpl.color);
    m.insert(QStringLiteral("highlightColor"), m_textTpl.highlight);
    m.insert(QStringLiteral("hasHighlight"),   m_textTpl.highlight.alpha() > 0);
    m.insert(QStringLiteral("alignment"),      m_textTpl.alignment);
    m.insert(QStringLiteral("vAlign"),         m_textTpl.vAlign);
    return m;
}

void ImageEditController::pushCommand(QUndoCommand* cmd) {
    m_stack.push(cmd);                              // führt redo() sofort aus
}

void ImageEditController::undo() {
    finishOpenSessions();
    finishDrawSession();
    m_stack.undo();
}

void ImageEditController::redo() {
    finishOpenSessions();
    finishDrawSession();
    m_stack.redo();
}

QString ImageEditController::sidecarPath(const QString& imgPath) {
    return imgPath + QStringLiteral(".mgedit.json");
}

bool ImageEditController::saveOverlay() {
    if (m_docPath.isEmpty())
        return false;
    finishOpenSessions();
    finishDrawSession();

    const QString sc = sidecarPath(m_docPath);
    bool ok = false;

    if (m_model.count() == 0) {
        ok = !QFile::exists(sc) || QFile::remove(sc);
    } else {
        QJsonArray arr;
        const QVector<ImageAnnotation> anns = m_model.annotations();
        for (const ImageAnnotation& a : anns)
            arr.append(a.toJson());
        QJsonObject rootObj;
        rootObj.insert(QStringLiteral("format"),  QStringLiteral("mediagallery-image-overlay"));
        rootObj.insert(QStringLiteral("version"), 1);
        rootObj.insert(QStringLiteral("anns"),    arr);
        if (m_recording)
            rootObj.insert(QStringLiteral("recording"), true);

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

bool ImageEditController::loadOverlay(const QString& imgPath) {
    const QString sc = sidecarPath(imgPath);
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
        != QLatin1String("mediagallery-image-overlay"))
        return false;

    if (m_recording != o.value(QStringLiteral("recording")).toBool(false)) {
        m_recording = o.value(QStringLiteral("recording")).toBool(false);
        emit recordingChanged();
    }

    QVector<ImageAnnotation> anns;
    const QJsonArray arr = o.value(QStringLiteral("anns")).toArray();
    anns.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        if (!v.isObject())
            continue;
        ImageAnnotation a = ImageAnnotation::fromJson(v.toObject());
        a.id = m_nextId++;                          // IDs sind sitzungslokal
        anns.append(a);
    }
    m_model.resetAnns(anns);
    return true;
}

QString ImageEditController::uniqueCopyPath(const QString& imgPath, const QString& ext) {
    const QFileInfo fi(imgPath);
    const QString dir  = fi.absolutePath();
    const QString base = fi.completeBaseName() + QStringLiteral("_bearbeitet");
    QString candidate = dir + QLatin1Char('/') + base + QLatin1Char('.') + ext;
    int n = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QLatin1Char('/') + base
                    + QStringLiteral(" (%1).").arg(n) + ext;
        ++n;
    }
    return candidate;
}

QString ImageEditController::exportTargetPath() const {
    if (m_docPath.isEmpty())
        return {};
    const QString srcExt = QFileInfo(m_docPath).suffix().toLower();
    QString outExt = (srcExt == QLatin1String("jpg") || srcExt == QLatin1String("jpeg")
                      || srcExt == QLatin1String("png")) ? srcExt : QStringLiteral("png");
    return uniqueCopyPath(m_docPath, outExt);
}

void ImageEditController::exportImage() {
    if (m_busy || m_docPath.isEmpty())
        return;
    finishOpenSessions();
    finishDrawSession();

    const QString target = exportTargetPath();
    if (target.isEmpty())
        return;

    const QString outExt = QFileInfo(target).suffix().toLower();
    QByteArray fmt;
    int quality = -1;
    if (outExt == QLatin1String("jpg") || outExt == QLatin1String("jpeg")) {
        fmt = "JPG";  quality = kJpegExportQuality;
    } else {
        fmt = "PNG";  quality = -1;
    }

    m_busy = true;
    emit busyChanged();

    const int gen = ++m_exportGen;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    m_pool.start(new ImageExportTask(this, m_docPath, target, fmt, quality,
                                     m_model.annotations(), gen, m_cancel));
}

void ImageEditController::exportTaskFinished(bool ok, const QString& target,
                                             const QString& error, int generation) {
    if (generation != m_exportGen)
        return;
    m_busy = false;
    emit busyChanged();
    emit exportFinished(ok, target, error);
}

QStringList ImageEditController::standardFonts() const {
    return { QStringLiteral("Times New Roman"),
             QStringLiteral("Arial"),
             QStringLiteral("Calibri"),
             QStringLiteral("Helvetica"),
             QStringLiteral("Courier New") };
}

QString ImageEditController::resolvedFont(const QString& family) const {
    return QFontInfo(QFont(family)).family();
}
