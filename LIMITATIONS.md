# MediaGallery - Limitations

Where the app stops, and why. [FEATURES.md](FEATURES.md) says what it does; this
file is the counterpart. Each entry names what you notice, why it happens, and
what you can do about it.

What is simply *not built yet* is listed separately at the end, under
[Not built yet](#not-built-yet).

---

## Platform

**Only Linux is built and tested.**
Why: development happens on Arch Linux with Qt 6.11. The code avoids
platform-specific paths, but Windows and macOS have never been compiled.

**True fullscreen (`F`) has only been tested under KDE/Wayland.**
Workaround: if your window manager behaves differently, `Esc` also leaves it.

**On Wayland the mouse wheel does nothing while you drag a file.**
Why: during a drag the desktop keeps the mouse to itself; the app receives no
wheel events at all (measured: 892 drag events, 0 wheel events).
Workaround: move the pointer to the top or bottom edge to scroll, or drop on the
folder bar that appears at the bottom. On X11 the wheel works.

---

## Optional dependencies

**Without ZLIB there is no DOCX editing, without Tesseract no text recognition
(OCR), without Hunspell no spell checking.**
Why: all three are optional, so the app can be built without them.
Workaround: the app runs normally and says why the feature is off.

---

## Gallery and files

**Scrolling is less smooth when every tile carries a tag or is selected.**
Why: a selected tile draws a tinted overlay, and a tagged tile draws a coloured
dot per tag. With 12,000 tiles a frame takes about 6 ms plain, 10 ms once every
tile is tagged, and 14 ms with a full selection on top. Both cost about the same.
Workaround: the compact tile (options mode off, `Alt+S`) does not show the dots
and is back to about 8 ms. A cheaper way would change how a selection and a tag
look; three attempts were measured and none of them helped.

**"Show all files" really shows everything**, including archives and programs.
Why: anything narrower would hide the `.bak` backups the switch exists for.
Unknown types carry a badge with their extension.

**A new file with an unknown extension stays hidden until "Show all files" is on.**
Why: the gallery decides by extension. Files without one only show up if they are
well-known names such as `LICENSE` or `Makefile`.
Workaround: the status line says so when you create the file. The switch is not
flipped for you - it is your setting.

**Deleting only works where a trash exists.**
Why: without a trash the app refuses rather than deleting a file for good.

**The selection rectangle does not scroll the gallery.**
Why: if the view moved underneath it, the result would depend on how fast you
dragged.
Workaround: scroll first, then drag - or use `Ctrl`+click and `Shift`+click.

**Changing a filter clears the selection** (so does opening another folder).
Why: otherwise `Ctrl+C` or *Delete* could act on files you can no longer see.

**Other programs sometimes receive only a few of the files you copied.**
Why: KDE's clipboard manager (Klipper) cuts long file lists short - measured with
29 files, only 3 arrived. The app hands over the full list.
Workaround: pasting inside the app always brings every file. For other programs,
drag the files instead, or turn Klipper off.

**Folders can be selected, but not dragged or copied.**
Why: dragging a folder into another program is not offered.

**Dropping many files asks about each name collision separately.**
Why: every collision concerns a different file; cancelling skips only that one.

**In the list arrangement, tags and renaming go through the right-click menu.**
Why: a row is too low for the editing strip a tile shows on hover.
Workaround: right-click, or switch back to *Tiles*.

**A bookmark group name cannot contain a slash.**
Why: the slash separates nesting levels ("Personal/Learning").
Workaround: write "C, C++" or make two groups. Bookmark names themselves take any
character.

**To put a bookmark group before another one, drop it on the thin strip above
the row.**
Why: dropping on the row itself puts the group *inside* the other one.

**A folder with tens of thousands of files pauses briefly before the first tile.**
Why: the folder is read and sorted once up front, which makes the rest much
faster (12,000 files: 18 ms pause, whole read 161 ms instead of 628 ms).
Below a few thousand files the pause is under 3 ms.

**A folder of very large PNGs loads a little slower than one of JPEGs.**
Why: a PNG has to be decoded at full size before it can be shrunk. The app limits
how many of those run at once, so memory stays low (200 large PNGs: 323 MB
instead of 421 MB).
Workaround: none needed; the second visit comes from the thumbnail cache.

---

## Tags and categories

**Tagging in a folder of 20,000 files takes about 10 ms.**
Why: every change re-reads the tags of all rows and filters the gallery again.
That is still below one frame.

**Setting a date changes the file's modification time.**
Why: the date is written into the file so every program sees it. Backup and sync
tools will therefore copy the file again.
*Reset* returns to the **creation date**, not to the previous date - that one is
not stored anywhere.

**Tags and categories belong to a folder, not to the whole library.**
Why: they live in a small file next to the media (`<Folder>.mgstore`), so a
folder keeps them when you move or copy it.
A moved file takes its tag along, unless the target folder already has a tag of
that name.

**Tag undo only reaches back within the open folder.**
Why: each step restores that folder's own tag file. Opening another folder clears
the history. Up to 20 steps are kept.

**With the same folder open in both halves, they share one undo history.**
Why: both write the same folder file. The bar always names the step that would be
taken back.

**Undoing a tag deletion across a huge folder tree may not restore every
subfolder.**
Why: the undo keeps at most 512 folders or 8 MB. Beyond that, the open folder
comes back fully, the subfolders only in part - and the status line says so.

**Tag undo has no keyboard shortcut.**
Why: `Ctrl+Z` in the gallery undoes file operations. A second meaning would make
the key unpredictable.
Workaround: use the bar at the bottom of the tag panel.

**Tags and categories share one undo history.**
Why: many actions change both at once, such as deleting a tag that sits in
categories.

---

## PDF

**A text-to-PDF page made only of very short lines cannot be searched in Chrome or
in this app.**
Why: those viewers mistake a narrow column (under about 30 characters) for
vertical text. The file itself is correct; other PDF readers find the words.

**Form fields are drawn by the app.**
Why: the PDF engine Qt uses cannot draw them.
Text fields, check boxes, radio buttons and lists work; push buttons are shown
but do nothing.

**Tracked changes cover adding and deleting annotations, not editing them.**
Why: tracking every move or colour change would mean storing a copy before each
one.

**Page changes can only be undone while the file is open.**
Why: moving, rotating, removing or inserting pages is written into the PDF
immediately. The temporary copy that makes undo possible is deleted when you
close the file.
Workaround: copy the file first if you want a safety net.

**Typed page text becomes permanent if you also changed pages in the same
session.**
Why: the rebuilt file already contains the typed text. Notes, drawings,
highlights and redactions are not affected.

**Extracting pages from more than eight PDFs at once makes the result larger.**
Why: to save memory, only eight source files are kept open at a time; shared
fonts of the others may be written twice. The result is always correct.

**A scanned page has no text until you make the document searchable.**
Workaround: run *Document ▸ Make document searchable* once. It writes the
recognised words into the file.

**Making a document searchable takes about one second per page.**
Why: text recognition is slow, and running pages in parallel was hardly faster
(+10 %) but nearly doubled memory. It shows progress and can be cancelled.

**The recognised text layer holds Latin script only.**
Why: Arabic, Japanese or Cyrillic would need a special embedded font. Such words
are skipped - the app says how many - rather than written wrongly.

**Recognition mistakes become part of the file.**
Why: that is true of every OCR PDF. A misread word is what the search finds.

**On a scanned page, blacking out cannot remove words** - there are only pixels.
Workaround: export as an image, so the covered pixels are gone.

**Selecting text on a page rotated by 90 or 180 degrees loses the last
character.**
Why: a bug in Qt 6.11; the text itself is correct.
Workaround: drag a little past the word.

---

## Audio player

**A track change is gapless only when the next track is known in advance.**
Why: the next track is prepared while the current one plays. At the end of the
queue, and when *shuffle + repeat all* starts a new round, there is a short pause
(about 0.8 s).

**Your own track order cannot be changed while shuffle is on.**
Why: shuffle replaces your order for as long as it runs. Dragging would write
the current shuffle down as your order, so the grips are hidden instead.
Workaround: switch shuffle off, sort, switch it on again.

**A folder shown with its subfolders keeps no track order.**
Why: the order lives next to the media as `<Folder>.mgal`, and a list made of
several folders has no single place to put it. The order you drag there applies
to the current session only.

**Title and artist are read, never written.**
Why: one wrong byte would damage the file.
Workaround: use a tag editor; the app picks up the change.

**WMA files show their file name instead of the title.**
Why: their tag format is not supported.

**Noise reduction can make very quiet music slightly quieter.**
Why: anything that stays below about -48 dB is treated as noise - a faint
sustained instrument sounds just like hiss (measured: -1.3 dB on very quiet
music, 0 dB on normal music).
Workaround: lower the strength slider; at 0 the signal passes unchanged.

**Noise reduction needs about 1.4 seconds before it works fully.**
Why: it first has to hear what the noise floor is. The start of a track is left
unprocessed.

**Noise reduction is for light hiss, not heavy restoration.**
Why: it lowers broad noise by about 29 dB and costs almost no CPU. Deeper methods
would add a delay the gapless playback cannot absorb.
Workaround: use a dedicated audio editor for damaged recordings.

**The equalizer works on audio files only, not on videos.**
Why: Qt's video player does not hand out its sound, so audio uses the app's own
playback chain.

**Jumping inside long M4A, OGG, FLAC or WAV files is slow.**
Why: for these formats the decoder still starts from the beginning. MP3, AAC,
AC-3 and most MKV files jump directly (45 minutes into a track: 21 ms instead of
2.8 s).
Unnoticeable for songs; noticeable in an hours-long audiobook.

**There is one playback for the whole app.**
Why: starting a second track replaces the first, and changing the folder of the
playing half ends the queue.

**Saving a video's sound works for MP4/MOV and MKV/WEBM - not for AVI or WMV.**
Why: the sound is copied byte for byte, which needs a reader for each container.
AVI and WMV are not planned. Their sound still plays in player mode.

**AAC from MKV/WEBM is saved as `.aac`, not `.m4a`.**
Why: it is the simplest valid form; every player opens it and the sound is
identical to the source.

**A raw sound file (`.ac3`, `.eac3`, `.mp3`, `.aac`) starts a few milliseconds
earlier than the video.**
Why: such files have no timestamps. The offset is at most 23 ms - inaudible.

**A 5.1 track stays 5.1.**
Why: the sound is copied, not converted. The player mixes it down for listening,
but the file keeps all channels.

**For Vorbis, the shown duration can be off by a fraction of a second.**
Why: the exact value would need every packet to be examined. The sound is
identical.

**After a jump, a saved Opus or Vorbis file lands up to 18 ms away from the
video.**
Why: the containers store timing differently. From that point on, the sound is
identical.

**A streaming MP4 with missing parts cannot be saved.**
Why: the file announces sound that is not there - usually an incomplete download.
The message says so.

**DTS, ALAC and raw PCM tracks cannot be saved.**
Why: they would need a container this app does not write.

**The saved sound keeps the video's format.**
Why: nothing is re-encoded - no quality loss. Converting to MP3 or FLAC is not
planned.

**A strongly boosted equalizer plays loud passages quieter.**
Why: boosting cannot create headroom. *Prevent clipping* lowers only the parts
that would distort (loud passages about -6.6 dB, quiet ones -3.3 dB).
Workaround: turn up the volume, or switch *Prevent clipping* off in
*Settings ▸ Audio* and accept the distortion.

**Older equalizer presets sound slightly tamer.**
Why: neighbouring bands no longer amplify each other (three bands at +12 dB now
peak at 12.7 dB instead of 17). That is what removes most distortion.
Workaround: adjust the bands and save the preset again.

---

## Two-pane mode

**At most four open files** (two per half when the window is split).
Why: beyond that the tiles are too small to work with.

**In true fullscreen a tile cannot be rearranged.**
Why: the header you would drag is hidden.
Workaround: press `F` or `Esc` first.

---

## Editors

**Notes cannot be read by an older version of the app any more.**
Why: the side file next to a document (`<name>.mgedit`) now holds the app's own
compact format instead of text, which makes saving three to four times faster.
Older files still open, and are quietly rewritten in the new form the first time
you open them - so going back to an older version stops working even for
documents you never edited.
Workaround: keep a copy of your side files before going back to an older version.

**The editor cannot tell you whether a function, a class or an include exists.**
Why: that needs a compiler front end - a preprocessor, resolved include paths
and the project's build flags. The editor reads the open file and nothing else,
so it reports only what can be proven wrong from that file alone. A tool that
guessed would put red marks under correct code.

**Structural checking stops at 4 MB.**
Why: the pass runs in the window's own thread, and checking a very large file on
every pause in typing would stutter. Above that size nothing is underlined.

**Code inside `#if 0` can produce one wrong report.**
Why: brackets in alternative preprocessor branches are followed (`#ifdef` /
`#else` / `#endif` are handled), but a block switched off with `#if 0` that
leaves a bracket open is still counted.
Workaround: switch the check off in *Settings ▸ Editor*.

**A raw string whose content contains `)` plus the same number of characters
plus a quote ends too early.**
Why: the colouring keeps one number per line, and the delimiter of `R"CPP( … )CPP"`
does not fit in it - only its length does. The `LR"` and `u8R"` forms are not
recognised at all.

**A text file over 8 MB opens read-only.**
Why: only the first 8 MB are loaded, so a large log does not freeze the window.
Saving would cut off the rest of the file, so it is blocked; the status bar says
why.
Workaround: use another editor for such files.

**Typing gets slower in files with hundreds of thousands of lines.**
Why: Qt re-arranges the whole document on each keystroke (41 ms at 240,000
lines). The syntax colouring is not the cause.

**The DOCX margin rulers move the page margins, not the paper.**
Why: that is what a margin is, in Word too. A narrower text column usually means
more pages.

**Scrolling while holding a ruler handle changes the margin.**
Why: an A4 page is taller than the window; this is how you reach the bottom
margin. The distance you scroll is added to the margin.
Workaround: let go, scroll, and grab the handle again.

**Dragging a ruler changes the document.**
Why: margins are stored in the document - the only place Word reads them from.
`Ctrl+Z` takes a drag back, the reset button restores 2.5 cm.

**A tab becomes a space in the text of an exported PDF.**
Why: Qt writes spaces into PDFs in a way that reads back as tabs. The app corrects
this, and real tabs turn into spaces along with it.

**A PDF from another Qt program may be read back with split words.**
Why: Chrome and this app then read "Hallo" as "H allo". Files written by this app
are repaired; foreign files are left untouched.
Workaround: Okular or Evince read such files correctly.

**The page number in the text-to-PDF export is fixed** (centred, "1/3").
Why: only the DOCX export offers a choice.

**The *Like the editor* PDF export always prints the editor's background.**
Why: its colours only work on that background. A dark theme therefore costs a lot
of toner.
Workaround: use *One colour* (the default) for printing.

**An `#if` branch that cuts through a function cannot be folded.**
Why: a branch that opens a function's brace ends before that function does, and
folding one would hide half of the other. The function itself still folds.
Workaround: fold the function, or the whole block around both.

**The text-to-PDF export has no line numbers and ignores folding.**
Why: a folded block in the PDF would mean text missing without notice. The full
text is always exported.

**The DOCX editor only changes what you edit.**
Why: parts of the file it does not understand are kept unchanged - but they
cannot be edited either.

**Dragging a margin ruler stutters in long documents.**
Why: every mouse movement reflows the whole document (about 70 ms at 4,000
paragraphs).
Workaround: drag in short steps.

**A very long DOCX keeps its whole layout in memory.**
Why: page breaks are only known if every paragraph is measured (648 pages: about
166 MB). Typing stays fast.

**A very large photo is loaded at screen size, not at full size.**
Why: a 27-megapixel photo needs 108 MB, yet the window shows only a fraction of
it (one open photo: 157 MB instead of 250 MB). At 100 % zoom the full image is
loaded. PNGs open about 30 % slower this way; JPEGs faster.

**Files written by the app have not been checked in Word itself.**
Why: tests read them back with the app's own reader only.

**A search pattern never reaches across a line break.**
Why: search runs line by line (paragraph by paragraph in DOCX), as in most
editors. `.*` stops at the end of the line.

**Replacing inserts the text literally - `\1` is not supported.**
Why: every search is also a literal search, where `\1` has no meaning.

**A badly written pattern can freeze the editor for a moment.**
Why: patterns such as `(a+)+$` can take extremely long on a long line; Qt sets no
time limit.

**In a PDF, a pattern search stops after 500 hits per page.**
Why: a pattern like `.` would match every character. Plain-text search has no
limit.

---

## Markdown files

**Very large Markdown files pause the window briefly when they open.**
Why: the text view lays out the whole document at once before it shows it. A
typical file (under 20 KB) takes about 25 ms, a 750 KB file about half a second.

**Quotes appear as a shaded box, not with a bar on the left.**
Why: the text view the app uses cannot draw a single edge of a box, and a box
with a border makes it crash. For the same reason table headers are bold but
not shaded, and rows have no alternating colour.

**Formulas and diagrams are not drawn.**
Why: `$…$` maths and Mermaid diagrams need their own renderers. Formulas stay
as written, diagrams appear as a code block.

**HTML inside Markdown is only partly understood.**
Why: formatting tags work (bold, italic, code, keys, sub- and superscript, line
breaks, links, images and folded `<details>` hints); other tags appear as they
are written, and HTML tables are not drawn as tables.

**Images from the internet are not shown.**
Why: the app does not go online. The image's description appears instead, as a
link. Local images wider than 1600 pixels are scaled down, and beyond 64 MB of
images per file only the descriptions appear.

**`&thinsp;`, `&ensp;` and `&emsp;` appear as a normal space.**
Why: named characters such as `&copy;` are read by the HTML reader built into
Qt, and it treats these three as ordinary spaces.

**Task list boxes cannot be ticked in the formatted view.**
Workaround: change `[ ]` to `[x]` in the editable text.

**Copied text contains a blank line around code blocks, quotes and tables.**
Why: the spacing around these boxes is an invisible line in the document, and it
is copied with the text.

---

## CSV and TSV files

**A `.txt` only opens as a table if you allow it and it looks like one.**
Why: most `.txt` files are notes or logs. With *Settings ▸ View ▸ Files ▸ Open
.txt as a table too* on, a file still needs the same number of columns on most of
its first 20 lines.
Workaround: rename it to `.csv`.

**Only three date forms are recognised.**
Why: `01.01.2025`, `2025-01-01` and `01/02/2025` work. `1.1.25` or `Jan 3, 2025`
would need a guessed century or language. Whether `03/04/2025` is March or April
is one setting for all tables (*Settings ▸ View ▸ Files*).
If a single cell in a column is not a date, the whole column is sorted as text.

**Formatting belongs to a column, not to a single cell.**
Why: per cell there would be one entry per cell instead of 125 per file, and
every one of them would have to travel along when you sort, filter or insert a
row. Per column nothing has to move.

**Thousands separators are only added to numbers that have decimals.**
Why: a whole number in a table is usually an identifier. Without the rule an
account number `8400` became `8.400` and a document number `201802010` became
`201.802.010`. The price is that a round amount written as `64083` keeps its
plain shape.
Workaround: write the amount with its decimals (`64083,00`).

**Column formatting and widths are lost if you delete the column and save.**
Why: they live next to the file and follow the column number. Undo brings them
back, but only while the file is open.

**`>200` in the filter compares values**, so a cell that literally says `>200` is
not found this way.
Workaround: use `Ctrl+F`, which always searches literally.

**Dragging with the mouse does not select a block.**
Why: a drag over the table scrolls it, and the table cannot have both on the
same button.
Workaround: hold `Shift` and click the far corner, or hold `Shift` and use the
arrow keys.

**A block of more than 500,000 cells is not copied.**
Why: the text is put together while the window waits, and half a million cells
already fill the clipboard with tens of megabytes. The table says so instead of
freezing.
Workaround: copy it in parts, or filter first.

**A single `,` or `.` in a number is read as a decimal separator.**
Why: `1.234` could be one point two or twelve hundred. The German reading was
chosen; `1.234,56` and `1,234.56` are always read correctly.

**Formulas do not move when you insert or delete rows and columns.**
Why: the formula is plain text in the file, and rewriting it behind your back
could quietly change a result. `=A5` still means row 5 afterwards.
Workaround: correct the formula, or insert rows below the ones you refer to.

**A formula only reaches cells in its own table.**
Why: in a file with several tables, `A1` is the first data row of the table the
formula sits in - otherwise the same formula would show different numbers
depending on which tab is open. Reaching past the table gives `#REF!`.

**Inside a formula, `.` is the decimal point and `;` separates arguments.**
Why: a comma has to be both in different places. A comma between digits counts as
a decimal point (`=1,5*2` works); anywhere else it separates. The *result* is
shown with the separator the file itself uses.

**A formula containing `;` is put in quotes when the file uses `;` as its
separator.**
Why: without the quotes the next reader would see two columns instead of one
formula. Spreadsheets do the same; the file stays correct.

**Formulas are recalculated as a whole after every change.**
Why: about 8 ms on a 15 MB table with 100,000 rows. Tables without any formula
are not affected at all.

**Tagging very many files at once takes a moment.**
Why: about 60 ms for 12,000 files. Each file's previous state is kept so one
`Ctrl+Z` takes the whole batch back.

**A file changed by another program while you edit is not saved over.**
Why: saving would destroy the other change.
Workaround: *Save as copy* writes `<name>_edited.<ext>`; *Reload* takes the other
version. Leaving the file saves the copy automatically.

**In a Windows-1252 file, characters that encoding cannot hold are refused.**
Why: converting the whole file for one cell would change it everywhere.
Workaround: remove the character, or leave the file - your changes are then saved
as a UTF-8 copy.

**Inserting or deleting a column rewrites every line in the file's usual style.**
Why: those lines no longer have an original form. Editing a cell changes only
that line.

**Rows and columns can only be added in a table's own tab, not in *All*.**
Why: in the flat view a new row could belong to either of two tables.

**A filtered or sorted table does not rearrange itself while you edit.**
Why: a row jumping away while you type is worse, as in any spreadsheet.
Workaround: set the filter or click the heading again.

**Only one cell can be selected at a time.**
Why: range selection needs a faster way of drawing the table (see
[Not built yet](#not-built-yet)). Pasting a block of cells works.

**After the table saved, switching to the raw file clears the raw editor's undo
history.**
Why: the raw view reloads the changed file.

**A search stops at 200,000 hits.**
Why: a one-letter search in a large file would otherwise use a lot of memory.
Workaround: type more letters.

**Searching a very large table takes a moment.**
Why: 100,000 rows take about 57 ms, so the search runs in the background; the
table stays usable.
Workaround: switching on `Aa` makes it about four times faster.

**Tables are read up to 32 MB.**
Why: that is about 100,000 rows and 82 MB of memory. Larger files show only their
beginning, and the footer says so.

**The separator and header row are detected automatically and cannot be changed.**
Why: manual switches were built and removed again - they took up space for a
rare case. The footer shows what was detected.
Workaround: the raw view shows the file as it is.

**Several tables in one file are only split at blank lines.**
Why: a blank line is the only clear sign in the file; anything else would be a
guess. The *All* tab always shows the whole file.

**A single-field line at the top of a block is taken as the table's title.**
Why: that is how such exports are laid out, and text-only tables can be
recognised no other way.
Workaround: the *All* tab shows every row as data.

---

## DATEV files

**Search only covers the columns that are shown.**
Why: a hit in a hidden column could not be displayed. Hidden columns are empty
anyway.
Workaround: switch on *all columns*; the search runs again.

**Most header fields are shown by number ("Dateikopf 7") instead of by name.**
Why: DATEV's official field list could not be retrieved, and names from memory
would be unreliable. All values are visible.

**Showing all 125 columns of a DATEV batch freezes the window briefly** (about
0.1 s).
Why: every visible cell is its own element. A faster way of drawing the table is
not built yet.

**Batches larger than 32 MB are cut off.**
Why: that is about 100,000 bookings. The totals then cover only what was read.
Workaround: split the batch.

**DATEV files cannot be edited or exported.**
Why: one wrong field in a bookkeeping file does real damage. This will not
change.

**Only files beginning with `"EXTF";` or `"DTVF";` are shown as a DATEV batch.**
Why: otherwise ordinary CSV files would be mistaken for bookkeeping data. DATEV's
free-form *individual ASCII format* opens as plain text.

**A field like `" "Normalabschr. immater. VermG" "` keeps its inner quotes.**
Why: the file is ambiguous at that point; keeping the quotes loses nothing.
Workaround: the raw view shows the line exactly as written.

---

**Opening the tag or category panel in a very large folder takes a moment.**
Why: the panel builds a chip for every tag and a row for every category. With
12,000 files and 400 tags the panel used to freeze the window for 1.7 seconds;
it is now built piece by piece across frames, so the longest stall is about 80
milliseconds. Closing it still costs about 160 milliseconds, because dropping
the items cannot be spread out.

**A page of a PDF stays blank for a moment while it is being drawn.**
Why: pages are drawn one at a time in the background. Until one is ready, its
place shows the small preview and the document's own background colour instead
of white, so fast scrolling stays readable.

---

## Not built yet

Planned work, not limits. Once something is built, it moves to
[FEATURES.md](FEATURES.md).

- **Spell checking for Japanese** - Japanese has no spaces between words, so it
  would need a different engine (such as MeCab) plus its own dictionary.
  Arabic only needs the `hunspell-ar` dictionary installed.
- **More syntax languages** - 28 are covered; each new one is a table entry.
- **Writing audio tags** (title, artist) - deliberately left out: one wrong byte
  damages the file.
- **Naming what does not exist** - "this function is unknown", "this include is
  not found". Every editor that does this runs a separate language server
  (clangd, pyright and the like) that has to be installed, needs the project's
  build flags, and uses several hundred megabytes. MediaGallery opens files, not
  projects, so on a single file such a tool would mostly report errors that are
  not there.
- **Charts from table data** - bars, lines, pies and graphs. Nothing of it is
  designed yet.
- **Search in the formatted Markdown view** - the editable text has it (`Ctrl+F`).
- **Printing or a PDF of the formatted Markdown view** - the editable text can be
  saved as PDF, but only as plain text.
- **Own drawing for the formatted Markdown view** - would bring quote bars, shaded
  table headers and no pause on very large files. Planned, not decided: typical
  files would barely get faster.
