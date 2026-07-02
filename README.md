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
- **Text files**: TXT, Markdown, source code (C++, Python, Rust, Go, JS/TS, …), configs, scripts, logs, CSV and more — editable directly in the app

### Gallery & View
- **Grid view**: 1–25 columns, zoom with `Ctrl+Scroll` or `Shift+Scroll`
- **Fullscreen gallery**: Prev/Next, Random mode, up to 10× zoom, pan with mouse
- **Image viewer**: Hardware-accelerated QML viewer with pinch-zoom, wheel-zoom and mouse pan
- **Compact mode**: Options mode toggle with `Alt+S` — works in the gallery and inside the open media viewer
- **Cover mode**: Cover/uncover gallery with `B`
- **Live folder watch**: New or deleted files are detected automatically

### Text Editor
- Opens any supported text/source file in a monospace editor inside the fullscreen view
- **Save** button and `Ctrl+S` shortcut
- Unsaved-changes indicator (`*` in the filename label)
- **Auto-Save**: optional timer-based auto-save (configurable interval, Settings → Text Editor)
- Confirmation dialog on navigation away from unsaved changes (Save / Discard / Cancel)

### Tags & Categories
- **Tags**: Per-folder, unlimited, freely named, color-coded
- **Categories**: Hierarchical tag categories with optional color inheritance
- **Unified side panel**: Tags and categories live in one panel with two equal sections — all tags as toggleable chips with a clearly visible active/inactive state, plus the full category tree below
- **Individual panel toggles**: The Filter popup has a merged "Tags & Categories" section where the Tag panel and the Category panel can be shown or hidden independently, each with a clearly visible on/off state
- **"+" buttons everywhere**: Create new tags and new categories directly from the panel headers, and — in options mode (`Alt+S`) — straight from a media tile, each "+" sitting right next to its corresponding button (new tags/categories are assigned to that file immediately)
- **Smart filter cascade**: Deselecting a category (or subcategory) automatically deactivates its dependent subcategories and tags — unless they are still needed by another active filter, in which case they stay active
- **Universal converter**: Convert in every direction between tags, subcategories, and top-level categories (Settings → Converter) — pick the direction from a dropdown and the form adapts to it
- **Filter modes**: OR, AND, ONLY, INCLUSIVE — combinable with media-type filter
- **Sorting**: Date, Name, File size (ascending/descending)

### PDF Viewer
- Full multi-page rendering via Qt6 PDF
- Thumbnail sidebar for quick page navigation
- Zoom in/out, fit-page, fit-width, single-page / multi-page scroll modes
- Embedded audio and video annotation playback (Sound, Screen, Movie subtypes)
- Sidecar audio file fallback (auto-detected by filename)
- **First-load fix**: PDFs now open and render immediately on the first click
- **Stable audio playback**: Embedded PDF audio now plays reliably on every file — the previous alternating failure (every second file staying silent) is fixed

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
- **Text thumbnails**: First few lines of the file rendered in monospace with extension badge
- **Language**: English / German — switchable at runtime (Settings → General)
- **Themes**: Fully customizable — every color, every surface (Settings → Design)

---

## Keyboard Shortcuts

| Action | Shortcut |
|--------|----------|
| Open folder | `Ctrl+O` |
| Reload / refresh thumbnails | `F5` / `R` |
| Toggle options mode (gallery & media viewer) | `Alt+S` |
| Toggle cover mode | `B` |
| Open fullscreen view | Double-click |
| Next item | `→` |
| Previous item | `←` |
| Back to gallery | `Esc` |
| Back to gallery (from any viewer) | `Alt+←` |
| Save text file | `Ctrl+S` |
| Edit date (fullscreen) | `D` |
| Open date editor | Calendar button (fullscreen) |
| Delete file | Delete button (fullscreen) |
| Image: zoom in | `Ctrl++` |
| Image: zoom out | `Ctrl+-` |
| PDF: zoom in | `+` |
| PDF: zoom out | `-` |
| PDF: previous page | `←` |
| PDF: next page | `→` |

---

## Build Instructions

### Requirements
- Qt 6.4+ with modules: `Multimedia`, `MultimediaWidgets`, `Pdf`, `PdfWidgets`, `Qml`, `Quick`
- CMake 3.21+
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
- **Feature**: Added HTML support with live rendering.
- **UI**: PDF documents now use a white background instead of black for improved readability.
- **Fix**: Restored M4A playback.
- **Fix**: Restored the Add Tag and Add Category actions after the QML migration.
- **UI**: Various UI refinements, including improved dialog layouts, button placement, filter text styling, and general visual consistency.

---

## License

MIT License. See `LICENSE` for details.
