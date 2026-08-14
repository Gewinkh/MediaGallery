#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfTextLayout.h — WO steht welches Zeichen auf einer PDF-Seite?
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  Liefert für jedes gezeigte Zeichen einer Seite seinen Platz: Ursprung,
//  Vorschubbreite, Höhe, Schriftgröße — in PDF-Punkten mit Ursprung
//  OBEN-LINKS (also so, wie die App rechnet).
//
//  Das ist die im README als Voraussetzung genannte Grundlage für das direkte
//  Bearbeiten der Textebene: Ohne zu wissen, wo ein Zeichen anfängt und wie
//  breit es ist, lässt sich weder ein Caret setzen noch eine Zeile nach einer
//  Änderung neu umbrechen. Genutzt wird sie für
//   • Treffersuche  (Punkt → Zeichenposition, `hitTest`)
//   • Caret-Geometrie (Zeichenposition → Rechteck, `caretRect`)
//   • künftig: zeichenweises Einfügen/Löschen im Content-Stream.
//
//  WOHER DIE BREITEN KOMMEN
//  ────────────────────────
//  Ausschließlich aus dem Dokument selbst:
//   • einfache Fonts → `/FirstChar` + `/Widths` (Glyphenraum, 1/1000 em),
//   • Type0/CID      → `/W`-Array des Nachfahren-Fonts, sonst `/DW` (Standard 1000),
//   • Courier*       → fest 600 (die Schrift ist per Definition dicktengleich).
//
//  Fehlen die Breiten (z. B. eine Standard-14-Schrift ohne `/Widths`), wird
//  ABGELEHNT statt geschätzt: eine falsche Breite verschiebt jedes folgende
//  Zeichen, der Caret stünde sichtbar daneben. Eingebaute AFM-Tabellen aus
//  dem Gedächtnis nachzubilden wäre genau die Art Halbwissen, die hier nichts
//  zu suchen hat — reale Erzeuger (Word, LaTeX, InDesign) schreiben `/Widths`
//  ohnehin immer mit.
//
//  BERÜCKSICHTIGT den vollständigen Textzustand: `Tf` `Tm` `Td` `TD` `T*` `TL`
//  `Tc` (Zeichenabstand) `Tw` (Wortabstand, nur Byte 32 in Ein-Byte-Kodierungen)
//  `Tz` (Horizontalskalierung) `Ts` (Grundlinienversatz), die Zeigeoperatoren
//  `Tj` `TJ` `'` `"` sowie die Grafikmatrix (`q` `Q` `cm`).
//
//  ABHÄNGIGKEITEN: Qt6::Core + ZLIB (über PdfObjects). Kein Q_OBJECT/moc.
// ══════════════════════════════════════════════════════════════════════════════

#include <QByteArray>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace mg {

//  Ein gezeigtes Zeichen mit seiner Lage auf der Seite.
struct PdfGlyph {
    QChar   ch;             // dekodiertes Zeichen (U+FFFD, wenn nicht abbildbar)
    QRectF  box;            // Ursprung oben-links, PDF-Punkte (Vorschub × Zeilenhöhe)
    qreal   fontSizePt = 0; // wirksame Schriftgröße (inkl. Matrix-Skalierung)
    int     showIndex  = 0; // Nummer des Zeigeoperators, in dem das Zeichen steht
    int     byteOffset = 0; // Byte-Position in dessen ZUSAMMENGEFÜGTEN Bytes
                            // (PdfShowSpan::bytes) — bei TJ über alle Glieder
};

//  Ein Zeigeoperator (Tj/TJ/'/") mit seiner Lage IM ENTPACKTEN Content-Stream.
//  `PdfGlyph::showIndex` zeigt hierauf. Damit lässt sich ein Zeichen nicht nur
//  anzeigen, sondern auch gezielt ersetzen: `bytesStart/bytesEnd` umschließen
//  den Operanden, `enc` sagt, wie neue Zeichen zu kodieren sind.
struct PdfShowSpan {
    qint64     operandStart = 0;   // '(' bzw. '[' des Operanden
    qint64     operandEnd   = 0;   // hinter dem schliessenden Zeichen
    bool       isArray      = false;
    QByteArray bytes;              // entschlüsselte Rohbytes (TJ: zusammengefügt)
    QByteArray fontRes;            // Ressourcenname der aktiven Schrift
    //  ── Positionierung, die diese Zeile gesetzt hat ─────────────────────────
    //  Ohne sie lässt sich Text nur ÄNDERN, nicht VERSCHIEBEN — und genau das
    //  braucht ein Absatz, der um eine Zeile wächst (alles darunter muss nach
    //  unten). `posStart/posEnd` umschließen die vollständige Anweisung
    //  (Operanden + Operatorname), sind also direkt ersetzbar.
    qint64         posStart = -1;
    qint64         posEnd   = -1;
    QByteArray     posOp;          // "Tm" | "Td" | "TD" | "T*" | leer
    QVector<qreal> posArgs;        // Operanden in Schreibreihenfolge
    //  Laufende Nummer des Textobjekts (`BT`): Ein neues Textobjekt setzt die
    //  Textmatrix zurück — eine relative Verschiebung wirkt nicht darüber
    //  hinaus.
    int            objIndex = 0;
    //  SEITEN-PUNKTE, um die ein TJ-Versatz von −1000 hier nach rechts schiebt
    //  (= `Tfs · Tz/100 · waagerechte Skalierung von Textmatrix und CTM`).
    //  Wer Zeichen aus einem Zeigeoperator herausschneidet, muss die Lücke mit
    //  genau so einem Versatz ausgleichen, sonst rutscht der Rest der Zeile
    //  nach links. Die Umrechnung gehört hierher: nur an dieser Stelle ist der
    //  vollständige Textzustand (inkl. `Tz`) bekannt.
    qreal          tjUnitPt = 0;
};

//  Alles, was eine Seite an Text hergibt.
struct PdfPageText {
    QVector<PdfGlyph>    glyphs;
    QVector<PdfShowSpan> spans;
    QByteArray           content;      // entpackter Content-Stream
    int                  contentObj = -1;   // Objektnummer (−1 = mehrteilig)
    qreal                pageHeightPt = 0;  // /MediaBox-Höhe (Umrechnung oben/unten)
    //  Flächen, die NICHT Text sind (Pfade, XObjects, Inline-Bilder), in
    //  denselben Koordinaten wie `PdfGlyph::box` (Ursprung oben-links). Wer
    //  Text verschieben will, muss wissen, was dabei stehen bliebe: Grafik
    //  wandert nicht mit.
    QVector<QRectF>      paints;
    //  Nur die BILDartigen davon (XObject `Do`, Inline-Bild `BI`, Schattierung
    //  `sh`). Wer schwärzt, muss sie kennen: Text lässt sich aus dem Strom
    //  entfernen, Bildpunkte NICHT — unter einem Balken über einem Bild bliebe
    //  das Original erhalten, sichtbar zu machen durch Entfernen des Balkens.
    QVector<QRectF>      imagePaints;
};

class PdfTextLayout {
public:
    //  Baut das Layout EINER Seite (0-basiert). Liefert false, wenn die Seite
    //  nicht sicher auswertbar ist (fehlende Breiten, fremder Stream-Filter,
    //  verschlüsselt …); `err` erhält dann einen kurzen Grund.
    static bool buildForPage(const QString& pdfPath, int pageIndex,
                             QVector<PdfGlyph>* out, QString* err = nullptr);

    //  Wie oben, liefert zusätzlich die Zeigeoperator-Bereiche und den
    //  entpackten Stream — die Grundlage fürs zeichenweise Bearbeiten.
    static bool buildForPage(const QString& pdfPath, int pageIndex,
                             PdfPageText* out, QString* err = nullptr);

    //  Index des Zeichens, dessen Kasten `ptTopLeft` enthält bzw. das ihm am
    //  nächsten liegt (−1 bei leerer Seite). Für die Caret-Platzierung.
    static int hitTest(const QVector<PdfGlyph>& glyphs, const QPointF& ptTopLeft);

    //  Caret-Rechteck VOR dem Zeichen `index` (bzw. hinter dem letzten, wenn
    //  `index == glyphs.size()`); leer bei ungültigem Index.
    static QRectF caretRect(const QVector<PdfGlyph>& glyphs, int index);
};

} // namespace mg
