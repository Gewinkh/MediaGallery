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
// 0–4 sind identisch zum Bild-Editor (ImageAnnKind), damit QML/Export dieselbe
// Semantik teilen; Replace (5) ist PDF-exklusiv („Text ersetzen").
enum class PdfAnnKind {
    Text     = 0,   // Post-it-artige Textbox (bisheriges Verhalten)
    Freehand = 1,   // Freihand-Stift (Polylinie)
    Arrow    = 2,   // Pfeil (Start → Ende)
    Rect     = 3,   // Rechteck (Kontur + optionale Füllung)
    Ellipse  = 4,   // Ellipse (Kontur + optionale Füllung)
    // „Text ersetzen": deckende, fix WEISSE Fläche exakt über dem gewählten
    // Bereich + frei editierbare Textbox darüber — EIN Annotation-Objekt
    // (gemeinsames Verschieben/Skalieren/Löschen/Kopieren/Undo). KEINE
    // Post-it-Optik (kein Schatten, kein Eselsohr); `highlight` trägt die
    // Deckfläche und wird vom Controller auf deckendes Weiß erzwungen.
    Replace  = 5,
    // Textmarkierung auf der eingebetteten Textebene: Markieren (gefüllte
    // Fläche), Unterstreichen, Durchstreichen. Anders als die übrigen Arten
    // trägt sie MEHRERE Bereiche — eine Markierung über drei Zeilen ist EIN
    // Objekt mit drei Rechtecken (so wie `/QuadPoints` es im PDF hält). Die
    // Rechtecke liegen paarweise (obere linke, untere rechte Ecke) in
    // `points`; `rect` ist ihre Hülle. Der Stil steht in `markupStyle`.
    Markup   = 6,
    // „Text schwärzen": deckende Fläche über dem Bereich UND Entfernen des
    // darunterliegenden Textes aus dem Content-Stream (`origText` trägt den
    // erkannten Text). Bewusst NICHT „Redaktion" genannt: Geschützt ist damit
    // das Kopieren/Durchsuchen im Betrachter — der alte Strom bleibt beim
    // inkrementellen Update in den Rohbytes der Datei stehen.
    Redact   = 7,
    // Signatur-/Stempelbild: eine Bilddatei, im Dokument platziert. Der Pfad
    // steht in `imagePath`; eingebettet wird das Bild erst beim Export
    // (`PdfImageEmbed`) — das Sidecar bleibt so klein wie bisher.
    Stamp    = 8
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
    // „Text ersetzen": der ursprünglich unter der Box erkannte eingebettete Text
    // (Vorbefüllung). Wird für das VERLUSTFREIE Content-Stream-Editing gebraucht,
    // um die Originalzeichenkette im Stream wiederzufinden. Leer bei Text-Notizen
    // und auf gescannten Seiten ohne Textebene → dann greift der Cover-Patch.
    QString origText;
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
    // Reflow: verkettete Textboxen. chainNext = Sitzungs-ID der Folgebox (0 =
    // keine). Text fließt Kopf → chainNext → … : überläuft eine Box, wandert der
    // Rest automatisch in die nächste. NICHT direkt in toJson (IDs sind
    // sitzungslokal) — persistiert als Index-Array „chains" im Sidecar.
    int     chainNext  = 0;
    // Höhe, die die Box hatte, BEVOR sie als Ketten-ENDE automatisch gewachsen
    // ist (0 = nie gewachsen). Nur das letzte Glied einer Kette wächst mit
    // seinem Inhalt; wird die Kette hinter ihm verlängert, ist es nicht mehr das
    // Ende und muss auf seine ursprüngliche, vom Nutzer gesetzte Höhe
    // zurückschrumpfen — sonst behält es die aufgeblähte Höhe, fasst weiterhin
    // den GESAMTEN Resttext und in der neuen Folgebox kommt nie etwas an (das
    // sah aus, als funktioniere die Verkettung nicht). Sitzungslokal wie
    // chainNext, aber im Sidecar als "growBase" mitgeführt, damit die Höhe auch
    // über einen Neustart hinweg wiederherstellbar bleibt.
    qreal   growBaseH  = 0.0;
    // ── Herkunft: aus dem DOKUMENT übernommene Annotation ─────────────────────
    //  0 = eigene Notiz (Normalfall). >0 = diese Box ist beim Öffnen aus einer
    //  echten PDF-Annotation entstanden (`mg::PdfAnnotations`) und trägt deren
    //  Objektnummer. Bedeutung für die Ausgabe: Solange die Box UNVERÄNDERT ist,
    //  darf sie NICHT noch einmal gezeichnet werden — sie steht bereits in der
    //  Datei; sonst stünde jede importierte Notiz doppelt. Wird sie bearbeitet
    //  oder gelöscht, muss stattdessen das ORIGINAL aus `/Annots` entfernt
    //  werden (sonst bliebe die alte Fassung darunter sichtbar).
    int     srcObjNum  = 0;
    // Nur `Markup`: 0 = Markieren (Fläche), 1 = Unterstreichen, 2 = Durchstreichen.
    int     markupStyle = 0;
    // Nur `Stamp`: Pfad der Bilddatei (lokal, absolut).
    QString imagePath;

    bool isStroke() const { return kind == PdfAnnKind::Freehand || kind == PdfAnnKind::Arrow; }
    // Textmarkierung (mehrere Bereiche, an den Text gebunden).
    bool isMarkup() const { return kind == PdfAnnKind::Markup; }
    bool isRedact() const { return kind == PdfAnnKind::Redact; }
    bool isStamp()  const { return kind == PdfAnnKind::Stamp; }
    // Textführende Arten: klassische Notiz UND „Text ersetzen" (beide nutzen
    // die vollständige Text-Pipeline — Schrift/Farben/Ausrichtung/Sidecar).
    bool hasText()  const { return kind == PdfAnnKind::Text || kind == PdfAnnKind::Replace; }

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
        //  Punkte trägt nicht nur der Strich: eine Markierung hält darin ihre
        //  Bereiche (je zwei Ecken). Deshalb am INHALT entscheiden, nicht an
        //  der Art — sonst verlöre ein neuer Typ seine Geometrie im Sidecar.
        if (!points.isEmpty()) {
            QJsonArray pts;
            for (const QPointF& p : points) {
                pts.append(p.x());
                pts.append(p.y());
            }
            o.insert(QStringLiteral("pts"), pts);
        }
        if (srcObjNum > 0) o.insert(QStringLiteral("srcobj"), srcObjNum);
        if (isMarkup())    o.insert(QStringLiteral("mstyle"), markupStyle);
        if (isStamp() && !imagePath.isEmpty())
            o.insert(QStringLiteral("img"), imagePath);
        if (isRedact()) {
            //  Die Schwärzung braucht ihre Fläche UND den Text, der beim
            //  Export aus dem Strom verschwinden soll.
            o.insert(QStringLiteral("hilite"), highlight.name(QColor::HexArgb));
            if (!origText.isEmpty()) o.insert(QStringLiteral("orig"), origText);
        }
        o.insert(QStringLiteral("stroke"), stroke.name(QColor::HexArgb));
        o.insert(QStringLiteral("lw"),     lineWidth);
        o.insert(QStringLiteral("fill"),   fill.name(QColor::HexArgb));
        if (hasText()) {
            o.insert(QStringLiteral("text"),   text);
            if (kind == PdfAnnKind::Replace && !origText.isEmpty())
                o.insert(QStringLiteral("orig"), origText);
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
        b.kind       = (k >= 0 && k <= 8) ? static_cast<PdfAnnKind>(k) : PdfAnnKind::Text;
        b.rect       = QRectF(o.value(QStringLiteral("x")).toDouble(0.0),
                              o.value(QStringLiteral("y")).toDouble(0.0),
                              o.value(QStringLiteral("w")).toDouble(120.0),
                              o.value(QStringLiteral("h")).toDouble(28.0));
        if (o.contains(QStringLiteral("pts"))) {
            const QJsonArray pts = o.value(QStringLiteral("pts")).toArray();
            for (int i = 0; i + 1 < pts.size(); i += 2)
                b.points.append(QPointF(pts.at(i).toDouble(), pts.at(i + 1).toDouble()));
        }
        //  Herkunftsmarke (fehlt in älteren Sidecars → 0 = eigene Notiz).
        b.srcObjNum = qMax(0, o.value(QStringLiteral("srcobj")).toInt(0));
        b.markupStyle = qBound(0, o.value(QStringLiteral("mstyle")).toInt(0), 2);
        b.imagePath   = o.value(QStringLiteral("img")).toString();
        b.stroke    = QColor(o.value(QStringLiteral("stroke")).toString(QStringLiteral("#ffe62c2c")));
        b.lineWidth = o.value(QStringLiteral("lw")).toDouble(2.0);
        b.fill      = QColor(o.value(QStringLiteral("fill")).toString(QStringLiteral("#00000000")));
        b.text       = o.value(QStringLiteral("text")).toString();
        b.origText   = o.value(QStringLiteral("orig")).toString();
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
        // „Text ersetzen": Die Deckfläche muss DECKEND sein (sonst schimmert der
        // gedruckte Text durch) — die FARBE ist jedoch frei konfigurierbar. Also
        // Alpha auf 255 zwingen (auch gegen handeditierte/fremde Sidecars),
        // fehlende/ungültige Farbe → Weiß als Standard.
        if (b.kind == PdfAnnKind::Replace) {
            if (b.highlight.isValid() && b.highlight.alpha() > 0) b.highlight.setAlpha(255);
            else                                                  b.highlight = QColor(255, 255, 255, 255);
        }
        if (b.page < 0) b.page = 0;
        if (b.rect.width()  < 2.0) b.rect.setWidth(2.0);
        if (b.rect.height() < 2.0) b.rect.setHeight(2.0);
        // Bei Strichen die Bounding-Box aus den Punkten sicherstellen.
        if (b.isStroke() && !b.points.isEmpty())
            b.recomputeBounds();
        return b;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  PdfPlanPage — EIN Eintrag des Seiten-Plans (Ansichts-Reihenfolge).
//
//  Der Plan ist die einzige Quelle dafür, WELCHE Seite WO und WIE steht; die
//  Anzeige rendert stets die daraus gebackene Arbeitsdatei (s.
//  PdfEditController::renderSourcePath), sodass „Ansichts-Index == Seitenindex
//  der gerenderten Datei" gilt.
//
//   • `src`  ≥ 0 = Seitenindex in der Quelle, −1 = eingefügte A4-Leerseite.
//   • `doc`  0 = pristines Hauptdokument, 1 = Begleitdatei „<pdf>.mgpages.pdf"
//            mit den aus FREMDEN PDFs verlustfrei übernommenen Seiten.
//   • `rot`  zusätzliche Drehung in Grad (0/90/180/270) ÜBER die Drehung, die
//            die Quellseite schon selbst trägt.
//   • `key`  STABILE Kennung dieser Ansichts-Seite: Notizen (PdfEditBox::page)
//            adressieren ihre Seite über den Key, nicht über die Position. Nur
//            so bleiben sie beim Umsortieren/Einfügen/Entfernen an ihrer Seite,
//            und zwei Leerseiten sind unterscheidbar.
//            RÜCKWÄRTSKOMPATIBEL: Seiten des pristinen Dokuments tragen
//            key == src, damit Sidecars aus älteren Versionen (Notiz-Seite ==
//            Seitenindex, Plan als reines Int-Array) unverändert weitergelten.
//            Neu eingefügte Seiten (leer/importiert) bekommen Keys ≥ Seitenzahl
//            des pristinen Dokuments.
// ─────────────────────────────────────────────────────────────────────────────
struct PdfPlanPage {
    int src = -1;
    int doc = 0;
    int rot = 0;
    int key = -1;

    bool isBlank() const    { return src < 0; }
    bool isImported() const { return doc == 1 && src >= 0; }
    //  Seite des pristinen Hauptdokuments — nur diese ist zeichenweise
    //  bearbeitbar (Caret) und trägt key == src.
    bool isPristine() const { return doc == 0 && src >= 0; }

    QJsonObject toJson() const {
        QJsonObject o;
        o.insert(QStringLiteral("src"), src);
        o.insert(QStringLiteral("key"), key);
        if (doc != 0) o.insert(QStringLiteral("doc"), doc);
        if (rot != 0) o.insert(QStringLiteral("rot"), rot);
        return o;
    }

    static PdfPlanPage fromJson(const QJsonObject& o) {
        PdfPlanPage p;
        p.src = o.value(QStringLiteral("src")).toInt(-1);
        p.key = o.value(QStringLiteral("key")).toInt(-1);
        p.doc = o.value(QStringLiteral("doc")).toInt(0);
        p.rot = o.value(QStringLiteral("rot")).toInt(0);
        // Defensive Klemmen gegen defekte/fremde Sidecars (Regel 22): unbekannte
        // Quelle → Leerseite; Drehung auf ein Vielfaches von 90° normalisieren.
        if (p.src < 0)              p.src = -1;
        if (p.key < 0)              p.key = -1;   // ungültig → wird neu vergeben
        if (p.doc != 0 && p.doc != 1) p.doc = 0;
        p.rot = ((p.rot % 360) + 360) % 360;
        p.rot = (p.rot / 90) * 90;
        if (p.src < 0) p.doc = 0;             // Leerseite hat keine Quelldatei
        return p;
    }
};

inline bool operator==(const PdfPlanPage& a, const PdfPlanPage& b) {
    return a.src == b.src && a.doc == b.doc && a.rot == b.rot && a.key == b.key;
}
inline bool operator!=(const PdfPlanPage& a, const PdfPlanPage& b) { return !(a == b); }

// ─────────────────────────────────────────────────────────────────────────────
//  PdfTextOp — EINE Änderung an der EINGEBETTETEN Textebene (Caret-Werkzeug).
//
//  Anders als PdfEditBox liegt diese Änderung nicht ÜBER der Seite, sondern IN
//  ihr. Trotzdem bleibt das Original unberührt: Die Ops sind ein DELTA, das im
//  Sidecar liegt und beim Anzeigen/Exportieren auf die pristine Datei
//  angewendet wird (PdfTextEditor). Damit ist das direkte Textbearbeiten genau
//  so reversibel wie die Notizen — Undo entfernt einfach die letzte Op.
//
//   • Einfügen  → `text` = eingefügte Zeichen, `removed` = 0
//   • Löschen   → `removed` = Anzahl, `text` = die ENTFERNTEN Zeichen (nur für
//                 das Undo/Redo-Delta; die Wiedergabe braucht sie nicht)
//  `index` ist der Glyphen-Index in dem Zustand, den die VORHERIGEN Ops erzeugt
//  haben — die Reihenfolge der Liste ist deshalb bedeutungstragend.
// ─────────────────────────────────────────────────────────────────────────────
struct PdfTextOp {
    int     page    = 0;    // Seitenindex (0-basiert, Ansichts-/Dateiseite)
    int     index   = 0;    // Glyphen-Index, vor dem eingefügt/ab dem gelöscht wird
    QString text;           // eingefügte bzw. entfernte Zeichen
    int     removed = 0;    // >0 = Löschung dieser Länge, 0 = Einfügung

    bool isInsert() const { return removed <= 0; }

    QJsonObject toJson() const {
        QJsonObject o;
        o.insert(QStringLiteral("page"), page);
        o.insert(QStringLiteral("at"),   index);
        o.insert(QStringLiteral("text"), text);
        if (removed > 0)
            o.insert(QStringLiteral("del"), removed);
        return o;
    }

    static PdfTextOp fromJson(const QJsonObject& o) {
        PdfTextOp t;
        t.page    = o.value(QStringLiteral("page")).toInt(0);
        t.index   = o.value(QStringLiteral("at")).toInt(0);
        t.text    = o.value(QStringLiteral("text")).toString();
        t.removed = o.value(QStringLiteral("del")).toInt(0);
        // Defensive Klemmen gegen defekte/fremde Sidecar-Dateien: negative
        // Indizes/Seiten würden im Editor auf ungültige Glyphen zeigen.
        if (t.page  < 0) t.page  = 0;
        if (t.index < 0) t.index = 0;
        if (t.removed < 0) t.removed = 0;
        // Länge und Text einer Löschung müssen zusammenpassen (Undo-Delta).
        if (t.removed > 0 && t.text.size() != t.removed)
            t.removed = t.text.isEmpty() ? t.removed : t.text.size();
        return t;
    }
};

//  Vergleich: der Controller prüft damit, ob die Arbeitsdatei die aktuelle
//  Op-Liste bereits abbildet — ein Neubau, der nichts ändert, unterbleibt.
inline bool operator==(const PdfTextOp& a, const PdfTextOp& b) {
    return a.page == b.page && a.index == b.index
           && a.removed == b.removed && a.text == b.text;
}
inline bool operator!=(const PdfTextOp& a, const PdfTextOp& b) { return !(a == b); }
