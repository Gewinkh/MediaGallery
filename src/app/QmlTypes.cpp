#include "app/QmlTypes.h"

#include "app/PaneController.h"
#include "app/PaneHost.h"
#include "core/FileBrowseModel.h"
#include "datev/DatevController.h"
#include "docx/edit/DocxEditController.h"
#include "table/TableController.h"
#include "docx/edit/DocxTextArea.h"
#include "editor/CodeHighlighter.h"
#include "editor/TextDecorations.h"
#include "editor/TextFoldBar.h"
#include "editor/TextGutter.h"
#include "editor/TextMinimap.h"
#include "image/edit/ImageEditController.h"
#include "media/GalleryRowModel.h"
#include "pdf/PdfAudioController.h"
#include "pdf/PdfTextController.h"
#include "pdf/edit/PdfEditController.h"

#include <QQmlEngine>

namespace mg {

void registerQmlTypes() {
    // Dezentrale, pro PdfSurface (PDF-Kachel) instanziierbare Editor-Controller -
    // eigener Zustand je geöffneter Datei (kein QML_ELEMENT-Makro, manuelle
    // Registrierung wie die übrigen Typen).
    qmlRegisterType<PdfTextController> ("MediaGallery", 1, 0, "PdfTextController");
    qmlRegisterType<PdfAudioController>("MediaGallery", 1, 0, "PdfAudioController");
    qmlRegisterType<PdfEditController> ("MediaGallery", 1, 0, "PdfEditController");
    // Dezentraler Bild-Editor: je ImageSurface-Kachel eine eigene Instanz.
    qmlRegisterType<ImageEditController>("MediaGallery", 1, 0, "ImageEditController");
    qmlRegisterType<DocxEditController>("MediaGallery", 1, 0, "DocxEditController");
    qmlRegisterType<DocxTextArea>      ("MediaGallery", 1, 0, "DocxTextArea");
    //  Seiten-Miniatur des DOCX-Editors (Delegate der Miniaturen-Leiste; malt
    //  über DocxTextArea::paintPageInto, hält also selbst kein Bild).
    qmlRegisterType<DocxPageThumb>     ("MediaGallery", 1, 0, "DocxPageThumb");
    //  Verzeichnis-Inhalt für den eigenen Datei-/Ordnerwähler
    //  (`qml/common/FileChooser.qml`) - ein Modell je Wähler, damit zwei
    //  geöffnete Wähler nicht im selben Verzeichnis stehen.
    qmlRegisterType<FileBrowseModel>   ("MediaGallery", 1, 0, "FileBrowseModel");
    //  Zeilenmodell der Galerie - je Ansicht eine Instanz, gespeist aus
    //  `galleryModel` (s. src/media/GalleryRowModel.h).
    qmlRegisterType<GalleryRowModel>   ("MediaGallery", 1, 0, "GalleryRowModel");
    //  Zwei-Fenster-Modus: `PaneController` je Hälfte, `PaneHost` erzeugt deren
    //  QML-Teilbaum mit eigenem Kontext (s. src/app/PaneHost.h).
    qmlRegisterType<PaneController>    ("MediaGallery", 1, 0, "PaneController");
    qmlRegisterType<PaneHost>          ("MediaGallery", 1, 0, "PaneHost");
    //  Syntaxfaerbung: EINE Instanz je Text-Kachel, sie haengt sich an das
    //  `textDocument` der dortigen TextArea.
    qmlRegisterType<mg::editor::CodeHighlighter>("MediaGallery", 1, 0, "CodeHighlighter");
    //  Zeilennummern-Spalte: malt nur die sichtbaren Bloecke (kein Item je Zeile).
    qmlRegisterType<mg::editor::TextGutter>("MediaGallery", 1, 0, "TextGutter");
    // ACHTUNG: `tests/bench/bench_shell.cpp` registriert dieselben Typen noch einmal selbst. Ein neuer Typ gehört
    // an BEIDE Stellen, sonst bricht der Prüfstand mit "X is not a type" (dreimal passiert).
    qmlRegisterType<mg::editor::TextMinimap>("MediaGallery", 1, 0, "TextMinimap");
    qmlRegisterType<mg::editor::TextFoldBar>("MediaGallery", 1, 0, "TextFoldBar");
    qmlRegisterType<mg::editor::TextDecorations>("MediaGallery", 1, 0, "TextDecorations");
    //  DATEV-Buchungsstapel: EINE Instanz je Kachel, damit zwei Haelften
    //  verschiedene Dateien zeigen koennen.
    qmlRegisterType<mg::datev::DatevController>("MediaGallery", 1, 0, "DatevController");
    //  Gewoehnliche Tabellendateien (CSV/TSV) - ebenfalls je Kachel.
    qmlRegisterType<mg::table::TableController>("MediaGallery", 1, 0, "TableController");
}

}  // namespace mg
