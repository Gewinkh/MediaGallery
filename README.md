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
- **Compact mode**: Option bar toggle with `S`
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
- **Text thumbnails**: First few lines of the file rendered in monospace with extension badge
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
- Qt 6.4+ with modules: `Widgets`, `Multimedia`, `MultimediaWidgets`, `Pdf`, `PdfWidgets`, `Qml`, `Quick`
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

### Performance
A major optimization pass targeting the three most user-visible bottlenecks: opening a folder, switching filters/tabs, and scrolling large galleries.

- **Faster PDF open (staged rendering)**: Opening a large PDF no longer stalls. Previously the page list and the thumbnail sidebar both started rendering the moment the document became ready, and since PDFium serializes all `render()` calls per document instance through a single mutex, the first visible page had to wait behind ~8 thumbnail renders plus several pre-rendered neighbour pages. Now the first visible page renders alone; the look-ahead buffer and the thumbnail strip are unlocked a fraction of a second later (mirroring the proven `QPdfView` version, which deferred thumbnails by 120 ms). The page look-ahead buffer was also reduced from 2.5 to 1.5 viewport-heights so scrolling stays responsive — fewer pre-renders competing with the page actually coming into view.

- **Incremental folder loading**: Folders no longer block the UI with a single full model reset. The first batch of tiles (256 items) is inserted synchronously so the viewport fills instantly, and the remaining entries stream in chunked (512 per tick) via a zero-delay timer that yields to the event loop between batches. The window stays responsive even with tens of thousands of files, and the first tiles appear almost immediately.
- **O(1) filter caches**: `MediaProxyModel` no longer walks the category tree per row. The effective tag set and the set of files in active categories are pre-computed once per filter change and looked up in constant time inside the hot path. Toggling boolean filters, tags or filter modes now uses a rows-only invalidation (no re-sort), so changing a tab or filter is near-instant instead of triggering a full re-sort/reset.
- **Smarter thumbnail loading**: Cache hits take a synchronous fast path and skip the thread pool entirely. Visible tiles are prioritized (newest request first), and thumbnails for tiles that scrolled out of view — or got recycled by the `GridView` — are actively cancelled, both for not-yet-started tasks (removed from the queue) and running ones (cooperative abort flag). This keeps the worker pool free for what's actually on screen. The gallery cache buffer was also widened to pre-render one extra row above and below the viewport for smoother scrolling.

### Latest
- **Performance**: Faster loading for PDF-files (tested for 100MB files)

---

## License

MIT License. See `LICENSE` for details.
