#pragma once
// Overlay wie beim PDF-Editor: das Bild bleibt unveraendert, Annotationen liegen
// darueber und im Sidecar. Alle Geometrien in nativen Bild-Pixeln (Ursprung oben-links);
// Text/Rect/Ellipse haben rect, Freihand/Pfeil points mit abgeleiteter Bounding-Box.

#include <QString>
#include <QRectF>
#include <QPointF>
#include <QVarLengthArray>
#include <QVector>
#include <QColor>
#include "core/MGEditBin.h"

#include <QJsonObject>
#include <QJsonArray>

enum class ImageAnnKind {
    Text     = 0,   // Post-it-artige Textnotiz (volle Parität zum PDF-Editor)
    Freehand = 1,   // Freihand-Stift (Polylinie)
    Arrow    = 2,   // Pfeil (Start -> Ende)
    Rect     = 3,   // Rechteck (Kontur + optionale Füllung)
    Ellipse  = 4    // Ellipse (Kontur + optionale Füllung)
};

// Nachverfolgte Änderung, identisch zum PDF-Editor (`PdfTrackState`), damit beide dieselbe Semantik teilen.
// `Deleted` BLEIBT bis zur Entscheidung stehen, sonst ließe sich das Verwerfen der Löschung nicht zurücknehmen.
enum class ImageTrackState { None = 0, Added = 1, Deleted = 2 };

enum class ImageAnnField {
    Track,         // Zustand der Nachverfolgung (ImageTrackState)
    Text,
    Geometry,      // rect (+ bei Strichen zusätzlich points, s. GeometryCommand)
    Points,
    FontFamily,
    FontSize,
    Bold,
    Italic,
    Underline,
    Color,
    Highlight,
    Alignment,
    VAlign,
    Stroke,        // Linienfarbe (Formen/Striche)
    LineWidth,     // Linienbreite (Formen/Striche)
    Fill           // Füllfarbe (Rect/Ellipse)
};

struct ImageAnnotation {
    int          id   = 0;                       // laufende Sitzungs-ID (nicht persistiert)
    ImageAnnKind kind = ImageAnnKind::Text;
    QRectF       rect;                           // Bounding-Box in Bild-Pixeln
    QVector<QPointF> points;                     // Freihand/Pfeil: Stützpunkte (Bild-px)

    QColor stroke    = QColor(230, 44, 44);      // Linienfarbe (deckend)
    qreal  lineWidth = 4.0;                       // Linienbreite in Bild-Pixeln
    QColor fill      = QColor(0, 0, 0, 0);       // Füllung Rect/Ellipse (a=0 -> nur Kontur)

    QString text;
    QString fontFamily = QStringLiteral("Helvetica");
    qreal   fontSizePx = 28.0;                   // Bild-Pixel (nicht Punkte)
    bool    bold       = false;
    bool    italic     = false;
    bool    underline  = false;
    QColor  color      = QColor(0, 0, 0);        // Textfarbe (deckend)
    // Notiz-Hintergrund („Post-it-Papier"): füllt das ganze Box-Rechteck; Alpha 0
    // = kein Papier (reiner Text). Standard = klassisches Haftnotiz-Gelb.
    QColor  highlight  = QColor(254, 243, 155, 232);
    int     alignment  = 0;                       // 0=links, 1=zentriert, 2=rechts
    int     vAlign     = 0;                        // 0=oben (Word-Stil), 1=zentriert

    bool isStroke() const { return kind == ImageAnnKind::Freehand || kind == ImageAnnKind::Arrow; }

    void recomputeBounds() {
        if (points.isEmpty())
            return;
        qreal minX = points.first().x(), maxX = minX;
        qreal minY = points.first().y(), maxY = minY;
        for (const QPointF& p : points) {
            minX = qMin(minX, p.x()); maxX = qMax(maxX, p.x());
            minY = qMin(minY, p.y()); maxY = qMax(maxY, p.y());
        }
        const qreal m = qMax<qreal>(2.0, lineWidth * 0.5);
        rect = QRectF(minX - m, minY - m, (maxX - minX) + 2 * m, (maxY - minY) + 2 * m);
    }

    // Sidecar-Serialisierung (IDs werden beim Laden neu vergeben)
    // Nachverfolgte Änderung (s. ImageTrackState); im Sidecar als "tr".
    ImageTrackState track = ImageTrackState::None;

    //  In den Binaerkoerper - geschrieben wird nur noch diese Form.
    void schreibe(mg::mgeb::Schreiber& s) const {
        using namespace mg::mgeb;
        s.beginneObjekt();
        s.feld(k_kind, static_cast<int>(kind));
        if (track != ImageTrackState::None)
            s.feld(k_tr, static_cast<int>(track));
        //  Bei einem Strich ergibt sich das Rechteck aus den Punkten und wird
        //  beim Lesen neu berechnet - es waere vier Doubles umsonst.
        if (!(isStroke() && !points.isEmpty())) {
            s.feld(k_x, rect.x());
            s.feld(k_y, rect.y());
            s.feld(k_w, rect.width());
            s.feld(k_h, rect.height());
        }
        if (isStroke()) {
            QVarLengthArray<double, 256> flach(points.size() * 2);
            for (int i = 0; i < points.size(); ++i) {
                flach[2 * i]     = points.at(i).x();
                flach[2 * i + 1] = points.at(i).y();
            }
            s.schluessel(k_pts);
            s.gibDoubles(flach.data(), int(flach.size()));
        }
        s.feld(k_stroke, stroke.name(QColor::HexArgb));
        s.feld(k_lw,     lineWidth);
        s.feld(k_fill,   fill.name(QColor::HexArgb));
        if (kind == ImageAnnKind::Text) {
            s.feld(k_text,   text);
            s.feld(k_font,   fontFamily);
            s.feld(k_size,   fontSizePx);
            s.feld(k_bold,   bold);
            s.feld(k_italic, italic);
            s.feld(k_under,  underline);
            s.feld(k_color,  color.name(QColor::HexRgb));
            s.feld(k_hilite, highlight.name(QColor::HexArgb));
            s.feld(k_align,  alignment);
            s.feld(k_valign, vAlign);
        }
        s.beendeObjekt();
    }

    //  EIN Codec fuer beide Quellen: `Obj` ist der Binaerleser oder, bei einer
    //  alten Beidatei, `JsonObjekt` ueber dem Qt-Baum.
    template <class Obj>
    static ImageAnnotation lade(const Obj& o) {
        ImageAnnotation a;
        const int k = o.value(mg::mgeb::k_kind).toInt(0);
        a.kind = (k >= 0 && k <= 4) ? static_cast<ImageAnnKind>(k) : ImageAnnKind::Text;
        //  Unbekannter Wert ⇒ None (wie im PDF-Editor): eine verfälschte
        //  Datei darf keine Annotation unauflösbar machen.
        const int tr = o.value(mg::mgeb::k_tr).toInt(0);
        a.track = (tr == 1 || tr == 2) ? static_cast<ImageTrackState>(tr)
                                       : ImageTrackState::None;
        a.rect = QRectF(o.value(mg::mgeb::k_x).toDouble(0.0),
                        o.value(mg::mgeb::k_y).toDouble(0.0),
                        o.value(mg::mgeb::k_w).toDouble(120.0),
                        o.value(mg::mgeb::k_h).toDouble(48.0));
        if (o.contains(mg::mgeb::k_pts)) {
            const auto pts = o.value(mg::mgeb::k_pts).toArray();
            //  Ein QPointF ist genau zwei Doubles in dieser Reihenfolge - aus
            //  dem Binaerfeld wird der ganze Block in einem Zug uebernommen.
            a.points.resize(pts.size() / 2);
            const int geholt = mg::mgeb::holePunkte(pts, a.points);
            for (int i = geholt; i + 1 < pts.size(); i += 2)
                a.points[i / 2] = QPointF(pts.at(i).toDouble(), pts.at(i + 1).toDouble());
        }
        a.stroke    = QColor(o.value(mg::mgeb::k_stroke).toString(QStringLiteral("#ffe62c2c")));
        a.lineWidth = o.value(mg::mgeb::k_lw).toDouble(4.0);
        a.fill      = QColor(o.value(mg::mgeb::k_fill).toString(QStringLiteral("#00000000")));
        a.text       = o.value(mg::mgeb::k_text).toString();
        a.fontFamily = o.value(mg::mgeb::k_font).toString(QStringLiteral("Helvetica"));
        a.fontSizePx = o.value(mg::mgeb::k_size).toDouble(28.0);
        a.bold       = o.value(mg::mgeb::k_bold).toBool(false);
        a.italic     = o.value(mg::mgeb::k_italic).toBool(false);
        a.underline  = o.value(mg::mgeb::k_under).toBool(false);
        a.color      = QColor(o.value(mg::mgeb::k_color).toString(QStringLiteral("#000000")));
        a.highlight  = QColor(o.value(mg::mgeb::k_hilite).toString(QStringLiteral("#fefb9b")));
        a.alignment  = o.value(mg::mgeb::k_align).toInt(0);
        a.vAlign     = o.value(mg::mgeb::k_valign).toInt(0);

        if (!a.stroke.isValid())    a.stroke    = QColor(230, 44, 44);
        if (!a.fill.isValid())      a.fill      = QColor(0, 0, 0, 0);
        if (!a.color.isValid())     a.color     = QColor(0, 0, 0);
        if (!a.highlight.isValid()) a.highlight = QColor(0, 0, 0, 0);
        if (a.lineWidth < 0.5)   a.lineWidth = 0.5;
        if (a.lineWidth > 200.0) a.lineWidth = 200.0;
        if (a.fontSizePx < 4.0)   a.fontSizePx = 4.0;
        if (a.fontSizePx > 800.0) a.fontSizePx = 800.0;
        if (a.alignment < 0 || a.alignment > 2) a.alignment = 0;
        if (a.vAlign    < 0 || a.vAlign    > 1) a.vAlign    = 0;
        if (a.rect.width()  < 1.0) a.rect.setWidth(1.0);
        if (a.rect.height() < 1.0) a.rect.setHeight(1.0);
        if (a.isStroke() && !a.points.isEmpty())
            a.recomputeBounds();
        return a;
    }

    //  Der Weg fuer eine alte Beidatei.
    static ImageAnnotation fromJson(const QJsonObject& o) {
        return lade(mg::mgeb::JsonObjekt(o));
    }
};
