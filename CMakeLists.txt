cmake_minimum_required(VERSION 3.21)
project(MediaGallery VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ──────────────────────────────────────────────────────────────────────────────
#  BAUZEIT
#
#  Gemessen am 2026-08-28 (roh, ohne ccache, Release, i7-11370H = 4 Kerne / 8
#  Faeden). Vollbau von Grund auf, Sekunden:
#
#      --parallel        1       2       4       8
#      -O3 ohne LTO      150.3   84.8    52.4    49.4
#      -O3 MIT LTO        88.4   59.5    46.1    43.9   <- Vorgabe (MG_LTO)
#      -O3 mit MG_UNITY  126.5   81.7    62.9    64.6
#
#  DARAUS FOLGT DIE VORGABE: `MG_UNITY` ist AUS. Buendeln spart Header-Arbeit
#  (bei einem Faden 20 % schneller), nimmt aber Parallelitaet UND laesst `-O3`
#  ueber groessere Einheiten laufen - der Optimierer waechst darin ueberlinear.
#  Ab vier Faeden verliert es deshalb in JEDER Buendelgroesse (2/4/6/12 alle
#  gemessen, keine gewinnt). Wer wirklich mit `--parallel 1` baut, schaltet es
#  ein; sonst nicht.
#
#  Was ausserdem gemessen und VERWORFEN wurde, damit es niemand nochmal probiert:
#   · Ninja statt Make: 155.4 gegen 157.4 s bei einem Faden, 50.2 gegen 51.2 bei
#     acht. Im Rauschen - der Generator ist hier nicht der Engpass.
#   · Vorkompiliertes Kopfstueck erweitern: die haeufigsten Qt-Klassen stehen
#     laengst darin. Was fehlt, kommt hoechstens 8-mal in 65 Dateien vor, und
#     QtPdf/QtMultimedia duerfen nicht hinein (nicht jedes Ziel linkt sie).
#
#   · `-pipe`: 156.3 gegen 157.4 s. Im Rauschen, bleibt trotzdem drin - es
#     kostet nichts.
#
#  WO DIE ZEIT WIRKLICH LIEGT (aus Ninjas Protokoll, acht Faeden):
#      Kompilieren + Linken     261.8 s
#      AUTOMOC/RCC-Vorstufen     52.8 s   (17 %, generierter Qt-Code)
#      laengster Einzeljob        16.7 s   (`PdfEditController.cpp`)
#      Linken des Programms        0.4 s
#  Summe aller Jobs 314.6 s; bei acht Faeden waeren rechnerisch ~39 s die
#  Untergrenze, gemessen sind 51.2 s. Die Luecke ist Hyperthreading (vier echte
#  Kerne, acht Faeden) und die Reihenfolge der Ziele - nicht fehlende Buendelung
#  und nicht der Linker. Es ist also nichts Grosses mehr zu holen, ausser ueber
#  die Optimierstufe:
#
#      -O3 (Vorgabe)   157.4 s / 51.2 s     (ein Faden / acht Faeden)
#      -O2             147.8 s / 46.1 s
#      -O1             105.8 s / 33.8 s
#
#  Die Stufe ist BEWUSST NICHT gesenkt: sie tauscht Laufzeit gegen Bauzeit, und
#  Laufzeit steht in `CLAUDE.md` §0 auf Platz 2. Wer beim Entwickeln schneller
#  bauen will, setzt sie fuer SEINEN Ordner:
#      cmake -S. -B build-schnell -DCMAKE_BUILD_TYPE=Release \
#            -DCMAKE_CXX_FLAGS_RELEASE="-O1 -DNDEBUG"
#
#   MG_CCACHE  - zweiter Bau derselben Quelle kommt aus dem Zwischenspeicher.
#                AUS misst die ROHE Bauzeit (dafuer gedacht, nicht zum Sparen).
#   MG_UNITY   - mehrere Quellen je Uebersetzungseinheit (s. oben).
#   MG_UNITY_BATCH - Quellen je Buendel. Nur wirksam bei MG_UNITY=ON.
# ──────────────────────────────────────────────────────────────────────────────
option(MG_CCACHE "Compiler-Zwischenspeicher benutzen, wenn vorhanden" ON)
option(MG_LTO    "Ueber Dateigrenzen hinweg optimieren (schneller UND gleich schnell)" ON)
option(MG_UNITY  "Quellen je Domaene buendeln (lohnt NUR bei --parallel 1)" OFF)
set(MG_UNITY_BATCH 12 CACHE STRING "Quellen je Buendel (nur bei MG_UNITY=ON)")

#  ── LTO ───────────────────────────────────────────────────────────────────
#  Der Compiler legt je Datei nur eine Zwischendarstellung ab; optimiert wird
#  EINMAL beim Linken statt 65-mal. Deshalb schlaegt es bei wenigen Faeden am
#  staerksten durch. Gemessen (Vollbau, roh, Release, Sekunden):
#
#      --parallel      1       2       4       8
#      -O3           150.3   84.8    52.4    49.4
#      -O3 + LTO      88.4   59.5    46.1    43.9      <- gewinnt ueberall
#
#  Und es kostet KEINE Laufzeit: der Equalizer (die rechenintensivste Stelle,
#  je Sample und Band) braucht mit und ohne LTO dieselben 81 ms fuer 60 s Ton;
#  alles andere liegt innerhalb des Messrauschens von +/-10 %. Der Speicher der
#  laufenden App ist unveraendert (RSS auf die Kilobyte gleich), der Bau
#  braucht in der Spitze 2.5 GB (668 MB je Prozess) - unkritisch.
#
#  `check_ipo_supported` statt blindem Einschalten: auf einem Compiler ohne
#  Unterstuetzung soll das Projekt bauen, nicht scheitern (Portabilitaet,
#  §0-Prioritaet 3). Nur fuer optimierte Bauarten - in einem Debug-Bau kostet
#  LTO Zeit und erschwert das Debuggen.
if(MG_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT MG_IPO_OK OUTPUT MG_IPO_WHY)
    if(MG_IPO_OK)
        foreach(cfg RELEASE RELWITHDEBINFO MINSIZEREL)
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_${cfg} TRUE)
        endforeach()
        message(STATUS "LTO aktiv (Release-Bauarten)")
    else()
        message(STATUS "LTO nicht moeglich: ${MG_IPO_WHY}")
    endif()
endif()

find_program(MG_CCACHE_PROGRAM NAMES ccache NO_CACHE)
if(NOT MG_CCACHE)
    set(MG_CCACHE_PROGRAM "")
    message(STATUS "ccache: abgeschaltet (MG_CCACHE=OFF)")
endif()
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
    #  Zwischenstufen durch die Pipe statt ueber Dateien - spart I/O je
    #  Uebersetzungseinheit und kostet nichts.
    add_compile_options(-pipe)
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
        message(STATUS "Sanitizer aktiv (address,undefined) - NICHT für Auslieferung")
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
    #  Rechtschreibprüfung - OPTIONAL, wie ZLIB und Tesseract: fehlt sie, bleibt
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
    #  Buendeln: die Qt-Header einer Domaene werden einmal je Buendel gelesen
    #  statt einmal je Datei. Das PCH bleibt daneben wirksam - es deckt Core/Gui
    #  ab, die Buendel sparen zusaetzlich Quick/Pdf/Multimedia.
    if(MG_UNITY)
        set_target_properties(${name} PROPERTIES
            UNITY_BUILD ON
            UNITY_BUILD_BATCH_SIZE ${MG_UNITY_BATCH})
    endif()
endfunction()

#  ──────────────────────────────────────────────────────────────────────────
#  Diese Quellen bleiben EINZELN (kein Buendel). Es sind die handgeschriebenen
#  Format-Leser: sie tragen ihre Helfer im anonymen Namensraum, und mehrere von
#  ihnen benutzen dieselben kurzen Namen (`be16`, `be32`, `Box`, `fcc`,
#  `parenBytes`, `have`, `u`). In EINER Uebersetzungseinheit wuerden die
#  einander ueberschreiben.
#
#  Bewusst so und nicht durch Umbenennen im Code geloest: die Namen sind dort
#  richtig - sie sind lokal, kurz und stehen dicht bei ihrer Benutzung. Ein Bau
#  soll den Code nicht umschreiben.
set(MG_NO_UNITY
    src/audio/AudioTags.cpp
    src/audio/AudioSeekIndex.cpp
    src/audio/MkvAudioExtract.cpp
    src/audio/Mp4AudioExtract.cpp
    src/pdf/edit/PdfTextEditor.cpp
    src/pdf/edit/PdfTextReflow.cpp
    src/pdf/edit/PdfFontEmbed.cpp
    src/pdf/edit/PdfFormFields.cpp
    src/pdf/edit/PdfAnnotations.cpp
    src/pdf/edit/PdfVectorExport.cpp
    src/pdf/extract/PdfPageCopier.cpp
    src/audio/AudioEqualizer.cpp
    src/docx/DocxZip.cpp
)
if(MG_UNITY)
    set_source_files_properties(${MG_NO_UNITY}
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
endif()

mg_domain(mg_core
    SOURCES
        src/core/ISettings.h
        src/core/AppSettings.cpp
        src/core/AppSettings.h
        src/core/Strings.cpp
        src/core/Strings.h
        src/core/JsonStorage.cpp
        src/core/JsonStorage.h
        src/core/SearchPattern.cpp
        src/core/SearchPattern.h
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
        src/core/PdfGlyphRuns.cpp
        src/core/PdfGlyphRuns.h
    LIBS
        Qt6::Core Qt6::Gui Qt6::Quick
)

#  Der Shim ist die EINZIGE Stelle im Baum, die zlib.h einbindet - alle
#  Domänen erben ihn über mg_core (PUBLIC), keine braucht ZLIB::ZLIB selbst.
if(ZLIB_FOUND)
    target_link_libraries(mg_core PUBLIC ZLIB::ZLIB)
    target_compile_definitions(mg_core PUBLIC MG_HAVE_ZLIB)
    message(STATUS "ZLIB gefunden (${ZLIB_VERSION_STRING}) - DOCX verfügbar")
else()
    message(STATUS "ZLIB fehlt - Qt-Fallback aktiv, DOCX deaktiviert (PDF vollständig)")
endif()

mg_domain(mg_tags
    SOURCES
        src/tags/TagCategory.h
        src/tags/TagManager.cpp
        src/tags/TagManager.h
        src/tags/TagController.cpp
        src/tags/TagController.h
        src/tags/TagUndoMark.cpp
        src/tags/TagUndoMark.h
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

#  ── Texteditor: Syntaxfaerbung ───────────────────────────────────────────────
#  Zerleger und Sprachtabelle sind reines C++ und haengen nur an Qt6::Core
#  (QString/QStringView). Der Anschluss an Qts Dokumentmodell steht getrennt in
#  CodeHighlighter - so bleibt der Kern ohne GUI testbar.
mg_domain(mg_editor
    SOURCES
        src/editor/SyntaxTypes.h
        src/editor/LanguageTable.cpp
        src/editor/LanguageTable.h
        src/editor/SyntaxScanner.cpp
        src/editor/SyntaxScanner.h
        src/editor/FoldScanner.cpp
        src/editor/FoldScanner.h
        src/editor/TextFoldBar.cpp
        src/editor/TextFoldBar.h
        src/editor/TextDecorations.cpp
        src/editor/TextDecorations.h
        src/editor/TextDecorations.cpp
        src/editor/TextDecorations.h
        src/editor/SyntaxPalette.cpp
        src/editor/SyntaxPalette.h
        src/editor/EditorController.cpp
        src/editor/EditorController.h
        src/editor/CodeHighlighter.cpp
        src/editor/CodeHighlighter.h
        src/editor/TextGutter.cpp
        src/editor/TextGutter.h
        src/editor/TextMinimap.cpp
        src/editor/TextMinimap.h
    LIBS
        mg_core Qt6::Quick
)

mg_domain(mg_media
    SOURCES
        src/media/MediaItem.h
        src/media/ContentSniff.cpp
        src/media/ContentSniff.h
        src/media/MediaModel.cpp
        src/media/MediaModel.h
        src/media/MediaProxyModel.cpp
        src/media/MediaProxyModel.h
        src/media/GalleryRowModel.cpp
        src/media/GalleryRowModel.h
        src/media/FolderService.cpp
        src/media/FolderService.h
        src/media/ThumbnailLoader.cpp
        src/media/ThumbnailLoader.h
    LIBS
        #  `mg_audio` nur wegen `AudioTags`: die Kachel einer Audiodatei zeigt
        #  ihr eingebettetes Titelbild. Kein Kreis - mg_audio kennt mg_media nicht.
        #  `mg_editor` fuer die VORSCHAUKARTEN von Textdateien: sie zeigen die
        #  ersten Zeilen mit derselben Syntaxfaerbung wie der Editor
        #  (`LanguageTable`, `SyntaxScanner`, `SyntaxPalette`). Kein Kreis -
        #  mg_editor kennt nur mg_core. Der Preis ist Qt6::Quick auf der
        #  Linkzeile jedes mg_media-Nutzers: die Koepfe von `CodeHighlighter`
        #  und `TextGutter` binden Quick ein, und CMake kann das nicht je
        #  Kopfdatei trennen. Reine Linkzeit, keine Laufzeit.
        mg_core mg_editor mg_tags mg_docx mg_audio Qt6::Multimedia Qt6::Pdf
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
        src/pdf/edit/PdfOcrLayer.cpp
        src/pdf/edit/PdfOcrLayer.h
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
        #  `mg_pdf` wegen `mg::ocr`: „Dokument durchsuchbar machen" erkennt die
        #  Seiten und schreibt die Textebene - Erkennung wohnt in mg_pdf, das
        #  Schreiben hier. Kein Zyklus: mg_pdf kennt mg_pdfedit nicht.
        mg_core mg_pdf mg_pdfextract Qt6::Pdf
)

#  Zwei Quellen dürfen NICHT in eine Sammel-Übersetzungseinheit (Unity-Build):
#  sie führen gleichnamige Symbole im anonymen Namensraum (`parenBytes`,
#  `kTables`) wie ihre Nachbarn. Die Eigenschaft kostet nichts, solange
#  CMAKE_UNITY_BUILD aus ist, und hält den Unity-Bau übersetzbar, wenn er
#  eingeschaltet wird (Messung s. Structure.md §1 ▸ Build).
set_source_files_properties(
    src/pdf/edit/PdfTextReflow.cpp
    src/pdf/edit/PdfFontEmbed.cpp
    PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)

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

#  Audio-Wiedergabe mit Equalizer (README ▸ Planned). Eigene Kette, weil
#  QMediaPlayer seine Samples nicht herausgibt - s. src/audio/AudioEqualizer.h.
mg_domain(mg_audio
    SOURCES
        src/audio/Xoshiro.h
        src/audio/PlayQueue.cpp
        src/audio/PlayQueue.h
        src/audio/AudioEqualizer.cpp
        src/audio/AudioEqualizer.h
        src/audio/AudioRing.cpp
        src/audio/AudioRing.h
        src/audio/AudioEngine.cpp
        src/audio/AudioEngine.h
        src/audio/AudioController.cpp
        src/audio/AudioController.h
        src/audio/Mp4AudioExtract.cpp
        src/audio/Mp4AudioExtract.h
        src/audio/AudioTags.cpp
        src/audio/AudioTags.h
        src/audio/MkvAudioExtract.cpp
        src/audio/MkvAudioExtract.h
        src/audio/AudioSeekIndex.cpp
        src/audio/AudioSeekIndex.h
        src/audio/AudioCoverProvider.cpp
        src/audio/AudioCoverProvider.h
    LIBS
        mg_core Qt6::Multimedia Qt6::Quick
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
        src/app/PaneController.cpp
        src/app/PaneController.h
        src/app/PaneHost.cpp
        src/app/PaneHost.h
        src/app/PaneListModel.cpp
        src/app/PaneListModel.h
        src/app/QmlTypes.cpp
        src/app/QmlTypes.h
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
    mg_audio
    mg_core
    mg_editor
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
