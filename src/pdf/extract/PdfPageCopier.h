#pragma once
// Verlustfreie Extraktion auf ROH-OBJEKTEBENE: Qt kann PDF-Seiten nicht vektoriell durchreichen (QPdfDocument
// rendert nur). Beherrscht XRef-Streams, Objekt-Streams und Brute-Scan-Recovery; Streams werden verbatim samt
// /Filter kopiert, Verweise auf ungewählte Seiten zu `null` gekappt. Gelesen per mmap - kein Heap-Vollload.

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QSizeF>

#include <memory>

class QIODevice;

class PdfAssembler {
public:
    // out: geöffnetes, beschreibbares Gerät (typisch QSaveFile). Der Assembler
    // schreibt sequenziell und führt die Byte-Offsets selbst mit.
    explicit PdfAssembler(QIODevice* out);
    ~PdfAssembler();

    // Schreibt den PDF-Header und reserviert Katalog (Obj 1) + Seitenbaum
    // (Obj 2). Genau einmal vor allen add*-Aufrufen aufrufen.
    bool begin(QString* err);

    // Übernimmt die Seiten (0-basiert, aufsteigend erwartet) VERLUSTFREI. false bei nicht kopierbarer Quelle -
    // dann wurde für sie NICHTS geschrieben und der Aufrufer kann je Datei auf Rasterung ausweichen.
    bool addSourcePages(const QString& sourcePath, const QVector<int>& pages,
                        QString* err);

    // Wie oben, zusätzlich mit einer DREHUNG je Seite (Vielfaches von 90, parallel zu `pages`). Sie wirkt ZUSÄTZLICH
    // zur Eigendrehung und wird als materialisiertes /Rotate geschrieben; der Seiteninhalt bleibt unverändert.
    bool addSourcePages(const QString& sourcePath, const QVector<int>& pages,
                        const QVector<int>& rotations, QString* err);

    // Fallback-Seite: hängt EIN JPEG (DCTDecode) als vollflächige Bildseite mit
    // der gegebenen Seitengröße in PDF-Punkten an. RGB, 8 Bit je Kanal.
    bool addRasterPage(const QByteArray& jpeg, int pxW, int pxH,
                       const QSizeF& pagePt, QString* err);

    // Leere Seite mit der gegebenen Größe anhängen, ohne Ressourcen und mit leerem Inhaltsstrom - der
    // PDF-Seitengrund ist per Definition weiß. Verlustfrei einreihbar zwischen kopierte Quellseiten.
    bool addBlankPage(const QSizeF& pagePt, QString* err);

    bool finish(QString* err);

    int pageCount() const { return m_pageObjs.size(); }

    // Schreibt die Datei VOLLSTÄNDIG neu, jedes Objekt einmal, ohne /Prev-Kette. Nötig fürs Schwärzen: alle anderen
    // Einheiten hängen an, der Text wäre aus der Anzeige verschwunden, stünde aber noch in den alten Bytes.
    // PREIS: der Katalog entsteht neu - /AcroForm (vorher `flatten`), Lesezeichen, Ziele und Metadaten fallen weg.
    static bool rebuild(const QString& sourcePath, const QString& outputPath,
                        QString* err);

    // Anzahl Seiten einer PDF ermitteln, ohne sie zu rendern (leichtgewichtiger
    // Struktur-Parse; -1 bei Fehlschlag). Für den Ordner-Scan des globalen
    // Extraktionsdialogs - vermeidet QPdfDocument-Vollladungen je Datei.
    static int probePageCount(const QString& sourcePath);

private:
    bool writeRaw(const QByteArray& bytes, QString* err);
    bool beginObject(int num, QString* err);

    // Ein Auswahlauftrag ruft `addSourcePages` je zusammenhängendem Block auf; gemischte Seiten treffen dieselbe
    // Quelle immer wieder. Der Zwischenspeicher hält je Quelle Struktur-Parse und Objektnummern-Zuordnung, damit
    // geteilte Objekte genau EINMAL in die Ausgabe wandern. Gedeckelt (LRU), darüber gilt das alte Verhalten.
    struct SourceCache;
    std::unique_ptr<SourceCache> m_sources;

    QIODevice*        m_out = nullptr;
    qint64            m_pos = 0;          // mitgeführter Schreib-Offset
    QVector<qint64>   m_offsets;          // Objektnummer -> Byte-Offset (Index 0 frei)
    QVector<int>      m_pageObjs;         // Objektnummern der Seiten (Ausgabereihenfolge)
    int               m_nextObj = 3;      // 1 = Katalog, 2 = Seitenbaum (reserviert)
    bool              m_begun  = false;
    bool              m_failed = false;
};
