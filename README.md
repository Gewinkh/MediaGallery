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
- **Software rendering by design**: the preview runs without GPU acceleration. Local documents rasterize fast enough in software, it avoids a whole class of graphics-driver crashes and hangs, and it saves the memory of a separate GPU process. Advanced users can override it by setting `QTWEBENGINE_CHROMIUM_FLAGS` before starting the app
- **Clear failure handling**: if a page cannot be loaded or the render process dies, a readable hint replaces the blank area; links that would leave the local file are ignored
- **Smooth scrolling**: the rendered preview scrolls with animation (Chromium smooth scrolling), consistent with the web-style smooth wheel scrolling of the gallery, PDF, DOCX and text views

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
- Full multi-page rendering via Qt6 PDF, with a thumbnail sidebar for quick navigation
- Zoom in/out, fit-page, fit-width, single-page and continuous scrolling
- **Browser-style text selection**: click and drag across the embedded text layer, `Ctrl+C` to copy, `Ctrl+A` to select the page; the selection survives switching into edit mode
- **Search across the document** (`Ctrl+F`): matches light up on the page while you type, the bar shows "x of n", and ▲/▼ (or Enter) step through them and jump to the right page. Long documents stay responsive — pages are searched in small batches. Scanned pages you ran OCR on take part too, where the recognized line is the match
- **Embedded audio and video**: annotation playback (Sound, Screen, Movie subtypes) plus a side panel listing every audio clip on the current page with a seek slider; a sidecar audio file next to the PDF is picked up automatically

### PDF Page Extraction
- **Extract pages from an open PDF**: right-click any page → **Extract page** (single) or **Extract multiple pages…** (page picker). The new PDF is written next to the source file.
- **Extract across the whole folder**: the **Extract** button in the filter bar (next to *+ Create*) collects **every PDF in the current folder** — pick pages from several files and get them merged into a **single** new PDF.
- **Workbench layout** (default): the picker is a three-panel workbench — a **PDF list on the left** (the active file is highlighted; a fully-selected file shows a check, a partially-selected one shows an *N / M* count), the **pages of the active PDF on the right**, and a **selection bar at the bottom** that appears once you pick a page. The bar shows each selected page as its own thumbnail; **its left-to-right order is the extraction order**. Reorder pages by **dragging them inside the bar**, remove one by **dragging it out of the bar** (or the small ×). Drag a page from the grid, or a whole PDF from the left list (adds all its pages), straight into the bar; a **+ / −** button on each PDF row toggles all its pages at once.
- **Compact layout** (optional, *Settings → View*): a minimalist single grid of all pages with a selection count — for anyone who prefers the simpler, less busy dialog. Output stays in original page order here.
- **Lossless by default**: pages are copied at the PDF object level, so the **text layer, vector graphics, embedded fonts and annotations stay intact** — no rasterizing, no quality loss. If a source can't be copied that way (e.g. an encrypted file), just that file's pages fall back to a 150 dpi image page in the same output, so you always get a result.
- **Selection**: a plain **left click** selects/deselects a page — no modifier needed. Hold **Ctrl** and hover a page for a **large preview** (~80% of the dialog) that disappears as soon as you release Ctrl.
- **Scrolling** in the page grid uses the same smooth wheel behaviour as the main gallery.
- Page tiles use your gallery tile size; the selected-page highlight style (**frame** or **overlay**) is configurable under *Settings → View*.
- **Order**: in the workbench the **selection bar defines the output order**; the compact layout always writes pages in original order. Names default to `<source> - Page N` / `<source>-Selected` (required for the folder-wide mode); `.pdf` is appended automatically and existing names get ` (1)`, ` (2)`, … appended instead of being overwritten.

### PDF Editor
Everything below is **non-destructive**: your original PDF is never modified. Notes live in a sidecar file (`<name>.mgedit.json`) next to it and stay editable across sessions; **Export** writes a new copy (`…_edited(.n).pdf`).

**Tools** (palette in the dockable panel): select/move · text note · freehand pen · arrow · rectangle · ellipse · replace text · edit text · highlight/underline/strikethrough · black out text · signature or stamp image.

**Notes and drawings**
- Sticky-note text boxes with full formatting: font family (with a hint when the system substitutes one), size, bold/italic/underline, alignment, vertical alignment, text and paper colour, opacity
- Drawings with stroke colour, line width in PDF points and (for shapes) fill colour
- Select, move, resize, copy/paste (`Ctrl+C`/`Ctrl+V`), delete, full undo/redo (`Ctrl+Z` / `Ctrl+Shift+Z`), and a visibility toggle (`Alt+Q`)
- **Cross-page dragging**: pull an annotation past the top or bottom edge and it moves to the neighbouring page
- **Line-snapping**: a new note anchors to a detected text line when you place it nearby, or floats freely elsewhere
- A new note or drawing **inherits the last-used style** (without the text)
- **Linked text boxes**: chain a box to a follow-up box (**🔗**) and text flows across the chain — the last box grows with its content, and editing anywhere re-flows the whole chain as one undo step

**Working with the text that is already in the page**
- **Replace text**: drag across the text (or select it and press **⇄**). The box snaps onto the detected line, adopts its font size and comes pre-filled with the embedded text, so you edit instead of retyping. On a scanned page without a text layer it stays usable as a blank patch
- **Edit text** (⌶): click into the page text and type — characters go into the PDF's own text layer, so the page stays vector and searchable. Arrow keys, `Home`/`End`, `Backspace`/`Delete` work as expected, continuous typing undoes as one step, and the paragraph re-flows as you type (gaining a line and pushing the content below down where that is provably safe)
- **Highlight, underline, strikethrough**: pick one of the three markers and drag across the text. The mark snaps to the text lines, so a sweep across three lines is **one** marker covering exactly those lines — recolour or delete it in one go. Highlights are translucent so the text stays readable
- **Black out text**: an opaque bar *and* removal of the covered text from the document on export, so it can no longer be selected, copied or found — and the exported copy is written from scratch, so the text is not left behind in its raw bytes either. Where it cannot be removed safely, the page is written out as an image instead and you are told
- **OCR for scanned PDFs**: the **OCR** button recognizes the page text (Tesseract, optional). Afterwards line-snapping, the *Replace text* pre-fill, selection and search work on that page as if it had a real text layer

**Forms, foreign annotations, images**
- **Fill in PDF forms (AcroForm)**: text fields, check boxes, radio groups and drop-down lists become visible and editable right on the page — in view mode as well as edit mode, because a form belongs to the document. Qt's PDF renderer does not draw form widgets at all, so they are drawn as an overlay that matches what gets written into the file. Values are buffered while you type and kept in the sidecar, so a half-filled form survives closing it; the **☑** button writes them into a new copy (`…_ausgefuellt(.n).pdf`) as an incremental update, with a proper appearance stream per field so the values show up in any reader and in print. Read-only fields stay visible but locked. If you reordered, rotated or removed pages, the copy follows what you see: the values are drawn into the pages first and the file is then rebuilt in your order — that copy is a finished document rather than a form you can go on filling in, and the message after saving says so
- **Annotations from other PDF readers** are picked up when you open a file: sticky notes, text boxes, rectangles, ellipses, lines, freehand drawings, highlights, underlines and strikeouts become normal editable notes. Each remembers which object it came from, so an untouched one is never exported twice and one you edited or deleted is removed from the exported copy instead of lingering underneath
- **Your own notes as real PDF annotations** (*Settings → Editor → PDF Editor*): instead of being drawn into the page they become annotation objects that stay selectable and deletable in other readers. Off by default — drawn notes look identical everywhere. *Replace text* patches and linked boxes are always drawn, and if a page holds one, everything on it is drawn (never a mixture)
- **Signature and stamp images**: place a PNG or JPEG anywhere on the page; it keeps its aspect ratio and moves, resizes, fades and undoes like any other note. On export it is embedded into the PDF with its transparency intact, and the same file placed several times is embedded only once

**Pages and export**
- **Manage pages** (edit mode): the **"+" line** below a page inserts a blank A4 page; right-click for **Remove page**, **Rotate left/right** or **Insert pages from PDF…**; drag a thumbnail in the sidebar to reorder. Every one of them undoes as a single step, and notes travel and rotate with their page. Inserted pages are copied losslessly into a companion file, so they survive even if the file you took them from is gone. Choose **non-destructive** (default) or **destructive** in *Settings → Editor (PDF)*
- **One Export button**; which path it takes is a setting (*Settings → Editor → PDF Editor → Export*): **lossless when possible** (default) or **always as an image**. Lossless writes annotations as real vector content, keeps the original page content byte-for-byte and leaves untouched pages alone; for *Replace text* it rewrites the embedded text directly in the content stream, including text split across several show operators, non-ASCII text via the font's encoding, and CID/Type0 fonts via `/ToUnicode`. Notes in a font outside the standard 14 are embedded rather than silently substituted
- Where lossless is not provably safe the export falls back to the image path — always correct, and you are told when it happens
- The formatting panel docks as a **right sidebar** or a **Word-style ribbon** (*Settings → Text Editor*); `Ctrl` + mouse wheel pans the ribbon sideways when the window is narrow

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
- **Formatting-preserving clipboard**: copy/cut keeps font family, size, bold/italic/underline and color when pasting back into the editor (or into another DOCX tile). The clipboard also carries an **HTML** flavour, so pasting into Word, LibreOffice or a browser keeps the formatting too; plain text remains available for everything else
- **Word-style caret**: the text cursor is drawn in the size of the character that will be typed next — it follows a font-size change immediately, even in an empty line
- **Line keeps its own formatting**: deleting the last character of a line does *not* make it fall back to the paragraph style (e.g. the heading it inherited its properties from). The line keeps its formatting until you press Backspace again and the caret actually moves into the previous line
- **Paragraph formatting**: alignment (left/center/right/justify), spacing (line spacing plus space before/after, grouped behind one button), **bulleted & numbered lists** (numbering definitions are created and spliced into `numbering.xml` on save). Pressing Enter in an **empty list item ends the list** instead of adding another bullet — the Word behaviour
- **Word-style page**: the text runs on an always-white paper strip on top of your theme background; page breaks appear as dashed markers
- **Themed, compact toolbar**: every control follows the app theme; on narrow split panes the toolbar **scrolls horizontally with the mouse wheel** so nothing is cut off
- Tables and other complex blocks are shown as placeholders and remain **fully intact** in the file; embedded objects (images, fields, hyperlinks) are protected as atomic units
- **Live transliteration** (Arabic/Japanese) while typing, sharing the app-wide schemes
- **Find & Replace** (`Ctrl+F`): a themed search bar with next/previous match, match-case toggle, single replace and replace-all. Replace-all is one undo step; the search wraps around and skips tables and other non-text blocks
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
- **Themed standard controls**: buttons, checkboxes, radio buttons, combo boxes, spin boxes, text fields, sliders, scrollbars, tooltips and dialogs are drawn by the app's own control style — rounded corners, accent-colored checked states, consistent hover/pressed animations. They follow the selected theme (including custom colors) instead of the desktop color scheme, so the app looks identical on every platform and under every desktop theme

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
- **Keyboard-shortcut overview**: a themed, grouped cheat-sheet in **Settings → General** lists every shortcut with its key combination and function, sorted by context (gallery, media viewer, PDF/image/DOCX/text editor)
- **Context-correct, language-independent shortcuts**: each shortcut only fires in the surface it belongs to — in split view only the **active pane** reacts, so the same key (e.g. `Ctrl+C`, `Alt+Q`) is never ambiguous across panes; the top menu no longer generates `Alt`+letter accelerators that clashed with app shortcuts, and every shortcut behaves the same regardless of the interface language
- **Smooth wheel scrolling in Settings**: the settings pages scroll ~45 % of the visible height per wheel notch with a short eased animation — the same behavior as the gallery and the PDF page grid
- **Dedicated fullscreen view**: opening a file hides the application menu bar — only the viewer and its own header are visible, and the freed space goes to the content
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
| DOCX: find & replace | `Ctrl+F` |
| Any editor: jump to end of the last line | `↓` (with `Shift` to select) |

---

## Build Instructions

### Requirements
- Qt 6.4+ (developed against 6.11) with modules: `Core`, `Gui`, `Qml`, `Quick`, `QuickControls2`, `Multimedia`, `Pdf`, `Svg`, `WebEngineQuick`
- ZLIB (used for inflating embedded PDF audio streams)
- **Optional**: Tesseract + Leptonica (via pkg-config) to enable **OCR for scanned PDFs**. If absent, the app builds and runs normally with OCR disabled
- CMake 3.21+
- C++20-capable compiler (MSVC 2022, GCC 12+, Clang 15+)

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Tests

A regression suite is built by default and run with `ctest`:

```bash
ctest --test-dir build --output-on-failure   # everything
ctest --test-dir build -R docx               # one domain only
```

Disable it with `-DMG_BUILD_TESTS=OFF`. The drivers are plain executables (no
test framework, no extra dependency) and cover the hand-written DOCX/ZIP parser,
the document model, path handling, gallery sorting/filtering and the
tag/category sidecar persistence.

The `tests/` directory is not part of the published repository. The build simply
skips it when it is absent, so a fresh clone configures and compiles without it.

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
- **Feature**: Major PDF editor improvements:
  - Lossless PDF editing and export with preserved text layers, fonts, vectors, and annotations.
  - Added text editing, text replacement, OCR support, PDF search, highlighting, underlining, strikeout, signatures, stamps, and form filling.
  - Added page management with reorder, rotation, insertion, and extraction while preserving document quality.
  - Added improved annotation support and compatibility with external PDF readers.

- **Feature**: Improved PDF text handling:
  - Better support for embedded fonts, CID fonts, non-ASCII text, and complex text layouts.
  - Added text wrapping and linked text boxes with automatic reflow.

- **Feature**: Improved PDF export reliability:
  - Safer redaction/export handling.
  - Preserves annotations and avoids unnecessary rasterization.
  - Automatically falls back when lossless editing is not possible.

- **Feature**: Added PDF page extraction improvements with drag & drop, reordering, and better workflow.

- **Change**: Improved DOCX editing:
  - Better formatting preservation.
  - Improved text editing performance and stability.

- **Change**: Improved application performance:
  - Faster folder loading and gallery browsing.
  - Reduced memory usage for large folders and documents.
  - Improved thumbnail generation and background processing.

- **Fix**: Improved stability:
  - Fixed PDF audio loading/closing issues.
  - Fixed data corruption risks during metadata saving.
  - Fixed various PDF editor, annotation, and document handling issues.

---

## Issues

- :o

## Planned

### DOCX-Editor
- **`Ctrl` + mouse wheel to pan the format toolbar sideways**, the same way the PDF editor's ribbon already does — so every control stays reachable when the window is only half a screen wide or the editor sits in a split-view pane.
- True pagination with a page-accurate view and page thumbnails.
- Editing support for tables, images, and headers/footers.
- Style templates (Formatvorlagen), footnotes, table of contents, multi-column layouts.
- Comments and tracked changes as an authoring tool.
- Spell checking. (later with other languages)

---

## Known limits (by design, not planned work)

Things the app deliberately does not do — listed so you know where the edges are, not as a to-do list.
- **Highlights cannot be dragged**: a highlight belongs to the text underneath it, so it can be recoloured and deleted but not moved. Every PDF reader behaves that way.
- **Signature fields (`/Sig`) are left alone**: they are neither shown nor filled in. Filling one means actually signing — certificates, byte-range digests, a CMS container, a trust story — which is cryptography rather than PDF work, and a field you could never complete is a promise without cover. For putting a signature *image* on a page, use the signature/stamp tool.
- **A form you filled in after reordering pages** comes out as a finished document rather than a form you can keep filling in: the rebuilt file cannot carry the form definition, so the values are drawn into the pages instead. Saving without changing the page order gives you a fully fillable form.
- **Lossless export is not always possible**: encrypted files, unusual font encodings or text a replacement cannot be expressed in fall back to the image-based export. That path always works, but the page becomes a picture — and you are told when it happens.

---

## License

MIT License. See `LICENSE` for details.
