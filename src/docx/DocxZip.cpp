#include "docx/DocxZip.h"

#include <QFile>
#include <QtEndian>
#include <zlib.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Interne Helfer: Little-Endian-Lesen/Schreiben über QByteArray/QIODevice.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

quint16 rd16(const uchar* p) { return qFromLittleEndian<quint16>(p); }
quint32 rd32(const uchar* p) { return qFromLittleEndian<quint32>(p); }

void put16(QByteArray& b, quint16 v) {
    char buf[2]; qToLittleEndian<quint16>(v, buf); b.append(buf, 2);
}
void put32(QByteArray& b, quint32 v) {
    char buf[4]; qToLittleEndian<quint32>(v, buf); b.append(buf, 4);
}

constexpr quint32 kSigLocal   = 0x04034b50;   // lokaler Dateiheader
constexpr quint32 kSigCentral = 0x02014b50;   // Zentralverzeichnis-Eintrag
constexpr quint32 kSigEocd    = 0x06054b50;   // End of Central Directory

} // namespace

namespace DocxZip {

// ─────────────────────────────────────────────────────────────────────────────
//  zlib (raw, windowBits −15)
// ─────────────────────────────────────────────────────────────────────────────
QByteArray inflateRaw(const QByteArray& comp, quint32 expectedSize, bool* ok) {
    if (ok) *ok = false;
    // RAM-Kantenschutz: DOCX-Teile sind klein; 256 MB Deckel wie PdfPageCopier.
    if (expectedSize > 256u * 1024u * 1024u)
        return {};
    QByteArray out;
    out.resize(int(expectedSize));

    z_stream zs; memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, -15) != Z_OK)
        return {};
    zs.next_in   = reinterpret_cast<Bytef*>(const_cast<char*>(comp.constData()));
    zs.avail_in  = uInt(comp.size());
    zs.next_out  = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = uInt(out.size());
    const int rc = inflate(&zs, Z_FINISH);
    const bool good = (rc == Z_STREAM_END) && (zs.total_out == expectedSize);
    inflateEnd(&zs);
    if (!good)
        return {};
    if (ok) *ok = true;
    return out;
}

QByteArray deflateRaw(const QByteArray& plain, bool* ok) {
    if (ok) *ok = false;
    z_stream zs; memset(&zs, 0, sizeof(zs));
    // Level 6 = zlib-Standard (guter Kompromiss Zeit/Größe, Word-üblich).
    if (deflateInit2(&zs, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return {};
    QByteArray out;
    out.resize(int(deflateBound(&zs, uLong(plain.size()))));
    zs.next_in   = reinterpret_cast<Bytef*>(const_cast<char*>(plain.constData()));
    zs.avail_in  = uInt(plain.size());
    zs.next_out  = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = uInt(out.size());
    const int rc = deflate(&zs, Z_FINISH);
    const bool good = (rc == Z_STREAM_END);
    out.truncate(int(zs.total_out));
    deflateEnd(&zs);
    if (!good)
        return {};
    if (ok) *ok = true;
    return out;
}

quint32 crcOf(const QByteArray& data) {
    return quint32(crc32(0L, reinterpret_cast<const Bytef*>(data.constData()),
                         uInt(data.size())));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Reader
// ─────────────────────────────────────────────────────────────────────────────
Reader::~Reader() { close(); }

void Reader::close() {
    delete m_dev;
    m_dev = nullptr;
    m_entries.clear();
}

bool Reader::open(const QString& path, QString* err) {
    close();
    auto* f = new QFile(path);
    if (!f->open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("Datei nicht lesbar: %1").arg(path);
        delete f;
        return false;
    }
    m_dev = f;
    const qint64 fileSize = f->size();

    // EOCD vom Dateiende suchen (Kommentar bis 64 KB möglich; DOCX: praktisch 0).
    const qint64 tailLen = qMin<qint64>(fileSize, 65557);
    if (tailLen < 22) {
        if (err) *err = QStringLiteral("Kein ZIP (zu klein).");
        close(); return false;
    }
    f->seek(fileSize - tailLen);
    const QByteArray tail = f->read(tailLen);
    int eocd = -1;
    for (int i = tail.size() - 22; i >= 0; --i) {
        if (rd32(reinterpret_cast<const uchar*>(tail.constData() + i)) == kSigEocd) {
            eocd = i; break;
        }
    }
    if (eocd < 0) {
        if (err) *err = QStringLiteral("Kein ZIP (EOCD fehlt).");
        close(); return false;
    }
    const uchar* e = reinterpret_cast<const uchar*>(tail.constData() + eocd);
    const quint16 count  = rd16(e + 10);
    const quint32 cdSize = rd32(e + 12);
    const quint32 cdOfs  = rd32(e + 16);
    if (count == 0xFFFF || cdOfs == 0xFFFFFFFFu) {
        if (err) *err = QStringLiteral("ZIP64 wird nicht unterstützt.");
        close(); return false;
    }
    if (qint64(cdOfs) + qint64(cdSize) > fileSize) {
        if (err) *err = QStringLiteral("Zentralverzeichnis außerhalb der Datei.");
        close(); return false;
    }

    f->seek(cdOfs);
    const QByteArray cd = f->read(cdSize);
    if (cd.size() != int(cdSize)) {
        if (err) *err = QStringLiteral("Zentralverzeichnis unvollständig.");
        close(); return false;
    }

    // Zentralverzeichnis-Einträge parsen.
    int pos = 0;
    m_entries.reserve(count);
    for (int i = 0; i < count; ++i) {
        if (pos + 46 > cd.size()) {
            if (err) *err = QStringLiteral("Zentralverzeichnis defekt.");
            close(); return false;
        }
        const uchar* p = reinterpret_cast<const uchar*>(cd.constData() + pos);
        if (rd32(p) != kSigCentral) {
            if (err) *err = QStringLiteral("Zentralverzeichnis-Signatur fehlt.");
            close(); return false;
        }
        Entry en;
        en.versionMade  = rd16(p + 4);
        en.versionNeed  = rd16(p + 6);
        const quint16 flags = rd16(p + 8);
        en.method       = rd16(p + 10);
        en.modTime      = rd16(p + 12);
        en.modDate      = rd16(p + 14);
        en.crc32        = rd32(p + 16);
        en.compSize     = rd32(p + 20);
        en.uncompSize   = rd32(p + 24);
        const quint16 nameLen  = rd16(p + 28);
        const quint16 extraLen = rd16(p + 30);
        const quint16 commLen  = rd16(p + 32);
        en.internalAttr = rd16(p + 36);
        en.externalAttr = rd32(p + 38);
        const quint32 localOfs = rd32(p + 42);

        if (en.compSize == 0xFFFFFFFFu || en.uncompSize == 0xFFFFFFFFu
            || localOfs == 0xFFFFFFFFu) {
            if (err) *err = QStringLiteral("ZIP64 wird nicht unterstützt.");
            close(); return false;
        }
        if (flags & 0x0001) {
            if (err) *err = QStringLiteral("Verschlüsselte ZIP-Einträge werden nicht unterstützt.");
            close(); return false;
        }
        if (en.method != 0 && en.method != 8) {
            if (err) *err = QStringLiteral("Nicht unterstützte Kompressionsmethode (%1).")
                                .arg(en.method);
            close(); return false;
        }
        if (pos + 46 + nameLen + extraLen + commLen > cd.size()) {
            if (err) *err = QStringLiteral("Zentralverzeichnis defekt (Längen).");
            close(); return false;
        }
        // Eintragsnamen sind in DOCX immer ASCII/UTF-8.
        en.name         = QString::fromUtf8(cd.constData() + pos + 46, nameLen);
        en.extraCentral = cd.mid(pos + 46 + nameLen, extraLen);
        en.comment      = cd.mid(pos + 46 + nameLen + extraLen, commLen);

        // Datenoffset über den LOKALEN Header ermitteln (dessen Namens-/Extra-
        // Längen können vom Zentralverzeichnis abweichen — Extra-Felder!).
        if (qint64(localOfs) + 30 > fileSize) {
            if (err) *err = QStringLiteral("Lokaler Header außerhalb der Datei.");
            close(); return false;
        }
        f->seek(localOfs);
        const QByteArray lh = f->read(30);
        if (lh.size() != 30
            || rd32(reinterpret_cast<const uchar*>(lh.constData())) != kSigLocal) {
            if (err) *err = QStringLiteral("Lokaler Header defekt (%1).").arg(en.name);
            close(); return false;
        }
        const quint16 lNameLen  = rd16(reinterpret_cast<const uchar*>(lh.constData()) + 26);
        const quint16 lExtraLen = rd16(reinterpret_cast<const uchar*>(lh.constData()) + 28);
        en.dataOffset = qint64(localOfs) + 30 + lNameLen + lExtraLen;
        if (en.dataOffset + qint64(en.compSize) > fileSize) {
            if (err) *err = QStringLiteral("Eintragsdaten außerhalb der Datei (%1).").arg(en.name);
            close(); return false;
        }

        m_entries.append(en);
        pos += 46 + nameLen + extraLen + commLen;
    }
    return true;
}

int Reader::indexOf(const QString& name) const {
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).name == name)
            return i;
    return -1;
}

QByteArray Reader::rawData(int index, bool* ok) const {
    if (ok) *ok = false;
    if (!m_dev || index < 0 || index >= m_entries.size())
        return {};
    const Entry& e = m_entries.at(index);
    if (!m_dev->seek(e.dataOffset))
        return {};
    QByteArray raw = m_dev->read(e.compSize);
    if (raw.size() != int(e.compSize))
        return {};
    if (ok) *ok = true;
    return raw;
}

QByteArray Reader::fileData(int index, bool* ok) const {
    if (ok) *ok = false;
    bool rawOk = false;
    const QByteArray raw = rawData(index, &rawOk);
    if (!rawOk)
        return {};
    const Entry& e = m_entries.at(index);
    if (e.method == 0) {                       // Store: 1:1
        if (ok) *ok = true;
        return raw;
    }
    bool infOk = false;
    QByteArray plain = inflateRaw(raw, e.uncompSize, &infOk);
    if (!infOk)
        return {};
    // Integrität: CRC muss stimmen (schützt vor stiller Korruption).
    if (crcOf(plain) != e.crc32)
        return {};
    if (ok) *ok = true;
    return plain;
}

QByteArray Reader::fileData(const QString& name, bool* ok) const {
    return fileData(indexOf(name), ok);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Writer
// ─────────────────────────────────────────────────────────────────────────────
Writer::Writer(QIODevice* target) : m_dev(target) {}

bool Writer::writeLocal(const Entry& e, const QByteArray& data, QString* err) {
    if (!m_dev || m_finished) {
        if (err) *err = QStringLiteral("Writer nicht bereit.");
        return false;
    }
    Written w;
    w.e = e;
    w.localOffset = m_dev->pos();

    const QByteArray name = e.name.toUtf8();
    QByteArray h;
    h.reserve(30 + name.size());
    put32(h, kSigLocal);
    put16(h, e.versionNeed);
    put16(h, 0);                    // Flags: keine Data-Descriptors, keine Verschlüsselung
    put16(h, e.method);
    put16(h, e.modTime);
    put16(h, e.modDate);
    put32(h, e.crc32);
    put32(h, e.compSize);
    put32(h, e.uncompSize);
    put16(h, quint16(name.size()));
    put16(h, 0);                    // lokales Extra-Feld: normalisiert leer
    h.append(name);

    if (m_dev->write(h) != h.size() || m_dev->write(data) != data.size()) {
        if (err) *err = QStringLiteral("Schreiben fehlgeschlagen (%1).").arg(e.name);
        return false;
    }
    m_written.append(w);
    return true;
}

bool Writer::addRaw(const Entry& src, const QByteArray& rawCompressed, QString* err) {
    if (int(src.compSize) != rawCompressed.size()) {
        if (err) *err = QStringLiteral("Rohdaten-Größe passt nicht (%1).").arg(src.name);
        return false;
    }
    return writeLocal(src, rawCompressed, err);
}

bool Writer::addFile(const QString& name, const QByteArray& uncompressed,
                     const Entry* like, QString* err) {
    Entry e;
    if (like) {                      // Zeitstempel/Attribute der Quelle fortführen
        e = *like;
        e.extraCentral.clear();      // Extra bezog sich auf den ALTEN Inhalt
        e.comment.clear();
    }
    e.name       = name;
    e.crc32      = crcOf(uncompressed);
    e.uncompSize = quint32(uncompressed.size());

    bool defOk = false;
    QByteArray comp = deflateRaw(uncompressed, &defOk);
    if (!defOk) {
        if (err) *err = QStringLiteral("Deflate fehlgeschlagen (%1).").arg(name);
        return false;
    }
    if (comp.size() < uncompressed.size()) {
        e.method   = 8;
        e.compSize = quint32(comp.size());
        return writeLocal(e, comp, err);
    }
    // Store, wenn Deflate nichts bringt (winzige/inkompressible Teile).
    e.method   = 0;
    e.compSize = e.uncompSize;
    return writeLocal(e, uncompressed, err);
}

bool Writer::finish(QString* err) {
    if (!m_dev || m_finished) {
        if (err) *err = QStringLiteral("Writer nicht bereit.");
        return false;
    }
    const qint64 cdStart = m_dev->pos();
    QByteArray cd;
    for (const Written& w : std::as_const(m_written)) {
        const Entry& e = w.e;
        const QByteArray name = e.name.toUtf8();
        put32(cd, kSigCentral);
        put16(cd, e.versionMade);
        put16(cd, e.versionNeed);
        put16(cd, 0);                                // Flags normalisiert
        put16(cd, e.method);
        put16(cd, e.modTime);
        put16(cd, e.modDate);
        put32(cd, e.crc32);
        put32(cd, e.compSize);
        put32(cd, e.uncompSize);
        put16(cd, quint16(name.size()));
        put16(cd, quint16(e.extraCentral.size()));
        put16(cd, quint16(e.comment.size()));
        put16(cd, 0);                                // Disk-Nummer
        put16(cd, e.internalAttr);
        put32(cd, e.externalAttr);
        put32(cd, quint32(w.localOffset));
        cd.append(name);
        cd.append(e.extraCentral);
        cd.append(e.comment);
    }
    QByteArray eocd;
    put32(eocd, kSigEocd);
    put16(eocd, 0); put16(eocd, 0);                  // Disk-Nummern
    put16(eocd, quint16(m_written.size()));
    put16(eocd, quint16(m_written.size()));
    put32(eocd, quint32(cd.size()));
    put32(eocd, quint32(cdStart));
    put16(eocd, 0);                                  // Kommentar leer

    if (m_dev->write(cd) != cd.size() || m_dev->write(eocd) != eocd.size()) {
        if (err) *err = QStringLiteral("Zentralverzeichnis-Schreiben fehlgeschlagen.");
        return false;
    }
    m_finished = true;
    return true;
}

} // namespace DocxZip
