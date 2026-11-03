#include "app/QmlTypes.h"

#include "app/PaneController.h"
#include "app/PaneHost.h"
#include "audio/PlayQueue.h"
#include "core/FileBrowseModel.h"
#include "datev/DatevController.h"
#include "docx/edit/DocxEditController.h"
#include "table/TableController.h"
#include "docx/edit/DocxTextArea.h"
#include "editor/CodeHighlighter.h"
#include "editor/MarkdownView.h"
#include "editor/TextDecorations.h"
#include "editor/TextFoldBar.h"
#include "editor/TextGutter.h"
#include "editor/TextMinimap.h"
#include "editor/TextProblems.h"
#include "html/HtmlView.h"
#include "image/edit/ImageEditController.h"
#include "media/GalleryRowModel.h"
#include "tags/TagListModel.h"
#include "pdf/PdfAudioController.h"
#include "pdf/PdfTextController.h"
#include "pdf/edit/PdfEditController.h"

#include <QQmlEngine>

namespace mg {

void registerQmlTypes() {
    // Editor-Controller je PDF-Kachel: eigener Zustand je geöffneter Datei.
    qmlRegisterType<PdfTextController> ("MediaGallery", 1, 0, "PdfTextController");
    qmlRegisterType<PdfAudioController>("MediaGallery", 1, 0, "PdfAudioController");
    qmlRegisterType<PdfEditController> ("MediaGallery", 1, 0, "PdfEditController");
    qmlRegisterType<ImageEditController>("MediaGallery", 1, 0, "ImageEditController");
    qmlRegisterType<DocxEditController>("MediaGallery", 1, 0, "DocxEditController");
    qmlRegisterType<DocxTextArea>      ("MediaGallery", 1, 0, "DocxTextArea");
    //  Seiten-Miniatur des DOCX-Editors; malt über paintPageInto, hält also kein Bild.
    qmlRegisterType<DocxPageThumb>     ("MediaGallery", 1, 0, "DocxPageThumb");
    //  Ein Modell je Datei-/Ordnerwähler, damit zwei offene nicht im selben Verzeichnis stehen.
    qmlRegisterType<FileBrowseModel>   ("MediaGallery", 1, 0, "FileBrowseModel");
    //  Zeilenmodell der Galerie: je Ansicht eine Instanz.
    qmlRegisterType<GalleryRowModel>   ("MediaGallery", 1, 0, "GalleryRowModel");
    //  Ein Repeater ueber ein JS-Feld baute bei jeder Aenderung ALLE Chips neu.
    qmlRegisterType<TagListModel>      ("MediaGallery", 1, 0, "TagListModel");
    //  Zwei-Fenster-Modus: `PaneHost` erzeugt je Hälfte den QML-Teilbaum mit eigenem Kontext.
    qmlRegisterType<PaneController>    ("MediaGallery", 1, 0, "PaneController");
    qmlRegisterType<PaneHost>          ("MediaGallery", 1, 0, "PaneHost");
    //  Syntaxfaerbung: EINE Instanz je Text-Kachel.
    qmlRegisterType<mg::editor::CodeHighlighter>("MediaGallery", 1, 0, "CodeHighlighter");
    qmlRegisterType<mg::editor::TextGutter>("MediaGallery", 1, 0, "TextGutter");
    qmlRegisterType<mg::editor::TextMinimap>("MediaGallery", 1, 0, "TextMinimap");
    qmlRegisterType<mg::editor::TextFoldBar>("MediaGallery", 1, 0, "TextFoldBar");
    qmlRegisterType<mg::editor::TextDecorations>("MediaGallery", 1, 0, "TextDecorations");
    //  Unterstreichung und Minimap-Punkt lesen dieselbe Liste.
    qmlRegisterType<mg::editor::TextProblems>("MediaGallery", 1, 0, "TextProblems");
    qmlRegisterType<mg::editor::MarkdownView>("MediaGallery", 1, 0, "MarkdownView");
    qmlRegisterType<mg::html::HtmlView>("MediaGallery", 1, 0, "HtmlView");
    //  Je Kachel eine Instanz: zwei Haelften zeigen verschiedene Dateien.
    qmlRegisterType<mg::datev::DatevController>("MediaGallery", 1, 0, "DatevController");
    qmlRegisterType<mg::table::TableController>("MediaGallery", 1, 0, "TableController");
    qmlRegisterType<PlayQueue>("MediaGallery", 1, 0, "PlayQueue");
}

}  // namespace mg
