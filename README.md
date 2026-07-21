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
- **Word documents**: DOCX — opened in a built-in, loss-preserving editor (see *DOCX Editor* below)
- **HTML**: Rendered live preview (via Qt WebEngine) alongside the editable source view

### Gallery & View
- **Grid view**: 1–25 columns, zoom with `Ctrl+Scroll` or `Shift+Scroll`
- **Tile size dialog**: Adjust tile width/height with a live drag-resize preview
- **Fullscreen gallery**: Prev/Next, Random mode, up to 10× zoom, pan with mouse
- **Image viewer & editor**: Hardware-accelerated QML viewer with PDF-style zoom/pan (fit-to-window, 100%, wheel-zoom, drag-pan) and a full non-destructive **Image Editor** — see the dedicated section below
- **Split view**: Open up to **4 files side by side** in the fullscreen view (like a 2-, 3-, or 4-player split screen: 2 = two columns, 3 = two on top + one full-width below, 4 = 2×2). Each pane is an independent viewer with its own header and Prev/Next navigation, and its own PDF Editor / Image Editor / zoom / playback state. The **boundaries between panes can be dragged** to resize the tiles (a column divider for 2 panes, plus a row divider for 3/4 panes). A **"+" button** in each pane's header returns to the gallery and adds the next clicked file as another pane (no file dialog — you pick straight from the gallery). **Back / `Esc`** on a pane closes just that file and frees its memory immediately; closing the last pane returns to the gallery. Resizing the window or adding a pane keeps every pane on its current page/position.
- **Split view docking (drag & drop)**: Grab a pane by its **header bar** and drag it to rearrange the layout, with **visual drop zones and a live layout preview** (VS Code style). With 2 files you can switch between side-by-side and stacked (drop on an edge zone) or swap positions (drop on the other pane). With 3 files, dropping on an edge zone makes the dragged file the **large pane** on that side (the other two share the remaining half); dropping on a **corner zone** shrinks it back into that quadrant and the adjacent pane takes over the freed area; dropping on another pane swaps positions. With 4 files, dropping on another pane simply **swaps** the two (the 2×2 layout is fixed). Closing or adding panes keeps the arrangement as close as possible to what you set up; the custom layout lasts until the app is closed (not persisted).
- **Compact mode**: Options mode toggle with `Alt+S` — works in the gallery and inside the open media viewer
- **Cover mode**: Cover/uncover gallery with `B`
- **Live folder watch**: New or deleted files are detected automatically
- **Folder bookmarks**: Save folders under a custom name for one-click access (Menu → Folder, and Settings → Bookmarks). "Add Folder" from the menu **pre-fills the path with the currently open folder** if it is not saved yet (comparison is case-sensitive with trailing separators normalized) — the path stays editable before confirming
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

### PDF Page Extraction
- **Extract pages from an open PDF**: right-click any page → **Extract page** (single) or **Extract multiple pages…** (grid picker). The new PDF is written next to the source file.
- **Extract across the whole folder**: the **Extract** button in the filter bar (next to *+ Create*) collects **every PDF in the current folder** into one page grid — pick pages from several files and get them merged into a **single** new PDF.
- **Lossless by default**: pages are copied at the PDF object level, so the **text layer, vector graphics, embedded fonts and annotations stay intact** — no rasterizing, no quality loss. If a source can't be copied that way (e.g. an encrypted file), just that file's pages fall back to a 150 dpi image page in the same output, so you always get a result.
- **Selection**: a plain **left click** selects/deselects a page — no modifier needed. Hold **Ctrl** and hover a page for a **large preview** (~80% of the dialog) that disappears as soon as you release Ctrl.
- **Scrolling** in the page grid uses the same smooth wheel behaviour as the main gallery.
- Page tiles use your gallery tile size; the selected-page highlight style (**frame** or **overlay**) is configurable under *Settings → View*.
- **Output is always in original order**, regardless of the order you clicked. Names default to `<source> - Page N` / `<source>-Selected` (required for the folder-wide mode); `.pdf` is appended automatically and existing names get ` (1)`, ` (2)`, … appended instead of being overwritten.

### PDF Editor (Post-it Notes, Drawings & Text Replacement)
- Add sticky-note style text boxes anywhere on a PDF page — the original file is **never modified**
- **Tools**: Select/move, Text note, Freehand pen, Arrow, Rectangle, Ellipse — the full image-editor tool set, now on PDFs (tool palette in the dockable panel)
- **Drawings** with adjustable stroke color, line width (in PDF points) and (for shapes) fill color; drawings move/resize/copy/paste/undo exactly like notes, including **cross-page dragging**
- Notes and drawings are saved to a **sidecar file** next to the PDF (non-destructive) and stay editable across sessions; old sidecars load unchanged
- **Export** writes a brand-new PDF copy (`…_edited(.n).pdf`) with all annotations permanently rendered onto the pages — your original is always preserved
- **Add / remove pages** (in edit mode): the **"+" line** beneath each page inserts a blank A4 page; **right-click → Remove page** deletes one; **Ctrl+Z** undoes both. Choose the behaviour in **Settings → Editor (PDF)**: **non-destructive** (default — the change is applied on export, the original stays untouched) or **destructive** (the original PDF is rewritten immediately, with a one-time pristine backup)
- Full text formatting: font family (with automatic substitution hint for missing fonts), size, bold/italic/underline, alignment, vertical alignment, text color and note-paper (highlight) color, including an opacity slider
- A newly created note/drawing **inherits the last-used style** (only without the text)
- **Line-snapping**: new notes anchor precisely to detected text lines when placed nearby, or float freely elsewhere
- **Replace text** tool: two ways to use it — drag across the text you want to replace (you see the familiar blue text selection while dragging, and the white patch appears when you release), or select text first and then press the **⇄** button. Either way it stays as non-destructive as everything else (original file and sidecar workflow unchanged)
- Text selection keeps working **inside edit mode** with the Select tool, and a selection made before entering edit mode is preserved
- The replacement box snaps exactly onto the detected text line(s), adopts their font size, and comes **pre-filled with the embedded text** underneath, so you edit instead of retyping; on scanned PDFs without a text layer the tool stays fully usable as a blank patch
- Replacement boxes keep a fixed width with automatic word wrap and **grow in height with their content**; typing and the resulting growth undo together as a single step
- **Cross-page dragging**: drag an annotation past the top/bottom of a page and it automatically moves to the neighboring page on release
- Full undo/redo history (`Ctrl+Z` / `Ctrl+Shift+Z`) and a note-visibility toggle (`Alt+Q`) that hides/shows all annotations in both view and edit mode
- **Copy & paste** the selected annotation (`Ctrl+C` / `Ctrl+V` or the toolbar/panel buttons) — duplicates it with all settings and text
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

### DOCX Editor (Basic)
- Opens `.docx` files directly in the viewer/split view as an **editable continuous text** (no page look yet; page breaks are shown as dashed markers)
- **Loss-preserving by design**: only the XML nodes you actually touch are rewritten — untouched paragraphs, tables, images, headers/footers, styles and every other part of the file are carried over **byte-identically** (the document is never regenerated from scratch); a two-stage self-check on load refuses editing rather than risk silent data loss
- **Text editing**: type, delete, select (mouse/keyboard), split & merge paragraphs, line breaks (`Shift+Enter`), full **undo/redo** with keystroke coalescing
- **Character formatting**: font family, size, **bold/italic/underline**, text color — applied to the selection, or to the next typed text when nothing is selected (Word-style pending format)
- **Paragraph formatting**: alignment (left/center/right/justify), spacing (line spacing plus space before/after, grouped behind one button), **bulleted & numbered lists** (numbering definitions are created and spliced into `numbering.xml` on save). Pressing Enter in an **empty list item ends the list** instead of adding another bullet — the Word behaviour
- **Word-style page**: the text runs on an always-white paper strip on top of your theme background; page breaks appear as dashed markers
- **Themed, compact toolbar**: every control follows the app theme; on narrow split panes the toolbar **scrolls horizontally with the mouse wheel** so nothing is cut off
- Tables and other complex blocks are shown as placeholders and remain **fully intact** in the file; embedded objects (images, fields, hyperlinks) are protected as atomic units
- **Live transliteration** (Arabic/Japanese) while typing, sharing the app-wide schemes
- **Two save modes** (Settings → Text Editor): **Save directly** to the original file (a one-time `.bak` backup per session is created next to it) or **Export a copy** `<name>_edited(.n).docx` leaving the original untouched; `Ctrl+S` and auto-save on leaving the tile follow the chosen mode
- **Export to PDF** (**→ PDF** button in the toolbar): writes an A4 PDF next to the document (`<name>.pdf`, collision-suffixed) using the same Qt text engine as the editor view; the original `.docx` is kept. Runs asynchronously so the UI stays responsive.
- **Create new Word documents** via the gallery's "+" button (empty A4 document, standard margins)
- Gallery **thumbnails** show the first paragraphs of the document; `.docx` files appear under the Text filter
- Fully decentralized: each split-view tile has its **own** independent DOCX editor

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
- **Rename**: Also renames the file on disk. In the fullscreen view the filename in the header is editable **only in options mode (`Alt+S`)** — outside it, the header acts as the drag handle for split-view docking
- **Drag & Drop**: Drop a folder or individual media files onto the window
- **JSON storage**: `<FolderName>.json` stored directly in the target folder (file-centric format v2)

### Playback & UI
- **Video playback**: Native (Qt Multimedia) or external player
- **Audio thumbnails**: Styled previews with waveform decoration and format badge
- **Text thumbnails**: First few lines of the file rendered in monospace with extension badge
- **HTML thumbnails**: Auto-generated design cards instead of raw source code (see HTML Viewer)
- **Language**: English / German — switchable at runtime (Settings → General)
- **Audio player accent**: Theme color or Apple Blue for the PDF audio mini-player (Settings → General)
- **Mono-Play**: Only one audio/video playback at a time (enabled by default) — starting playback in another split-view pane automatically **pauses** the one already playing (position is kept). Disable it in Settings → General to allow parallel playback
- **Graphics backend**: Vulkan, OpenGL, or Software rendering, with an automatic crash-guard that now **degrades gracefully** (Vulkan/D3D11/Metal → OpenGL → Software) if a backend fails to start, a Vulkan loader pre-check, validation of stale/foreign config values, and a runtime guard that switches to a safer backend on the next start after a GPU device-loss (Settings → General)
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
| PDF / Image Editor: undo / redo | `Ctrl+Z` / `Ctrl+Shift+Z` |
| Image Editor: toggle edit mode | ✎ toolbar button |
| DOCX: save (follows the chosen save mode) | `Ctrl+S` |
| DOCX: bold / italic / underline | `Ctrl+B` / `Ctrl+I` / `Ctrl+U` |
| DOCX: undo / redo | `Ctrl+Z` / `Ctrl+Shift+Z` (or `Ctrl+Y`) |
| DOCX: select all / copy / cut / paste | `Ctrl+A` / `Ctrl+C` / `Ctrl+X` / `Ctrl+V` |
| DOCX: line break inside a paragraph | `Shift+Enter` |
| Any editor: jump to end of the last line | `↓` (with `Shift` to select) |

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
- **Feature**: Added support for adding and removing pages in the PDF editor.
- **Feature**: Added DOCX to PDF export.

---

## Issues

### PDF Editor
- The **Replace Text** button in the PDF editor is not working yet. Text cannot be selected while using this tool (selection works outside edit mode).
- Drawing tools (freehand, arrow, rectangle, ellipse, etc.) do not work correctly. Shapes are only drawn for a few pixels.

### DOCX Editor
- Copying text does not preserve formatting such as font size, font family, or text style (italic, etc.).
- The caret/position indicator is not updated correctly when the font size changes.
- Formatting inheritance issue example: If a heading (e.g. font size 20) is followed by normal text (font size 12), deleting the normal text keeps its formatting until the last character is removed. Once the final character is deleted, the editor immediately inherits the heading's formatting. Instead, the current line's formatting should remain until Backspace is pressed again and the caret actually moves into the heading line. A line should not inherit the previous line's formatting as long as text has existed on that line.

### HTML View
- HTML rendering occasionally fails and can freeze the application.
- HTML rendering is sometimes noticeably slow.
- Closing an HTML document can completely freeze the system, requiring a restart.
- The HTML viewer should be thoroughly reviewed and optimized for performance and stability.

### Settings
- Under **View/Layout → Tile Arrangement**, the **"Manual (free area)"** option allows adjusting the height, but this has no effect. The option should only allow changing the width.

---

## Planned

### PDF-Editor
- True content-stream editing (rewriting the embedded PDF text directly instead of covering it), if the cover-patch approach reaches its limits.
- Configurable cover-patch color (currently fixed white).
- Optional automatic text recognition/OCR for scanned PDFs.
- Automatic reflow across multiple linked boxes.

### DOCX-Editor
- True pagination with a page-accurate view and page thumbnails.
- Editing support for tables, images, headers/footers, and find & replace.
- Style templates (Formatvorlagen), footnotes, table of contents, multi-column layouts.
- Comments and tracked changes as an authoring tool.
- Legacy `.doc` support.
- Spell checking.

### HTML View
- make the scrolling in rendered HTMLs visually smooth
- JavaScript support

---

## License

MIT License. See `LICENSE` for details.
