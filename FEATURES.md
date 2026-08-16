# MediaGallery — Features

The complete list. `README.md` gives one sentence per area; this file has the
details, including every keyboard shortcut and where the app keeps its data.

Optional dependencies change what is available: without **ZLIB** the DOCX editor
is disabled, without **Tesseract** there is no OCR for scanned PDFs, and without
**Hunspell** plus a dictionary there is no spell checking. In each case the app
builds and runs normally and says why the feature is off.

---

## Media Formats
- **Images**: JPG, PNG, GIF, BMP, WebP, TIFF, HEIC, HEIF, AVIF, SVG, ICO, RAW (CR2, NEF, ARW, DNG)
- **Videos**: MP4, MKV, AVI, MOV, WMV, WebM, M4V, MPEG, 3GP, OGV, TS, M2TS, VOB, RMVB, ASF, DIVX
- **Audio**: MP3, FLAC, WAV, OGG, AAC, M4A, WMA, Opus, AIFF, APE, ALAC, MIDI and more
- **PDFs**: Full page rendering with thumbnail sidebar and media annotation support
- **Text files**: TXT, Markdown, source code (C++, Python, Rust, Go, JS/TS, …), configs, scripts, logs, CSV and more - editable directly in the app
- **Word documents**: DOCX - opened in a built-in, loss-preserving editor (see *DOCX Editor* below)
- **HTML**: Rendered live preview (via Qt WebEngine) alongside the editable source view

## Gallery & View
- **Grid view**: 1–25 columns, zoom with `Ctrl+Scroll` or `Shift+Scroll`
- **Scrolling the gallery**: mouse wheel (smoothly animated), the scrollbar, or the keyboard - `Up`/`Down` move half a tile row, `PageUp`/`PageDown` a screenful, `Home`/`End` jump to the start or end. The keys scroll the **view**; they do not move a selection. While you are typing in a text field the arrow keys stay with that field. Dragging on the gallery does **not** scroll it - a drag on a tile pulls the file out of the app instead
- **Tile size dialog**: Adjust tile width/height with a live drag-resize preview
- **Fullscreen gallery**: Prev/Next, Random mode, up to 10× zoom, pan with mouse
- **Image viewer & editor**: Hardware-accelerated QML viewer with PDF-style zoom/pan (fit-to-window, 100%, wheel-zoom, drag-pan) and a full non-destructive **Image Editor** - see the dedicated section below
- **Split view**: Open up to **4 files side by side** in the fullscreen view (like a 2-, 3-, or 4-player split screen: 2 = two columns, 3 = two on top + one full-width below, 4 = 2×2). Each pane is an independent viewer with its own header and its own PDF Editor / Image Editor / zoom / playback state; the arrow keys act on the focused pane. The **boundaries between panes can be dragged** to resize the tiles (a column divider for 2 panes, plus a row divider for 3/4 panes). A **"+" button** in each pane's header returns to the gallery and adds the next clicked file as another pane (no file dialog - you pick straight from the gallery). **Back / `Esc`** on a pane closes just that file and frees its memory immediately; closing the last pane returns to the gallery. Resizing the window or adding a pane keeps every pane on its current page/position.
- **Split view docking (drag & drop)**: Grab a pane by its **header bar** and drag it to rearrange the layout, with **visual drop zones and a live layout preview** (VS Code style). With 2 files you can switch between side-by-side and stacked (drop on an edge zone) or swap positions (drop on the other pane). With 3 files, dropping on an edge zone makes the dragged file the **large pane** on that side (the other two share the remaining half); dropping on a **corner zone** shrinks it back into that quadrant and the adjacent pane takes over the freed area; dropping on another pane swaps positions. With 4 files, dropping on another pane simply **swaps** the two (the 2×2 layout is fixed). Closing or adding panes keeps the arrangement as close as possible to what you set up; the custom layout lasts until the app is closed (not persisted).
- **Compact mode**: Options mode toggle with `Alt+S` - works in the gallery and inside the open media viewer
- **Cover mode**: Cover/uncover gallery with `B`
- **Drop a file on a tag or a category to assign it**: drag a gallery tile onto a **tag** in the right-hand panel - either in the tag section or under a category - and the file gets that tag; drop it on a **category header** and the file joins that category. Dropping only ever **adds** - it never removes an assignment that is already there. Dragging a tag chip onto a category header still moves the tag itself, as before. Files dragged in from outside the app are accepted too, as long as they are in the folder that is currently open (categories and tags are stored per folder)
- **Drag a file onto a bookmark to file it away**: while you drag a tile, a bar of your saved folders appears at the bottom of the window - drop the file on one and it goes there. By default it is **moved** (switchable to copying in Settings -> General). A move takes the file's **tags, its category and its custom date with it** into the target folder, and leaves nothing behind in the source folder; a copy arrives without them, so the original keeps its own. If the target folder already holds a file of that name, you are asked: replace, rename (`Name (2).ext`) or cancel - nothing is ever overwritten silently. A move can be taken back with `Ctrl+Z` like a deletion
- **Drag a file out of the app**: grab a gallery tile and drop it into another program - a file manager, a mail draft, a chat window or a file upload in the browser. The file itself is handed over (as any file manager would), one file per drag, and always as a **copy** - a drop target can never move your file out of its folder. Dragging starts only after a short movement, so clicking, tagging and the context menu are unaffected
- **Live folder watch**: New or deleted files are detected automatically
- **Folder bookmarks**: Save folders under a custom name for one-click access (Menu -> Folder, and Settings -> Bookmarks). "Add Folder" from the menu **pre-fills the path with the currently open folder** if it is not saved yet (comparison is case-sensitive with trailing separators normalized) - the path stays editable before confirming
- **Window title shows folder and file**: `MediaGallery - <folder> / <file>`; with several files open side by side it follows the active pane
- **The header bar comes back in fullscreen**: immersive fullscreen (`F`) hides the header as before, but moving the mouse to the **top edge** brings it back over the content - so the View menu and the back button stay reachable. The page does not jump: in fullscreen the bar overlays instead of pushing the content down
- **View menu in the header bar**: the header of an open file carries a **View** menu next to the back button - random mode, date/metadata, add a file beside this one, switch HTML source/preview, compact mode (`Alt+S`) and immersive fullscreen, each by name instead of only as an icon. The icons on the right keep the two shortcuts worth one click (add a file beside this one, random mode); the date button moved into the menu. The header itself is about 30 % slimmer than before, because the file name moved into the window title; renaming still happens in the header field, which now appears in compact mode (`Alt+S`) where renaming lives
- **Fullscreen transitions**: Slide or Fade page animation (Settings -> General)

## Text Editor
- Opens any supported text/source file in a monospace editor inside the fullscreen view
- **Save** button and `Ctrl+S` shortcut
- Unsaved-changes indicator (`*` in the filename label)
- **Auto-Save**: optional timer-based auto-save (configurable interval, Settings -> Text Editor)
- Confirmation dialog on navigation away from unsaved changes (Save / Discard / Cancel)
- Proper Arabic/CJK font fallback in the editor (no more missing-glyph "tofu" boxes)
- **Export as PDF** (`→ PDF` button next to Save): writes `<name>.pdf` **beside the source file**; the text file itself is never touched, and an existing PDF of that name is never overwritten (`<name> (2).pdf`, `(3)`, …)
  - What gets printed is **what stands in the editor**, including unsaved changes
  - Fixed layout: monospace 10 pt in **medium weight** (indentation, ASCII tables and columns keep their alignment), A4 portrait, 20 mm margins all round, footer carrying **only the page count** (`1/3`, centred) - no file name
  - **Choosable text colour**, with black as the default. A colour swatch sits left of the `→ PDF` button: what you pick there applies **to this file only** and is remembered in the folder's JSON sidecar next to its tags and date. Once a file has its own colour, a reset button appears **inside the same frame**, separated by a thin line, and drops it back to the default. The default itself is one setting for all files (Settings -> Text Editor -> *Text colour for text-to-PDF export*)
  - **The colour never follows the app theme.** It is written into the document, so a theme switch must not change it - before this was pinned down, a dark theme once printed near-white text onto white paper. An unusable stored value falls back to black rather than to something invisible. The medium weight is part of the same promise: at 10 pt a regular monospace is so thin that a full page reads grey on screen even though the ink is solid
  - Long lines **wrap** (never truncated); tabs are eight characters wide; CRLF and old Mac CR line endings are handled
  - Real text, not an image: the result can be selected, copied and searched (see *Known limitations* for pages made up of very short lines)
  - Runs in the background - the editor stays usable, and a short status message reports the written file or the error
- **Themeable editor backgrounds**: the TXT/code editor and the HTML source view each have their **own separate** background color, independent of each other and of the card/panel background (Settings -> Design)

## HTML Viewer
- **.html / .htm** files open in a rendered live preview by default, with a one-click toggle back to the editable source view
- Preview runs fully **offline**: JavaScript stays enabled (quizzes, search, shortcuts keep working) while remote network access is blocked - no external fonts or trackers load
- **Design-card thumbnails**: instead of showing raw source code, HTML thumbnails are auto-generated from the page's hero section (title, subtitle, colors, RTL/Arabic star patterns or gradients) and refresh automatically when the file changes
- **Lazy rendering engine**: Qt WebEngine now initializes only the first time you actually open an `.html`/`.htm` file - for a faster start and a lower memory baseline. Until then, HTML files fall back to the editable source view and the preview toggle appears once the engine is ready
- **Software rendering by design**: the preview runs without GPU acceleration. Local documents rasterize fast enough in software, it avoids a whole class of graphics-driver crashes and hangs, and it saves the memory of a separate GPU process. Advanced users can override it by setting `QTWEBENGINE_CHROMIUM_FLAGS` before starting the app
- **Clear failure handling**: if a page cannot be loaded or the render process dies, a readable hint replaces the blank area; links that would leave the local file are ignored
- **Smooth scrolling**: the rendered preview scrolls with animation (Chromium smooth scrolling), consistent with the web-style smooth wheel scrolling of the gallery, PDF, DOCX and text views

## Live Transliteration
- Type in Latin letters and get **Arabic (with full Harakat/diacritics)** or **Japanese (Hiragana/Katakana)** automatically as you type - no separate conversion step
- Works in the **text editor**, **HTML source view**, and **PDF Editor notes**
- Smart, unambiguous conversion: waits for the next keystroke whenever a shorter match could still extend into a longer one (e.g. holds `a` until it's clear whether `aa` follows)
- Supports the Arabic definite article (sun/moon letter assimilation), doubled consonants (auto-Shadda), and word-boundary handling
- Fully customizable mapping tables per script (Settings -> Text Editor), with add/edit/remove/reset controls
- Toggle button (with scheme picker) available directly in the editor toolbar and the PDF Editor toolbar

## Tags & Categories
- **Tags**: Per-folder, unlimited, freely named, color-coded
- **Categories**: Hierarchical tag categories with optional color inheritance
- **Unified side panel**: Tags and categories live in one panel with two equal sections - all tags as toggleable chips with a clearly visible active/inactive state, plus the full category tree below
- **Individual panel toggles**: The Filter popup has a merged "Tags & Categories" section where the Tag panel and the Category panel can be shown or hidden independently, each with a clearly visible on/off state
- **"+" buttons everywhere**: Create new tags and new categories directly from the panel headers, and - in options mode (`Alt+S`) - straight from a media tile, each "+" sitting right next to its corresponding button (new tags/categories are assigned to that file immediately)
- **Right-click context menu on tiles**: Assign existing tags or categories to a file directly from a submenu, with already-assigned entries checked, without opening a panel
- **Smart filter cascade**: Deselecting a category (or subcategory) automatically deactivates its dependent subcategories and tags - unless they are still needed by another active filter, in which case they stay active
- **Universal converter**: Convert in every direction between tags, subcategories, and top-level categories (Settings -> Converter) - pick the direction from a dropdown and the form adapts to it, and move categories anywhere in the tree
- **Search field**: a search box sits right next to the Filter button in the gallery. It filters **live on every keystroke** - no Enter, no button - and matches a substring of the **display name, the file name (including the extension) and the tags**, ignoring case. It searches the **open folder only** (no subfolders, no file contents) and is **combined with every other filter**, so it narrows down whatever the filters already show. The number of hits is shown next to the field, `Ctrl+F` jumps into it, `Esc` clears it and hands the keyboard back to the gallery
- **Filter modes**: OR, AND, ONLY, INCLUSIVE - combinable with media-type filter
- **Sorting**: Date, Name, Tags or File size, each ascending or descending. Sorting lives **inside the Filter button** as its own "Sorting" section - field and direction are both plain radio rows, and the section header shows the current setting at a glance ("File Size - Descending"). The filter counter on the button ignores sorting, since something is always sorted

## PDF Viewer
- Full multi-page rendering via Qt6 PDF, with a thumbnail sidebar for quick navigation
- Zoom in/out, fit-page, fit-width, single-page and continuous scrolling
- **Browser-style text selection**: click and drag across the embedded text layer, `Ctrl+C` to copy, `Ctrl+A` to select the page; the selection survives switching into edit mode
- **Search across the document** (`Ctrl+F`): matches light up on the page while you type, the bar shows "x of n", and ▲/▼ (or Enter) step through them and jump to the right page. Long documents stay responsive - pages are searched in small batches. Scanned pages you ran OCR on take part too, where the recognized line is the match
- **Embedded audio and video**: annotation playback (Sound, Screen, Movie subtypes) plus a side panel listing every audio clip on the current page with a seek slider; a sidecar audio file next to the PDF is picked up automatically

## PDF Page Extraction
- **Extract pages from an open PDF**: right-click any page -> **Extract page** (single) or **Extract multiple pages…** (page picker). The new PDF is written next to the source file.
- **Extract across the whole folder**: the **Extract** button in the filter bar (next to *+ Create*) collects **every PDF in the current folder** - pick pages from several files and get them merged into a **single** new PDF.
- **Workbench layout** (default): the picker is a three-panel workbench - a **PDF list on the left** (the active file is highlighted; a fully-selected file shows a check, a partially-selected one shows an *N / M* count), the **pages of the active PDF on the right**, and a **selection bar at the bottom** that appears once you pick a page. The bar shows each selected page as its own thumbnail; **its left-to-right order is the extraction order**. Reorder pages by **dragging them inside the bar**, remove one by **dragging it out of the bar** (or the small ×). Drag a page from the grid, or a whole PDF from the left list (adds all its pages), straight into the bar; a **+ / −** button on each PDF row toggles all its pages at once.
- **Compact layout** (optional, *Settings -> View*): a minimalist single grid of all pages with a selection count - for anyone who prefers the simpler, less busy dialog. Output stays in original page order here.
- **Lossless by default**: pages are copied at the PDF object level, so the **text layer, vector graphics, embedded fonts and annotations stay intact** - no rasterizing, no quality loss. If a source can't be copied that way (e.g. an encrypted file), just that file's pages fall back to a 150 dpi image page in the same output, so you always get a result.
- **Selection**: a plain **left click** selects/deselects a page - no modifier needed. Hold **Ctrl** and hover a page for a **large preview** (~80% of the dialog) that disappears as soon as you release Ctrl.
- **Scrolling** in the page grid uses the same smooth wheel behaviour as the main gallery.
- Page tiles use your gallery tile size; the selected-page highlight style (**frame** or **overlay**) is configurable under *Settings -> View*.
- **Order**: in the workbench the **selection bar defines the output order**; the compact layout always writes pages in original order. Names default to `<source> - Page N` / `<source>-Selected` (required for the folder-wide mode); `.pdf` is appended automatically and existing names get ` (1)`, ` (2)`, … appended instead of being overwritten.

## PDF Editor
Everything below is **non-destructive**: your original PDF is never modified. Notes live in a sidecar file (`<name>.mgedit.json`) next to it and stay editable across sessions; **Export** writes a new copy (`…_edited(.n).pdf`).

**Tools** (palette in the dockable panel): select/move · text note · freehand pen · arrow · rectangle · ellipse · replace text · edit text · highlight/underline/strikethrough · black out text · signature or stamp image.

**Notes and drawings**
- Sticky-note text boxes with full formatting: font family (with a hint when the system substitutes one), size, bold/italic/underline, alignment, vertical alignment, text and paper colour, opacity
- Drawings with stroke colour, line width in PDF points and (for shapes) fill colour
- Select, move, resize, copy/paste (`Ctrl+C`/`Ctrl+V`), delete, full undo/redo (`Ctrl+Z` / `Ctrl+Shift+Z`), and a visibility toggle (`Alt+Q`)
- **Cross-page dragging**: pull an annotation past the top or bottom edge and it moves to the neighbouring page
- **Line-snapping**: a new note anchors to a detected text line when you place it nearby, or floats freely elsewhere
- A new note or drawing **inherits the last-used style** (without the text)
- **Linked text boxes**: chain a box to a follow-up box (**🔗**) and text flows across the chain - the last box grows with its content, and editing anywhere re-flows the whole chain as one undo step

**Working with the text that is already in the page**
- **Replace text**: drag across the text (or select it and press **⇄**). The box snaps onto the detected line, adopts its font size and comes pre-filled with the embedded text, so you edit instead of retyping. On a scanned page without a text layer it stays usable as a blank patch
- **Edit text** (⌶): click into the page text and type - characters go into the PDF's own text layer, so the page stays vector and searchable. Arrow keys, `Home`/`End`, `Backspace`/`Delete` work as expected, continuous typing undoes as one step, and the paragraph re-flows as you type (gaining a line and pushing the content below down where that is provably safe)
- **Highlight, underline, strikethrough**: pick one of the three markers and drag across the text. The mark snaps to the text lines, so a sweep across three lines is **one** marker covering exactly those lines - recolour or delete it in one go. Highlights are translucent so the text stays readable
- **Black out text**: select the text and press the redaction button (or drag across it with the tool). You get an opaque bar and, on export, removal of the covered text from the document, so it can no longer be selected, copied or found - and the exported copy is written from scratch, so the text is not left behind in its raw bytes either. **The rest of the document stays a document.** Removal no longer depends on recognising the covered text as a string: what sits *under* the bar is cut out of the page geometrically, and the gap is closed so precisely that the rest of the line does not move by a hair. Only where that cannot honestly be done is the page turned into an image - if the bar sits over a **picture** (a scanned page: pixels cannot be cut out of a stream, a bar over them would be a mere cover), if the page draws its text through a form object the app cannot look into, or if the page is rotated. Before, a single redaction whose text could not be matched cost the **whole file** its text layer - a 40-page document with one blacked-out line became 40 images. Whatever happens, the app checks the finished file: if a single character were still standing under a bar, the export falls back to the image route and says so, so a black bar over still-selectable text cannot happen. What blacking out does and does not protect is explained once, the first time you use it, instead of filling every tooltip. On a page without a text layer the app says so rather than doing nothing. A selection that runs **over a line break** is handled too: the check that decides whether anything is left to remove compares both sides without whitespace, because the selection carries a line break that the page's raw glyphs do not - before that, such a redaction went out as vector with the text still readable under the bar
- **OCR for scanned PDFs**: the **OCR** button recognizes the page text (Tesseract, optional). Afterwards line-snapping, the *Replace text* pre-fill, selection and search work on that page as if it had a real text layer

**Forms, foreign annotations, images**
- **Fill in PDF forms (AcroForm)**: text fields, check boxes, radio groups and drop-down lists become visible and editable right on the page - in view mode as well as edit mode, because a form belongs to the document. Qt's PDF renderer does not draw form widgets at all, so they are drawn as an overlay that matches what gets written into the file. Values are buffered while you type and kept in the sidecar, so a half-filled form survives closing it; the **☑** button writes them into a new copy (`…_ausgefuellt(.n).pdf`) as an incremental update, with a proper appearance stream per field so the values show up in any reader and in print. Read-only fields stay visible but locked. If you reordered, rotated or removed pages, the copy follows what you see: the values are drawn into the pages first and the file is then rebuilt in your order - that copy is a finished document rather than a form you can go on filling in, and the message after saving says so
- **Annotations from other PDF readers** are picked up when you open a file: sticky notes, text boxes, rectangles, ellipses, lines, freehand drawings, highlights, underlines and strikeouts become normal editable notes. Each remembers which object it came from, so an untouched one is never exported twice and one you edited or deleted is removed from the exported copy instead of lingering underneath
- **Your own notes as real PDF annotations** (*Settings -> Editor -> PDF Editor*): instead of being drawn into the page they become annotation objects that stay selectable and deletable in other readers. Off by default - drawn notes look identical everywhere. *Replace text* patches and linked boxes are always drawn, and if a page holds one, everything on it is drawn (never a mixture)
- **Signature and stamp images**: place a PNG or JPEG anywhere on the page; it moves, fades and undoes like any other note and **always scales proportionally** - every handle, corner or edge, keeps the aspect ratio, so a signature can never be stretched out of shape. On export it is embedded into the PDF with its transparency intact, and the same file placed several times is embedded only once

**Pages and export**
- **Manage pages** (edit mode): the **"+" line** below a page inserts a blank A4 page; right-click for **Remove page**, **Rotate left/right** or **Insert pages from PDF…**; drag a thumbnail in the sidebar to reorder. Every one of them undoes as a single step, and notes travel and rotate with their page. Inserted pages are copied losslessly into a companion file, so they survive even if the file you took them from is gone. Choose **non-destructive** (default) or **destructive** in *Settings -> Editor (PDF)*
- **One Export button**; which path it takes is a setting (*Settings -> Editor -> PDF Editor -> Export*): **lossless when possible** (default) or **always as an image**. Lossless writes annotations as real vector content, keeps the original page content byte-for-byte and leaves untouched pages alone; for *Replace text* it rewrites the embedded text directly in the content stream, including text split across several show operators, non-ASCII text via the font's encoding, and CID/Type0 fonts via `/ToUnicode`. Notes in a font outside the standard 14 are embedded rather than silently substituted
- Where lossless is not provably safe the export falls back to the image path - always correct, and you are told when it happens
- The formatting panel docks as a **right sidebar** or a **Word-style ribbon** (*Settings -> Text Editor*); `Ctrl` + mouse wheel pans the ribbon sideways when the window is narrow

- **Track changes for your annotations**: a **Track changes** button in the header bar (edit mode only) opens a small menu with a **Record** switch. While recording, every note, drawing, highlight or redaction you add counts as an **open change**, and deleting one only *marks* it as deleted instead of removing it - so the deletion can still be taken back. Open changes are framed in the accent colour; ones marked for deletion stay visible but pale and struck through. The menu shows how many are open and offers **Accept all** / **Reject all**; a single one is decided by right-clicking it (*Accept change* / *Reject change*). Accepting keeps a new note and completes a deletion, rejecting does the opposite. Every decision is one undo step - and *Accept all* / *Reject all* is a single one, so one `Ctrl+Z` brings the whole batch back. **What counts as a change**: adding an annotation and deleting one. Editing an existing annotation (moving it, recolouring it, changing its text) stays a normal edit and is not tracked - tracking that would mean storing the state before every single change. The switch belongs to the document: it is kept in the sidecar and applies to that file the next time you open it. **The image editor has the same feature**, with the same button, wording and shortcuts - what you learn in one editor works in the other

## Image Editor
- Opens on any image via the **✎ Edit** button in the image viewer's toolbar - the original file is **never modified**
- **Tools**: Select/move, Text note, Freehand pen, Arrow, Rectangle, Ellipse (tool palette in the dockable panel)
- **Text notes** with the same post-it styling as the PDF Editor: font family, size, bold/italic/underline, horizontal/vertical alignment, text color and note-paper (highlight) color with an opacity slider, plus **live transliteration** (Arabic/Japanese) while typing
- **Drawings** with adjustable stroke color, line width and (for shapes) fill color
- **Select / move / resize / delete** any annotation, with full **undo/redo** history and a note-visibility toggle (`Alt+Q`)
- **Copy & paste** the selected annotation (`Ctrl+C` / `Ctrl+V` or the toolbar button) - duplicates it with all settings and text
- A newly created note/shape **inherits the last-used style** (only without the text)
- Notes are saved to a **sidecar file** `<image>.mgedit.json` next to the image (non-destructive) and stay editable across sessions
- **Export** writes a brand-new image copy `…_bearbeitet(.n).<ext>` with the annotations permanently rendered onto it (QImage + QPainter, WYSIWYG); the copy keeps the **source format** (JPG->JPG, PNG->PNG, otherwise PNG)
- Formatting panel can be docked as a **right sidebar** or a **Word-style ribbon** at the top (shares the PDF Editor's panel-position setting, Settings -> Text Editor)
- Fully decentralized: each split-view tile has its **own** independent image editor

## DOCX Editor
- Opens `.docx` files directly in the viewer/split view as an **editable, page-accurate document**
- **Loss-preserving by design**: only the XML nodes you actually touch are rewritten - untouched paragraphs, tables, images, headers/footers, styles and every other part of the file are carried over **byte-identically** (the document is never regenerated from scratch); a two-stage self-check on load refuses editing rather than risk silent data loss
- **Text editing**: type, delete, select (mouse/keyboard), split & merge paragraphs, line breaks (`Shift+Enter`), full **undo/redo** with keystroke coalescing
- **Character formatting**: font family, size, **bold/italic/underline**, text color - applied to the selection, or to the next typed text when nothing is selected (Word-style pending format)
- **Formatting-preserving clipboard**: copy/cut keeps font family, size, bold/italic/underline and color when pasting back into the editor (or into another DOCX tile). The clipboard also carries an **HTML** flavour, so pasting into Word, LibreOffice or a browser keeps the formatting too; plain text remains available for everything else
- **Word-style caret**: the text cursor is drawn in the size of the character that will be typed next - it follows a font-size change immediately, even in an empty line
- **Line keeps its own formatting**: deleting the last character of a line does *not* make it fall back to the paragraph style (e.g. the heading it inherited its properties from). The line keeps its formatting until you press Backspace again and the caret actually moves into the previous line
- **Paragraph formatting**: alignment (left/center/right/justify), spacing (line spacing plus space before/after, grouped behind one button), **bulleted & numbered lists** (numbering definitions are created and spliced into `numbering.xml` on save). Pressing Enter in an **empty list item ends the list** instead of adding another bullet - the Word behaviour
- **Style templates (Formatvorlagen)**: the document's own paragraph styles appear in a toolbar dropdown (the same set Word offers - hidden internal styles stay out of the way). Applying one writes a real `w:pStyle`, so headings keep working in Word; picking the default style removes it again, and direct formatting of the paragraph survives either way
- **Headings work in every document**: most .docx files define no heading style at all, which used to leave the dropdown with nothing but "Normal". *Heading 1-3* are always offered; the first time you apply one, its definition is written into the document's `styles.xml` with Word's built-in name, so it looks like a heading in the editor straight away *and* Word recognises it (navigation pane, table of contents)
- **True pagination**: the document is laid out on **real pages** taken from its own page setup (`w:sectPr` - page size, orientation, margins), separated by gaps on your theme background. A paragraph that crosses a page boundary is split **line by line**, exactly where Word breaks it. Because the line width is the page's text width rather than the window's, the page count in a narrow split pane is the same as in Word - a narrow pane scales the page down instead of re-wrapping it
- **Multi-column layout**: sections set to two or more columns (`w:cols`) are laid out and paginated in columns
- **Page thumbnails**: a sidebar shows every page as a live miniature with the current page highlighted; clicking one scrolls there. The miniatures are drawn on demand through the same painting path as the page view, so they cost no extra memory
- **Themed, compact toolbar**: every control follows the app theme; on narrow split panes the toolbar **scrolls horizontally with the mouse wheel** - plain wheel, `Shift`+wheel or `Ctrl`+wheel, the same grip the PDF editor's ribbon offers - so nothing is cut off
- **Insert a table of contents** from the toolbar: it lists the document's headings with a dot leader and the page number from the editor's own pagination, and updates itself when a heading changes. The file keeps a declarative `TOC` field with no page numbers baked in, so Word recalculates them. It sits on **its own page** - the text after it starts on the next one, and a contents list that outgrows a page simply takes the following one, which also carries nothing else (Word gets the same layout through `w:pageBreakBefore`). Typing inside it is not possible; **font and font size** are the two things you can set
- **Wrap text around a picture**: the picture's right-click menu switches between *In line with text* (the default: the picture is part of the line) and *Wrap text around* (the text flows beside it over its whole height). The latter is written as a real Word floating picture, so it looks the same in Word. A wrapping picture can be **dragged anywhere on the page** - grab it and move it, the text re-flows around its new place, and dropping it over a **different paragraph re-anchors it there** (as Word does), so that paragraph's text wraps around it instead of starting below it - moving and re-anchoring are a single undo step, and the same menu picks the **side the text runs on** (both sides / wider side / left only / right only). With *both sides* - Word's own default - a line is split into a piece left and a piece right of the picture, just like in Word; if one side has no room for a readable column, that line falls back to the wider side. If there is no room for a readable column beside it, the text starts **below** the picture instead of squeezing through a sliver.
- **Insert a signature or stamp**: the signature button offers the pictures in the document's folder (or a file dialog) and places the chosen one as a *free-floating* picture - selected right away, so you can drag it wherever it belongs. It is written as an ordinary anchored Word picture, so Word can move it too.
- **Insert a table** (rows/columns from a small popup) or **an image** straight from the toolbar. The image button first offers the pictures **in the document's own folder** as thumbnails - every format Qt can read - with the file dialog one click away; `Ctrl+V` also pastes an image straight from the clipboard
- **Insert PDF pages as pictures**: picking a PDF in that popup opens the **same page-selection screen as *Extract pages*** - every PDF of the folder on the left, the page grid of the one you clicked on the right, `Ctrl`+hover for a large preview, and a bottom bar that holds the chosen pages in the order you want them inserted
- **Text runs beside a table** when there is room for it: a table narrow enough to leave a readable column, and short enough to fit on its page, steps out of the flow just like a wrapped picture - the text that follows starts *next to* it and continues below once it outgrows it. Wide tables and tables that span pages keep the old behaviour. To deliberately continue *below* such a table, the right-click menu offers **Continue below the table** - that writes Word's "text wrapping, clear all" break, so Word shows it the same way.
- **Table context menu** (right-click a cell): insert or delete rows and columns, set the **column widths in millimetres**, or delete the whole table - each as a single undo step. The column total stays constant when you add or remove a column, the way Word does it. A table with **merged cells** is deliberately left untouched (the menu then only offers "Delete table") rather than risk tearing the grid apart
- **The selection frame follows a table across pages**: a table that is continued on the next page gets **one frame piece per page**, each around exactly the rows shown there, so the frame always sits on the grid rather than floating above it. Its handles resize the table **horizontally** - a table's height comes from what is in its cells, so no handle pretends otherwise.
- **Delete a whole table from the keyboard**: click its selection frame (the accent-coloured border around the table) to select it as an object, then `Delete` or `Backspace`. Selecting cells by dragging deliberately does *not* do this - it only clears their text, so a stray drag cannot destroy a table
- **Copy, cut and paste a whole table** (`Ctrl+C` / `Ctrl+X` / `Ctrl+V` with the cursor in a cell and nothing selected, or from the right-click menu): the table is transferred as its *form* - rows, columns, column widths and cell contents with their formatting - and rebuilt in the target document, so it also works between two files. Cut + paste is how you move a table. Other programs receive the cells as tab-separated lines
- **Resize a picture**: click it to select, then drag any of the **eight handles** - corners keep the aspect ratio, edge midpoints may stretch - or type width and height in millimetres via the context menu. Only the size is rewritten, so crops, effects, alt text and wrapping of a Word-authored picture survive untouched. **Copying a picture keeps that size** - pasting it back inserts it exactly as large as it was, not at its full pixel resolution, while other programs still get the plain image
- **Tracked changes from Word are shown and can be resolved**: insertions underlined, deletions struck through, one colour per author. A strip above the page says how many there are and who made them, and offers **Accept all** / **Reject all**; right-click resolves a single one. Each is a single undo step, and markup you do not touch stays byte-identical. Recording your own changes is deliberately not offered.
- **Tables are editable**: they are laid out as real grids (rows, columns including `w:gridSpan`, the document's own column widths) and you can click into a cell and type, with the full undo, formatting and find & replace you have everywhere else. The table has its **true height**, so every page break *after* it lands where Word puts it, and a table longer than a page is **continued on the next one** row by row instead of being pushed along as one block. Structure is protected: Backspace or Delete at a cell edge will not merge cells away, and a table you did not touch stays **byte-identical** in the file
- **Pictures sit in the text, like in Word**: a picture is inserted **where the cursor is** and is laid out as part of the line, at the size the document asks for (`wp:extent`), scaled down to fit the text width. Insert two pictures in a row and they stand **side by side**; whatever you type after them appears **next to them** as long as there is room, and moves below once there is not. The same holds **inside table cells** (the picture is fitted to the cell and the row grows to match) and in the **PDF export**. Click a picture to select it - handles, size, copy and delete all act on that one picture. The decoded image is only kept while it is near the visible area, and the picture's own bytes are never touched
- A **table nested inside a cell** is shown as a placeholder (not interpreted) and stays fully intact; other complex blocks remain placeholders too, and embedded objects (images, fields, hyperlinks) are protected as atomic units
- **Live transliteration** (Arabic/Japanese) while typing, sharing the app-wide schemes
- **Spell checking**: unknown words get a red wavy underline; right-click one for suggestions or "Ignore word". Marking only - nothing is replaced without you choosing it. Needs a Hunspell dictionary and is off until you switch it on
- **Find & Replace** (`Ctrl+F`): a themed search bar with next/previous match, match-case toggle, single replace and replace-all. Replace-all is one undo step; the search wraps around and skips tables and other non-text blocks
- **Two save modes** (Settings -> Text Editor): **Save directly** to the original file (a one-time `.bak` backup per session is created next to it) or **Export a copy** `<name>_edited(.n).docx` leaving the original untouched; `Ctrl+S` and auto-save on leaving the tile follow the chosen mode
- **Export to PDF** (**-> PDF** button in the toolbar): writes a PDF next to the document (`<name>.pdf`, collision-suffixed) in the page size the document asks for; the original `.docx` is kept. The export **draws the very pages the editor shows**, so the PDF has exactly the same page count and the same line breaks as the view. Its text stays selectable and searchable, and each page carries only its own lines - a paragraph running over a page boundary is not repeated in the neighbouring page's text
- **Create new Word documents** via the gallery's "+" button (empty A4 document, standard margins)
- Gallery **thumbnails** show the first paragraphs of the document; `.docx` files appear under the Text filter
- Fully decentralized: each split-view tile has its **own** independent DOCX editor

## Full Color Customization (Settings -> Design)
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
- **Themed icons**: every icon in the interface (toolbars, panels, filter bar) is drawn by the app itself from geometric shapes - there are no icon image files. Icons take the theme's text colour as a live binding, so a colour change repaints them instantly instead of reloading images, and they stay sharp at any interface scaling (100 %, 125 %, 150 %, 200 %) because edges snap to whole device pixels
- **Themed standard controls**: buttons, checkboxes, radio buttons, combo boxes, spin boxes, text fields, sliders, scrollbars, tooltips, **menus** and dialogs are drawn by the app's own control style - rounded corners, accent-colored checked states, consistent hover/pressed animations. This includes **every menu**: the menu bar, right-click menus on tiles, PDF pages and tags, and even the editing menu Qt itself opens in a text field. They follow the selected theme (including custom colors) instead of the desktop color scheme, so the app looks identical on every platform and under every desktop theme

## Companion Files
- **The app's own files stay out of the way**: the folder file holding tags and categories, the editors' notes (`<file>.mgedit.json`) and DOCX backups (`.bak`) are **hidden by default** - in the gallery and in the app's own file chooser
- **Show all files** (Settings -> View -> *Files*): shows them anyway - and then really everything, including file types the app does not recognise. Nothing is deleted or moved by this switch; it only changes what you see
- **Delete a companion file without touching the file itself**: right-click a tile -> *Delete notes and drawings* or *Delete previous version*. The entries appear only when such a file actually exists, ask before deleting, and the deletion goes to the system trash - `Ctrl+Z` brings it back like any other file operation
- **File menu in the header bar**: next to *View*, a **File** menu appears for files that have an editor (PDF, images). It carries *Delete notes and drawings*, which discards every note, drawing, highlight and redaction of that file in one undo step - the same action the page context menu offers in the PDF, now reachable for images too
- **Inside an open PDF**: right-click a page -> *Delete notes and drawings* discards every note, drawing, highlight and redaction of that file in one undo step. Deliberately not a file deletion: the editor still holds the notes in memory and would write them back on the next save

## Metadata & File Management
- **Date editor**: Custom date per file, persisted in JSON
- **Delete file**: Red delete button in fullscreen view, plus a right-click "Delete file…" entry on gallery tiles - both with a confirmation dialog; the file goes to the system trash and its metadata/sidecar are cleaned up automatically
- **Undo a deletion** (`Ctrl+Z` in the gallery, `Ctrl+Shift+Z` to delete again): the file comes back out of the trash to exactly where it was, **and so do its tags, its category membership and its custom date**. The stack belongs to the open folder and to the running session - switching folders clears it. If the system offered no trash (the file was deleted for good), no undo is offered rather than a promise that cannot be kept, and an undo never overwrites a file that has meanwhile taken that place again. While you are typing in a field, `Ctrl+Z` belongs to the text
- **Create file**: A "+ Create" button in the filter bar creates an empty PDF, HTML, or text file directly in the current folder (PDF starts as one blank A4 page, ready to annotate)
- **Rename**: Also renames the file on disk. In the fullscreen view the filename in the header is editable **only in options mode (`Alt+S`)** - outside it, the header acts as the drag handle for split-view docking
- **Drag & Drop**: Drop a folder or individual media files onto the window
- **JSON storage**: `<FolderName>.json` stored directly in the target folder

## Playback & UI
- **Video playback**: Native (Qt Multimedia) or external player
- **Audio thumbnails**: Styled previews with waveform decoration and format badge
- **Text thumbnails**: First few lines of the file rendered in monospace with extension badge
- **HTML thumbnails**: Auto-generated design cards instead of raw source code (see HTML Viewer)
- **Language**: English / German - switchable at runtime (Settings -> General)
- **Audio player accent**: Theme color or Apple Blue for the PDF audio mini-player (Settings -> General)
- **Mono-Play**: Only one audio/video playback at a time (enabled by default) - starting playback in another split-view pane automatically **pauses** the one already playing (position is kept). Disable it in Settings -> General to allow parallel playback
- **Graphics backend**: Vulkan, OpenGL, or Software rendering, with an automatic crash-guard that now **degrades gracefully** (Vulkan/D3D11/Metal -> OpenGL -> Software) if a backend fails to start, a Vulkan loader pre-check, validation of stale/foreign config values, and a runtime guard that switches to a safer backend on the next start after a GPU device-loss (Settings -> General)
- **Keyboard-shortcut overview**: a themed, grouped cheat-sheet in **Settings -> General** lists every shortcut with its key combination and function, sorted by context (gallery, media viewer, PDF/image/DOCX/text editor)
- **Context-correct, language-independent shortcuts**: each shortcut only fires in the surface it belongs to - in split view only the **active pane** reacts, so the same key (e.g. `Ctrl+C`, `Alt+Q`) is never ambiguous across panes; the top menu no longer generates `Alt`+letter accelerators that clashed with app shortcuts, and every shortcut behaves the same regardless of the interface language
- **Smooth wheel scrolling in Settings**: the settings pages scroll ~45 % of the visible height per wheel notch with a short eased animation - the same behavior as the gallery and the PDF page grid
- **The app's own file and folder chooser**: every *Open folder*, *Choose file* and *Save as* now opens a themed chooser **inside the window** instead of Qt's separate dialog - the same colors as the rest of the app, the same **animated wheel scrolling**, a places sidebar (Home, Documents, Pictures, ...), a clickable breadcrumb path, file size and date, a name filter, *Show hidden*, and a Save button that says **Overwrite** when the target file already exists
- **Dedicated fullscreen view**: opening a file hides the application menu bar - only the viewer and its own header are visible, and the freed space goes to the content
- **True fullscreen (`F`)**: in the media viewer, `F` also drops the window decoration and the viewer's own header - just the medium (and, for video/audio, the progress bar). `F` again or `Esc` brings everything back; leaving the file does too. If the window was **already fullscreen** (via your window manager), `F` leaves the window alone and only hides the chrome - you keep your window state. From a windowed or maximized window it switches to real fullscreen and returns to exactly that state afterwards. In fullscreen the arrow keys keep browsing images, but **seek** a video or audio track
- **Keyboard-only file navigation**: the floating Prev/Next buttons at the bottom of the viewer are gone - `->` and `<-` move to the next/previous item, in every view mode
- **Seek step**: how far `->` / `<-` skip a video in fullscreen, adjustable from 1 to 600 seconds (Settings -> General, default 15 s)
- **Themes**: Fully customizable - every color, every surface (Settings -> Design)

---


---

## Keyboard Shortcuts

| Action | Shortcut |
|--------|----------|
| Open folder | `Ctrl+O` |
| Jump to the search field (gallery) | `Ctrl+F` |
| Undo / redo a file deletion (gallery) | `Ctrl+Z` / `Ctrl+Shift+Z` |
| Reload / refresh thumbnails | `F5` / `R` |
| Toggle options mode (gallery & media viewer) | `Alt+S` |
| Toggle cover mode | `B` |
| Open fullscreen view | Double-click |
| Next item | `->` |
| Previous item | `<-` |
| Toggle fullscreen (media viewer) | `F` |
| Leave fullscreen (before closing the file) | `Esc` |
| Fullscreen + video/audio: seek forward / back | `->` / `<-` |
| Back to gallery | `Esc` |
| Back to gallery (from any viewer) | `Alt+←` |
| Save text file | `Ctrl+S` |
| Edit date (fullscreen) | `D` |
| Open date editor | Calendar button (fullscreen) |
| Delete file | Delete button (fullscreen) |
| Image: zoom in / out | Mouse wheel · toolbar `+` / `-` |
| Image: fit to window / 100% | Toolbar buttons |
| Image / PDF: pan when zoomed | Left-drag (PDF: on non-text areas) |
| PDF: zoom in | `+` |
| PDF: zoom out | `-` |
| PDF: previous page | `<-` |
| PDF: next page | `->` |
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

---

## Configuration & Data

All settings are stored via `QSettings` (platform-native).

Per-folder metadata (tags, dates, categories, per-file PDF text colour) is stored as JSON alongside the media:
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
