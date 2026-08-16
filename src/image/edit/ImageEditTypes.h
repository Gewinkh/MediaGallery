#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  ImageEditTypes.h — Datentypen des Bild-Editor-Overlays (header-only).
// ══════════════════════════════════════════════════════════════════════════════
//
//  KONZEPT (Overlay-Architektur — analog zum PDF-Editor, s. PdfEditTypes.h)
//  ───────────────────────────────────────────────────────────────────────
//  Das Original-BILD bleibt UNVERÄNDERT. Alle Bearbeitungen sind
//  ImageAnnotation-Objekte, die als eigenständige Ebene ÜBER dem Bild liegen:
//   • Anzeige:   QML zeichnet die Annotationen über das gerenderte Bild.
//   • Sidecar:   Die Annotationen werden als JSON neben dem Bild gesichert
//                (<pfad>.mgedit.json) → bleiben dauerhaft editierbar.
//   • Export:    Erst der Export rendert Original + Overlay in eine NEUE
//                Bildkopie (ImageEditController, QImage+QPainter).
//
//  KOORDINATEN: Alle Geometrien liegen in NATIVEN BILD-PIXELN (Ursprung
//  oben-links) — analog zu den „PDF-Punkten" des PDF-Editors. QML rechnet über
//  `imgScale` (angezeigte Pixel je Bild-Pixel) in Bildschirm-Pixel um; der
//  Export zeichnet 1:1 in die native Bildauflösung → WYSIWYG. Schriftgröße und
//  Linienbreite sind ebenfalls Bild-Pixel (auflösungsecht).
//
//  EIN vereinheitlichtes Struct mit `kind`-Enum deckt alle fünf Annotations-
//  arten ab (Text-Notiz, Freihand, Pfeil, Rechteck, Ellipse):
//   • Text / Rect / Ellipse → Geometrie = `rect` (Bounding-Box).
//   • Freihand / Pfeil       → Geometrie = `points` (Bild-Pixel); `rect` ist die
//                              daraus abgeleitete Bounding-Box (Auswahl/Skalieren).
// ══════════════════════════════════════════════════════════════════════════════

#include <QString>
#include <QRectF>
#include <QPointF>
#include <QVector>
#include <QColor>
#include <QJsonObject>
#include <QJsonArray>

// Art der Annotation (im Sidecar als Ganzzahl gespeichert).
enum class ImageAnnKind {
    Text     = 0,   // Post-it-artige Textnotiz (volle Parität zum PDF-Editor)
    Freehand = 1,   // Freihand-Stift (Polylinie)
    Arrow    = 2,   // Pfeil (Start → Ende)
    Rect     = 3,   // Rechteck (Kontur + optionale Füllung)
    Ellipse  = 4    // Ellipse (Kontur + optionale Füllung)
};

// Welches Feld ändert sich? (Delta-Undo + gezielte dataChanged-Rollen)
// Nachverfolgte Änderung („Track Changes") — identisch zum PDF-Editor
// (`PdfTrackState`), damit Oberfläche und Bedienung dieselbe Semantik teilen.
//   Added   — während der Aufzeichnung entstanden.
//   Deleted — während der Aufzeichnung gelöscht; die Annotation BLEIBT bis zur
//             Entscheidung stehen, sonst ließe sich das Verwerfen der Löschung
//             nicht mehr zurücknehmen.
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

// ─────────────────────────────────────────────────────────────────────────────
//  ImageAnnotation — EINE Overlay-Annotation (Text/Freihand/Pfeil/Rect/Ellipse).
// ─────────────────────────────────────────────────────────────────────────────
struct ImageAnnotation {
    int          id   = 0;                       // laufende Sitzungs-ID (nicht persistiert)
    ImageAnnKind kind = ImageAnnKind::Text;
    QRectF       rect;                           // Bounding-Box in Bild-Pixeln
    QVector<QPointF> points;                     // Freihand/Pfeil: Stützpunkte (Bild-px)

    // ── Zeichnen (Formen + Striche) ──────────────────────────────────────────
    QColor stroke    = QColor(230, 44, 44);      // Linienfarbe (deckend)
    qreal  lineWidth = 4.0;                       // Linienbreite in Bild-Pixeln
    QColor fill      = QColor(0, 0, 0, 0);       // Füllung Rect/Ellipse (a=0 → nur Kontur)

    // ── Text (kind == Text) — volle Post-it-Parität zum PDF-Editor ───────────
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

    // Bounding-Box aus den Punkten neu berechnen (Freihand/Pfeil). Ein
    // Linienbreiten-Rand hält Auswahlrahmen/Handles außerhalb des Strichs.
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

    // ── Sidecar-Serialisierung (IDs werden beim Laden neu vergeben) ───────────
    // Nachverfolgte Änderung (s. ImageTrackState); im Sidecar als "tr".
    ImageTrackState track = ImageTrackState::None;

    QJsonObject toJson() const {
        QJsonObject o;
        o.insert(QStringLiteral("kind"),   static_cast<int>(kind));
        if (track != ImageTrackState::None)
            o.insert(QStringLiteral("tr"), static_cast<int>(track));
        o.insert(QStringLiteral("x"),      rect.x());
        o.insert(QStringLiteral("y"),      rect.y());
        o.insert(QStringLiteral("w"),      rect.width());
        o.insert(QStringLiteral("h"),      rect.height());
        if (isStroke()) {
            QJsonArray pts;
            for (const QPointF& p : points) {
                pts.append(p.x());
                pts.append(p.y());
            }
            o.insert(QStringLiteral("pts"), pts);
        }
        o.insert(QStringLiteral("stroke"), stroke.name(QColor::HexArgb));
        o.insert(QStringLiteral("lw"),     lineWidth);
        o.insert(QStringLiteral("fill"),   fill.name(QColor::HexArgb));
        if (kind == ImageAnnKind::Text) {
            o.insert(QStringLiteral("text"),   text);
            o.insert(QStringLiteral("font"),   fontFamily);
            o.insert(QStringLiteral("size"),   fontSizePx);
            o.insert(QStringLiteral("bold"),   bold);
            o.insert(QStringLiteral("italic"), italic);
            o.insert(QStringLiteral("under"),  underline);
            o.insert(QStringLiteral("color"),  color.name(QColor::HexRgb));
            o.insert(QStringLiteral("hilite"), highlight.name(QColor::HexArgb));
            o.insert(QStringLiteral("align"),  alignment);
            o.insert(QStringLiteral("valign"), vAlign);
        }
        return o;
    }

    static ImageAnnotation fromJson(const QJsonObject& o) {
        ImageAnnotation a;
        const int k = o.value(QStringLiteral("kind")).toInt(0);
        a.kind = (k >= 0 && k <= 4) ? static_cast<ImageAnnKind>(k) : ImageAnnKind::Text;
        //  Unbekannter Wert ⇒ None (wie im PDF-Editor): eine verfälschte
        //  Datei darf keine Annotation unauflösbar machen.
        const int tr = o.value(QStringLiteral("tr")).toInt(0);
        a.track = (tr == 1 || tr == 2) ? static_cast<ImageTrackState>(tr)
                                       : ImageTrackState::None;
        a.rect = QRectF(o.value(QStringLiteral("x")).toDouble(0.0),
                        o.value(QStringLiteral("y")).toDouble(0.0),
                        o.value(QStringLiteral("w")).toDouble(120.0),
                        o.value(QStringLiteral("h")).toDouble(48.0));
        if (o.contains(QStringLiteral("pts"))) {
            const QJsonArray pts = o.value(QStringLiteral("pts")).toArray();
            for (int i = 0; i + 1 < pts.size(); i += 2)
                a.points.append(QPointF(pts.at(i).toDouble(), pts.at(i + 1).toDouble()));
        }
        a.stroke    = QColor(o.value(QStringLiteral("stroke")).toString(QStringLiteral("#ffe62c2c")));
        a.lineWidth = o.value(QStringLiteral("lw")).toDouble(4.0);
        a.fill      = QColor(o.value(QStringLiteral("fill")).toString(QStringLiteral("#00000000")));
        a.text       = o.value(QStringLiteral("text")).toString();
        a.fontFamily = o.value(QStringLiteral("font")).toString(QStringLiteral("Helvetica"));
        a.fontSizePx = o.value(QStringLiteral("size")).toDouble(28.0);
        a.bold       = o.value(QStringLiteral("bold")).toBool(false);
        a.italic     = o.value(QStringLiteral("italic")).toBool(false);
        a.underline  = o.value(QStringLiteral("under")).toBool(false);
        a.color      = QColor(o.value(QStringLiteral("color")).toString(QStringLiteral("#000000")));
        a.highlight  = QColor(o.value(QStringLiteral("hilite")).toString(QStringLiteral("#fefb9b")));
        a.alignment  = o.value(QStringLiteral("align")).toInt(0);
        a.vAlign     = o.value(QStringLiteral("valign")).toInt(0);

        // Defensive Klemmen gegen defekte/fremde Sidecar-Dateien.
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
        // Bei Strichen die Bounding-Box aus den Punkten sicherstellen.
        if (a.isStroke() && !a.points.isEmpty())
            a.recomputeBounds();
        return a;
    }
};
