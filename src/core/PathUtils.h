#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  PathUtils.h — gemeinsame, zustandslose Pfad-Helfer.
//
//  Zentralisiert `toLocalPath`, das zuvor byte-identisch in ViewerController,
//  PdfTextController und PdfThumbnailProvider dupliziert war. Header-only
//  (inline) → kein zusaetzliches Kompilat, keine Verlinkung noetig.
// ─────────────────────────────────────────────────────────────────────────────
#include <QString>
#include <QUrl>
#include <QLatin1String>

namespace mg {

// Wandelt eine "file:"-URL in einen lokalen Dateipfad um; ein bereits lokaler
// Pfad wird unveraendert zurueckgegeben.
inline QString toLocalPath(const QString& s) {
    if (s.startsWith(QLatin1String("file:")))
        return QUrl(s).toLocalFile();
    return s;
}

} // namespace mg
