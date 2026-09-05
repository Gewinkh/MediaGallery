#pragma once
// Liest Titel, Interpret, Album und Titelbild selbst: QMediaMetaData haengt an einem
// laufenden QMediaPlayer, den die Audiokette bewusst nicht benutzt. ID3v2/v1, MP4-ilst,
// Vorbis-Kommentare. Gelesen wird nur der Kopf; das Bild holt withCover getrennt.

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
