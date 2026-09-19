cmake_minimum_required(VERSION 3.21)

#  Ohne Angabe Release, sonst gaebe es weder -O noch LTO
if(NOT DEFINED CMAKE_BUILD_TYPE AND NOT DEFINED ENV{CMAKE_BUILD_TYPE})
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Bauart: Debug, Release, RelWithDebInfo, MinSizeRel")
endif()

project(MediaGallery VERSION 1.0.0 LANGUAGES CXX)

#  Bauzeitmessung: Release-Vollbau, ohne ccache, frischer Build-Ordner je Lauf,
#  Ninja, i7-11370H (4C/8T). Minimum aus zwei Laeufen, Sekunden.
#
#      --parallel                      1       2       4       8
#      15 Domaenen-Bibliotheken      110.6    72.2    54.0    49.8
#      eine Bibliothek (heute)       104.3    67.5    50.1    45.8
#
#  Ursache: Jede Domaenen-Bibliothek erzeugte einen eigenen AUTOMOC-Vorlauf
#  und verzoegerte dadurch mg_app bis alle Abhaengigkeiten archiviert waren.
#  Auch die CPU-Zeit sank (321 -> 301 s bei -j8): weniger Arbeit, nicht nur
#  bessere Parallelisierung.
#
#  Verworfen:
#    ohne LTO       150.3 / 84.8 / 52.4 / 49.4 s - LTO gewinnt immer.
#    Unity-Build    126.5 / 81.7 / 62.9 / 64.6 s - nur bei -j1 schneller.
#    -O2 / -O1      -j1 147.8 / 105.8 s - schnellerer Bau, aber langsamere Laufzeit.
#    PGO            Equalizer 81 ms mit/ohne PGO - doppelter Bau ohne Gewinn.
#    PCH erweitern  QtPdf + QtQuick: 67 -> 70 s - groesseres PCH schadet.
#    Ninja vs. Make -j1: 155.4 vs. 157.4 s - kein relevanter Unterschied.
#    mold / lld     Linken nur ~6 % der Bauzeit.
#    Sonstiges      -pipe, ggc-Parameter, AUTOGEN_PARALLEL, OPTIMIZE_DEPENDENCIES,
#                   moc --no-notes: jeweils <2 %, im Rauschen.
#
#  Fuer schnellere Entwicklungs-Builds eigenen Ordner mit
#  -DCMAKE_CXX_FLAGS_RELEASE="-O1 -DNDEBUG" verwenden; Release-Vorgabe bleibt -O3.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(MG_LTO         "Ueber Dateigrenzen hinweg optimieren (nur optimierte Bauarten)" ON)
option(MG_CCACHE      "ccache benutzen, wenn vorhanden (AUS misst die rohe Bauzeit)" ON)
option(MG_WARNINGS    "Compiler-Warnungen -Wall -Wextra" ON)
option(MG_SANITIZE    "Address-/UB-Sanitizer im ganzen Baum - nur in einem eigenen Ordner" OFF)
option(MG_BUILD_TESTS "Regressionssuite tests/ einbinden, sofern vorhanden" ON)
#  Handgeschriebene Assembly. Bewusst AUS: der Baum muss sich ohne sie bauen
#  lassen, sonst waere die Plattformunabhaengigkeit weg und nicht nur gefaehrdet.
#  Sie greift NUR dort, wo eine Fassung fuer genau diese Aufrufkonvention
#  vorliegt (heute: System V auf x86-64); ueberall sonst bleibt es beim C++-Weg,
#  ohne dass am Aufruf etwas anders aussieht.
option(MG_ASM "Handgeschriebene Assembly mitbauen, wo es eine Fassung gibt" OFF)

#  Optimiert wird einmal beim Linken statt je Datei: schneller gebaut (s. oben)
#  und ohne Laufzeitverlust - Equalizer 81 ms mit und ohne, RSS der App gleich,
#  Bau-Spitze 2.5 GB. Debug bleibt ohne, dort stoert es nur.
if(MG_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT MG_IPO_OK OUTPUT MG_IPO_WHY)
    if(MG_IPO_OK)
        foreach(cfg RELEASE RELWITHDEBINFO MINSIZEREL)
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_${cfg} ON)
        endforeach()
    else()
        message(STATUS "LTO nicht moeglich: ${MG_IPO_WHY}")
    endif()
endif()

#  NO_CACHE, damit ein spaeter installiertes ccache ohne --fresh greift. Ohne
#  die Nachsicht pch_defines,time_macros verweigert ccache bei vorkompilierten
#  Koepfen jeden Treffer.
if(MG_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(MG_CCACHE_PROGRAM NAMES ccache NO_CACHE)
    if(MG_CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env
            CCACHE_SLOPPINESS=pch_defines,time_macros ${MG_CCACHE_PROGRAM})
        message(STATUS "ccache: ${MG_CCACHE_PROGRAM}")
    endif()
endif()

#  Global statt je Ziel: das PCH-Abbild wird geteilt (REUSE_FROM), und
#  abweichende Flags verwerfen es. Sanitizer aendern die Codeerzeugung - ein
#  halb instrumentierter Baum passt nicht zu seinen eigenen Bibliotheken.
if(NOT MG_WARNINGS)
    add_compile_options($<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
elseif(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()
if(MG_SANITIZE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
        message(STATUS "Sanitizer aktiv (address,undefined) - nicht fuer Auslieferung")
    else()
        message(WARNING "MG_SANITIZE ignoriert: nur GCC/Clang")
    endif()
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Multimedia Pdf Svg WebEngineQuick)

#  Optional - fehlt eine, entfaellt nur ihr Feature: ZLIB -> DOCX (ZIP-Eintraege
#  sind rohes Deflate, s. src/core/ZCodec.h), Tesseract -> OCR, Hunspell ->
#  Rechtschreibpruefung.
find_package(ZLIB)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(TESSERACT IMPORTED_TARGET tesseract lept)
    pkg_check_modules(HUNSPELL IMPORTED_TARGET hunspell)
endif()

file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/mg_pch.cpp CONTENT "")
add_library(mg_pch OBJECT ${CMAKE_BINARY_DIR}/mg_pch.cpp)
set_target_properties(mg_pch PROPERTIES AUTOMOC OFF AUTORCC OFF)
target_link_libraries(mg_pch PRIVATE Qt6::Core Qt6::Gui)
target_precompile_headers(mg_pch PRIVATE pch/Precompiled.h)

#  Die Weiche steht VOR dem Ziel, damit die Quellenliste sie schon kennt.
#  `CMAKE_SYSTEM_PROCESSOR` entscheidet ueber den Befehlssatz, das System ueber
#  die Aufrufkonvention - beides muss passen, sonst wird gar nichts gebaut.
set(MG_ASM_SOURCES "")
set(MG_ASM_WHY "aus (MG_ASM=OFF)")
if(MG_ASM)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$"
       AND (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
       AND NOT MSVC)
        enable_language(ASM)
        set(MG_ASM_SOURCES src/simd/asm/sysv_x86_64/findchar_avx2.S)
        set(MG_ASM_WHY "an (System V, x86-64)")
    else()
        set(MG_ASM_WHY "aus (keine Fassung fuer ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR})")
    endif()
endif()
message(STATUS "Assembly: ${MG_ASM_WHY}")
#  `MG_ASM_ENABLED` schaltet die Deklaration in `FindChar.h` frei. Ohne das
#  Merkzeichen gibt es das Symbol gar nicht, und die Weiche kennt den Weg nicht.

add_library(mg_lib STATIC
    src/app/AppController.cpp
    src/app/AppController.h
    src/app/PaneController.cpp
    src/app/PaneController.h
    src/app/PaneHost.cpp
    src/app/PaneHost.h
    src/app/PaneListModel.cpp
    src/app/PaneListModel.h
    src/app/QmlTypes.cpp
    src/app/QmlTypes.h
    src/app/TransliterationController.cpp
    src/app/TransliterationController.h
    src/app/ViewerController.cpp
    src/app/ViewerController.h
    src/app/WebEngineController.cpp
    src/app/WebEngineController.h

    src/audio/AudioController.cpp
    src/audio/AudioController.h
    src/audio/AudioCoverProvider.cpp
    src/audio/AudioCoverProvider.h
    src/audio/AudioDenoise.cpp
    src/audio/AudioDenoise.h
    src/audio/AudioEngine.cpp
    src/audio/AudioEngine.h
    src/audio/AudioEqualizer.cpp
    src/audio/AudioLimiter.cpp
    src/audio/AudioEqualizer.h
    src/audio/AudioRing.cpp
    src/audio/AudioRing.h
    src/audio/AudioSeekIndex.cpp
    src/audio/AudioSeekIndex.h
    src/audio/AudioTags.cpp
    src/audio/AudioTags.h
    src/audio/MkvAudioExtract.cpp
    src/audio/MkvAudioExtract.h
    src/audio/Mp4AudioExtract.cpp
    src/audio/Mp4AudioExtract.h
    src/audio/PlayQueue.cpp
    src/audio/PlayQueue.h
    src/audio/Xoshiro.h

    src/core/AppSettings.cpp
    src/core/AppSettings.h
    src/core/EditSidecar.cpp
    src/core/EditSidecar.h
    src/core/FileBrowseModel.cpp
    src/core/FileBrowseModel.h
    src/core/FolderImages.cpp
    src/core/FolderImages.h
    src/core/ISettings.h
    src/core/JsonStorage.cpp
    src/core/JsonStorage.h
    src/core/MGStorage.cpp
    src/core/MGStorage.h
    src/core/MediaLogs.cpp
    src/core/MediaLogs.h
    src/core/MemoryUtils.h
    src/core/PathUtils.h
    src/core/PdfGlyphRuns.cpp
    src/core/PdfGlyphRuns.h
    src/core/RhiProber.cpp
    src/core/RhiProber.h
    src/core/SearchPattern.cpp
    src/core/SearchPattern.h
    src/core/SpellChecker.cpp
    src/core/SpellChecker.h
    src/core/Strings.cpp
    src/core/Strings.h
    src/core/TextEncoding.cpp
    src/core/TextEncoding.h
    src/core/ZCodec.cpp
    src/core/ZCodec.h

    src/datev/DatevController.cpp
    src/datev/DatevController.h
    src/datev/DatevCsv.cpp
    src/datev/DatevCsv.h
    src/datev/DatevFormat.cpp
    src/datev/DatevFormat.h

    src/docx/DocxController.cpp
    src/docx/DocxController.h
    src/docx/DocxDocument.cpp
    src/docx/DocxDocument.h
    src/docx/DocxZip.cpp
    src/docx/DocxZip.h

    src/docx/edit/DocxEditCommands.cpp
    src/docx/edit/DocxEditCommands.h
    src/docx/edit/DocxEditController.cpp
    src/docx/edit/DocxEditController.h
    src/docx/edit/DocxTextArea.cpp
    src/docx/edit/DocxTextArea.h

    src/editor/CodeHighlighter.cpp
    src/editor/CodeHighlighter.h
    src/editor/EditorController.cpp
    src/editor/EditorController.h
    src/editor/FoldScanner.cpp
    src/editor/FoldScanner.h
    src/editor/LanguageTable.cpp
    src/editor/LanguageTable.h
    src/editor/SyntaxPalette.cpp
    src/editor/SyntaxPalette.h
    src/editor/SyntaxScanner.cpp
    src/editor/SyntaxScanner.h
    src/editor/SyntaxTypes.h
    src/editor/TextDecorations.cpp
    src/editor/TextDecorations.h
    src/editor/TextFoldBar.cpp
    src/editor/TextFoldBar.h
    src/editor/TextGutter.cpp
    src/editor/TextGutter.h
    src/editor/TextMinimap.cpp
    src/editor/TextMinimap.h
    src/editor/TextPdfExporter.cpp
    src/editor/TextPdfExporter.h

    src/image/edit/ImageEditCommands.cpp
    src/image/edit/ImageEditCommands.h
    src/image/edit/ImageEditController.cpp
    src/image/edit/ImageEditController.h
    src/image/edit/ImageEditModel.cpp
    src/image/edit/ImageEditModel.h
    src/image/edit/ImageEditTypes.h

    src/media/ContentSniff.cpp
    src/media/ContentSniff.h
    src/media/FolderService.cpp
    src/media/FolderService.h
    src/media/GalleryRowModel.cpp
    src/media/GalleryRowModel.h
    src/media/MediaItem.h
    src/media/MediaModel.cpp
    src/media/MediaModel.h
    src/media/MediaProxyModel.cpp
    src/media/MediaProxyModel.h
    src/media/ThumbnailLoader.cpp
    src/media/ThumbnailLoader.h

    src/pdf/OcrEngine.cpp
    src/pdf/OcrEngine.h
    src/pdf/PdfAudioController.cpp
    src/pdf/PdfAudioController.h
    src/pdf/PdfMediaHandler.cpp
    src/pdf/PdfMediaHandler.h
    src/pdf/PdfTextController.cpp
    src/pdf/PdfTextController.h
    src/pdf/PdfThumbnailProvider.cpp
    src/pdf/PdfThumbnailProvider.h

    src/pdf/edit/PdfAnnotations.cpp
    src/pdf/edit/PdfAnnotations.h
    src/pdf/edit/PdfBaseFontWidths.cpp
    src/pdf/edit/PdfBaseFontWidths.h
    src/pdf/edit/PdfContentEditor.cpp
    src/pdf/edit/PdfContentEditor.h
    src/pdf/edit/PdfEditCommands.cpp
    src/pdf/edit/PdfEditCommands.h
    src/pdf/edit/PdfEditController.cpp
    src/pdf/edit/PdfEditController.h
    src/pdf/edit/PdfEditModel.cpp
    src/pdf/edit/PdfEditModel.h
    src/pdf/edit/PdfEditTypes.h
    src/pdf/edit/PdfEncodings.cpp
    src/pdf/edit/PdfEncodings.h
    src/pdf/edit/PdfFontEmbed.cpp
    src/pdf/edit/PdfFontEmbed.h
    src/pdf/edit/PdfFormFields.cpp
    src/pdf/edit/PdfFormFields.h
    src/pdf/edit/PdfImageEmbed.cpp
    src/pdf/edit/PdfImageEmbed.h
    src/pdf/edit/PdfObjects.cpp
    src/pdf/edit/PdfObjects.h
    src/pdf/edit/PdfOcrLayer.cpp
    src/pdf/edit/PdfOcrLayer.h
    src/pdf/edit/PdfTextEditor.cpp
    src/pdf/edit/PdfTextEditor.h
    src/pdf/edit/PdfTextLayout.cpp
    src/pdf/edit/PdfTextLayout.h
    src/pdf/edit/PdfTextReflow.cpp
    src/pdf/edit/PdfTextReflow.h
    src/pdf/edit/PdfVectorExport.cpp
    src/pdf/edit/PdfVectorExport.h

    src/pdf/extract/PdfExtractController.cpp
    src/pdf/extract/PdfExtractController.h
    src/pdf/extract/PdfPageCopier.cpp
    src/pdf/extract/PdfPageCopier.h

    src/simd/Dispatch.cpp
    src/simd/Dispatch.h
    src/simd/FindChar.cpp
    src/simd/FindChar.h
    src/simd/FindChar_arm.cpp
    src/simd/FindChar_plain.cpp
    src/simd/FindChar_x86.cpp
    ${MG_ASM_SOURCES}

    src/table/DelimitedText.cpp
    src/table/DelimitedText.h
    src/table/TableController.cpp
    src/table/TableController.h
    src/table/TableFilter.cpp
    src/table/TableFilter.h
    src/table/TableFormula.cpp
    src/table/TableFormula.h
    src/table/TableSearch.cpp
    src/table/TableSearch.h
    src/table/TableSort.cpp
    src/table/TableSort.h
    src/table/TableWidths.cpp
    src/table/TableWidths.h
    src/table/TableWriter.cpp
    src/table/TableWriter.h

    src/tags/TagCategory.h
    src/tags/TagController.cpp
    src/tags/TagController.h
    src/tags/TagListModel.cpp
    src/tags/TagListModel.h
    src/tags/TagManager.cpp
    src/tags/TagManager.h
    src/tags/TagUndoMark.cpp
    src/tags/TagUndoMark.h
    src/tags/TagUndoStack.cpp
    src/tags/TagUndoStack.h
)
target_include_directories(mg_lib PUBLIC src)
if(MG_ASM_SOURCES)
    target_compile_definitions(mg_lib PUBLIC MG_ASM_ENABLED=1)
endif()
target_link_libraries(mg_lib PUBLIC
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Multimedia Qt6::Pdf Qt6::WebEngineQuick)
target_precompile_headers(mg_lib REUSE_FROM mg_pch)

if(ZLIB_FOUND)
    target_link_libraries(mg_lib PUBLIC ZLIB::ZLIB)
    target_compile_definitions(mg_lib PUBLIC MG_HAVE_ZLIB)
    message(STATUS "ZLIB ${ZLIB_VERSION_STRING} - DOCX verfuegbar")
else()
    message(STATUS "ZLIB fehlt - DOCX deaktiviert, PDF vollstaendig")
endif()
if(HUNSPELL_FOUND)
    target_link_libraries(mg_lib PUBLIC PkgConfig::HUNSPELL)
    target_compile_definitions(mg_lib PUBLIC MG_HAVE_HUNSPELL)
    message(STATUS "Hunspell ${HUNSPELL_VERSION} - Rechtschreibpruefung verfuegbar")
else()
    message(STATUS "Hunspell fehlt - Rechtschreibpruefung deaktiviert")
endif()
if(TESSERACT_FOUND)
    target_link_libraries(mg_lib PUBLIC PkgConfig::TESSERACT)
    target_compile_definitions(mg_lib PUBLIC MG_HAVE_TESSERACT)
    message(STATUS "Tesseract ${TESSERACT_tesseract_VERSION} - OCR verfuegbar")
else()
    message(STATUS "Tesseract fehlt - OCR deaktiviert")
endif()

add_library(mg_qml OBJECT qml.qrc)
set_target_properties(mg_qml PROPERTIES AUTOMOC OFF)
target_link_libraries(mg_qml PRIVATE Qt6::Core)

qt_add_executable(MediaGallery main.cpp)
target_link_libraries(MediaGallery PRIVATE mg_lib mg_qml Qt6::QuickControls2 Qt6::Svg)
target_precompile_headers(MediaGallery REUSE_FROM mg_pch)
set_target_properties(MediaGallery PROPERTIES
    AUTOMOC OFF WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)

#  tests/ steht in der .gitignore - ein frischer Klon hat es nicht.
if(MG_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    enable_testing()
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS MediaGallery
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
