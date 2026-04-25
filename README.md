# MediaGallery

Eine performante, plattformübergreifende Mediengalerie-App für **Windows**, **Linux** und **macOS**.  
Gebaut mit **C++20** und **Qt 6.4+**.

---

## Funktionen

### Medienformate
- **Bilder**: JPG, PNG, GIF, BMP, WebP, TIFF, HEIC, HEIF, AVIF, SVG, ICO, RAW (CR2, NEF, ARW, DNG)
- **Videos**: MP4, MKV, AVI, MOV, WMV, WebM, M4V, MPEG, 3GP, OGV, TS, M2TS, VOB, RMVB, ASF, DIVX
- **Audio**: MP3, FLAC, WAV, OGG, AAC, M4A, WMA, Opus, AIFF, APE, ALAC, MIDI u. v. m.

### Galerie & Ansicht
- **Rasteransicht**: 1–25 Spalten, per `Shift+Scroll` zoomen
- **Vollbild-Galerie**: Vor/Zurück, Zufalls-Modus, Zoom bis 10×, Pan per Maus
- **Kompaktmodus**: Optionsleiste ein-/ausblendbar (`S`)
- **Cover-Modus**: Galerie abdecken/aufdecken (`B`)
- **Live-Ordner-Überwachung**: Neue oder gelöschte Dateien werden automatisch erkannt

### Tags & Kategorien
- **Tags**: Pro-Ordner, beliebig viele, frei benennbar, farblich kodierbar
- **Kategorien**: Hierarchische Tag-Kategorien mit optionaler Farbvererbung
- **Filter-Modi**: ODER, UND, NUR, INKLUSIV – kombinierbar mit Medientyp-Filter
- **Sortierung**: Datum, Name, Dateigröße (auf-/absteigend)

### Metadaten & Verwaltung
- **Datum-Editor**: Benutzerdefiniertes Datum pro Datei, wird in JSON persistiert  
  (Ändert auch den Dateisystem-Timestamp via `utime`)
- **Medium löschen**: Roter Löschen-Button (🗑) neben dem Datum-Button in der Vollbildansicht —  
  fragt per Bestätigungsdialog nach (OK-Button oder `Enter`), löscht dann die Datei von der  
  Festplatte **und** bereinigt alle zugehörigen Metadaten (Tags, Datum, Kategoriezugehörigkeiten) sauber aus der JSON
- **Umbenennen**: Ändert auch den Dateinamen auf der Festplatte
- **Drag & Drop**: Ordner oder Mediendateien auf das Fenster ziehen
- **JSON-Speicherung**: `<Ordnername>.json` direkt im Zielordner (datei-zentriertes Format v2)

### Wiedergabe & UI
- **Video-Wiedergabe**: Nativ (Qt Multimedia) oder externer Player
- **Audio-Thumbnails**: Stilisierte Vorschau mit Wellenform-Dekoration und Formatanzeige
- **Sprache**: Deutsch (Standard) / Englisch (umschaltbar zur Laufzeit)
- **Themes**: Anpassbare Hintergrund- und Akzentfarbe

---

## Tastenkürzel

| Aktion | Kürzel |
|--------|--------|
| Ordner öffnen | `Ctrl+O` |
| Neu laden / Thumbnails aktualisieren | `F5` / `R` |
| Optionen ein/aus | `S` |
| Cover-Modus ein/aus | `B` |
| Vollbild-Ansicht öffnen | Doppelklick |
| Nächstes Medium | `→` |
| Vorheriges Medium | `←` |
| Zurück zur Galerie | `Esc` |
| Datum bearbeiten | `D` (Vollbild) |
| Datum-Editor öffnen | Kalender-Button 📅 (Vollbild) |
| Medium löschen | Löschen-Button 🗑 (Vollbild) |
| Grid vergrößern | `Shift + Scroll ↑` |
| Grid verkleinern | `Shift + Scroll ↓` |
| Bild zoomen | `Shift + Scroll` (Vollbild) |

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

Für Videowiedergabe und Video-Thumbnails werden GStreamer-Plugins benötigt:

```bash
sudo apt install gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav
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

In der **„x64 Native Tools Command Prompt for VS 2022"**:

```cmd
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/msvc2022_64"
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
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/mingw_64"
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

## JSON-Format

Metadaten werden als `<Ordnername>.json` direkt im geöffneten Ordner gespeichert.  
Das aktuelle Format (v2) ist datei-zentrisch und minimalistisch:

```json
{
  "v": 2,
  "files": {
    "foto.jpg": { "t": ["Urlaub", "Kind"], "d": "2024-06-15T14:30:00" },
    "clip.mp4": { "t": ["Familie"] }
  },
  "tagColors": {
    "Urlaub": "#dc5050",
    "Kind":   "#50c878"
  },
  "categories": [
    {
      "id": "...",
      "name": "Boys",
      "tags": [],
      "files": ["foto.jpg"],
      "children": []
    }
  ]
}
```

Ältere Dateien im tag-zentrischen Format (v1) werden beim Öffnen automatisch migriert.

---

## Projektstruktur

```
MediaGallery/
├── CMakeLists.txt
├── main.cpp
├── README.md
└── src/
    ├── MainWindow.{h,cpp}          # Hauptfenster, Ordner-Handling, Löschen-Logik
    ├── GalleryView.{h,cpp}         # Rasteransicht mit virtuellem Scroll & Live-Watcher
    ├── FullscreenView.{h,cpp}      # Vollbild-Galerie (Zoom, Pan, Datum, Löschen)
    ├── MediaItem.{h,cpp}           # Datenstruktur für ein Medium (Pfad, Tags, Datum, Typ)
    ├── MediaThumbnail.{h,cpp}      # Einzelne Kachel (Thumbnail, Tag-Overlay, Tooltip)
    ├── ThumbnailLoader.{h,cpp}     # Async Thumbnail-Laden mit Disk- und Memory-Cache
    ├── VideoPlayer.{h,cpp}         # Eingebetteter Qt-Multimedia-Videoplayer
    ├── TagWidget.{h,cpp}           # Tag-Bar / Pill-Widget (anzeigen & bearbeiten)
    ├── TagManager.{h,cpp}          # Tag- und Kategorie-Logik (CRUD, Farben)
    ├── TagCategory.h               # Rekursive Kategorie-Datenstruktur
    ├── TagCategoryPanel.{h,cpp}    # Seitenleiste zur Kategorie-Verwaltung
    ├── FilterBar.{h,cpp}           # Filter- und Sortierleiste
    ├── JsonStorage.{h,cpp}         # JSON-Persistenz (datei-zentrisch, v2)
    ├── MetadataEditor.{h,cpp}      # Datum-Dialog (Bearbeiten / Zurücksetzen)
    ├── FolderService.{h,cpp}       # Ordner öffnen, speichern, letzten Ordner merken
    ├── SettingsDialog.{h,cpp}      # Einstellungen (Tags, Kategorien, Theme, Sprache)
    ├── ColorPickerButton.{h,cpp}   # Kompakter Farbwähler-Button
    ├── AppSettings.{h,cpp}         # Persistente globale Einstellungen (QSettings)
    ├── ISettings.h                 # Interface für Einstellungen (testbar/mockbar)
    ├── Strings.{h,cpp}             # Alle UI-Strings (DE/EN, zur Laufzeit umschaltbar)
    └── Style.{h,cpp}               # Zentrales Stylesheet / Theme-Farben
```

---

## Bekannte Einschränkungen

- **RAW-Formate** (CR2, NEF, ARW) benötigen ggf. systemseitige Codec-Unterstützung
- **HEIC/HEIF** auf Windows erfordert den HEVC-Codec (Microsoft Store)
- **Video-Thumbnails** werden über `QMediaPlayer` generiert – Geschwindigkeit und Unterstützung hängen vom installierten Multimedia-Backend ab
- Auf **Linux** müssen GStreamer-Plugins für Videowiedergabe installiert sein (siehe oben)
- Der **Löschen-Vorgang ist unwiderruflich** – die Datei wird direkt von der Festplatte entfernt (kein Papierkorb)
