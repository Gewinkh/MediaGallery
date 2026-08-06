#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  DocxZip — minimaler ZIP-Reader/-Writer für den DOCX-Editor (OOXML-Container).
//
//  Abhängigkeiten: Qt6::Core + mg::zcodec (src/core/ZCodec.h) — keine neue
//  Bibliothek; Muster PdfPageCopier. Kein Q_OBJECT/moc → isoliert testbar.
//  ZLIB ist dadurch OPTIONAL; ohne sie weist Reader::open Einträge mit
//  Methode 8 ab, also praktisch jedes DOCX (Begründung in ZCodec.h).
//
//  Kernprinzip (Verlusterhaltung, §0): Der Writer übernimmt ALLE nicht
//  angefassten Einträge als ROH-KOPIE — die komprimierten Datenbytes, CRC,
//  Größen und Methode stammen 1:1 aus der Quelle (KEIN Neu-Komprimieren).
//  Nur gezielt ersetzte Einträge (z. B. word/document.xml) werden neu
//  deflatiert. Die Container-Rahmung (lokale Header/Zentralverzeichnis) wird
//  normalisiert geschrieben (Größen im lokalen Header statt Data-Descriptor),
//  was für Office-Reader transparent ist.
//
//  Grenzen (bewusst, DOCX-tauglich): kein ZIP64 (DOCX << 4 GB), keine
//  Verschlüsselung, Methoden nur Store(0)/Deflate(8). Verletzungen führen zu
//  sauberen Fehlermeldungen statt stiller Korruption.
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QByteArray>
#include <QList>

class QIODevice;

namespace DocxZip {

// Ein Eintrag, wie er im Zentralverzeichnis der Quelle steht. Alle Felder, die
// für eine byteidentische Roh-Übernahme nötig sind, bleiben erhalten (inkl.
// Extra-Feldern und Kommentar des Zentralverzeichnisses).
struct Entry {
    QString    name;                 // Pfad im Archiv (z. B. "word/document.xml")
    quint16    method       = 8;    // 0 = Store, 8 = Deflate
    quint32    crc32        = 0;
    quint32    compSize     = 0;    // komprimierte Größe (Datenbytes)
    quint32    uncompSize   = 0;
    quint16    modTime      = 0;    // DOS-Zeit/-Datum (verbatim übernommen)
    quint16    modDate      = 0;
    quint16    versionMade  = 20;
    quint16    versionNeed  = 20;
    quint16    internalAttr = 0;
    quint32    externalAttr = 0;
    qint64     dataOffset   = -1;   // Dateioffset der ROH-Datenbytes in der Quelle
    QByteArray extraCentral;        // Extra-Feld des Zentralverzeichnisses (verbatim)
    QByteArray comment;             // Eintrags-Kommentar (verbatim)
};

// ── Reader ───────────────────────────────────────────────────────────────────
//  Liest das Zentralverzeichnis (EOCD-Suche vom Dateiende) und erlaubt
//  gezielten Zugriff: rawData() = komprimierte Bytes 1:1, fileData() =
//  dekomprimiert (zlib raw inflate bei Methode 8).
class Reader {
public:
    ~Reader();
    bool open(const QString& path, QString* err = nullptr);
    void close();
    bool isOpen() const { return m_dev != nullptr; }

    const QList<Entry>& entries() const { return m_entries; }
    int  indexOf(const QString& name) const;        // -1 wenn nicht vorhanden

    QByteArray rawData(int index, bool* ok = nullptr) const;   // komprimiert (roh)
    QByteArray fileData(int index, bool* ok = nullptr) const;  // dekomprimiert
    QByteArray fileData(const QString& name, bool* ok = nullptr) const;

private:
    QIODevice*   m_dev = nullptr;   // QFile (Heap, damit der Header schlank bleibt)
    QList<Entry> m_entries;
};

// ── Writer ───────────────────────────────────────────────────────────────────
//  Schreibt Einträge in Aufrufreihenfolge auf ein beliebiges QIODevice
//  (QSaveFile für atomare Datei-Ersetzung, QBuffer für In-Memory-Erzeugung).
//  addRaw() übernimmt einen Quell-Eintrag byteidentisch (Datenbytes + CRC +
//  Größen + Methode + Zeitstempel + Attribute); addFile() deflatiert neu.
class Writer {
public:
    explicit Writer(QIODevice* target);              // muss offen + schreibbar sein

    bool addRaw(const Entry& src, const QByteArray& rawCompressed, QString* err = nullptr);
    bool addFile(const QString& name, const QByteArray& uncompressed,
                 const Entry* like = nullptr,        // Zeitstempel/Attribute übernehmen
                 QString* err = nullptr);
    bool finish(QString* err = nullptr);             // Zentralverzeichnis + EOCD

private:
    struct Written {                                  // fürs Zentralverzeichnis
        Entry  e;
        qint64 localOffset = 0;
    };
    bool writeLocal(const Entry& e, const QByteArray& data, QString* err);

    QIODevice*     m_dev = nullptr;
    QList<Written> m_written;
    bool           m_finished = false;
};

// ── Codec-Helfer über mg::zcodec (raw deflate, windowBits −15 = ZIP) ────────
QByteArray inflateRaw(const QByteArray& comp, quint32 expectedSize, bool* ok);
QByteArray deflateRaw(const QByteArray& plain, bool* ok);
quint32    crcOf(const QByteArray& data);

} // namespace DocxZip
