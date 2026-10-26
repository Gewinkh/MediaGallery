# MediaGallery

A high-performance, cross-platform media gallery app for **Windows**, **Linux**, and **macOS**.
Built with **C++20** and **Qt 6.8+**.

---

## Features

One sentence each - the full list, all keyboard shortcuts and where the app
stores its data are in **[FEATURES.md](FEATURES.md)**.

- **Media formats** - images (including RAW and HEIC), video, audio, PDF, text and source files, DOCX and HTML.
- **Audio player** - `Alt+A` turns the gallery into a player with shuffle, repeat and a 10-band equalizer.
- **Gallery** - grid view with adjustable tiles, fullscreen, random mode, and a **split view** for up to four files side by side with draggable panes.
- **Tags & categories** - your own categories and tags per file, stored in one compact file per folder next to the media, with filtering and search.
- **PDF viewer** - page thumbnails, search, text selection, and audio/video annotations played in place.
- **PDF page extraction** - pick pages from one or many PDFs and save them losslessly as a new file.
- **PDF editor** - notes, drawings, highlights, redaction, signature stamps, form filling, page reordering, text editing and **tracked changes** for your own annotations; export keeps the original content byte-for-byte wherever possible.
- **Image editor** - non-destructive crop, rotate, adjust and draw, with the same **tracked changes** as the PDF editor; the original file is never overwritten.
- **DOCX editor** - a loss-preserving Word editor: only what you touch is rewritten. Tables, pictures, contents list, tracked changes (shown and resolvable), spell checking, find & replace, and PDF export.
- **Text & source editor** - syntax colouring for 28 languages, folding, line numbers, indent guides, bracket matching, find & replace, an overview column and its own colour themes; plus a live HTML preview and a formatted Markdown view.
- **Tables** - `.csv` and `.tsv` open as an editable table with detected separator and header row, formulas (`=A1+B2`), filter and sorting; a DATEV booking batch (`EXTF`/`DTVF`) is recognised by its content, shows a file-header summary and debit/credit totals, and stays read-only.
- **Live transliteration** - type Latin, get Arabic, Hiragana or Katakana while you write.
- **Appearance** - every colour of the interface is adjustable, the editor has its own separate palette, and both can be exported and shared.
- **Desktop integration (Linux)** - an application menu entry with its own icon, and *Open with* for supported files and folders.

---

## Build Instructions

### Requirements
- Qt 6.8 or newer (developed with Qt 6.11 and 6.12) with modules:
  `Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, `Multimedia`, `Pdf`, `Svg`, `WebEngineQuick`
- **Optional**: ZLIB, to enable the **DOCX editor**. If absent, the app builds and runs normally, falls back to Qt's own `qCompress`/`qUncompress`, and only DOCX is disabled - every PDF feature (viewing, editing, page extraction, embedded media) stays fully available. DOCX files still appear in the gallery, but their tiles are greyed out and explain on hover why they cannot be opened
- **Optional**: Tesseract + Leptonica (via pkg-config) to enable **OCR for scanned PDFs**. If absent, the app builds and runs normally with OCR disabled
- **Optional**: Hunspell (via pkg-config) plus a dictionary, to enable **spell checking** in the DOCX editor. If either is missing, the feature stays off and the settings page says why
- CMake 3.21+
- C++20-capable compiler:
  - Windows: MSVC 2022
  - Linux: GCC 12+ / Clang 15+
  - macOS: Clang 15+

Without `-DCMAKE_BUILD_TYPE`, the project configures as **Release** (`-O3` plus
link-time optimisation); pass `-DCMAKE_BUILD_TYPE=Debug` for a debug build.
`cmake --preset ninja` uses the bundled `CMakePresets.json` and builds into its
own directory.

### Clone
```bash
git clone https://github.com/Gewinkh/MediaGallery.git
cd MediaGallery
```

### Windows (vcpkg)

#### Build
```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg_root>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release --parallel 2
```

#### Start

```bash
.\build\Release\MediaGallery.exe
```

---

### Linux

#### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

#### Start

```bash
./build/MediaGallery
```

#### Install

```bash
sudo cmake --install build
```

Installs the program, a desktop entry and the app icon under `/usr/local` (set
`-DCMAKE_INSTALL_PREFIX` when configuring to choose another place). MediaGallery
then appears in the application menu and under *Open with* for its file types and
for folders. Some desktops pick up the new entry only after
`sudo update-desktop-database /usr/local/share/applications`. If the icon does not
show up, log out and in again; refreshing the icon cache is optional and needs `-t`
there: `sudo gtk-update-icon-cache -f -t /usr/local/share/icons/hicolor`.

To update, build again and install again - the new files replace the old ones.
Use the same build folder you installed from, and restart the app afterwards:

```bash
git pull
cmake --build build --parallel 2
sudo cmake --install build
```

Settings and the files next to your media are not touched. To remove the
installed files again: `sudo xargs rm < build/install_manifest.txt`.

---

### macOS

#### Prerequisites

```bash
brew install cmake ninja qt
```

#### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
```

#### Start

```bash
open build/MediaGallery.app
```

---

### Own HTML renderer (preview)

HTML files open in the built-in web engine by default. To try the app's own
renderer instead (faster), start it with `MG_HTML_EIGEN=1`:

```bash
# Linux
MG_HTML_EIGEN=1 ./build/MediaGallery
```
```bash
# macOS
MG_HTML_EIGEN=1 build/MediaGallery.app/Contents/MacOS/MediaGallery
```

```powershell
# Windows (PowerShell)
$env:MG_HTML_EIGEN=1; .\build\Release\MediaGallery.exe
```

---

## Changelog

### Latest
- **Improvement**: **Experimental custom HTML renderer** now handles vertical alignment, mixed-language text, transparency, conic gradients, and box shadows more accurately.
- **Improvement**: **HTML layout and interaction** now respond correctly to window resizing, with working text selection and search in sticky headers.
- **Security**: **HTML preview** no longer executes JavaScript or allows pages to redirect away from local files.
- **Security**: **Key logging (`MG_KEYLOG`)** removed from public builds and retained only in the developer tree.

---

## Issues

Known limitations, open bugs and what is not built yet:
**[LIMITATIONS.md](LIMITATIONS.md)**.

---

## License

MediaGallery is licensed under the MIT License. See `LICENSE` for details.

The MIT License covers MediaGallery's own source code only. Qt and every other
third-party component remain under their own licenses; they are listed, with
their sources and the obligations a binary distribution would carry, in
**[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)**.
