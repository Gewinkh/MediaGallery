cmake_minimum_required(VERSION 3.21)
project(MediaGallery VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Multimedia
    MultimediaWidgets
    LinguistTools
)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/ISettings.h
    src/AppSettings.cpp
    src/AppSettings.h
    src/FolderService.cpp
    src/FolderService.h
    src/GalleryView.cpp
    src/GalleryView.h
    src/MediaItem.cpp
    src/MediaItem.h
    src/MediaThumbnail.cpp
    src/MediaThumbnail.h
    src/FullscreenView.cpp
    src/FullscreenView.h
    src/TagManager.cpp
    src/TagManager.h
    src/TagCategory.h
    src/TagCategoryPanel.cpp
    src/TagCategoryPanel.h
    src/TagWidget.cpp
    src/TagWidget.h
    src/FilterBar.cpp
    src/FilterBar.h
    src/JsonStorage.cpp
    src/JsonStorage.h
    src/MetadataEditor.cpp
    src/MetadataEditor.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    src/ColorPickerButton.cpp
    src/ColorPickerButton.h
    src/VideoPlayer.cpp
    src/VideoPlayer.h
    src/ThumbnailLoader.cpp
    src/ThumbnailLoader.h
    src/Style.cpp
    src/Style.h
    src/Strings.cpp
    src/Strings.h
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
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
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
