cmake_minimum_required(VERSION 3.21)
project(MediaGallery VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_program(MG_CCACHE_PROGRAM NAMES ccache NO_CACHE)
if(MG_CCACHE_PROGRAM AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env
        CCACHE_SLOPPINESS=pch_defines,time_macros ${MG_CCACHE_PROGRAM})
    message(STATUS "ccache: ${MG_CCACHE_PROGRAM}")
else()
    message(STATUS "ccache: nicht benutzt")
endif()

#  MG_WARN_FLAGS gilt für ALLE Ziele (auch mg_pch, auch die Testtreiber):
#  abweichende Flags würden das per REUSE_FROM geteilte PCH-Abbild verwerfen.
option(MG_WARNINGS "Compiler-Warnungen (-Wall -Wextra) einschalten" ON)
if(MSVC)
    set(MG_WARN_FLAGS $<IF:$<BOOL:${MG_WARNINGS}>,/W4,/w>)
else()
    set(MG_WARN_FLAGS $<IF:$<BOOL:${MG_WARNINGS}>,-Wall$<SEMICOLON>-Wextra,-w>)
endif()

#  Bewusst GLOBAL (add_compile_options vor allen Zielen), nicht nur an den
#  Testzielen: Sanitizer ändern die Codeerzeugung, ein halb instrumentierter Baum
#  passt weder zum PCH-Abbild noch zu den Bibliotheken, gegen die die Treiber
#  linken. Gehört in einen EIGENEN Ordner, nie in den Auslieferungs-Build.
option(MG_SANITIZE "Address-/UB-Sanitizer in den ganzen Baum einbauen" OFF)
if(MG_SANITIZE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
        message(STATUS "Sanitizer aktiv (address,undefined) — NICHT für Auslieferung")
    else()
        message(WARNING "MG_SANITIZE ignoriert: nur GCC/Clang unterstützt")
    endif()
endif()

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

#  ZLIB ist OPTIONAL (wie Tesseract). Fehlt sie, weicht src/core/ZCodec.cpp auf
#  qCompress/qUncompress aus: PDF bleibt vollständig, DOCX entfällt, weil
#  ZIP-Einträge rohes Deflate sind (Begründung in ZCodec.h).
find_package(ZLIB)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(TESSERACT IMPORTED_TARGET tesseract lept)
    #  Rechtschreibprüfung — OPTIONAL, wie ZLIB und Tesseract: fehlt sie, bleibt
    #  die Prüfung aus und die Oberfläche sagt warum.
    pkg_check_modules(HUNSPELL IMPORTED_TARGET hunspell)
endif()

set(MG_PCH_STUB ${CMAKE_CURRENT_BINARY_DIR}/mg_pch_stub.cpp)
file(GENERATE OUTPUT ${MG_PCH_STUB} CONTENT "namespace mg { const int pchMarker = 0; }\n")
set_source_files_properties(${MG_PCH_STUB} PROPERTIES GENERATED TRUE)
add_library(mg_pch STATIC ${MG_PCH_STUB})
set_target_properties(mg_pch PROPERTIES AUTOMOC OFF AUTORCC OFF)
target_compile_options(mg_pch PRIVATE ${MG_WARN_FLAGS})
target_link_libraries(mg_pch PRIVATE Qt6::Core Qt6::Gui)
target_precompile_headers(mg_pch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Precompiled.h)

function(mg_domain name)
    cmake_parse_arguments(PARSE_ARGV 1 D "" "" "SOURCES;LIBS")
    add_library(${name} STATIC ${D_SOURCES})
    target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(${name} PRIVATE ${MG_WARN_FLAGS})
    target_link_libraries(${name} PUBLIC ${D_LIBS})
    target_precompile_headers(${name} REUSE_FROM mg_pch)
endfunction()

mg_domain(mg_core
    SOURCES
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
        src/core/FolderImages.cpp
        src/core/FolderImages.h
        src/core/SpellChecker.cpp
        src/core/SpellChecker.h
        src/core/TextPdfExporter.cpp
        src/core/TextPdfExporter.h
        src/core/PathUtils.h
        src/core/MemoryUtils.h
        src/core/ZCodec.cpp
        src/core/ZCodec.h
    LIBS
        Qt6::Core Qt6::Gui Qt6::Quick
)

#  Der Shim ist die EINZIGE Stelle im Baum, die zlib.h einbindet — alle
#  Domänen erben ihn über mg_core (PUBLIC), keine braucht ZLIB::ZLIB selbst.
if(ZLIB_FOUND)
    target_link_libraries(mg_core PUBLIC ZLIB::ZLIB)
    target_compile_definitions(mg_core PUBLIC MG_HAVE_ZLIB)
    message(STATUS "ZLIB gefunden (${ZLIB_VERSION_STRING}) — DOCX verfügbar")
else()
    message(STATUS "ZLIB fehlt — Qt-Fallback aktiv, DOCX deaktiviert (PDF vollständig)")
endif()

mg_domain(mg_tags
    SOURCES
        src/tags/TagCategory.h
        src/tags/TagManager.cpp
        src/tags/TagManager.h
        src/tags/TagController.cpp
        src/tags/TagController.h
    LIBS
        mg_core
)

mg_domain(mg_docx
    SOURCES
        src/docx/DocxZip.cpp
        src/docx/DocxZip.h
        src/docx/DocxDocument.cpp
        src/docx/DocxDocument.h
        src/docx/DocxController.cpp
        src/docx/DocxController.h
    LIBS
        mg_core
)

mg_domain(mg_docxedit
    SOURCES
        src/docx/edit/DocxEditController.cpp
        src/docx/edit/DocxEditController.h
        src/docx/edit/DocxEditCommands.cpp
        src/docx/edit/DocxEditCommands.h
        src/docx/edit/DocxTextArea.cpp
        src/docx/edit/DocxTextArea.h
    LIBS
        mg_docx Qt6::Pdf
)

mg_domain(mg_media
    SOURCES
        src/media/MediaItem.h
        src/media/MediaModel.cpp
        src/media/MediaModel.h
        src/media/MediaProxyModel.cpp
        src/media/MediaProxyModel.h
        src/media/FolderService.cpp
        src/media/FolderService.h
        src/media/ThumbnailLoader.cpp
        src/media/ThumbnailLoader.h
    LIBS
        mg_core mg_tags mg_docx Qt6::Multimedia Qt6::Pdf
)

mg_domain(mg_pdfextract
    SOURCES
        src/pdf/extract/PdfExtractController.cpp
        src/pdf/extract/PdfExtractController.h
        src/pdf/extract/PdfPageCopier.cpp
        src/pdf/extract/PdfPageCopier.h
    LIBS
        mg_core Qt6::Pdf
)

mg_domain(mg_pdfedit
    SOURCES
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
        src/pdf/edit/PdfFormFields.h
        src/pdf/edit/PdfAnnotations.cpp
        src/pdf/edit/PdfAnnotations.h
        src/pdf/edit/PdfImageEmbed.cpp
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
    LIBS
        mg_core mg_pdfextract Qt6::Pdf
)

mg_domain(mg_pdf
    SOURCES
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
    LIBS
        mg_core Qt6::Pdf Qt6::Multimedia
)

mg_domain(mg_imageedit
    SOURCES
        src/image/edit/ImageEditTypes.h
        src/image/edit/ImageEditModel.cpp
        src/image/edit/ImageEditModel.h
        src/image/edit/ImageEditCommands.cpp
        src/image/edit/ImageEditCommands.h
        src/image/edit/ImageEditController.cpp
        src/image/edit/ImageEditController.h
    LIBS
        mg_core
)

mg_domain(mg_app
    SOURCES
        src/app/AppController.cpp
        src/app/AppController.h
        src/app/IconProvider.cpp
        src/app/IconProvider.h
        src/app/ViewerController.cpp
        src/app/ViewerController.h
        src/app/WebEngineController.cpp
        src/app/WebEngineController.h
        src/app/TransliterationController.cpp
        src/app/TransliterationController.h
    LIBS
        mg_core mg_media mg_tags mg_pdf mg_pdfedit mg_docx
        Qt6::Qml Qt6::WebEngineQuick Qt6::Svg Qt6::Quick
)

if(HUNSPELL_FOUND)
    target_link_libraries(mg_core PUBLIC PkgConfig::HUNSPELL)
    target_compile_definitions(mg_core PUBLIC MG_HAVE_HUNSPELL)
    message(STATUS "Rechtschreibprüfung aktiviert (Hunspell ${HUNSPELL_VERSION})")
else()
    message(STATUS "Rechtschreibprüfung deaktiviert (Hunspell nicht gefunden)")
endif()

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

target_compile_options(MediaGallery PRIVATE ${MG_WARN_FLAGS})
target_precompile_headers(MediaGallery REUSE_FROM mg_pch)

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
