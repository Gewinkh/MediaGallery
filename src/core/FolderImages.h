#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  FolderImages - die Bilder NEBEN einer Datei, als Modell für ein Auswahl-
//  Popup. Beide Editoren bieten dieselbe Abkürzung an („nimm ein Bild aus dem
//  Ordner, in dem das Dokument liegt"), deshalb steht die Abfrage EINMAL hier
//  statt je Controller.
//
//  Rückgabe: `[{ name, url }]` - genau das, was ein QML-GridView als Modell
//  braucht. Reihenfolge ist der Dateiname (ohne Groß-/Kleinschreibung).
// ─────────────────────────────────────────────────────────────────────────────

#include <QString>
#include <QVariantList>

namespace mg {

//  `fileOrFolder` darf die Dokumentdatei ODER ihr Verzeichnis sein.
//  `maxCount` deckelt die Liste: das Popup zeigt Miniaturen, ein Ordner mit
//  tausenden Bildern soll die Kachel nicht lahmlegen (RAM = Priorität 1).
//  `includeNonImages` nimmt zusätzlich PDFs auf - der DOCX-Editor bietet für
//  sie die Seitenauswahl an, für einen Stempel wäre eine PDF unsinnig.
QVariantList folderImages(const QString& fileOrFolder, int maxCount = 300,
                          bool includeNonImages = false);

}   // namespace mg
