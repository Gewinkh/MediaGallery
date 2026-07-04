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
#include <QColor>
#include <QJsonObject>

// Welches Feld einer Box ändert sich? (Delta-Undo + gezielte dataChanged-Rollen)
enum class PdfEditField {
    Text,
    Geometry,
    FontFamily,
    FontSize,
    Bold,
    Italic,
    Underline,
    Color,
    Highlight,
    Alignment,
    VAlign
};

// ─────────────────────────────────────────────────────────────────────────────
//  PdfEditBox — EINE frei positionierbare (oder zeilenverankerte) Textbox.
// ─────────────────────────────────────────────────────────────────────────────
struct PdfEditBox {
    int     id   = 0;                    // laufende Sitzungs-ID (nicht persistiert)
    int     page = 0;                    // 0-basierte Seite
    QRectF  rect;                        // PDF-Punkte, Ursprung oben-links
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

    // ── Sidecar-Serialisierung (IDs werden beim Laden neu vergeben) ───────────
    QJsonObject toJson() const {
        QJsonObject o;
        o.insert(QStringLiteral("page"),   page);
        o.insert(QStringLiteral("x"),      rect.x());
        o.insert(QStringLiteral("y"),      rect.y());
        o.insert(QStringLiteral("w"),      rect.width());
        o.insert(QStringLiteral("h"),      rect.height());
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
        return o;
    }

    static PdfEditBox fromJson(const QJsonObject& o) {
        PdfEditBox b;
        b.page       = o.value(QStringLiteral("page")).toInt(0);
        b.rect       = QRectF(o.value(QStringLiteral("x")).toDouble(0.0),
                              o.value(QStringLiteral("y")).toDouble(0.0),
                              o.value(QStringLiteral("w")).toDouble(120.0),
                              o.value(QStringLiteral("h")).toDouble(28.0));
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
        if (!b.color.isValid())     b.color = QColor(0, 0, 0);
        if (!b.highlight.isValid()) b.highlight = QColor(0, 0, 0, 0);
        if (b.fontSizePt < 4.0)   b.fontSizePt = 4.0;
        if (b.fontSizePt > 200.0) b.fontSizePt = 200.0;
        if (b.alignment < 0 || b.alignment > 2) b.alignment = 0;
        if (b.vAlign    < 0 || b.vAlign    > 1) b.vAlign    = 0;
        if (b.page < 0) b.page = 0;
        if (b.rect.width()  < 8.0) b.rect.setWidth(8.0);
        if (b.rect.height() < 8.0) b.rect.setHeight(8.0);
        return b;
    }
};
