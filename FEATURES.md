# MediaGallery - Features

Everything the app can do, including keyboard shortcuts and where it keeps its
data. [README.md](README.md) gives one sentence per area; the limits of each
feature are in [LIMITATIONS.md](LIMITATIONS.md).

Some features need optional libraries: **ZLIB** for the DOCX editor,
**Tesseract** for text recognition in scanned PDFs, **Hunspell** plus a
dictionary for spell checking. Without them the app runs normally and says why a
feature is off.

---

## Media Formats
- **Images**: JPG, PNG, GIF, BMP, WebP, TIFF, HEIC, HEIF, AVIF, SVG, ICO, RAW (CR2, NEF, ARW, DNG)
- **Videos**: MP4, MKV, AVI, MOV, WMV, WebM, M4V, MPEG, 3GP, OGV, TS, M2TS, VOB, RMVB, ASF, DIVX
- **Audio**: MP3, FLAC, WAV, OGG, AAC, M4A/M4B, WMA, Opus, AIFF, APE, ALAC, MP2, AC-3, E-AC-3, DTS, MIDI and more
- **PDF**: pages with a thumbnail sidebar, including embedded audio and video
- **Text files**: plain text, Markdown, source code, configuration files, logs - all editable in the app
- **Tables**: CSV and TSV, plus DATEV booking batches
- **Word documents**: DOCX, in a built-in editor
- **HTML**: live preview next to the editable source

A `.ts` file can be TypeScript or a video; the app checks the first bytes to tell
which. Files without an extension such as `LICENSE`, `README`, `Makefile` or
`Dockerfile` open as text.

---

## Gallery & View

**Layout**
- **Tiles or list** (*Settings ▸ View*): a grid of tiles, or one row per file with small artwork, name, tag dots and file type
- **Size**: `Ctrl` with `+`/`-` or the mouse wheel changes the tile size (1 to 25 columns) or, in the list, the row height. Changes in the settings apply immediately
- **Previews**: images, PDFs and text files show their content on the tile; text files in the editor's syntax colours. Each kind can be switched off in *Settings ▸ View* to save memory - the tile then shows the file type
- **Scrolling**: smooth mouse wheel, scrollbar, or keys (`Up`/`Down`, `Page Up`/`Page Down`, `Home`/`End`)
- **Compact mode** (`Alt+S`) and **cover mode** (`B`)

**Folders**
- **Subfolders appear as tiles**, always first. A click opens a folder in place, right below its tile, as deep as you like; a double click makes it the main level, and `Alt+<-` takes you back
- An opened folder has a lighter background, indented contents and its own header with *Open folder*, *Create*, *New folder* and *Extract*
- Each folder tile shows how many media are inside
- **Search and filters include subfolders** below the open folder; folders with hits open by themselves and close again when you clear the search
- **Live folder watch**: new or deleted files appear automatically
- **Bookmarks** (*Folder* menu, *Settings ▸ Bookmarks*): save folders under a name, sorted into groups and subgroups. Groups fold open and closed in the menu, and can be rearranged by drag and drop in the settings

**Selecting and moving files**
- **Select like in a file manager**: `Ctrl`+click, `Shift`+click, drag a rectangle over empty space, `Ctrl+A` for everything the filter shows, `Esc` to clear
- Everything then works on the whole selection: dragging, tagging from the right-click menu, and deleting - which is one undo step
- **Copy and paste** with `Ctrl+C` / `Ctrl+V`, also between the app and your file manager
- **Drag files onto a folder tile, a bookmark or the other half of the window** to move them there (or copy - *Settings ▸ General*). Tags, category and date move along. Name collisions ask: replace, rename or cancel. `Ctrl+Z` undoes it
- While you drag, a bar with your bookmarks and the folders currently in the gallery appears at the bottom, and the pointer at the top or bottom edge scrolls
- **Drag files out** into other programs - always as a copy
- **Drop files from outside** into the folder under the pointer
- **Delete** with the `Delete` key or the right-click menu. Files go to the trash; `Ctrl+Z` brings them back together with their tags, category and date. `Enter` confirms, `Esc` cancels

**Viewing files**
- **Fullscreen view** by double click; `->` / `<-` move to the next or previous file, random mode picks one at random
- **True fullscreen** (`F`): hides the window frame and all bars. Moving the mouse to the top edge brings the header back
- **Document menu** in the header of an open file: date and metadata, source/preview or table/raw file, and the actions of that file type
- The window title shows folder and file
- **Split view**: up to four files side by side in one window. Drag the dividers to resize, drag a header to rearrange the layout, `Esc` closes one file
- **Two galleries side by side** (*View ▸ Split window*): each half has its own folder and filters, with a draggable divider. The same folder may be open in both. Each half opens up to two files. *View ▸ Swap panes* or dragging a half's bar swaps them; both halves come back on the next start
- **Page animation**: slide or fade (*Settings ▸ General*)

---

## Text Editor
- **Saves by itself**: when you leave the file, switch to another, and at an adjustable interval. `Ctrl+S` works too; a dot in the status bar shows unsaved changes
- **Syntax colouring for 27 languages**, including C/C++, Python, Java, JavaScript/TypeScript, C#, Go, Rust, PHP, Swift, Kotlin, shell, Ruby, Lua, CMake, YAML, SQL, HTML/XML, CSS, JSON, INI/TOML, QML and Markdown. HTML also colours the CSS and JavaScript inside it
- **Folding**: collapse functions, blocks, sections or headings from a bar next to the line numbers. The file itself is not changed
- **Find and replace** (`Ctrl+F`): hit counter, match case, whole words, all hits highlighted; *Replace all* is one undo step
- **Line numbers, current-line highlight, indent guides, bracket matching** (an unmatched bracket turns red)
- **Overview column** on the right: the whole file in miniature, click or drag to scroll
- **Line wrap** on screen only (*Settings ▸ Editor*); a wrapped line keeps one line number
- **Tab width 2 to 8**; the Tab key inserts spaces by default
- **Status bar**: line, column, language, encoding
- **Own colour themes**, separate from the app theme: Nightfall, Paper, Ember and Custom, saveable as JSON
- **Arabic and Japanese text** display correctly thanks to font fallback
- **Export as PDF** (*Document ▸ Save as PDF*): written next to the file, never overwriting an existing PDF
  - A4, 10 pt monospace, 20 mm margins, page count at the bottom; long lines wrap, indentation stays
  - *One colour* (default): your chosen text colour on white. The colour can be set per file
  - *Like the editor*: background and syntax colours of the editor theme
  - The text in the PDF can be selected and searched
- Files over 8 MB open read-only (see [LIMITATIONS.md](LIMITATIONS.md))

---

## HTML Viewer
- `.html` / `.htm` open as a **rendered preview**, one click away from the editable source
- Runs **offline**: JavaScript works, but nothing is loaded from the internet
- Tiles show a **design card** built from the page's title and colours
- The web engine starts only when you first open an HTML file, which keeps start-up fast and memory low
- Renders without the graphics card, which avoids driver problems; `QTWEBENGINE_CHROMIUM_FLAGS` overrides this
- A page that fails to load shows a readable message

---

## CSV and TSV Files

**Reading**
- **Opens as a table**; the raw file is one click away (table button or *Document* menu)
- **Separator and header row are detected** (`;` `,` tab `|`); the footer says what was found
- **Several tables in one file** (separated by blank lines) get one tab each, with an *All* tab showing the whole file
- **Row and column numbers** can be switched on in the top bar
- **Encoding**: UTF-8 or Windows-1252, detected automatically
- Problems such as unclosed quotes are named in the footer with their line number
- **Open `.txt` as a table too** (*Settings ▸ View ▸ Files*, off by default) - only if the file really has columns

**Search, sort, filter**
- **`Ctrl+F`** searches the table and shows the hit count. `Aa` matches case, *Cell* only finds exact cell contents. In a file with several tables, the bar names the other tables with hits
- **Click a heading to sort**: ascending, descending, back to file order. Numbers sort as numbers, dates by calendar (`DD.MM.YYYY`, `YYYY-MM-DD` and `DD/MM/YYYY` or `MM/DD/YYYY` as set in the settings)
- **Filter** (button right of *Document*): pick a column or all columns, then type text - or a comparison:
  - numbers: `>200`, `<200`, `>=200`, `<=200`, `100->200`
  - dates: `2025-01-01+` (from that day), `2025-01-01-` (up to that day), `2025-01->2025-04` (January to end of April); a year or month alone counts as a whole
  - the row numbers stay those of the file; the drawn X clears the filter
- **Hide columns** and **freeze the first column** from the heading's right-click menu
- Sorting and filtering never change the file

**Editing**
- **A click marks a cell** (for showing someone something); **double click or `F2` edits it**. `Enter` confirms and moves down, `Tab` moves right, `Esc` cancels
- **Insert and delete rows and columns**, rename columns - from the right-click menu
- **Paste a block** from a spreadsheet with `Ctrl+V`; copy a cell with `Ctrl+C`, a row with `Ctrl+Shift+C`
- **Undo and redo** with `Ctrl+Z` / `Ctrl+Y`, also after saving
- **Saves like the text editor**; `Ctrl+S` confirms with a short *Saved*
- **Only changed lines change in the file** - quoting, line endings and encoding stay as they were. Values are never reformatted (`1,00` stays `1,00`)
- **A file changed elsewhere is never overwritten**: you can save a copy (`<name>_edited.<ext>`) or reload

---

## DATEV Files (booking batches)
- **Recognised by content** (`"EXTF";` / `"DTVF";` at the start), whether named `.csv` or `.txt`
- **Header summary** on top (format, version, creation time); all header fields fold out below
- **Bookings as a table**, showing only the columns that contain data; one click shows all 125
- **Totals in the footer**: number of bookings, debit, credit and difference - red when they do not balance. With a filter on, the totals cover only the filtered bookings
- **Search, sort, filter, hide columns and copy** work as in the CSV view
- Works with reduced exports too, as columns are found by their names
- **Read-only**: the app never writes into a bookkeeping file

---

## Search
Every search box - gallery, text editor, PDF, DOCX - follows the same rule:
- **What you type is always searched as plain text.**
- **If it also works as a pattern (regular expression), it is searched as a pattern too**, and both results are combined. `\d{4}` finds every four-digit number *and* the literal text `\d{4}`
- A half-typed pattern is not an error; the plain search simply continues
- **Replacing always inserts plain text**
- *Settings ▸ General* lists the pattern characters with examples

Most useful: `.` any character · `\d` a digit · `[abc]` one of these · `*` any number
of times · `{4}` exactly four times · `^` line start · `$` line end · `a|b` either ·
`\.` a real dot.

---

## Tags & Categories
- **Tags** per folder, freely named, with colours
- **Categories** in a tree, holding tags; colours can be passed down
- **Side panel** with all tags as chips and the category tree, each with its own search field
- **Gallery search** next to the *Filter* button: filters by name, file name and tags as you type
- **Filter modes**: OR, AND, ONLY, INCLUSIVE, combined with the media type
- **Sorting** (inside the *Filter* button): date, name, tags or file size
- **Assign tags** from a file's right-click menu, with *+ New…* to create one on the spot
- **Drag and drop**: drop files on a tag or category to assign them; drag tags between categories; in compact mode (`Alt+S`) drag categories into each other
- **Group mode and Add-to-tag mode** (right-click a tag): tag many files quickly with a click each, then *Done*
- **Converter** (*Settings ▸ Converter*): turn tags into categories and back - the files come along
- **Deleting a tag** also removes it in all subfolders (switchable in *Settings ▸ Tags*)
- **Undo and redo** in a bar at the bottom of the panel, covering every tag and category change (up to 20 steps). Each side shows a short mark of what the button would do, for example `T:holiday` for a tag or `+3 T:holiday` for an assignment to three files, coloured green (added), red (deleted), blue (moved) or yellow (renamed). Hover for the full text
- Each half of a split window shows the tags of its own folder

---

## PDF Viewer
- Pages with a thumbnail sidebar; zoom, fit page, fit width
- **Select and copy text** like in a browser
- **Search** (`Ctrl+F`) with hits highlighted on the page
- **Embedded audio and video** play in place; a panel lists the page's audio clips

## PDF Page Extraction
- **From an open PDF**: right-click a page ▸ *Extract page* or *Extract multiple pages…*
- **From the whole folder**: the *Extract* button collects all PDFs, and you pick pages from several files into one new PDF
- **Workbench**: PDF list on the left, pages on the right, and a bar at the bottom whose order is the order of the new file - drag to reorder, drag out to remove. `Ctrl` + hover shows a large preview
- **Lossless**: pages are copied as they are - text, fonts and vector graphics stay intact. Only files that cannot be copied (for example encrypted ones) become image pages
- Existing files are never overwritten

## PDF Editor
Notes, drawings, highlights, redactions and form values are stored in a small
file next to the PDF (`<name>.mgedit.json`) and stay editable. **Export** writes a
new copy; the original is not touched.

**Page changes are the exception**: moving, rotating, removing and inserting
pages change the PDF directly. `Ctrl+Z` undoes them while the file is open.

- **Notes and drawings**: text boxes with full formatting, pen, arrow, rectangle, ellipse. Move, resize, copy, delete, undo. `Alt+Q` hides them all
- New notes take over the last style; notes can be dragged across pages and snap to text lines
- **Linked text boxes**: text flows from one box into the next
- **Replace text**: a box snaps onto a line and comes pre-filled with the existing text
- **Edit text**: type directly into the page's own text - it stays sharp and searchable
- **Highlight, underline, strike through**: drag across the text; the mark follows the lines
- **Black out text**: the covered text is really removed from the exported file, not just hidden. The app checks the result; where removal is impossible (for example over images), the page is exported as an image
- **Make a scanned PDF searchable** (*Document* menu, needs Tesseract): recognised words are written into the file as invisible text, so it can be searched and copied in any PDF reader
- **Fill in forms**: text fields, check boxes, radio buttons and lists; values are saved into a new copy
- **Annotations from other programs** become editable notes
- **Signatures and stamps**: place a PNG or JPEG; it always keeps its proportions
- **Pages**: insert blank pages, insert pages from another PDF, rotate, remove, reorder by dragging thumbnails
- **Export**: lossless where possible (the original page content stays unchanged), otherwise as images - a setting. You are told when the image route is taken
- **Tracked changes**: with *Record* on, new and deleted annotations count as changes you can accept or reject, one by one or all at once
- The formatting panel sits on the right or as a ribbon at the top

## Image Editor
- Opens from the **Edit** button in the image viewer; the original is never changed
- **Text notes and drawings** with the same tools and formatting as the PDF editor, including tracked changes
- Notes are saved next to the image (`<image>.mgedit.json`)
- **Export** writes a new image with the notes drawn in, in the same format (JPG stays JPG, PNG stays PNG)

## DOCX Editor
- **Opens Word files as real pages**, with the page size, margins and columns of the document. Page breaks fall where Word puts them
- **Changes only what you edit**; everything else in the file stays byte for byte. If the file cannot be read safely, editing is refused
- **Text**: typing, selection, paragraphs, line breaks (`Shift+Enter`), undo and redo
- **Formatting**: font, size, bold, italic, underline, colour, alignment, spacing, bulleted and numbered lists
- **Styles**: the document's own paragraph styles; *Heading 1-3* are always available and Word recognises them
- **Copy and paste keep formatting**, also into Word, LibreOffice or a browser
- **Tables**: insert, type in cells, add or remove rows and columns, set column widths in millimetres, copy, cut and paste whole tables. Long tables continue on the next page; text can run beside narrow tables
- **Pictures**: insert from the document's folder, from a file or with `Ctrl+V`; resize with handles or in millimetres; place in line or with text wrapped around, and drag freely
- **Signatures and stamps** as freely placed pictures
- **PDF pages as pictures**, chosen in the same screen as page extraction
- **Table of contents** from the headings, on its own page; Word updates the page numbers
- **Page thumbnails** in a sidebar
- **Margin rulers** at the top and right: drag to change the margins; Word shows the same. The reset button restores 2.5 cm
- **Tracked changes from Word** are shown and can be accepted or rejected
- **Spell checking** with suggestions (needs Hunspell); corrected words lose their underline by themselves. Underlines never appear in the PDF
- **Find and replace** (`Ctrl+F`)
- **Saving**: directly into the file (automatically, with a temporary `.bak` backup), or as a copy `<name>_edited.docx` - a setting
- **Export to PDF**: exactly the pages the editor shows, with selectable text. An optional page number can sit left, centre or right
- **New Word documents** from the gallery's **+** button
- Tables inside table cells are shown as placeholders and kept unchanged

---

## Live Transliteration
- Type Latin letters and get **Arabic (with vowel marks)** or **Japanese (Hiragana/Katakana)** as you type
- Works in the text editor, the HTML source and PDF notes
- Handles the Arabic article, doubled consonants and word endings
- The mapping tables can be edited in *Settings ▸ Editor*

---

## Audio Player Mode (`Alt+A`)
- **Turns a gallery half into a music player**: only playable files are shown (videos too, if switched on in *Settings ▸ Audio*). Leaving the mode restores your filter
- **Player bar** with previous, play/pause, next, progress, shuffle, repeat, equalizer and volume
- **Full player view** (double click): large controls and the queue on the right
- **The visible list is the queue** - filtered, searched and sorted as the gallery shows it
- `Space` plays and pauses, `<-` / `->` change tracks; *previous* returns to what you actually heard
- **Shuffle** without repeats; **repeat** off, one or all
- **Gapless**: the next track starts without a pause
- **Title, artist and cover** are read from the file and shown in the player, the queue and on the tile
- **Fast jumping** in long MP3, AAC, AC-3 and MKV files
- Playback continues while you look at other files, and each half remembers its mode across restarts
- **10-band equalizer** (31 Hz to 16 kHz, ±12 dB) with preamp. Bands add up instead of amplifying each other, which avoids most distortion
- **Prevent clipping** (on by default): a limiter at the end that lowers only the parts that would distort. The preamp is never changed by it
- **Noise reduction** with a strength slider: lowers background hiss (up to about 29 dB) and leaves normal music untouched. At 0 the sound passes unchanged
- **Presets**: five built in, your own added; save or overwrite from the equalizer. Delete, reorder and restore built-ins in *Settings ▸ Audio*
- **Save a video's sound** (right-click a video, the queue, or the *Document* menu): the sound is copied without conversion, so there is no quality loss
  - MP4, M4V and MOV become `.m4a`; MKV, WEBM and MKA become `.opus`, `.ogg`, `.ac3`, `.eac3`, `.mp3` or `.aac` depending on the sound format
  - A video with several sound tracks asks which one (language, codec, channels)
  - The new file takes over the video's tags and can be added to the queue (*Settings ▸ Audio*)
  - Existing files are never overwritten

---

## Playback & Interface
- **Video**: built-in player or an external one. A video jumps when you let go of the progress bar
- **Mono play** (on by default): starting playback in one half pauses the other
- **Seek step** for `->` / `<-` in fullscreen video: 1 to 600 seconds (*Settings ▸ General*)
- **Language**: English or German, switchable at runtime
- **Graphics**: Vulkan, OpenGL or software. If a mode fails to start, the app falls back to a safer one automatically
- **Keyboard shortcut overview** in *Settings ▸ General*
- Shortcuts only act where they belong - in a split window only in the active half
- **The app's own file chooser**, in the app's colours, with places, path, filter and hidden files
- Bars that do not fit a narrow window **scroll sideways** with the mouse wheel
- **Settings groups fold away**, and stay folded after a restart

## Colours & Themes (*Settings ▸ Design*)
- **8 interface themes** (Dark, Dark OLED, Ocean Depth, Inferno Blaze, Midnight Rose, Elegant, Simple, Custom) and **4 editor themes** - independent of each other
- **Custom theme editor** with live preview: backgrounds (solid or gradient), text, borders, accent and glow, tiles, PDF viewer, editor and syntax colours
- **Export and import** themes as JSON
- All icons and controls are drawn by the app itself, so they follow the theme and stay sharp at any display scaling

---

## Files Next to Your Media
- **The app's own files are hidden by default**: the folder file with tags (`<Folder>.mgstore`), editor notes (`<file>.mgedit.json`) and DOCX backups (`.bak`)
- **Show all files** (*Settings ▸ View ▸ Files*) shows them - and every other file type, with an extension badge
- **Delete notes or backups** without the file itself: right-click a tile, or *Document* menu for PDFs and images. Goes to the trash; `Ctrl+Z` brings it back
- **Look inside the folder file**: open the `.mgstore` to see all tags, categories and assignments in readable form; *Raw* shows the bytes

## Metadata & File Management
- **Date**: set a file's date; it is written to the file itself. *Reset* returns to the creation date
- **Info** (right-click ▸ *Info*): name, type, size, dates and location
- **Create** (the **+** button): new folder, or an empty PDF, HTML, text or Word file, or an empty file with any name
- **Rename** from the right-click menu, or in the header in compact mode (`Alt+S`)
- **Drop a folder** on the window to open it
- **The folder file follows the folder** when you rename it, also outside the app

---

## Keyboard Shortcuts

The same list, with every shortcut, is in *Settings ▸ General*.

| Action | Shortcut |
|--------|----------|
| **Gallery** | |
| Open folder | `Ctrl+O` |
| Jump to the search field | `Ctrl+F` |
| Reload / refresh thumbnails | `F5` / `R` |
| Tile size larger / smaller | `Ctrl` with `+` / `-` or mouse wheel |
| Select files / pick a range | `Ctrl`+click / `Shift`+click |
| Select everything shown / clear selection | `Ctrl+A` / `Esc` |
| Copy / paste files | `Ctrl+C` / `Ctrl+V` |
| Delete selected files | `Delete` |
| Undo / redo a file operation | `Ctrl+Z` / `Ctrl+Shift+Z` or `Ctrl+Y` |
| Back out of a subfolder | `Alt+<-` |
| Compact mode (gallery and viewer) | `Alt+S` |
| Cover mode | `B` |
| Audio player mode | `Alt+A` |
| Play / pause, previous / next track (player mode) | `Space`, `<-` / `->` |
| Confirm / cancel a delete question | `Enter` / `Esc` |
| **Viewer** | |
| Open a file | Double click |
| Next / previous file | `->` / `<-` |
| True fullscreen | `F` |
| Seek video forward / back (fullscreen) | `->` / `<-` |
| Back to gallery | `Esc` / `Alt+<-` |
| Edit date | `D` |
| **Text editor** | |
| Save | `Ctrl+S` |
| Find & replace / next / previous match | `Ctrl+F` / `Enter` / `Shift+Enter` |
| Close search | `Esc` |
| Undo / redo | `Ctrl+Z` / `Ctrl+Shift+Z` or `Ctrl+Y` |
| Indent (writes spaces) | `Tab` |
| Start / end of file | `Ctrl+Home` / `Ctrl+End` |
| Scroll a page (cursor stays) | `Page Up` / `Page Down` |
| **Tables (CSV, TSV, DATEV)** | |
| Mark a cell / remove the mark | Click / `Esc` |
| Edit a cell | Double click / `F2` |
| Confirm and move down / right | `Enter` / `Tab` |
| Move the mark | Arrow keys, `Tab`, `Page Up/Down`, `Home`/`End` |
| Copy cell / row | `Ctrl+C` / `Ctrl+Shift+C` |
| Paste (several cells too) | `Ctrl+V` |
| Undo / redo | `Ctrl+Z` / `Ctrl+Shift+Z` or `Ctrl+Y` |
| Save (with a check) | `Ctrl+S` |
| Search | `Ctrl+F` |
| Sort by a column | Click the heading |
| Column menu | Right-click the heading |
| **PDF and image editor** | |
| Zoom in / out (PDF) | `+` / `-` |
| Search the document (PDF) | `Ctrl+F` |
| Copy text / select page text (PDF) | `Ctrl+C` / `Ctrl+A` |
| Show / hide notes | `Alt+Q` |
| Delete selected note | `Delete` |
| Copy / paste note | `Ctrl+C` / `Ctrl+V` |
| Undo / redo | `Ctrl+Z` / `Ctrl+Shift+Z` or `Ctrl+Y` |
| **DOCX editor** | |
| Save | `Ctrl+S` |
| Bold / italic / underline | `Ctrl+B` / `Ctrl+I` / `Ctrl+U` |
| Find & replace | `Ctrl+F` |
| Select all / copy / cut / paste | `Ctrl+A` / `Ctrl+C` / `Ctrl+X` / `Ctrl+V` |
| Line break inside a paragraph | `Shift+Enter` |
| Undo / redo | `Ctrl+Z` / `Ctrl+Shift+Z` or `Ctrl+Y` |

---

## Configuration & Data

Settings are stored the usual way for each system (`QSettings`), including the
equalizer, presets, volume and the last played track.

Tags, tag colours and categories of a folder are kept in **one file next to the
media**:
```
MyPhotos/
├── photo1.jpg
├── photo2.png
└── MyPhotos.mgstore
```

**MGStorage** is the app's own compact binary format. Every tag and file name is
stored once and then referred to by number, identical tag combinations are stored
once, and numbers take only as many bytes as they need. On a real folder this
turned 3.1 MB of JSON into 245 KB. *Settings ▸ General* explains the format with
an example.

An older `<Folder>.json` is still read and replaced on the next save. The file is
written safely (to a temporary file first), so a crash cannot empty it, and
changes from two windows are merged.

Themes can be exported as JSON and shared:
```json
{
  "name": "My Theme",
  "background": "#0a1216",
  "accent": "#00b4a0",
  "pdfSidebarBg": "#0d1518",
  "sidebarBg": "#121c22"
}
```
