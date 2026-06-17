cmake_minimum_required(VERSION 3.21)
project(MediaGallery VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# AUTOUIC entfernt: keine .ui-Dateien mehr (UI = 100% QML).

# Phase 5: Widgets / MultimediaWidgets / PdfWidgets vollstaendig entfernt.
# UI ist 100% Qt Quick/QML, Backend 100% C++ (QGuiApplication).
find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    Multimedia
    Pdf
    Svg
    LinguistTools
)

set(SOURCES
    main.cpp
    src/ISettings.h
    src/AppSettings.cpp
    src/AppSettings.h
    src/AppController.cpp
    src/AppController.h
    src/TagController.cpp
    src/TagController.h
    src/ViewerController.cpp
    src/ViewerController.h
    src/FolderService.cpp
    src/FolderService.h
    src/MediaItem.cpp
    src/MediaItem.h
    src/MediaModel.cpp
    src/MediaModel.h
    src/MediaProxyModel.cpp
    src/MediaProxyModel.h
    src/TagManager.cpp
    src/TagManager.h
    src/TagCategory.h
    src/JsonStorage.cpp
    src/JsonStorage.h
    src/ThumbnailLoader.cpp
    src/ThumbnailLoader.h
    src/PdfMediaHandler.cpp
    src/PdfMediaHandler.h
    src/Strings.cpp
    src/Strings.h
    src/Icons.h
    qml.qrc
)

qt_add_executable(MediaGallery ${SOURCES})

qt_add_translations(MediaGallery
    TS_FILES
    translations/mediagallery_de.ts
    translations/mediagallery_en.ts
)

target_link_libraries(MediaGallery PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
    Qt6::Pdf
    Qt6::Svg
)

if(WIN32)
    set_target_properties(MediaGallery PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(APPLE)
    set_target_properties(MediaGallery PROPERTIES MACOSX_BUNDLE TRUE)
endif()

install(TARGETS MediaGallery
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
