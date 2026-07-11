#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfEditTypes.h — Datentypen des PDF-Editor-Overlays (header-only).
// ══════════════════════════════════════════════════════════════════════════════
//
//  KONZEPT (Overlay-Architektur)
//  ─────────────────────────────
//  Das Original-PDF bleibt UNVERÄNDERT. Alle Bearbeitungen sind PdfEditBox-
//  Objekte, die als eigenständige Ebene ÜBER dem Dokument liegen:
//   • Anzeige:   QML zeichnet die Boxen über die gerenderten Seiten.
//   • Sidecar:   Die Boxen werden als JSON neben dem PDF gesichert
//                (<pfad>.mgedit.json) → bleiben dauerhaft editierbar.
//   • Export:    Erst der Export rendert Original + Overlay in ein NEUES PDF
//                (PdfEditController) — kein Flatten-only-Workflow, da das
//                Sidecar erhalten bleibt.
//
//  KOORDINATEN: rect liegt in PDF-PUNKTEN (1/72 Zoll), Ursprung OBEN-LINKS —
//  identisch zur Konvention der übrigen PDF-Overlays (Annotationen/Audio/
//  Textauswahl, dort normalisiert). QML rechnet über pagePointSize() in Pixel
//  um, der Export zeichnet 1:1 (QPdfWriter mit 72 dpi → 1 pt = 1 Einheit).
// ══════════════════════════════════════════════════════════════════════════════

#include <QString>
#include <QRectF>
#include <QPointF>
#include <QVector>
#include <QColor>
#include <QJsonObject>
#include <QJsonArray>

// Art der Annotation (im Sidecar als Ganzzahl gespeichert). Alte Sidecars ohne
// „kind"-Feld laden als Text (0) — vollständig rückwärtskompatibel. Die Werte
// sind identisch zum Bild-Editor (ImageAnnKind), damit QML/Export dieselbe
// Semantik teilen.
enum class PdfAnnKind {
    Text     = 0,   // Post-it-artige Textbox (bisheriges Verhalten)
    Freehand = 1,   // Freihand-Stift (Polylinie)
    Arrow    = 2,   // Pfeil (Start → Ende)
    Rect     = 3,   // Rechteck (Kontur + optionale Füllung)
    Ellipse  = 4    // Ellipse (Kontur + optionale Füllung)
};

// Welches Feld einer Box ändert sich? (Delta-Undo + gezielte dataChanged-Rollen)
enum class PdfEditField {
    Text,
    Geometry,
    Points,        // Freihand/Pfeil-Stützpunkte (eigener Weg: applyPoints)
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
    LineWidth,     // Linienbreite in PDF-Punkten (Formen/Striche)
    Fill           // Füllfarbe (Rect/Ellipse)
};

// ─────────────────────────────────────────────────────────────────────────────
//  PdfEditBox — EINE Overlay-Annotation: frei positionierbare (oder zeilen-
//  verankerte) Textbox ODER Zeichnung (Freihand/Pfeil/Rechteck/Ellipse) —
//  ein vereinheitlichtes Struct mit kind-Enum, exakt wie ImageAnnotation:
//   • Text / Rect / Ellipse → Geometrie = `rect` (Bounding-Box, PDF-Punkte).
//   • Freihand / Pfeil       → Geometrie = `points` (PDF-Punkte der Seite);
//                              `rect` ist die abgeleitete Bounding-Box.
// ─────────────────────────────────────────────────────────────────────────────
struct PdfEditBox {
    int        id   = 0;                 // laufende Sitzungs-ID (nicht persistiert)
    int        page = 0;                 // 0-basierte Seite
    PdfAnnKind kind = PdfAnnKind::Text;
    QRectF     rect;                     // PDF-Punkte, Ursprung oben-links
    QVector<QPointF> points;             // Freihand/Pfeil: Stützpunkte (Punkte)

    // ── Zeichnen (Formen + Striche) — Maße in PDF-Punkten ────────────────────
    QColor stroke    = QColor(230, 44, 44);   // Linienfarbe (deckend)
    qreal  lineWidth = 2.0;                    // Linienbreite in Punkten
    QColor fill      = QColor(0, 0, 0, 0);    // Füllung Rect/Ellipse (a=0 → nur Kontur)

    QString text;
    QString fontFamily = QStringLiteral("Helvetica");
    qreal   fontSizePt = 12.0;
    bool    bold       = false;
    bool    italic     = false;
    bool    underline  = false;
    QColor  color      = QColor(0, 0, 0);          // Textfarbe (deckend)
    // Notiz-Hintergrund („Post-it-Papier"): füllt das ganze Box-Rechteck in
    // Anzeige UND Export. Standard = klassisches Haftnotiz-Gelb mit leichter
    // Transparenz (Deckkraft im Panel einstellbar); Alpha 0 = kein Papier
    // (reiner Text). Schatten + Eselsohr hängen an Alpha > 0.
    QColor  highlight  = QColor(254, 243, 155, 232);
    int     alignment  = 0;                        // 0=links, 1=zentriert, 2=rechts
    // Vertikale Textausrichtung im Box-Rechteck: 0 = OBEN (Word-Textfeld,
    // Standard), 1 = zentriert. Ältere Sidecars ohne Feld laden als 0.
    int     vAlign     = 0;
    bool    anchored   = false;                    // an erkannte PDF-Textzeile gefangen

    bool isStroke() const { return kind == PdfAnnKind::Freehand || kind == PdfAnnKind::Arrow; }

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
        const qreal m = qMax<qreal>(1.0, lineWidth * 0.5);
        rect = QRectF(minX - m, minY - m, (maxX - minX) + 2 * m, (maxY - minY) + 2 * m);
    }

    // ── Sidecar-Serialisierung (IDs werden beim Laden neu vergeben) ───────────
    //  Zeichen-Felder werden immer geschrieben (kind/stroke/lw/fill); die
    //  Text-Felder nur für Text-Boxen — alte Sidecars ohne „kind" laden als
    //  Text (0), das Format bleibt vollständig rückwärtskompatibel.
    QJsonObject toJson() const {
        QJsonObject o;
        o.insert(QStringLiteral("page"),   page);
        o.insert(QStringLiteral("kind"),   static_cast<int>(kind));
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
        if (kind == PdfAnnKind::Text) {
            o.insert(QStringLiteral("text"),   text);
            o.insert(QStringLiteral("font"),   fontFamily);
            o.insert(QStringLiteral("size"),   fontSizePt);
            o.insert(QStringLiteral("bold"),   bold);
            o.insert(QStringLiteral("italic"), italic);
            o.insert(QStringLiteral("under"),  underline);
            o.insert(QStringLiteral("color"),  color.name(QColor::HexRgb));
            // Alpha mitschreiben → „keine Hervorhebung" (Alpha 0) bleibt erhalten.
            o.insert(QStringLiteral("hilite"), highlight.name(QColor::HexArgb));
            o.insert(QStringLiteral("align"),  alignment);
            o.insert(QStringLiteral("valign"), vAlign);
            o.insert(QStringLiteral("anchor"), anchored);
        }
        return o;
    }

    static PdfEditBox fromJson(const QJsonObject& o) {
        PdfEditBox b;
        b.page       = o.value(QStringLiteral("page")).toInt(0);
        const int k  = o.value(QStringLiteral("kind")).toInt(0);   // fehlt in Alt-Sidecars → Text
        b.kind       = (k >= 0 && k <= 4) ? static_cast<PdfAnnKind>(k) : PdfAnnKind::Text;
        b.rect       = QRectF(o.value(QStringLiteral("x")).toDouble(0.0),
                              o.value(QStringLiteral("y")).toDouble(0.0),
                              o.value(QStringLiteral("w")).toDouble(120.0),
                              o.value(QStringLiteral("h")).toDouble(28.0));
        if (o.contains(QStringLiteral("pts"))) {
            const QJsonArray pts = o.value(QStringLiteral("pts")).toArray();
            for (int i = 0; i + 1 < pts.size(); i += 2)
                b.points.append(QPointF(pts.at(i).toDouble(), pts.at(i + 1).toDouble()));
        }
        b.stroke    = QColor(o.value(QStringLiteral("stroke")).toString(QStringLiteral("#ffe62c2c")));
        b.lineWidth = o.value(QStringLiteral("lw")).toDouble(2.0);
        b.fill      = QColor(o.value(QStringLiteral("fill")).toString(QStringLiteral("#00000000")));
        b.text       = o.value(QStringLiteral("text")).toString();
        b.fontFamily = o.value(QStringLiteral("font")).toString(QStringLiteral("Helvetica"));
        b.fontSizePt = o.value(QStringLiteral("size")).toDouble(12.0);
        b.bold       = o.value(QStringLiteral("bold")).toBool(false);
        b.italic     = o.value(QStringLiteral("italic")).toBool(false);
        b.underline  = o.value(QStringLiteral("under")).toBool(false);
        b.color      = QColor(o.value(QStringLiteral("color")).toString(QStringLiteral("#000000")));
        b.highlight  = QColor(o.value(QStringLiteral("hilite")).toString(QStringLiteral("#00000000")));
        b.alignment  = o.value(QStringLiteral("align")).toInt(0);
        b.vAlign     = o.value(QStringLiteral("valign")).toInt(0);
        b.anchored   = o.value(QStringLiteral("anchor")).toBool(false);
        // Defensive Klemmen gegen defekte/fremde Sidecar-Dateien.
        if (!b.stroke.isValid())    b.stroke = QColor(230, 44, 44);
        if (!b.fill.isValid())      b.fill = QColor(0, 0, 0, 0);
        if (b.lineWidth < 0.2)  b.lineWidth = 0.2;
        if (b.lineWidth > 72.0) b.lineWidth = 72.0;
        if (!b.color.isValid())     b.color = QColor(0, 0, 0);
        if (!b.highlight.isValid()) b.highlight = QColor(0, 0, 0, 0);
        if (b.fontSizePt < 4.0)   b.fontSizePt = 4.0;
        if (b.fontSizePt > 200.0) b.fontSizePt = 200.0;
        if (b.alignment < 0 || b.alignment > 2) b.alignment = 0;
        if (b.vAlign    < 0 || b.vAlign    > 1) b.vAlign    = 0;
        if (b.page < 0) b.page = 0;
        if (b.rect.width()  < 2.0) b.rect.setWidth(2.0);
        if (b.rect.height() < 2.0) b.rect.setHeight(2.0);
        // Bei Strichen die Bounding-Box aus den Punkten sicherstellen.
        if (b.isStroke() && !b.points.isEmpty())
            b.recomputeBounds();
        return b;
    }
};
