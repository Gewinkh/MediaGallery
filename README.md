# MediaGallery

A high-performance, cross-platform media gallery app for **Windows**, **Linux**, and **macOS**.
Built with **C++20** and **Qt 6.4+**.

---

## Features

One sentence each - the full list, all keyboard shortcuts and where the app
stores its data are in **[FEATURES.md](FEATURES.md)**.

- **Media formats** - images (including RAW and HEIC), video, audio, PDF, text and source files, DOCX and HTML.
- **Audio player** - `Alt+A` turns the gallery into a player with shuffle, repeat and a 10-band equalizer.
- **Gallery** - grid view with adjustable tiles, fullscreen, random mode, and a **split view** for up to four files side by side with draggable panes.
- **Tags & categories** - your own categories and tags per file, stored as JSON next to the media, with filtering and search.
- **PDF viewer** - page thumbnails, search, text selection, and audio/video annotations played in place.
- **PDF page extraction** - pick pages from one or many PDFs and save them losslessly as a new file.
- **PDF editor** - notes, drawings, highlights, redaction, signature stamps, form filling, page reordering, text editing and **tracked changes** for your own annotations; export keeps the original content byte-for-byte wherever possible.
- **Image editor** - non-destructive crop, rotate, adjust and draw, with the same **tracked changes** as the PDF editor; the original file is never overwritten.
- **DOCX editor** - a loss-preserving Word editor: only what you touch is rewritten. Tables, pictures, contents list, tracked changes (shown and resolvable), spell checking, find & replace, and PDF export.
- **Text & HTML** - a plain-text and source editor with a live HTML preview. Syntax colouring is not built yet (see [LIMITATIONS.md](LIMITATIONS.md)).
- **Live transliteration** - type Latin, get Arabic, Hiragana or Katakana while you write.
- **Appearance** - every colour of the interface is adjustable and themes can be exported and shared.

---

## Build Instructions

### Requirements
- Qt 6.4+ (developed against Qt 6.11) with modules:
  `Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, `Multimedia`, `Pdf`, `Svg`, `WebEngineQuick`
- **Optional**: ZLIB, to enable the **DOCX editor**. If absent, the app builds and runs normally, falls back to Qt's own `qCompress`/`qUncompress`, and only DOCX is disabled - every PDF feature (viewing, editing, page extraction, embedded media) stays fully available. DOCX files still appear in the gallery, but their tiles are greyed out and explain on hover why they cannot be opened
- **Optional**: Tesseract + Leptonica (via pkg-config) to enable **OCR for scanned PDFs**. If absent, the app builds and runs normally with OCR disabled
- **Optional**: Hunspell (via pkg-config) plus a dictionary, to enable **spell checking** in the DOCX editor. If either is missing, the feature stays off and the settings page says why
- CMake 3.21+
- C++20-capable compiler:
  - Windows: MSVC 2022
  - Linux: GCC 12+ / Clang 15+
  - macOS: Clang 15+

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

---

### macOS

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

### Tests (Developer Only)

A regression suite is built by default and can be executed with `ctest`:

```bash
ctest --test-dir build --output-on-failure   # Run all tests
ctest --test-dir build -R docx               # Run a specific 
```

Disable tests during configuration:

```bash
cmake -B build -DMG_BUILD_TESTS=OFF
```

The test suite uses plain executables (no external test framework or additional dependencies) and covers:

- DOCX/ZIP parsing
- Document model behavior
- Path handling
- Gallery sorting and filtering
- Tag/category sidecar persistence
- Tracked changes in the PDF and image editor
- Companion-file handling (hiding, removing, undo)

The `tests/` directory is not included in the published repository. If it is missing, the build automatically skips the test targets, allowing a fresh clone to configure and compile normally.

---

## Changelog

### Latest
- Added nested bookmark groups with independent expand/collapse at every level
- Improved bookmark management with drag-and-drop reordering across group levels
- Improved bookmark controls with drawn icons for editing and deleting bookmarks

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
