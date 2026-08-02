cmake_minimum_required(VERSION 3.21)
project(MediaGallery VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    Multimedia
    Pdf
    Svg
    WebEngineQuick
)

find_package(ZLIB REQUIRED)

#  OCR (optional): Tesseract + Leptonica via pkg-config. Fehlt es, wird OCR
#  einfach ausgelassen (available()==false) — der Build bleibt portabel.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(TESSERACT IMPORTED_TARGET tesseract lept)
endif()

#  Sources are grouped by domain folder under src/ (see Structure.md §2).
#  src/ itself is an include directory, so every local include is written
#  relative to it (e.g. #include "pdf/edit/PdfEditModel.h").

#  src/core — cross-cutting infrastructure: settings, strings, persistence,
#  RHI backend choice, header-only helpers.
set(SOURCES_CORE
    src/core/ISettings.h
    src/core/AppSettings.cpp
    src/core/AppSettings.h
    src/core/Strings.cpp
    src/core/Strings.h
    src/core/JsonStorage.cpp
    src/core/JsonStorage.h
    src/core/RhiProber.cpp
    src/core/RhiProber.h
    src/core/FileBrowseModel.cpp
    src/core/FileBrowseModel.h
    src/core/PathUtils.h
    src/core/MemoryUtils.h
)

#  src/app — application-level bridges (App, Viewer, WebEngine, Translit).
set(SOURCES_APP
    src/app/AppController.cpp
    src/app/AppController.h
    src/app/ViewerController.cpp
    src/app/ViewerController.h
    src/app/WebEngineController.cpp
    src/app/WebEngineController.h
    src/app/TransliterationController.cpp
    src/app/TransliterationController.h
)

#  src/media — media item data, models, folder scan, gallery thumbnails.
set(SOURCES_MEDIA
    src/media/MediaItem.h
    src/media/MediaModel.cpp
    src/media/MediaModel.h
    src/media/MediaProxyModel.cpp
    src/media/MediaProxyModel.h
    src/media/FolderService.cpp
    src/media/FolderService.h
    src/media/ThumbnailLoader.cpp
    src/media/ThumbnailLoader.h
)

#  src/tags — tags + category tree (truth source and QML facade).
set(SOURCES_TAGS
    src/tags/TagCategory.h
    src/tags/TagManager.cpp
    src/tags/TagManager.h
    src/tags/TagController.cpp
    src/tags/TagController.h
)

#  src/pdf — PDF subsystem: viewing, text/OCR, embedded media, page audio.
set(SOURCES_PDF
    src/pdf/PdfThumbnailProvider.cpp
    src/pdf/PdfThumbnailProvider.h
    src/pdf/PdfTextController.cpp
    src/pdf/PdfTextController.h
    src/pdf/OcrEngine.cpp
    src/pdf/OcrEngine.h
    src/pdf/PdfMediaHandler.cpp
    src/pdf/PdfMediaHandler.h
    src/pdf/PdfAudioController.cpp
    src/pdf/PdfAudioController.h
)

#  src/pdf/edit — PDF annotation editor (model, undo commands, controller)
#  plus lossless content-stream editing.
set(SOURCES_PDF_EDIT
    src/pdf/edit/PdfEditTypes.h
    src/pdf/edit/PdfEditModel.cpp
    src/pdf/edit/PdfEditModel.h
    src/pdf/edit/PdfEditCommands.cpp
    src/pdf/edit/PdfEditCommands.h
    src/pdf/edit/PdfEditController.cpp
    src/pdf/edit/PdfEditController.h
    src/pdf/edit/PdfContentEditor.cpp
    src/pdf/edit/PdfContentEditor.h
    src/pdf/edit/PdfEncodings.cpp
    src/pdf/edit/PdfEncodings.h
    src/pdf/edit/PdfObjects.cpp
    src/pdf/edit/PdfObjects.h
    src/pdf/edit/PdfVectorExport.cpp
    src/pdf/edit/PdfVectorExport.h
    src/pdf/edit/PdfFormFields.cpp
    src/pdf/edit/PdfAnnotations.cpp
    src/pdf/edit/PdfImageEmbed.cpp
    src/pdf/edit/PdfFormFields.h
    src/pdf/edit/PdfAnnotations.h
    src/pdf/edit/PdfImageEmbed.h
    src/pdf/edit/PdfBaseFontWidths.cpp
    src/pdf/edit/PdfBaseFontWidths.h
    src/pdf/edit/PdfTextLayout.cpp
    src/pdf/edit/PdfTextLayout.h
    src/pdf/edit/PdfTextEditor.cpp
    src/pdf/edit/PdfTextEditor.h
    src/pdf/edit/PdfTextReflow.cpp
    src/pdf/edit/PdfTextReflow.h
    src/pdf/edit/PdfFontEmbed.cpp
    src/pdf/edit/PdfFontEmbed.h
)

#  src/pdf/extract — page extraction and lossless page copying.
set(SOURCES_PDF_EXTRACT
    src/pdf/extract/PdfExtractController.cpp
    src/pdf/extract/PdfExtractController.h
    src/pdf/extract/PdfPageCopier.cpp
    src/pdf/extract/PdfPageCopier.h
)

#  src/docx — DOCX container/document model and PDF export.
set(SOURCES_DOCX
    src/docx/DocxZip.cpp
    src/docx/DocxZip.h
    src/docx/DocxDocument.cpp
    src/docx/DocxDocument.h
    src/docx/DocxController.cpp
    src/docx/DocxController.h
    src/docx/DocxPdfExporter.cpp
    src/docx/DocxPdfExporter.h
)

#  src/docx/edit — DOCX editor (controller, undo commands, painted text item).
set(SOURCES_DOCX_EDIT
    src/docx/edit/DocxEditController.cpp
    src/docx/edit/DocxEditController.h
    src/docx/edit/DocxEditCommands.cpp
    src/docx/edit/DocxEditCommands.h
    src/docx/edit/DocxTextArea.cpp
    src/docx/edit/DocxTextArea.h
)

#  src/image/edit — image annotation editor.
set(SOURCES_IMAGE_EDIT
    src/image/edit/ImageEditTypes.h
    src/image/edit/ImageEditModel.cpp
    src/image/edit/ImageEditModel.h
    src/image/edit/ImageEditCommands.cpp
    src/image/edit/ImageEditCommands.h
    src/image/edit/ImageEditController.cpp
    src/image/edit/ImageEditController.h
)

# ─────────────────────────────────────────────────────────────────────────────
#  Domänen-BIBLIOTHEKEN — jede Quelle wird GENAU EINMAL übersetzt
#
#  Vorher trug jedes Testziel seine Quellen selbst bei: 31 Dateien wurden im
#  Testbaum 201 mal übersetzt (DocxDocument.cpp allein elfmal à ~22 s). Gemessen
#  waren das 2116 s von 2575 s CPU eines vollen Builds. Jetzt ist jeder
#  Domänenordner eine statische Bibliothek; App und Treiber VERLINKEN sie nur.
#  Der Linker zieht dabei aus einer statischen Bibliothek ausschließlich die
#  Objekte, die wirklich gebraucht werden — ein Treiber bekommt also weiterhin
#  nur das, was er prüft, nur eben ohne es neu zu übersetzen.
#
#  Neue Datei → in die `set(SOURCES_…)`-Liste ihres Ordners (s. oben); an den
#  Bibliotheken selbst ist dann nichts zu tun.
# ─────────────────────────────────────────────────────────────────────────────
function(mg_add_lib name)
    add_library(${name} STATIC ${ARGN})
    #  Alle projektinternen Includes stehen relativ zu src/ (s. Structure.md).
    target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(${name} PRIVATE -w)
endfunction()

mg_add_lib(mg_core       ${SOURCES_CORE})
mg_add_lib(mg_tags       ${SOURCES_TAGS})
mg_add_lib(mg_docx       ${SOURCES_DOCX})
mg_add_lib(mg_docxedit   ${SOURCES_DOCX_EDIT})
mg_add_lib(mg_media      ${SOURCES_MEDIA})
mg_add_lib(mg_pdfextract ${SOURCES_PDF_EXTRACT})
mg_add_lib(mg_pdfedit    ${SOURCES_PDF_EDIT})
mg_add_lib(mg_pdf        ${SOURCES_PDF})
mg_add_lib(mg_imageedit  ${SOURCES_IMAGE_EDIT})
mg_add_lib(mg_app        ${SOURCES_APP})

#  Abhängigkeiten PUBLIC: wer eine Bibliothek verlinkt, bekommt automatisch die
#  Qt-Module, die ihre Objekte brauchen — ein Treiber muss sie nicht kennen.
#  RhiProber (mg_core) fasst QQuickWindow an, deshalb Quick schon hier.
target_link_libraries(mg_core       PUBLIC Qt6::Core Qt6::Gui Qt6::Quick)
target_link_libraries(mg_tags       PUBLIC mg_core)
target_link_libraries(mg_docx       PUBLIC mg_core ZLIB::ZLIB)
target_link_libraries(mg_docxedit   PUBLIC mg_docx Qt6::Pdf)
target_link_libraries(mg_media      PUBLIC mg_core mg_tags mg_docx
                                           Qt6::Multimedia Qt6::Pdf)
target_link_libraries(mg_pdfextract PUBLIC mg_core Qt6::Pdf ZLIB::ZLIB)
target_link_libraries(mg_pdfedit    PUBLIC mg_core mg_pdfextract Qt6::Pdf ZLIB::ZLIB)
target_link_libraries(mg_pdf        PUBLIC mg_core Qt6::Pdf Qt6::Multimedia ZLIB::ZLIB)
target_link_libraries(mg_imageedit  PUBLIC mg_core)
target_link_libraries(mg_app        PUBLIC mg_core mg_media mg_tags mg_pdf
                                           mg_pdfedit mg_docx Qt6::Qml
                                           Qt6::WebEngineQuick)

#  OCR optional dazulinken (Degradationskette: ohne Tesseract kein OCR).
#  Gehört an mg_pdf — dort liegt OcrEngine.cpp.
if(TESSERACT_FOUND)
    target_link_libraries(mg_pdf PUBLIC PkgConfig::TESSERACT)
    target_compile_definitions(mg_pdf PUBLIC MG_HAVE_TESSERACT)
    message(STATUS "OCR aktiviert (Tesseract ${TESSERACT_VERSION})")
else()
    message(STATUS "OCR deaktiviert (Tesseract nicht gefunden)")
endif()

qt_add_executable(MediaGallery main.cpp qml.qrc)

target_link_libraries(MediaGallery PRIVATE
    mg_app
    mg_core
    mg_media
    mg_tags
    mg_pdf
    mg_pdfedit
    mg_pdfextract
    mg_docx
    mg_docxedit
    mg_imageedit
    Qt6::QuickControls2
    Qt6::Svg
)

target_compile_options(MediaGallery PRIVATE -w)

if(WIN32)
    set_target_properties(MediaGallery PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(APPLE)
    set_target_properties(MediaGallery PROPERTIES MACOSX_BUNDLE TRUE)
endif()

option(MG_BUILD_TESTS "Regressionssuite (tests/) mitbauen, sofern vorhanden" ON)
if(MG_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
    message(STATUS "Regressionssuite aktiviert (ctest)")
elseif(MG_BUILD_TESTS)
    message(STATUS "Regressionssuite übersprungen (tests/ nicht vorhanden)")
endif()

install(TARGETS MediaGallery
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
