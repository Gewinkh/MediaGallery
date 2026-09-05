#pragma once
// Dünne Schicht über Deflate/Inflate/CRC-32; ZLIB ist damit optional (sonst qCompress/qUncompress).
// `Wrap::Raw` ENTPACKEN geht ohne zlib nicht - qUncompress verlangt den Adler-32 über den entpackten Daten.
// Ohne zlib fällt DOCX ganz weg, PDF bleibt (dessen Ströme sind zlib-gerahmt).

#include <QByteArray>
#include <QtGlobal>

namespace mg::zcodec {

enum class Wrap {
    Zlib,   //  15    - zlib-Kopf + Adler-32 (PDF /FlateDecode im Normalfall)
    Raw,    // −15    - nackter Deflate-Strom (ZIP-Eintrag)
    Auto    //  15+32 - Kopf selbst erkennen (zlib oder gzip); nur Entpacken
};

//  true, wenn gegen zlib gebaut wurde. false = Qt-Fallback, dann liefert
//  inflate() mit Wrap::Raw immer leer (s. Kopfkommentar).
bool available();

//  Obergrenze der Ausgabe in Byte (RAM-Kantenschutz). Ein Strom, der mehr
//  erzeugen will, gilt als Fehler.
constexpr qint64 kMaxOutput = 256LL << 20;

// `sizeHint` = erwartete Ausgabegröße, 0 wenn unbekannt - eine falsche Angabe kostet Zeit, nie Korrektheit.
// `tolerant` verwertet auch einen abgeschnittenen Strom, sofern schon Daten angefallen sind.
QByteArray inflate(const QByteArray& src, Wrap wrap, qint64 sizeHint,
                   bool tolerant, bool* ok);

//  Packt `src` mit `level` (0–9, wie zlib). Wrap::Auto wird als Wrap::Zlib
//  behandelt. Leer + ok=false bei Fehler.
QByteArray deflate(const QByteArray& src, Wrap wrap, int level, bool* ok);

quint32 crc32(const QByteArray& data);

}  // namespace mg::zcodec
