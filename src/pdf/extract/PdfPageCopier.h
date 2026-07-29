#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfPageCopier.h
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  VERLUSTFREIE Extraktion ausgewählter Seiten aus PDF-Dateien in eine NEUE PDF
//  auf ROH-OBJEKTEBENE (Textebene, Vektoren, Schriften und eingebettete
//  Annotationen bleiben 1:1 erhalten). Qt selbst kann PDF-Seiten nicht
//  vektoriell durchreichen (QPdfDocument rendert nur) — deshalb parst diese
//  Klasse die PDF-Struktur direkt, wie es das Projekt für eingebettete Audios
//  bereits tut (PdfAudioController/PdfMediaHandler), hier jedoch vollständig:
//
//   • XRef: klassische Tabellen UND XRef-STREAMS (PDF 1.5+), inkl. /Prev-Ketten,
//     Hybrid-Dateien (/XRefStm) und FlateDecode mit PNG-Prädiktoren.
//   • OBJEKT-STREAMS (/Type/ObjStm): komprimierte Objekte werden dekodiert und
//     daraus geparst (das können die bestehenden Roh-Parser des Projekts nicht).
//   • Recovery: ist die XRef-Kette defekt, wird die Objekttabelle per
//     Brute-Scan („N G obj") rekonstruiert (letztes Vorkommen gewinnt =
//     jüngster Inkrement-Save) und der Katalog notfalls per /Type/Catalog-Suche
//     gefunden.
//
//  KOPIE-VERFAHREN
//  ───────────────
//  Je gewählter Seite wird der transitive Objektgraph (Ressourcen, Fonts,
//  XObjects, Inhaltsströme, Annotationen …) eingesammelt, umnummeriert und
//  verbatim in die Ziel-PDF geschrieben — Stream-Rohdaten werden UNVERÄNDERT
//  (samt /Filter) aus dem Quell-Mapping kopiert, es wird nichts neu kodiert.
//  Vererbte Seitenattribute (/Resources /MediaBox /CropBox /Rotate) werden aus
//  dem Seitenbaum materialisiert. Referenzen auf NICHT gewählte Seiten (z. B.
//  GoTo-Ziele von Link-Annotationen) werden zu `null` gekappt, damit der
//  Graph-Abschluss nicht das halbe Dokument mitzieht.
//
//  RAM: Die Quelldatei wird per QFile::map (mmap) gelesen — kein Heap-Vollload,
//  auch bei 100–300-MB-PDFs; nur dekodierte XRef-/Objekt-Streams liegen
//  transient im Speicher. Passt zur RAM-Priorität (§0, Prio 1 der MD).
//
//  GRENZEN (→ Aufrufer nutzt den Raster-Fallback, s. PdfExtractController):
//   • Verschlüsselte PDFs (/Encrypt) — Strings/Streams wären umzuschlüsseln.
//   • Exotische XRef-Filter/Prädiktoren außerhalb von FlateDecode/PNG.
//  addSourcePages() plant dafür ZUERST vollständig im Speicher und schreibt
//  erst bei Erfolg — ein Fehlschlag hinterlässt KEINE Fragmente in der Ausgabe.
//
//  ABHÄNGIGKEITEN: nur Qt6::Core + ZLIB (beides bestehende Projekt-
//  Abhängigkeiten) → isoliert testbar, keine neue Bibliothek (§0-Priorität 3).
// ══════════════════════════════════════════════════════════════════════════════

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QSizeF>

class QIODevice;

class PdfAssembler {
public:
    // out: geöffnetes, beschreibbares Gerät (typisch QSaveFile). Der Assembler
    // schreibt sequenziell und führt die Byte-Offsets selbst mit.
    explicit PdfAssembler(QIODevice* out);

    // Schreibt den PDF-Header und reserviert Katalog (Obj 1) + Seitenbaum
    // (Obj 2). Genau einmal vor allen add*-Aufrufen aufrufen.
    bool begin(QString* err);

    // Übernimmt die angegebenen Seiten (0-basiert, aufsteigend erwartet) der
    // Quelldatei VERLUSTFREI. Liefert false bei nicht kopierbarer Quelle
    // (verschlüsselt/defekt/exotisch) — dann wurde für diese Quelle NICHTS
    // geschrieben und der Aufrufer kann pro Datei auf Rasterung ausweichen.
    bool addSourcePages(const QString& sourcePath, const QVector<int>& pages,
                        QString* err);

    // Wie oben, zusätzlich mit einer DREHUNG je übernommener Seite (Grad,
    // Vielfaches von 90; parallel zu `pages`, leer = keine). Der Wert wirkt
    // ZUSÄTZLICH zur Eigendrehung der Quellseite und wird als materialisiertes
    // /Rotate der Zielseite geschrieben — der Seiteninhalt selbst bleibt
    // byteweise unverändert (Grundlage von „Seite drehen" im PDF-Editor).
    bool addSourcePages(const QString& sourcePath, const QVector<int>& pages,
                        const QVector<int>& rotations, QString* err);

    // Fallback-Seite: hängt EIN JPEG (DCTDecode) als vollflächige Bildseite mit
    // der gegebenen Seitengröße in PDF-Punkten an. RGB, 8 Bit je Kanal.
    bool addRasterPage(const QByteArray& jpeg, int pxW, int pxH,
                       const QSizeF& pagePt, QString* err);

    // Leere (weiße) Seite mit der gegebenen Größe in PDF-Punkten anhängen
    // (Aufgabe 3 „+ Seite" — Aufrufer nutzt A4 = 595.276 × 841.890 pt). Ohne
    // Ressourcen, leerer Inhaltsstrom — der PDF-Seitengrund ist per Definition
    // weiß. Verlustfrei einreihbar zwischen kopierte Quellseiten.
    bool addBlankPage(const QSizeF& pagePt, QString* err);

    // Schließt die Datei ab: Seitenbaum, Katalog, XRef-Tabelle, Trailer.
    bool finish(QString* err);

    // Bisher registrierte Seiten (für Fortschritt/Plausibilität).
    int pageCount() const { return m_pageObjs.size(); }

    // Schreibt `sourcePath` VOLLSTÄNDIG NEU nach `outputPath`: alle Seiten in
    // ihrer Reihenfolge verlustfrei kopiert, jedes Objekt genau EINMAL, mit
    // frischer XRef-Tabelle und OHNE /Prev-Kette.
    //
    // WOZU: Alle schreibenden Einheiten des Projekts arbeiten inkrementell
    // (anhängen, Originalbytes bleiben). Für „Text schwärzen" ist genau das
    // die Lücke: Der Text ist aus der Anzeige verschwunden, steht aber noch in
    // den alten Bytes der Datei — ein Hex-Editor findet ihn. Nach diesem
    // Neuschreiben ist nur noch der aktuelle Stand in der Datei; was kein
    // Objekt des Seitengraphen mehr ist, wird nicht mitkopiert.
    //
    // PREIS (der Aufrufer muss ihn kennen): Der Katalog entsteht neu — was
    // NICHT am Seitengraphen hängt, geht verloren: `/AcroForm` (Formulare
    // deshalb vorher mit `mg::PdfFormFields::flatten` festschreiben),
    // Lesezeichen, benannte Ziele, Dokument-Metadaten, Seitenbeschriftungen.
    // Deshalb wird NICHT jede Ausgabe so geschrieben, sondern nur die, die es
    // braucht.
    static bool rebuild(const QString& sourcePath, const QString& outputPath,
                        QString* err);

    // Anzahl Seiten einer PDF ermitteln, ohne sie zu rendern (leichtgewichtiger
    // Struktur-Parse; -1 bei Fehlschlag). Für den Ordner-Scan des globalen
    // Extraktionsdialogs — vermeidet QPdfDocument-Vollladungen je Datei.
    static int probePageCount(const QString& sourcePath);

private:
    bool writeRaw(const QByteArray& bytes, QString* err);
    // Beginnt ein neues Objekt „<num> 0 obj“ und merkt den Offset. Liefert num.
    bool beginObject(int num, QString* err);

    QIODevice*        m_out = nullptr;
    qint64            m_pos = 0;          // mitgeführter Schreib-Offset
    QVector<qint64>   m_offsets;          // Objektnummer → Byte-Offset (Index 0 frei)
    QVector<int>      m_pageObjs;         // Objektnummern der Seiten (Ausgabereihenfolge)
    int               m_nextObj = 3;      // 1 = Katalog, 2 = Seitenbaum (reserviert)
    bool              m_begun  = false;
    bool              m_failed = false;
};
