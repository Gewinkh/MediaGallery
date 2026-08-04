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

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(TESSERACT IMPORTED_TARGET tesseract lept)
endif()

set(MG_PCH_STUB ${CMAKE_CURRENT_BINARY_DIR}/mg_pch_stub.cpp)
file(GENERATE OUTPUT ${MG_PCH_STUB} CONTENT "namespace mg { const int pchMarker = 0; }\n")
set_source_files_properties(${MG_PCH_STUB} PROPERTIES GENERATED TRUE)
add_library(mg_pch STATIC ${MG_PCH_STUB})
set_target_properties(mg_pch PROPERTIES AUTOMOC OFF AUTORCC OFF)
target_compile_options(mg_pch PRIVATE -w)
target_link_libraries(mg_pch PRIVATE Qt6::Core Qt6::Gui)
target_precompile_headers(mg_pch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Precompiled.h)

function(mg_domain name)
    cmake_parse_arguments(PARSE_ARGV 1 D "" "" "SOURCES;LIBS")
    add_library(${name} STATIC ${D_SOURCES})
    target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(${name} PRIVATE -w)
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
        src/core/PathUtils.h
        src/core/MemoryUtils.h
    LIBS
        Qt6::Core Qt6::Gui Qt6::Quick
)

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
        src/docx/DocxPdfExporter.cpp
        src/docx/DocxPdfExporter.h
    LIBS
        mg_core ZLIB::ZLIB
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
        mg_core Qt6::Pdf ZLIB::ZLIB
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
        mg_core mg_pdfextract Qt6::Pdf ZLIB::ZLIB
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
        mg_core Qt6::Pdf Qt6::Multimedia ZLIB::ZLIB
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
        src/app/ViewerController.cpp
        src/app/ViewerController.h
        src/app/WebEngineController.cpp
        src/app/WebEngineController.h
        src/app/TransliterationController.cpp
        src/app/TransliterationController.h
    LIBS
        mg_core mg_media mg_tags mg_pdf mg_pdfedit mg_docx
        Qt6::Qml Qt6::WebEngineQuick
)

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
