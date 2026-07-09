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
- **HTML**: Rendered live preview (via Qt WebEngine) alongside the editable source view

### Gallery & View
- **Grid view**: 1–25 columns, zoom with `Ctrl+Scroll` or `Shift+Scroll`
- **Tile size dialog**: Adjust tile width/height with a live drag-resize preview
- **Fullscreen gallery**: Prev/Next, Random mode, up to 10× zoom, pan with mouse
- **Image viewer & editor**: Hardware-accelerated QML viewer with PDF-style zoom/pan (fit-to-window, 100%, wheel-zoom, drag-pan) and a full non-destructive **Image Editor** — see the dedicated section below
- **Split view**: Open up to **4 files side by side** in the fullscreen view (like a 2-, 3-, or 4-player split screen: 2 = two columns, 3 = two on top + one full-width below, 4 = 2×2). Each pane is an independent viewer with its own header and Prev/Next navigation, and its own PDF Editor / Image Editor / zoom / playback state. The **boundaries between panes can be dragged** to resize the tiles (a column divider for 2 panes, plus a row divider for 3/4 panes). A **"+" button** in each pane's header returns to the gallery and adds the next clicked file as another pane (no file dialog — you pick straight from the gallery). **Back / `Esc`** on a pane closes just that file and frees its memory immediately; closing the last pane returns to the gallery. Resizing the window or adding a pane keeps every pane on its current page/position.
- **Compact mode**: Options mode toggle with `Alt+S` — works in the gallery and inside the open media viewer
- **Cover mode**: Cover/uncover gallery with `B`
- **Live folder watch**: New or deleted files are detected automatically
- **Folder bookmarks**: Save folders under a custom name for one-click access (Menu → Folder, and Settings → Bookmarks)
- **Fullscreen transitions**: Slide or Fade page animation (Settings → General)

### Text Editor
- Opens any supported text/source file in a monospace editor inside the fullscreen view
- **Save** button and `Ctrl+S` shortcut
- Unsaved-changes indicator (`*` in the filename label)
- **Auto-Save**: optional timer-based auto-save (configurable interval, Settings → Text Editor)
- Confirmation dialog on navigation away from unsaved changes (Save / Discard / Cancel)
- Proper Arabic/CJK font fallback in the editor (no more missing-glyph "tofu" boxes)
- **Themeable editor backgrounds**: the TXT/code editor and the HTML source view each have their **own separate** background color, independent of each other and of the card/panel background (Settings → Design)

### HTML Viewer
- **.html / .htm** files open in a rendered live preview by default, with a one-click toggle back to the editable source view
- Preview runs fully **offline**: JavaScript stays enabled (quizzes, search, shortcuts keep working) while remote network access is blocked — no external fonts or trackers load
- **Design-card thumbnails**: instead of showing raw source code, HTML thumbnails are auto-generated from the page's hero section (title, subtitle, colors, RTL/Arabic star patterns or gradients) and refresh automatically when the file changes
- **Lazy rendering engine**: Qt WebEngine now initializes only the first time you actually open an `.html`/`.htm` file — for a faster start and a lower memory baseline. Until then, HTML files fall back to the editable source view and the preview toggle appears once the engine is ready

### Live Transliteration
- Type in Latin letters and get **Arabic (with full Harakat/diacritics)** or **Japanese (Hiragana/Katakana)** automatically as you type — no separate conversion step
- Works in the **text editor**, **HTML source view**, and **PDF Editor notes**
- Smart, unambiguous conversion: waits for the next keystroke whenever a shorter match could still extend into a longer one (e.g. holds `a` until it's clear whether `aa` follows)
- Supports the Arabic definite article (sun/moon letter assimilation), doubled consonants (auto-Shadda), and word-boundary handling
- Fully customizable mapping tables per script (Settings → Text Editor), with add/edit/remove/reset controls
- Toggle button (with scheme picker) available directly in the editor toolbar and the PDF Editor toolbar

### Tags & Categories
- **Tags**: Per-folder, unlimited, freely named, color-coded
- **Categories**: Hierarchical tag categories with optional color inheritance
- **Unified side panel**: Tags and categories live in one panel with two equal sections — all tags as toggleable chips with a clearly visible active/inactive state, plus the full category tree below
- **Individual panel toggles**: The Filter popup has a merged "Tags & Categories" section where the Tag panel and the Category panel can be shown or hidden independently, each with a clearly visible on/off state
- **"+" buttons everywhere**: Create new tags and new categories directly from the panel headers, and — in options mode (`Alt+S`) — straight from a media tile, each "+" sitting right next to its corresponding button (new tags/categories are assigned to that file immediately)
- **Right-click context menu on tiles**: Assign existing tags or categories to a file directly from a submenu, with already-assigned entries checked, without opening a panel
- **Smart filter cascade**: Deselecting a category (or subcategory) automatically deactivates its dependent subcategories and tags — unless they are still needed by another active filter, in which case they stay active
- **Universal converter**: Convert in every direction between tags, subcategories, and top-level categories (Settings → Converter) — pick the direction from a dropdown and the form adapts to it, and move categories anywhere in the tree
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
- **Browser-style text selection**: click-and-drag to select the embedded text layer, `Ctrl+C` to copy, `Ctrl+A` to select all on the current page
- **Embedded audio panel**: a dedicated side panel lists every audio clip on the current page with a seek slider, plus an Apple-style mini-player that keeps playing while you scroll to other pages; clickable on-page audio hotspots

### PDF Editor (Post-it Notes)
- Add sticky-note style text boxes anywhere on a PDF page — the original file is **never modified**
- Notes are saved to a **sidecar file** next to the PDF (non-destructive) and stay editable across sessions
- **Export** writes a brand-new PDF copy (`…_edited(.n).pdf`) with the notes permanently rendered onto the pages — your original is always preserved
- Full text formatting: font family (with automatic substitution hint for missing fonts), size, bold/italic/underline, alignment, vertical alignment, text color and note-paper (highlight) color, including an opacity slider
- **Line-snapping**: new notes anchor precisely to detected text lines when placed nearby, or float freely elsewhere
- **Cross-page dragging**: drag a note past the top/bottom of a page and it automatically moves to the neighboring page on release
- Full undo/redo history and a note-visibility toggle (`Alt+Q`) that hides/shows all notes in both view and edit mode
- Formatting panel can be docked as a **right sidebar** or a **Word-style ribbon** at the top (Settings → Text Editor)

### Image Editor
- Opens on any image via the **✎ Edit** button in the image viewer's toolbar — the original file is **never modified**
- **Tools**: Select/move, Text note, Freehand pen, Arrow, Rectangle, Ellipse (tool palette in the dockable panel)
- **Text notes** with the same post-it styling as the PDF Editor: font family, size, bold/italic/underline, horizontal/vertical alignment, text color and note-paper (highlight) color with an opacity slider, plus **live transliteration** (Arabic/Japanese) while typing
- **Drawings** with adjustable stroke color, line width and (for shapes) fill color
- **Select / move / resize / delete** any annotation, with full **undo/redo** history and a note-visibility toggle (`Alt+Q`)
- **Copy & paste** the selected annotation (`Ctrl+C` / `Ctrl+V` or the toolbar button) — duplicates it with all settings and text
- A newly created note/shape **inherits the last-used style** (only without the text)
- Notes are saved to a **sidecar file** `<image>.mgedit.json` next to the image (non-destructive) and stay editable across sessions
- **Export** writes a brand-new image copy `…_bearbeitet(.n).<ext>` with the annotations permanently rendered onto it (QImage + QPainter, WYSIWYG); the copy keeps the **source format** (JPG→JPG, PNG→PNG, otherwise PNG)
- Formatting panel can be docked as a **right sidebar** or a **Word-style ribbon** at the top (shares the PDF Editor's panel-position setting, Settings → Text Editor)
- Fully decentralized: each split-view tile has its **own** independent image editor

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
  - Editor background (text / HTML source editor surface)
- Export / Import custom themes as JSON files
- All color changes apply live without restarting

### Metadata & File Management
- **Date editor**: Custom date per file, persisted in JSON
- **Delete file**: Red delete button in fullscreen view, plus a right-click "Delete file…" entry on gallery tiles — both with a confirmation dialog; the file goes to the system trash and its metadata/sidecar are cleaned up automatically
- **Create file**: A "+ Create" button in the filter bar creates an empty PDF, HTML, or text file directly in the current folder (PDF starts as one blank A4 page, ready to annotate)
- **Rename**: Also renames the file on disk
- **Drag & Drop**: Drop a folder or individual media files onto the window
- **JSON storage**: `<FolderName>.json` stored directly in the target folder (file-centric format v2)

### Playback & UI
- **Video playback**: Native (Qt Multimedia) or external player
- **Audio thumbnails**: Styled previews with waveform decoration and format badge
- **Text thumbnails**: First few lines of the file rendered in monospace with extension badge
- **HTML thumbnails**: Auto-generated design cards instead of raw source code (see HTML Viewer)
- **Language**: English / German — switchable at runtime (Settings → General)
- **Audio player accent**: Theme color or Apple Blue for the PDF audio mini-player (Settings → General)
- **Graphics backend**: Vulkan, OpenGL, or Software rendering, with an automatic crash-guard fallback if a backend fails to start (Settings → General)
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
| Image: zoom in / out | Mouse wheel · toolbar `+` / `−` |
| Image: fit to window / 100% | Toolbar buttons |
| Image / PDF: pan when zoomed | Left-drag (PDF: on non-text areas) |
| PDF: zoom in | `+` |
| PDF: zoom out | `-` |
| PDF: previous page | `←` |
| PDF: next page | `→` |
| PDF: copy selected text | `Ctrl+C` |
| PDF: select all text on page | `Ctrl+A` |
| PDF / Image Editor: toggle note visibility | `Alt+Q` |
| PDF / Image Editor: delete selected annotation | `Delete` |
| PDF / Image Editor: copy / paste selected annotation | `Ctrl+C` / `Ctrl+V` |
| Image Editor: toggle edit mode | ✎ toolbar button |

---

## Build Instructions

### Requirements
- Qt 6.4+ (developed against 6.11) with modules: `Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, `Multimedia`, `Pdf`, `Svg`, `WebEngineQuick`
- ZLIB (used for inflating embedded PDF audio streams)
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
- **Feature**: New **Image Editor** — the image viewer is now a full non-destructive editor, mirroring the PDF Editor. PDF-style **zoom/pan** (fit-to-window, 100%, wheel-zoom, drag-pan) replaces the old simple scaling; add **text notes** (full post-it styling, live transliteration) and **drawings** (freehand pen, arrow, rectangle, ellipse) with color, line width and fill; **select/move/resize/delete** with full undo/redo. The original file is never touched: annotations save to a **sidecar** `<image>.mgedit.json` and **export** writes a new copy `…_bearbeitet(.n).<ext>` in the source format (JPG→JPG, PNG→PNG, else PNG). Each split-view tile has its own independent editor.
- **Feature**: **Copy & paste stickers** — `Ctrl+C`/`Ctrl+V` (or the toolbar/panel button) duplicates the selected note/annotation with all of its settings **and** its text, in both the PDF and Image editors.
- **Feature**: **Style inheritance** — a newly created note/shape inherits the last-used style (font, colors, opacity, line width, fill), only without the text — in both editors.
- **Feature**: **Separate editor background colors** — the TXT/code editor and the HTML source view now have **two independent** background colors (Settings → Design), instead of sharing one.
- **Feature**: **Draggable split dividers** — in split view the boundaries between panes can be dragged to resize the tiles (one divider for 2 panes, a row + column divider for 3/4 panes).
- **Change**: **Zoomed panning** — when zoomed in, hold the left mouse button and drag to pan and reveal content that was off-screen (PDF & image). Over selectable PDF text, left-drag still selects text.

---

### Issue
- Resizing the window or adding a file in Split View may reset PDFs/images to another or the first page instead of preserving the current page and scroll position.

## License

MIT License. See `LICENSE` for details.
