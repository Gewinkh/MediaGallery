# MediaGallery

A high-performance, cross-platform media gallery app for **Windows**, **Linux**, and **macOS**.
Built with **C++20** and **Qt 6.4+**.

---

## Features

One sentence each - the full list, all keyboard shortcuts and where the app
stores its data are in **[FEATURES.md](FEATURES.md)**.

- **Media formats** - images (including RAW and HEIC), video, audio, PDF, text and source files, DOCX and HTML.
- **Gallery** - grid view with adjustable tiles, fullscreen, random mode, and a **split view** for up to four files side by side with draggable panes.
- **Tags & categories** - your own categories and tags per file, stored as JSON next to the media, with filtering and search.
- **PDF viewer** - page thumbnails, search, text selection, and audio/video annotations played in place.
- **PDF page extraction** - pick pages from one or many PDFs and save them losslessly as a new file.
- **PDF editor** - notes, drawings, highlights, redaction, signature stamps, form filling, page reordering and text editing; export keeps the original content byte-for-byte wherever possible.
- **Image editor** - non-destructive crop, rotate, adjust and draw; the original file is never overwritten.
- **DOCX editor** - a loss-preserving Word editor: only what you touch is rewritten. Tables, pictures, headers and footers, contents list, footnotes, tracked changes, spell checking, find & replace, and PDF export.
- **Text & HTML** - editor with syntax awareness and a live HTML preview.
- **Live transliteration** - type Latin, get Arabic, Hiragana or Katakana while you write.
- **Appearance** - every colour of the interface is adjustable and themes can be exported and shared.

---

## Build Instructions

### Requirements
- Qt 6.4+ (developed against Qt 6.11) with modules:
  `Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, `Multimedia`, `Pdf`, `Svg`, `WebEngineQuick`
- **Optional**: ZLIB, to enable the **DOCX editor**. If absent, the app builds and runs normally, falls back to Qt's own `qCompress`/`qUncompress`, and only DOCX is disabled — every PDF feature (viewing, editing, page extraction, embedded media) stays fully available. DOCX files still appear in the gallery, but their tiles are greyed out and explain on hover why they cannot be opened
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

* DOCX/ZIP parsing
* Document model behavior
* Path handling
* Gallery sorting and filtering
* Tag/category sidecar persistence

The `tests/` directory is not included in the published repository. If it is missing, the build automatically skips the test targets, allowing a fresh clone to configure and compile normally.

---

## Changelog

### Latest
- **Improved DOCX editing** with footnotes, tracked changes, spell checking, and better text wrapping around images and tables
- **Improved DOCX/PDF compatibility** with footnote export, long-table pagination, and better image positioning
- **Improved PDF editing/export** with preserved annotations, cleaner page output, and folder-based stamp selection
- **Improved UI feedback** with temporary status messages and better image selection popups
- **Fixed** various DOCX layout, rendering, and PDF export issues


---

## Issues

### Known limitations
- **Exported PDF is not page-for-page identical** to the editor: text wraps on one side of a floating image instead of both, and every page reserves the same room for footnotes. Both come from the same cause and are fixed together.
- **Tracked changes are only shown, not recorded**: you can accept or reject existing changes, but your own edits are never marked.
- **Spell checking needs an installed Hunspell dictionary**; without one it stays off and says so.

### Known bugs
- **Selection frame of a table split across pages sits wrong** (the table itself edits normally).
- **Dragging a table vertically does nothing** - only the width is applied; row height always follows the content.
- **Blacking out text in a PDF is hard to trigger**: it needs a drag with the tool; an existing text selection is ignored.

### Planned
- Create footnotes from the editor (reading, showing and editing them already works).
- Write next to a table, the way text already flows next to a picture.
- Proportional-only scaling for signature stamps.

---

## License

MIT License. See `LICENSE` for details.
