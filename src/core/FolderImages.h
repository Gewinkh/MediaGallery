#pragma once
// Die Bilder NEBEN einer Datei als Modell für ein Auswahl-Popup. Beide Editoren bieten dieselbe Abkürzung an,
// deshalb steht die Abfrage EINMAL hier statt je Controller. Rückgabe `[{ name, url }]`, sortiert nach Name.

#include <QString>
#include <QVariantList>

namespace mg {

// `maxCount` deckelt die Liste: ein Ordner mit tausenden Bildern soll die Kachel nicht lahmlegen.
// `includeNonImages` nimmt PDFs auf - der DOCX-Editor bietet für sie die Seitenauswahl an.
QVariantList folderImages(const QString& fileOrFolder, int maxCount = 300,
                          bool includeNonImages = false);

}   // namespace mg
