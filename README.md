# MediaGallery

A high-performance, cross-platform media gallery app for **Windows**, **Linux**, and **macOS**.  
Built with **C++20** and **Qt 6.4+**.

---

## Features

### Media Formats
- **Images**: JPG, PNG, GIF, BMP, WebP, TIFF, HEIC, HEIF, AVIF, SVG, ICO, RAW (CR2, NEF, ARW, DNG)
- **Videos**: MP4, MKV, AVI, MOV, WMV, WebM, M4V, MPEG, 3GP, OGV, TS, M2TS, VOB, RMVB, ASF, DIVX
- **Audio**: MP3, FLAC, WAV, OGG, AAC, M4A, WMA, Opus, AIFF, APE, ALAC, MIDI and more
- **PDFs**: Full page rendering with thumbnail sidebar and media annotation support

### Gallery & View
- **Grid view**: 1–25 columns, zoom with `Shift+Scroll`
- **Fullscreen gallery**: Prev/Next, Random mode, up to 10× zoom, pan with mouse
- **Compact mode**: Option bar toggle with `S`
- **Cover mode**: Cover/uncover gallery with `B`
- **Live folder watch**: New or deleted files are detected automatically

### Tags & Categories
- **Tags**: Per-folder, unlimited, freely named, color-coded
- **Categories**: Hierarchical tag categories with optional color inheritance
- **Filter modes**: OR, AND, ONLY, INCLUSIVE — combinable with media-type filter
- **Sorting**: Date, Name, File size (ascending/descending)

### PDF Viewer
- Full multi-page rendering via Qt6 PDF
- Thumbnail sidebar for quick page navigation
- Zoom in/out, fit-page, fit-width, single-page / multi-page scroll modes
- Embedded audio and video annotation playback (Sound, Screen, Movie subtypes)
- Sidecar audio file fallback (auto-detected by filename)
- **First-load fix**: PDFs now open and render immediately on the first click

### Full Color Customization (Settings → Design)
- **9 built-in themes**: Dark, Dark OLED, Ocean Depth, Inferno Blaze, Neon Purple, Midnight Rose, Elegant, Simple, Custom
- **Custom Theme Editor** with live preview:
  - Main background (solid or gradient)
  - Card / panel background
  - Primary and muted text colors
  - Border color
  - Accent color (solid, gradient, or glow)
  - Glow radius and intensity
  - Thumbnail card background (solid, gradient, transparent)
  - Tile hover glow effect
  - **PDF Viewer** sidebar, toolbar, and scrollbar colors
  - Sidebar background color
- Export / Import custom themes as JSON files
- All color changes apply live without restarting

### Metadata & File Management
- **Date editor**: Custom date per file, persisted in JSON
- **Delete file**: Red delete button in fullscreen view with confirmation dialog
- **Rename**: Also renames the file on disk
- **Drag & Drop**: Drop a folder or individual media files onto the window
- **JSON storage**: `<FolderName>.json` stored directly in the target folder (file-centric format v2)

### Playback & UI
- **Video playback**: Native (Qt Multimedia) or external player
- **Audio thumbnails**: Styled previews with waveform decoration and format badge
- **Language**: English / German — switchable at runtime (Settings → General)
- **Themes**: Fully customizable — every color, every surface (Settings → Design)

---

## Keyboard Shortcuts

| Action | Shortcut |
|--------|----------|
| Open folder | `Ctrl+O` |
| Reload / refresh thumbnails | `F5` / `R` |
| Toggle options bar | `S` |
| Toggle cover mode | `B` |
| Open fullscreen view | Double-click |
| Next item | `→` |
| Previous item | `←` |
| Back to gallery | `Esc` |
| Edit date (fullscreen) | `D` |
| Open date editor | Calendar button (fullscreen) |
| Delete file | Delete button (fullscreen) |
| PDF: zoom in | `+` |
| PDF: zoom out | `-` |
| PDF: previous page | `←` |
| PDF: next page | `→` |

---

## Build Instructions

### Requirements
- Qt 6.4+ with modules: `Widgets`, `Multimedia`, `MultimediaWidgets`, `Pdf`, `PdfWidgets`, `Concurrent`
- CMake 3.19+
- C++20-capable compiler (MSVC 2022, GCC 12+, Clang 15+)

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Windows (vcpkg)
```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg_root>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

---

## Configuration & Data

All settings are stored via `QSettings` (platform-native).

Per-folder metadata (tags, dates, categories) is stored as JSON alongside the media:
```
MyPhotos/
├── photo1.jpg
├── photo2.png
└── MyPhotos.json
```

Custom themes can be exported to JSON and shared:
```json
{
  "name": "My Theme",
  "background": "#0a1216",
  "accent": "#00b4a0",
  "pdfSidebarBg": "#0d1518",
  "sidebarBg": "#121c22"
}
```

---

## Changelog

### Latest
- **PDF first-load fix**: Document renders immediately on the very first open (no second click needed)
- **Full color theming**: PDF sidebar, toolbar, scrollbar, and app sidebar backgrounds are individually customizable under Settings → Design → Custom Theme Editor
- **Full English UI**: All buttons, tooltips, status messages, and dialogs are now consistently in English; German remains available via Settings → General → Language
- **Updated README**

---

## License

MIT License. See `LICENSE` for details.
