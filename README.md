# MediaGallery

Eine performante, plattformübergreifende Mediengalerie-App für **Windows**, **Linux** und **macOS**.  
Gebaut mit **C++20** und **Qt 6.4+**.

---

## Funktionen

- **Bildformate**: JPG, PNG, GIF, BMP, WebP, TIFF, HEIC, AVIF, RAW (CR2, NEF, ARW, DNG), SVG, …
- **Videoformate**: MP4, MKV, AVI, MOV, WMV, WebM, M4V, MPEG, 3GP, …
- **Rasteransicht**: 1–25 Spalten, per `Shift+Scroll` zoomen
- **Vollbild-Galerie**: Vor/Zurück, Zufalls-Modus, Zoom bis 10×
- **Tags**: Pro-Ordner, beliebig viele, Farben, kombinierbar (UND/ODER)
- **Sortierung**: Datum, Name, Tags, Dateigröße (auf-/absteigend)
- **Filter**: Nur Bilder, nur Videos, nach Tags
- **Video-Wiedergabe**: Nativ (Qt Multimedia) oder externer Player
- **Datum-Editor**: Benutzerdefiniertes Datum pro Datei (wird in JSON gespeichert)
- **JSON-Speicherung**: `<Ordnername>.json` direkt im Zielordner
- **Umbenennen**: Ändert auch den Dateinamen auf der Festplatte
- **Sprache**: Deutsch (Standard) / Englisch
- **Tastenkürzel**: `S` = Optionen ein/aus, `F5` = Neu laden, `Esc` = Zurück
- **Drag & Drop**: Ordner auf das Fenster ziehen

---

## Voraussetzungen

| Paket | Version |
|-------|---------|
| Qt    | ≥ 6.4   |
| CMake | ≥ 3.21  |
| C++   | 20      |

---

## Installation der Abhängigkeiten

### Windows

**Option A – Qt Online Installer (empfohlen)**

1. Qt unter https://www.qt.io/download-qt-installer herunterladen und installieren.
2. Im Installer **Qt 6.x** mit der Komponente **MSVC 2022 64-bit** oder **MinGW 64-bit** auswählen.
3. CMake separat von https://cmake.org/download/ herunterladen (oder via `winget install Kitware.CMake`).
4. Für MSVC zusätzlich **Visual Studio 2022** (Community reicht) oder die **Build Tools for Visual Studio** installieren: https://visualstudio.microsoft.com/downloads/

---

### Linux (Ubuntu / Debian)

```bash
sudo apt update
sudo apt install \
    qt6-base-dev \
    qt6-multimedia-dev \
    qt6-tools-dev \
    qt6-l10n-tools \
    cmake \
    build-essential \
    ninja-build
```

Für ältere Ubuntu-Versionen (< 22.04) ohne Qt 6 in den Standardrepos:

```bash
sudo add-apt-repository ppa:ubuntuhandbook1/ppa
sudo apt update
sudo apt install qt6-base-dev qt6-multimedia-dev qt6-tools-dev cmake build-essential
```

**Fedora / RHEL**

```bash
sudo dnf install qt6-qtbase-devel qt6-qtmultimedia-devel qt6-linguist cmake gcc-c++ ninja-build
```

**Arch Linux**

```bash
sudo pacman -S qt6-base qt6-multimedia qt6-tools cmake ninja base-devel
```

---

### macOS

```bash
# Homebrew installieren (falls noch nicht vorhanden): https://brew.sh
brew install qt@6 cmake ninja

# Qt zum PATH hinzufügen (dauerhaft in ~/.zshrc eintragen):
echo 'export PATH="/opt/homebrew/opt/qt@6/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

---

## Projekt herunterladen

```bash
git clone https://github.com/<nutzername>/MediaGallery.git
cd MediaGallery
```

Oder als ZIP herunterladen und entpacken.

---

## Build

### Universell (alle Plattformen)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

---

### Windows – MSVC (Visual Studio 2022)

In der **"x64 Native Tools Command Prompt for VS 2022"**:

```cmd
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
cmake --build . --config Release --parallel
```

> Den Pfad `C:/Qt/6.x.x/msvc2022_64` durch den tatsächlichen Qt-Installationspfad ersetzen.

Die fertige `.exe` liegt unter `build/Release/MediaGallery.exe`.

**Qt-DLLs deployen** (damit die Anwendung ohne Qt-Installation läuft):

```cmd
cd build/Release
windeployqt MediaGallery.exe --multimedia
```

---

### Windows – MinGW

In der **Qt MinGW-Shell** (wird mit Qt mitgeliefert):

```cmd
mkdir build && cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/mingw_64"
cmake --build . --parallel
```

**Qt-DLLs deployen:**

```cmd
windeployqt MediaGallery.exe --multimedia
```

---

### Linux

```bash
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

Oder ohne Ninja:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

**Starten:**

```bash
./MediaGallery
```

---

### macOS

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
cmake --build . --parallel
```

**Starten:**

```bash
open MediaGallery.app
```

**App für Weitergabe deployen** (bündelt Qt-Frameworks):

```bash
macdeployqt MediaGallery.app -dmg
```

---

## Bedienung

| Aktion | Tastenkürzel |
|--------|-------------|
| Optionen ein/aus | `S` |
| Grid vergrößern | `Shift + Scroll ↑` |
| Grid verkleinern | `Shift + Scroll ↓` |
| Bild zoomen | `Shift + Scroll` (Vollbild) |
| Nächstes Bild | `→` oder Schaltfläche |
| Vorheriges Bild | `←` oder Schaltfläche |
| Zurück zur Galerie | `Esc` |
| Ordner öffnen | `Ctrl+O` |
| Neu laden | `F5` |

---

## Projektstruktur

```
MediaGallery/
├── CMakeLists.txt
├── main.cpp
├── Main.qml
├── src/
│   ├── main.cpp
│   ├── MainWindow.{h,cpp}          # Hauptfenster
│   ├── GalleryView.{h,cpp}         # Rasteransicht
│   ├── FullscreenView.{h,cpp}      # Vollbild-Galerie
│   ├── MediaItem.{h,cpp}           # Datenstruktur
│   ├── MediaThumbnail.{h,cpp}      # Einzelne Kachel
│   ├── ThumbnailLoader.{h,cpp}     # Async Thumbnail-Laden
│   ├── VideoPlayer.{h,cpp}         # Eingebetteter Videoplayer
│   ├── TagWidget.{h,cpp}           # Tag-Bar / Pill-Widget
│   ├── TagManager.{h,cpp}          # Tag-Logik
│   ├── TagCategory.h               # Tag-Kategorien
│   ├── TagCategoryPanel.{h,cpp}    # Tag-Kategorie-Panel
│   ├── FilterBar.{h,cpp}           # Filter-/Sortier-Leiste
│   ├── JsonStorage.{h,cpp}         # JSON-Persistenz
│   ├── MetadataEditor.{h,cpp}      # Datum-Dialog
│   ├── SettingsDialog.{h,cpp}      # Einstellungen
│   ├── ColorPickerButton.{h,cpp}   # Farbwähler-Knopf
│   ├── AppSettings.{h,cpp}         # Globale Einstellungen
│   ├── Strings.{h,cpp}             # Übersetzbare Strings
│   └── Style.{h,cpp}               # Zentrales Stylesheet
└── translations/
    ├── mediagallery_de.ts           # Deutsch
    └── mediagallery_en.ts           # Englisch
```

---

## Bekannte Einschränkungen

- **RAW-Formate** (CR2, NEF, ARW) benötigen ggf. systemseitige Codec-Unterstützung
- **HEIC** auf Windows erfordert den HEVC-Codec (Microsoft Store)
- **Video-Thumbnails** werden über `QMediaPlayer` generiert – kann je nach System variieren
- Auf **Linux** müssen GStreamer-Plugins für Videowiedergabe installiert sein:
  ```bash
  sudo apt install gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav
  ```
