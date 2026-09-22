#pragma once
// Datentypen des Editor-Overlays: das Original-PDF bleibt UNVERÄNDERT, jede Bearbeitung ist eine
// PdfEditBox in einer Ebene darüber und liegt in der Beidatei <pfad>.mgedit daneben - erst
// der Export backt beides zusammen. `rect` in PDF-Punkten, Ursprung oben-links.

#include <QString>
#include <QRectF>
#include <QPointF>
#include <QVector>
#include <QColor>
#include "core/MGEditBin.h"

#include <QVarLengthArray>
#include <QJsonObject>
#include <QJsonArray>

// Art der Annotation, im Sidecar als Ganzzahl. Alte Sidecars ohne "kind" laden als Text (0); die Werte 0-4 sind
// identisch zum Bild-Editor, damit QML und Export dieselbe Semantik teilen. Replace (5) ist PDF-exklusiv.
enum class PdfAnnKind {
    Text     = 0,   // Post-it-artige Textbox (bisheriges Verhalten)
    Freehand = 1,   // Freihand-Stift (Polylinie)
    Arrow    = 2,   // Pfeil (Start -> Ende)
    Rect     = 3,   // Rechteck (Kontur + optionale Füllung)
    Ellipse  = 4,   // Ellipse (Kontur + optionale Füllung)
    // "Text ersetzen": deckende weiße Fläche plus editierbare Textbox als EIN Objekt (gemeinsames Verschieben,
    // Löschen, Undo), ohne Post-it-Optik. `highlight` trägt die Deckfläche und wird auf deckendes Weiß erzwungen.
    Replace  = 5,
    // Textmarkierung auf der eingebetteten Textebene. Anders als die übrigen Arten trägt sie MEHRERE Bereiche - eine
    // Markierung über drei Zeilen ist EIN Objekt mit drei Rechtecken, so wie `/QuadPoints` es im PDF hält.
    Markup   = 6,
    // "Text schwärzen": deckende Fläche plus Entfernen des Textes aus dem Content-Stream. Geschützt ist damit das
    // Kopieren und Durchsuchen im Betrachter - der alte Strom bleibt beim inkrementellen Update in den Rohbytes.
    Redact   = 7,
    Stamp    = 8
};

// Nachverfolgte Änderung: der Zustand hängt an der Box, weil eine Änderung immer genau eine Annotation betrifft -
// eine zweite Liste liefe bei Undo auseinander. `Deleted` BLEIBT durchgestrichen stehen, sonst ließe sich das
// Verwerfen der Löschung nicht mehr zurücknehmen.
enum class PdfTrackState { None = 0, Added = 1, Deleted = 2 };

enum class PdfEditField {
    Track,         // Zustand der Nachverfolgung (PdfTrackState)
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

// EINE Overlay-Annotation als vereinheitlichtes Struct mit kind-Enum, wie ImageAnnotation: Text, Rechteck und
// Ellipse führen ihre Geometrie in `rect`, Freihand und Pfeil in `points` (`rect` ist dann die Hülle).
struct PdfEditBox {
    int        id   = 0;                 // laufende Sitzungs-ID (nicht persistiert)
    int        page = 0;                 // 0-basierte Seite
    PdfAnnKind kind = PdfAnnKind::Text;
    QRectF     rect;                     // PDF-Punkte, Ursprung oben-links
    QVector<QPointF> points;             // Freihand/Pfeil: Stützpunkte (Punkte)

    QColor stroke    = QColor(230, 44, 44);   // Linienfarbe (deckend)
    qreal  lineWidth = 2.0;                    // Linienbreite in Punkten
    QColor fill      = QColor(0, 0, 0, 0);    // Füllung Rect/Ellipse (a=0 -> nur Kontur)

    QString text;
    // Der ursprünglich unter der Box erkannte eingebettete Text - gebraucht, um die Originalzeichenkette beim
    // verlustfreien Editieren im Stream wiederzufinden. Leer bei Notizen und auf Seiten ohne Textebene.
    QString origText;
    QString fontFamily = QStringLiteral("Helvetica");
    qreal   fontSizePt = 12.0;
    bool    bold       = false;
    bool    italic     = false;
    bool    underline  = false;
    QColor  color      = QColor(0, 0, 0);          // Textfarbe (deckend)
    // Notiz-Hintergrund: füllt das ganze Box-Rechteck in Anzeige UND Export. Alpha 0 = kein Papier (reiner Text);
    // Schatten und Eselsohr hängen an Alpha > 0.
    QColor  highlight  = QColor(254, 243, 155, 232);
    int     alignment  = 0;                        // 0=links, 1=zentriert, 2=rechts
    int     vAlign     = 0;
    bool    anchored   = false;                    // an erkannte PDF-Textzeile gefangen
    // Verkettete Textboxen: `chainNext` ist die Sitzungs-ID der Folgebox (0 = keine) - überläuft eine Box, wandert
    // der Rest hinein. NICHT direkt in `toJson`, IDs sind sitzungslokal; persistiert wird ein Index-Array.
    int     chainNext  = 0;
    // Höhe der Box, BEVOR sie als Ketten-ENDE gewachsen ist (0 = nie gewachsen). Wird die Kette hinter ihr
    // verlängert, muss sie zurückschrumpfen - sonst fasst sie weiter den gesamten Resttext und in der neuen
    // Folgebox kommt nie etwas an.
    qreal   growBaseH  = 0.0;
    // 0 = eigene Notiz. >0 = beim Öffnen aus einer echten PDF-Annotation entstanden, mit deren Objektnummer.
    // Unverändert darf sie NICHT noch einmal gezeichnet werden - sie steht schon in der Datei; bearbeitet oder
    // gelöscht muss stattdessen das Original aus `/Annots` verschwinden.
    int     srcObjNum  = 0;
    int     markupStyle = 0;
    QString imagePath;
    // Nachverfolgte Änderung (s. PdfTrackState). Wird im Sidecar als "tr"
    // geführt; alte Sidecars ohne Feld laden als None.
    PdfTrackState track = PdfTrackState::None;

    bool isStroke() const { return kind == PdfAnnKind::Freehand || kind == PdfAnnKind::Arrow; }
    bool isMarkup() const { return kind == PdfAnnKind::Markup; }
    bool isRedact() const { return kind == PdfAnnKind::Redact; }
    bool isStamp()  const { return kind == PdfAnnKind::Stamp; }
    bool hasText()  const { return kind == PdfAnnKind::Text || kind == PdfAnnKind::Replace; }
    //  Wessen Rechteck sich aus den Punkten ergibt, braucht es nicht gespeichert.
    bool istAusPunkten() const { return isStroke() && !points.isEmpty(); }

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

    // Zeichen-Felder werden immer geschrieben (kind/stroke/lw/fill), die Text-Felder nur für Text-Boxen - alte
    // Sidecars ohne "kind" laden als Text (0), das Format bleibt vollständig rückwärtskompatibel.
    //  In den Binaerkoerper - geschrieben wird nur noch diese Form.
    void schreibe(mg::mgeb::Schreiber& s) const {
        using namespace mg::mgeb;
        s.beginneObjekt();
        s.feld(k_page, page);
        if (track != PdfTrackState::None)
            s.feld(k_tr, static_cast<int>(track));
        s.feld(k_kind, static_cast<int>(kind));
        //  Bei einem Strich ist das Rechteck die HUELLE der Punkte und wird
        //  beim Lesen ohnehin neu berechnet - es waere vier Doubles umsonst.
        if (!istAusPunkten()) {
            s.feld(k_x, rect.x());
            s.feld(k_y, rect.y());
            s.feld(k_w, rect.width());
            s.feld(k_h, rect.height());
        }
        //  Punkte traegt nicht nur der Strich: eine Markierung haelt darin ihre
        //  Bereiche (je zwei Ecken). Deshalb am INHALT entscheiden, nicht an
        //  der Art - sonst verloere ein neuer Typ seine Geometrie im Sidecar.
        if (!points.isEmpty()) {
            QVarLengthArray<double, 256> flach(points.size() * 2);
            for (int i = 0; i < points.size(); ++i) {
                flach[2 * i]     = points.at(i).x();
                flach[2 * i + 1] = points.at(i).y();
            }
            s.schluessel(k_pts);
            s.gibDoubles(flach.data(), int(flach.size()));
        }
        if (srcObjNum > 0) s.feld(k_srcobj, srcObjNum);
        if (isMarkup())    s.feld(k_mstyle, markupStyle);
        if (isStamp() && !imagePath.isEmpty())
            s.feld(k_img, imagePath);
        if (isRedact()) {
            //  Die Schwaerzung braucht ihre Flaeche UND den Text, der beim
            //  Export aus dem Strom verschwinden soll.
            s.feld(k_hilite, highlight.name(QColor::HexArgb));
            if (!origText.isEmpty()) s.feld(k_orig, origText);
        }
        s.feld(k_stroke, stroke.name(QColor::HexArgb));
        s.feld(k_lw,     lineWidth);
        s.feld(k_fill,   fill.name(QColor::HexArgb));
        if (hasText()) {
            s.feld(k_text, text);
            if (kind == PdfAnnKind::Replace && !origText.isEmpty())
                s.feld(k_orig, origText);
            s.feld(k_font,   fontFamily);
            s.feld(k_size,   fontSizePt);
            s.feld(k_bold,   bold);
            s.feld(k_italic, italic);
            s.feld(k_under,  underline);
            s.feld(k_color,  color.name(QColor::HexRgb));
            s.feld(k_hilite, highlight.name(QColor::HexArgb));
            s.feld(k_align,  alignment);
            s.feld(k_valign, vAlign);
            s.feld(k_anchor, anchored);
        }
        s.beendeObjekt();
    }

    //  EIN Codec fuer beide Quellen: `Obj` ist der Binaerleser oder, bei einer
    //  alten Beidatei, `JsonObjekt` ueber dem Qt-Baum.
    template <class Obj>
    static PdfEditBox lade(const Obj& o) {
        PdfEditBox b;
        b.page       = o.value(mg::mgeb::k_page).toInt(0);
        const int k  = o.value(mg::mgeb::k_kind).toInt(0);   // fehlt in Alt-Sidecars -> Text
        b.kind       = (k >= 0 && k <= 8) ? static_cast<PdfAnnKind>(k) : PdfAnnKind::Text;
        //  Unbekannter Wert ⇒ None: eine verfälschte Datei darf keine Box in
        //  einen Zustand bringen, den die Oberfläche nicht auflösen kann.
        const int tr = o.value(mg::mgeb::k_tr).toInt(0);
        b.track      = (tr == 1 || tr == 2) ? static_cast<PdfTrackState>(tr)
                                            : PdfTrackState::None;
        b.rect       = QRectF(o.value(mg::mgeb::k_x).toDouble(0.0),
                              o.value(mg::mgeb::k_y).toDouble(0.0),
                              o.value(mg::mgeb::k_w).toDouble(120.0),
                              o.value(mg::mgeb::k_h).toDouble(28.0));
        if (o.contains(mg::mgeb::k_pts)) {
            const auto pts = o.value(mg::mgeb::k_pts).toArray();
            //  Ein QPointF ist genau zwei Doubles in dieser Reihenfolge - aus
            //  dem Binaerfeld wird der ganze Block in einem Zug uebernommen.
            b.points.resize(pts.size() / 2);
            const int geholt = mg::mgeb::holePunkte(pts, b.points);
            for (int i = geholt; i + 1 < pts.size(); i += 2)
                b.points[i / 2] = QPointF(pts.at(i).toDouble(), pts.at(i + 1).toDouble());
        }
        b.srcObjNum = qMax(0, o.value(mg::mgeb::k_srcobj).toInt(0));
        b.markupStyle = qBound(0, o.value(mg::mgeb::k_mstyle).toInt(0), 2);
        b.imagePath   = o.value(mg::mgeb::k_img).toString();
        b.stroke    = QColor(o.value(mg::mgeb::k_stroke).toString(QStringLiteral("#ffe62c2c")));
        b.lineWidth = o.value(mg::mgeb::k_lw).toDouble(2.0);
        b.fill      = QColor(o.value(mg::mgeb::k_fill).toString(QStringLiteral("#00000000")));
        b.text       = o.value(mg::mgeb::k_text).toString();
        b.origText   = o.value(mg::mgeb::k_orig).toString();
        b.fontFamily = o.value(mg::mgeb::k_font).toString(QStringLiteral("Helvetica"));
        b.fontSizePt = o.value(mg::mgeb::k_size).toDouble(12.0);
        b.bold       = o.value(mg::mgeb::k_bold).toBool(false);
        b.italic     = o.value(mg::mgeb::k_italic).toBool(false);
        b.underline  = o.value(mg::mgeb::k_under).toBool(false);
        b.color      = QColor(o.value(mg::mgeb::k_color).toString(QStringLiteral("#000000")));
        b.highlight  = QColor(o.value(mg::mgeb::k_hilite).toString(QStringLiteral("#00000000")));
        b.alignment  = o.value(mg::mgeb::k_align).toInt(0);
        b.vAlign     = o.value(mg::mgeb::k_valign).toInt(0);
        b.anchored   = o.value(mg::mgeb::k_anchor).toBool(false);
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
        // Die Deckfläche muss DECKEND sein, sonst schimmert der gedruckte Text durch - die Farbe bleibt frei wählbar.
        // Alpha wird deshalb auf 255 gezwungen, auch gegen handeditierte Sidecars; fehlende Farbe -> Weiß.
        if (b.kind == PdfAnnKind::Replace) {
            if (b.highlight.isValid() && b.highlight.alpha() > 0) b.highlight.setAlpha(255);
            else                                                  b.highlight = QColor(255, 255, 255, 255);
        }
        if (b.page < 0) b.page = 0;
        if (b.rect.width()  < 2.0) b.rect.setWidth(2.0);
        if (b.rect.height() < 2.0) b.rect.setHeight(2.0);
        if (b.isStroke() && !b.points.isEmpty())
            b.recomputeBounds();
        return b;
    }

    //  Der Weg fuer eine alte Beidatei.
    static PdfEditBox fromJson(const QJsonObject& o) {
        return lade(mg::mgeb::JsonObjekt(o));
    }
};

// Ein Eintrag des Seiten-Plans - die einzige Quelle dafür, welche Seite wo und wie steht.
// `src` -1 = Leerseite, `doc` 1 = Begleitdatei mit übernommenen Fremdseiten, `rot` zusätzlich zur Quelle.
// `key` hält Notizen beim Umsortieren an ihrer Seite; pristine Seiten tragen key == src, damit ältere Sidecars gelten.
struct PdfPlanPage {
    int src = -1;
    int doc = 0;
    int rot = 0;
    int key = -1;

    bool isBlank() const    { return src < 0; }
    bool isImported() const { return doc == 1 && src >= 0; }
    bool isPristine() const { return doc == 0 && src >= 0; }

    void schreibe(mg::mgeb::Schreiber& s) const {
        using namespace mg::mgeb;
        s.beginneObjekt();
        s.feld(k_src, src);
        s.feld(k_key, key);
        if (doc != 0) s.feld(k_doc, doc);
        if (rot != 0) s.feld(k_rot, rot);
        s.beendeObjekt();
    }

    template <class Obj>
    static PdfPlanPage lade(const Obj& o) {
        PdfPlanPage p;
        p.src = o.value(mg::mgeb::k_src).toInt(-1);
        p.key = o.value(mg::mgeb::k_key).toInt(-1);
        p.doc = o.value(mg::mgeb::k_doc).toInt(0);
        p.rot = o.value(mg::mgeb::k_rot).toInt(0);
        // Defensive Klemmen gegen defekte/fremde Sidecars: unbekannte Quelle
        // -> Leerseite; Drehung auf ein Vielfaches von 90° normalisieren.
        if (p.src < 0)              p.src = -1;
        if (p.key < 0)              p.key = -1;   // ungültig -> wird neu vergeben
        if (p.doc != 0 && p.doc != 1) p.doc = 0;
        p.rot = ((p.rot % 360) + 360) % 360;
        p.rot = (p.rot / 90) * 90;
        if (p.src < 0) p.doc = 0;             // Leerseite hat keine Quelldatei
        return p;
    }

    //  Der Weg fuer eine alte Beidatei.
    static PdfPlanPage fromJson(const QJsonObject& o) {
        return lade(mg::mgeb::JsonObjekt(o));
    }
};

inline bool operator==(const PdfPlanPage& a, const PdfPlanPage& b) {
    return a.src == b.src && a.doc == b.doc && a.rot == b.rot && a.key == b.key;
}
inline bool operator!=(const PdfPlanPage& a, const PdfPlanPage& b) { return !(a == b); }

// Eine Änderung an der EINGEBETTETEN Textebene - liegt nicht über der Seite, sondern in ihr. Trotzdem
// ein Delta im Sidecar, das beim Anzeigen auf die pristine Datei angewendet wird, also genauso
// umkehrbar wie eine Notiz. `index` zählt nach den vorherigen Ops: die Listenreihenfolge trägt Bedeutung.
struct PdfTextOp {
    int     page    = 0;    // Seitenindex (0-basiert, Ansichts-/Dateiseite)
    int     index   = 0;    // Glyphen-Index, vor dem eingefügt/ab dem gelöscht wird
    QString text;           // eingefügte bzw. entfernte Zeichen
    int     removed = 0;    // >0 = Löschung dieser Länge, 0 = Einfügung

    bool isInsert() const { return removed <= 0; }

    void schreibe(mg::mgeb::Schreiber& s) const {
        using namespace mg::mgeb;
        s.beginneObjekt();
        s.feld(k_page, page);
        s.feld(k_at,   index);
        s.feld(k_text, text);
        if (removed > 0) s.feld(k_del, removed);
        s.beendeObjekt();
    }

    template <class Obj>
    static PdfTextOp lade(const Obj& o) {
        PdfTextOp t;
        t.page    = o.value(mg::mgeb::k_page).toInt(0);
        t.index   = o.value(mg::mgeb::k_at).toInt(0);
        t.text    = o.value(mg::mgeb::k_text).toString();
        t.removed = o.value(mg::mgeb::k_del).toInt(0);
        // Defensive Klemmen gegen defekte/fremde Sidecar-Dateien: negative
        // Indizes/Seiten würden im Editor auf ungültige Glyphen zeigen.
        if (t.page  < 0) t.page  = 0;
        if (t.index < 0) t.index = 0;
        if (t.removed < 0) t.removed = 0;
        if (t.removed > 0 && t.text.size() != t.removed)
            t.removed = t.text.isEmpty() ? t.removed : t.text.size();
        return t;
    }

    //  Der Weg fuer eine alte Beidatei.
    static PdfTextOp fromJson(const QJsonObject& o) {
        return lade(mg::mgeb::JsonObjekt(o));
    }
};

inline bool operator==(const PdfTextOp& a, const PdfTextOp& b) {
    return a.page == b.page && a.index == b.index
           && a.removed == b.removed && a.text == b.text;
}
inline bool operator!=(const PdfTextOp& a, const PdfTextOp& b) { return !(a == b); }
