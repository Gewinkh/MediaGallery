#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  ZCodec - dünne Zwischenschicht über Deflate/Inflate/CRC-32. ZLIB ist damit
//  eine OPTIONALE Abhängigkeit: ist sie da (MG_HAVE_ZLIB, von CMake gesetzt),
//  ruft dieser Kopf zlib direkt; fehlt sie, übernehmen die IMMER vorhandenen
//  Qt-Codecs (qCompress/qUncompress) - Muster OcrEngine (Regel 19).
//
//  Motiv: zlib ist zur Laufzeit nie wirklich abwesend (Qt6 Gui und Qt6 Pdf
//  linken libz selbst); fehlen kann in der Praxis nur der HEADER, also das
//  Entwicklungspaket. Der Fallback muss deshalb nicht abschalten, sondern
//  ausweichen (§0-Priorität 1 vor 3).
//
//  Was der Fallback trägt und was nicht - die Grenze verläuft am Rahmen des
//  Datenstroms, nicht am Feature:
//    · Wrap::Zlib  entpacken -> qUncompress mit vorangestellter Größenangabe.
//    · Wrap::Zlib/Raw packen -> qCompress; für Raw fallen die 2 Byte Kopf und
//      die 4 Byte Adler-32 am Ende weg, der Rest IST der rohe Deflate-Strom.
//    · crc32()               -> eigene Tabelle, bitgleich zu zlib.
//    · Wrap::Raw ENTPACKEN   -> NICHT möglich. qUncompress verlangt einen
//      gültigen Adler-32 über den entpackten Daten; den kennt man vor dem
//      Entpacken nicht.
//    · Wrap::Auto            -> ohne zlib nur zlib-gerahmt, kein gzip.
//
//  Daraus folgt der Funktionsumfang ohne zlib: PDF anzeigen, bearbeiten,
//  Seiten extrahieren und eingebettete Medien bleiben (PDF-Ströme sind
//  zlib-gerahmt), DOCX fällt GANZ weg - auch das Speichern, weil
//  Document::writeTo die Quelldatei zuerst wieder einliest.
//
//  `tolerant` (abgeschnittene Ströme trotzdem verwerten) kann der Fallback
//  nicht nachbilden: qUncompress liefert alles oder nichts.
//
//  Kein Q_OBJECT/moc; freie Funktionen im Namespace mg::zcodec.
//  Test: tests/core/tst_zcodec.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include <QByteArray>
#include <QtGlobal>

namespace mg::zcodec {

//  Rahmung des Datenstroms - entspricht zlibs windowBits.
enum class Wrap {
    Zlib,   //  15    - zlib-Kopf + Adler-32 (PDF /FlateDecode im Normalfall)
    Raw,    // −15    - nackter Deflate-Strom (ZIP-Eintrag)
    Auto    //  15+32 - Kopf selbst erkennen (zlib oder gzip); nur Entpacken
};

//  true, wenn gegen zlib gebaut wurde. false = Qt-Fallback, dann liefert
//  inflate() mit Wrap::Raw immer leer (s. Kopfkommentar).
bool available();

//  Obergrenze der Ausgabe in Byte (RAM-Kantenschutz, §0-Priorität 4). Ein
//  Strom, der mehr erzeugen will, gilt als Fehler.
constexpr qint64 kMaxOutput = 256LL << 20;

//  Entpackt `src`. `sizeHint` = erwartete Größe der Ausgabe, 0 wenn unbekannt
//  (dient nur der Vorbelegung bzw. dem ersten Puffer des Fallbacks - eine
//  falsche Angabe kostet Zeit, nie Korrektheit). `tolerant` verwertet auch
//  einen abgeschnittenen Strom, sofern schon Daten angefallen sind.
//  Leer + ok=false bei Fehler.
QByteArray inflate(const QByteArray& src, Wrap wrap, qint64 sizeHint,
                   bool tolerant, bool* ok);

//  Packt `src` mit `level` (0–9, wie zlib). Wrap::Auto wird als Wrap::Zlib
//  behandelt. Leer + ok=false bei Fehler.
QByteArray deflate(const QByteArray& src, Wrap wrap, int level, bool* ok);

//  CRC-32 (Polynom 0xEDB88320 - ZIP/PNG).
quint32 crc32(const QByteArray& data);

}  // namespace mg::zcodec
