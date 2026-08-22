#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  AudioTags - liest Titel, Interpret, Album und das eingebettete Bild aus einer
//  Audiodatei. Der Player zeigte bis dahin den DATEINAMEN und eine gezeichnete
//  Platzhalterfläche.
//
//  WARUM EIN EIGENER LESER (Entscheidung nach §0):
//  Qt hat keine eigenständige Tag-Schnittstelle - `QMediaMetaData` hängt an
//  einem laufenden `QMediaPlayer`, und genau den benutzt die Audiokette bewusst
//  NICHT (er gibt seine Samples nicht heraus, s. `AudioEngine.h`). Eine
//  Fremdbibliothek (TagLib) wäre eine neue Abhängigkeit für ein paar hundert
//  Zeilen Formatwissen - gegen §0-Priorität 3. Gelesen werden deshalb selbst:
//
//      MP3/AIFF …  ID3v2.2/2.3/2.4 (Kopf vorn), ID3v1 als Rückfall (128 Byte am Ende)
//      M4A/MP4     `moov/udta/meta/ilst` (©nam, ©ART, ©alb, covr)
//      FLAC        Vorbis-Kommentare + METADATA_BLOCK_PICTURE
//      OGG/OPUS    Vorbis-Kommentare im zweiten Ogg-Paket
//
//  Muster `PdfPageCopier`/`Mp4AudioExtract`: nur Qt6::Core, **kein `Q_OBJECT`**,
//  kein moc - isoliert testbar (`tests/audio/tst_audiotags.cpp`).
//
//  HÄRTUNG (Regel 21): Es sind FREMDE Dateien. Jede Länge wird gegen die
//  vorhandenen Bytes geprüft, bevor gelesen wird; Deckel gegen absurde Angaben
//  stehen als `k*`-Konstanten in der `.cpp`. Ein nicht erkanntes oder kaputtes
//  Feld führt zu einem LEEREN Feld, nie zu einem Zugriff daneben.
//
//  GELESEN WIRD NUR DER KOPF: ohne Bild kostet ein Aufruf einen Sprung und ein
//  paar Kilobyte - billig genug, um beim Titelwechsel zu laufen. Das Bild wird
//  getrennt geholt (`withCover`), weil es hundertfach größer sein kann.
// ─────────────────────────────────────────────────────────────────────────────

#include <QByteArray>
#include <QString>

namespace AudioTags {

struct Tags {
    //  Wurde überhaupt etwas gefunden? (Sonst bleibt es beim Dateinamen.)
    bool    ok = false;
    QString title;
    QString artist;
    QString album;
    int     trackNo = 0;
    //  Rohbytes des eingebetteten Bildes - NUR gefüllt, wenn `withCover` galt.
    QByteArray cover;
    QString    coverMime;      // "image/jpeg", "image/png", … (falls angegeben)
    //  Ein Bild ist DA, auch wenn es nicht geladen wurde (`withCover == false`).
    bool    hasCover = false;

    //  Was die Oberfläche anzeigt, wenn nichts anderes bekannt ist: der Titel
    //  aus den Tags, sonst der Dateiname ohne Endung.
    QString displayTitle(const QString& path) const;
    //  „Interpret - Album", je nachdem, was vorhanden ist (kann leer sein).
    QString subtitle() const;
};

//  Liest die Tags. `withCover = false` überspringt die Bilddaten und setzt nur
//  `hasCover` - das ist der Weg für Leiste und Liste.
Tags read(const QString& path, bool withCover = false);

//  Nur das eingebettete Bild (leer, wenn keines da ist). Getrennt, damit der
//  Bild-Anbieter es im eigenen Faden holen kann.
QByteArray readCover(const QString& path, QString* mime = nullptr);

} // namespace AudioTags
