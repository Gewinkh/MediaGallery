#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  DocxDocument — verlusterhaltendes Absatz-/Textlauf-Modell einer .docx-Datei.
//
//  VERLUSTERHALTUNGS-PRINZIP (bindend, §0): word/document.xml wird EINMAL
//  dekodiert (m_docXml) und jeder Absatz/Textlauf merkt sich seine Herkunft
//  als SPAN [start,len) in diesem Original. Beim Speichern werden:
//   • unangetastete Blöcke/Runs als ORIGINAL-TEILSTRING re-emittiert
//     (→ byteidentisch nach UTF-8-Re-Encoding),
//   • geänderte Absätze aus ihren Teilen zusammengesetzt, wobei unberührte
//     Fragmente (Start-Tag, pPr, rPr, opake Runs) VERBATIM aus dem Original
//     stammen — nur die tatsächlich betroffenen Knoten ändern sich,
//   • alle übrigen ZIP-Einträge byteidentisch roh kopiert (DocxZip::addRaw).
//  Das Dokument wird NIE komplett aus dem Editier-Modell neu generiert.
//
//  SELBSTPRÜFUNG: Direkt nach dem Parsen wird das Original aus Prefix +
//  Block-Spans + Suffix rekonstruiert und mit m_docXml verglichen. Nur bei
//  exakter Übereinstimmung ist die Datei editierbar — sonst schlägt load()
//  fehl (lieber nicht editieren als still Inhalte verlieren).
//
//  Text-Sentinels im entkodierten Run-Text (Rückabbildung beim Serialisieren):
//   '\t' = <w:tab/> · U+2028 = <w:br/>/<w:cr/> (QTextLayout-Zeilenumbruch) ·
//   U+E000 = <w:br w:type="page"/> (Seitenumbruch-MARKER, Aufgabe 2) ·
//   U+FFFC = atomarer opaker Run (Zeichnung/Feld — Raw bleibt verbatim).
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QStringView>
#include <QColor>
#include <QList>
#include <QHash>

class QIODevice;

namespace Docx {

// Zeichen-Sentinels (s. Kopfkommentar).
constexpr QChar kLineBreak(0x2028);
constexpr QChar kPageBreak(0xE000);
constexpr QChar kObjectChar(0xFFFC);

// Span in m_docXml (QChar-Offsets). len==0 ⇒ nicht vorhanden.
struct Span {
    int start = 0;
    int len   = 0;
    bool valid() const { return len > 0; }
};

// ── Zeichenformat (direkte Formatierung; set-Maske = explizit gesetzt) ───────
struct RunFmt {
    enum Field { FBold = 1, FItalic = 2, FUnderline = 4,
                 FSize = 8, FFont = 16, FColor = 32 };
    int     set = 0;
    bool    bold = false, italic = false, underline = false;
    qreal   sizePt = 11.0;
    QString font;
    QColor  color;

    bool operator==(const RunFmt& o) const {
        return set == o.set && bold == o.bold && italic == o.italic
               && underline == o.underline && qFuzzyCompare(sizePt + 1, o.sizePt + 1)
               && font == o.font && color == o.color;
    }
};

// ── Absatzformat ─────────────────────────────────────────────────────────────
struct ParFmt {
    enum Field { FAlign = 1, FLine = 2, FBefore = 4, FAfter = 8, FNum = 16 };
    int     set = 0;
    int     align = 0;          // 0 links, 1 zentriert, 2 rechts, 3 Blocksatz
    qreal   lineSpacing = 1.0;  // Vielfaches (w:line/240 bei lineRule auto)
    qreal   beforePt = 0.0;     // w:before Twips/20
    qreal   afterPt  = 0.0;     // w:after  Twips/20
    int     numId = -1;         // Liste (−1 = keine)
    int     ilvl  = 0;
    QString styleId;            // w:pStyle (nur Anzeige-Auflösung)
};

// ── Textlauf ─────────────────────────────────────────────────────────────────
struct Run {
    // Herkunft (leer bei neu erzeugten Runs):
    Span    rawSpan;            // gesamtes <w:r>…</w:r>
    Span    startTagSpan;       // "<w:r …>" (rsid-Attribute erhalten)
    Span    rprSpan;            // "<w:rPr>…</w:rPr>"
    QString rprXml;             // materialisiert, sobald Format geändert wurde
    bool    rprMaterialized = false;

    QString text;               // entkodiert (inkl. Sentinels)
    RunFmt  fmt;                // direkte Formatierung (geparst)
    bool    opaque = false;     // nicht verstandener Run — Raw bleibt verbatim
    bool    dirty  = false;     // Text/Format geändert → aus Teilen serialisieren

    QString currentRpr(QStringView docXml) const {
        if (rprMaterialized) return rprXml;
        if (rprSpan.valid()) return docXml.mid(rprSpan.start, rprSpan.len).toString();
        return {};
    }
};

// ── Block (Absatz oder opaker Fremdblock) ────────────────────────────────────
struct Block {
    enum Kind {
        Paragraph = 0,
        OpaqueVisible,          // z. B. w:tbl — Platzhalter in der Anzeige
        OpaqueHidden            // z. B. w:sectPr, bookmarkStart — unsichtbar
    };
    Kind    kind = Paragraph;
    Span    rawSpan;            // gesamter Block im Original
    Span    startTagSpan;       // "<w:p …>"
    Span    pprSpan;            // "<w:pPr>…</w:pPr>"
    QString pprXml;             // materialisiert bei Absatzformat-Änderung
    bool    pprMaterialized = false;
    QString opaqueName;         // Elementname opaker Blöcke ("w:tbl", …)

    QList<Run> runs;
    ParFmt  pfmt;
    bool    dirty = false;      // Struktur/Text geändert → Absatz serialisieren

    QString plainText() const {
        QString t;
        for (const Run& r : runs) t += r.text;
        return t;
    }
    int textLength() const {
        int n = 0;
        for (const Run& r : runs) n += r.text.size();
        return n;
    }
    QString currentPpr(QStringView docXml) const {
        if (pprMaterialized) return pprXml;
        if (pprSpan.valid()) return docXml.mid(pprSpan.start, pprSpan.len).toString();
        return {};
    }
};

// ── Nummerierungs-Definitionen (Anzeige + Erzeugung) ─────────────────────────
struct NumLevel {
    QString numFmt;             // "bullet" | "decimal" | …
    QString lvlText;            // z. B. "%1."
};

// ─────────────────────────────────────────────────────────────────────────────
class Document {
public:
    bool load(const QString& path, QString* err = nullptr);

    //  Speichern über gezieltes XML-Splicing + ZIP-Roh-Kopie: newDocumentXml()
    //  baut das neue document.xml, replacementParts() liefert zusätzlich zu
    //  ersetzende/neue ZIP-Einträge (numbering.xml, [Content_Types].xml,
    //  word/_rels/document.xml.rels — nur wenn Listen-Infrastruktur nötig
    //  wurde). writeTo() schreibt den kompletten Container auf ein QIODevice.
    QString newDocumentXml() const;
    QHash<QString, QByteArray> replacementParts() const;
    bool writeTo(QIODevice* target, QString* err = nullptr) const;

    QString path() const { return m_path; }
    QStringView docXml() const { return m_docXml; }

    QList<Block> blocks;        // vom Controller mutiert (Undo über Kopien)

    // ── Anzeige-Auflösung (docDefaults + pStyle-Kette + direkte Formate) ─────
    RunFmt resolveRun(const Block& b, const Run& r) const;
    ParFmt resolvePar(const Block& b) const;
    const RunFmt& defaultRun() const { return m_defRun; }

    //  true, wenn IRGENDEINE Absatzvorlage (oder docDefaults) eine Nummerierung
    //  mitbringt. Ist es false, kann resolvePar(b).numId ausschliesslich aus
    //  b.pfmt stammen — Aufrufer, die nur an der Nummerierung interessiert
    //  sind (DocxTextArea::rebuildMarkers, laeuft ueber ALLE Bloecke bei jedem
    //  Tastendruck), duerfen die Vorlagenaufloesung dann komplett ueberspringen.
    bool stylesMayNumber() const { return m_stylesMayNumber; }

    // ── Nummerierung ─────────────────────────────────────────────────────────
    NumLevel numLevel(int numId, int ilvl) const;
    //  Liefert eine numId für neue Listen; legt (lazy) eigene abstractNum/num-
    //  Definitionen an, die beim Speichern in word/numbering.xml gespliced
    //  werden (bzw. die Datei + ContentType-Override + Relationship anlegen).
    int newListNum(bool bullet);

    // ── Fabriken/Utilities ───────────────────────────────────────────────────
    static QByteArray emptyDocxBytes(const QString& title);   // leeres A4-Dokument
    static QString    plainTextPreview(const QString& path, int maxLines);
    static QString    xmlEscape(const QString& s);
    static QString    serializeRunsText(const QString& text); // Text → <w:t>/<w:tab/>…

    //  Kanonisch geordnetes Einfügen/Ersetzen EINES Property-Elements in einem
    //  bestehenden <w:rPr>/<w:pPr>-Fragment — alle übrigen Kinder bleiben
    //  verbatim erhalten (öffentlich für gezielte Tests).
    static QString upsertProp(const QString& prXml, const QString& wrapTag,
                              const QString& propName, const QString& newXml,
                              const QStringList& order);

private:
    struct StyleDef {
        QString basedOn;
        RunFmt  rf;
        ParFmt  pf;
    };

    bool parseDocumentXml(QString* err);
    bool parseStylesXml(const QByteArray& xml);
    bool parseNumberingXml(const QByteArray& xml);
    static void parseRunProps(QStringView xml, RunFmt* out);
    static void parseParProps(QStringView xml, ParFmt* out);

    QString buildParagraphXml(const Block& b) const;
    QString buildRunXml(const Run& r) const;
    QString buildRPrXml(const RunFmt& f) const;      // nur für NEUE Runs

    QString m_path;
    QString m_docXml;            // dekodiertes word/document.xml (EINZIGE Kopie)
    Span    m_bodyPrefix;        // alles vor dem ersten Block (inkl. <w:body>)
    Span    m_bodySuffix;        // alles nach dem letzten Block (</w:body>…)

    RunFmt  m_defRun;            // docDefaults (vollständig belegt)
    ParFmt  m_defPar;
    QHash<QString, StyleDef> m_styles;
    bool    m_stylesMayNumber = false;   // s. stylesMayNumber()

    QHash<int, QHash<int, NumLevel>> m_numLevels;    // numId → ilvl → Level
    QHash<int, int> m_numToAbstract;
    //  Lazy erzeugte eigene Listen-Definitionen (beim Speichern gespliced):
    mutable int m_ownAbstractBullet = -1;
    mutable int m_ownAbstractDecimal = -1;
    QList<QPair<int, bool>> m_pendingNums;           // (numId, bullet)
    int  m_nextNumId = 1;
    int  m_nextAbstractId = 0;
    bool m_hadNumberingPart = false;
    QString m_numberingXml;      // dekodierter Bestand (für Splice), sonst leer
};

} // namespace Docx
