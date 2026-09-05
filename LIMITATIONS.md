# MediaGallery - Limitations

Where the app stops, and why. `FEATURES.md` says what it does, `README.md` gives
one line per area - **this file is the honest counterpart**: every limit a user
can run into, with the reason behind it and, where one exists, the way around it.

Each entry follows the same shape:

> **What you notice** - what actually happens.
> **Why** - the cause, with the measurement when one was taken.
> **Workaround / status** - what to do instead, or what is planned.

The sections above are about behaviour you meet in the **running app**. What is
simply *not built yet* is kept apart at the end of this file, under
[Not built yet](#not-built-yet) - the two must not blur: "does not work" and
"does not exist yet" call for different answers.

---

## Platform

**Only Linux is built and tested.**
Why: development happens on Arch with Qt 6.11; the code avoids platform-specific
paths, but Windows and macOS have never been compiled or run.
Status: portability is a design priority, not a tested promise.

**True fullscreen (`F`) was only ever seen under KDE/Wayland.**
Why: leaving fullscreen goes through an explicit *windowed -> remembered geometry
-> maximised* sequence because some window managers swallow the direct jump.
Workaround: if your window manager behaves differently, `Esc` also leaves it.

**The mouse wheel does not scroll the gallery while you drag a file on Wayland.**
Why: during a drag the compositor owns the pointer and no wheel event reaches the
application at all - measured: 892 drag events, 0 wheel events.
Workaround: the pointer edges scroll the view during a drag, and a bar of the
visible folders appears at the bottom as a drop target. On X11 the wheel works.

---

## Optional dependencies

**Without ZLIB there is no DOCX editing**, without **Tesseract** no OCR for
scanned PDFs, without **Hunspell** (plus a dictionary) no spell checking.
Why: all three are optional at build time so the app stays buildable without them.
Status: the app builds and runs normally and says why the feature is off - DOCX
tiles stay visible but greyed out with a hover note.

---

## Gallery and files

**"Show all files" really shows everything**, including file types the app cannot
open (archives, executables).
Why: anything narrower would hide the `.bak` backups the switch exists to reveal.
Workaround: unknown types carry an extension badge, so they are recognisable.

**A file created with an extension the gallery does not know is invisible until
"Show all files" is on.**
Why: what appears as a tile is decided by the extension (`MediaItem::detectType`), and an
unknown one counts as `Unknown`. A name with no extension at all only shows up if it is one
of the known extensionless names (`LICENSE`, `README`, `Makefile`, `Dockerfile`, …) - so a
file called `NOTIZEN` stays hidden.
Workaround / status: the status line says so at the moment of creation ("only visible with
'Show all files'"). The switch is deliberately **not** flipped for you - it is your setting
(user's decision, 2026-09-03). The file itself is created either way.

**Deleting goes to the trash - where there is one.**
Why: on systems without a working trash the app refuses to delete rather than
removing a file for good.

**The selection rectangle does not scroll the gallery.**
What you notice: dragging it to the top or bottom edge does not carry on to the
tiles above or below, unlike dragging a *file*, which does scroll at the edge.
Why: the rectangle is evaluated over the rows that are currently laid out. If the
view scrolled underneath it, tiles would pass through it: what left the viewport
would be neither visibly selected nor visibly deselected, and the result would
depend on how fast you dragged. Measured cost of one pointer move over 3000
files: 230 µs while the selection changes, 55 µs while it does not.
Workaround: scroll first, then drag - or add to the selection with `Ctrl`+click
and `Shift`+click, which reach any tile.

**Any change to a filter clears the selection**, as does opening another folder or
a refresh.
Why: a selection the filter has hidden would be a trap - `Ctrl+C` and *Delete*
would act on files that are not on screen. The same reasoning keeps `Ctrl+A` to
what the filter is showing rather than the whole folder.

**The desktop's clipboard can shorten a long list of copied files.**
What you notice: you copy many files with `Ctrl+C` and another program
(a browser, a file manager) receives only the first few - sometimes only one.
Why: not the copy itself. Measured on KDE/Wayland with 29 files
(`bench_shell g`): the app hands over 4147 bytes / 29 addresses, and reading the
system clipboard back gives 429 bytes / 3 addresses, cut in the middle of an
address - twice out of five runs it was even the clipboard of an **earlier**
run, from a different folder. The mangled version always carries the format
`application/x-kde-onlyReplaceEmpty`, the fingerprint of KDE's clipboard manager
(Klipper). Leaving out `text/plain` does not help (measured, 5 runs each).
Workaround / status: **inside the app it does not happen** - `Ctrl+V` in the
gallery uses the list the app remembered when copying, so all files arrive
(measured: 29 copied, 29 pasted, three runs). For other programs, **dragging**
the selection works reliably where copying does not - a drag carries the whole
list. Turning Klipper off (or its "Text selection only" option on) removes the
problem at its source.

**Folders can be selected, but not dragged or copied.**
What you notice: a folder tile joins a multi-selection and is deleted with it,
but dragging the selection leaves it behind, and `Ctrl+C` does not copy it.
Why: dragging a folder into another program is a promise of its own, not a side
effect of selecting it; the gallery has never offered it.

**A multi-file drop asks about each name collision separately.**
What you notice: dropping 20 files into a folder that already holds five of those
names brings up the replace/rename question five times.
Why: there is no "apply to all" - each collision is a decision about a different
file. Cancelling applies to that one file only; the rest of the drop carries on.

**In the list arrangement the hover overlay is gone - tagging and renaming go
through the right-click menu.**
What you notice: with *Settings -> View -> Arrangement* set to *List*, hovering a
row no longer brings up the strip for editing tags, renaming inline and the
quick actions. The row still shows the name and coloured tag dots, so you can
see what a file carries - you just cannot change it from the row itself.
Everything is still reachable, from the right-click menu instead.
Why: a row is 46 pixels high, and the overlay is built for a tile - drawn over a
row it would cover the whole entry including the name. `MediaTile` therefore
switches the overlay off in list mode, along with the large thumbnail, the type
badge and the play circle. The same has always been true of player mode; the
setting only makes it reachable in the normal gallery as well.
Workaround / status: right-click gives tags, categories, rename and delete;
switch back to *Tiles* for the overlay.

**A bookmark group name cannot contain a slash.**
What you notice: typing "C/C++" as a group name leaves the OK button disabled,
with a line saying why.
Why: a group is identified by its full path, and "/" is what separates the
levels - "Personal/Learning" IS the nesting. A slash inside a name would make the
same text mean two different places in the tree. The same goes for a tab
character, which separates the columns of the stored line.
Workaround / status: use another separator in the name ("C, C++" or "C - C++"),
or make it two groups. Bookmarks themselves are unaffected - their display name
takes any character.

**Reordering groups that sit side by side needs the thin strip, not the row.**
What you notice: dragging a group onto another group puts it *inside* it. To
place it *before* a sibling you have to hit the 10-pixel insert strip that
appears above each group row while you drag.
Why: one drop target cannot mean two things. "Into" is the operation the nesting
exists for, so the row carries it, and the strip carries "before, same level".
The strip's space is reserved permanently, so nothing shifts when a drag starts.

---

## Tags and categories

**Setting a date changes the file's modification time - and that has side
effects.**
Why: the date is written to the file so the rest of the system sees it. Backup
and sync tools notice a changed timestamp and will copy the file again, and the
gallery's thumbnail for that file is regenerated once (its cache is keyed on the
modification time).
Consequence: *Reset* goes back to the file's **creation date**, not to the date
the file had before - that one is not kept anywhere, by design (no second copy
of something the file system already stores).
Workaround / status: on a read-only file, or a file system that stores no
creation date, the app says so and changes nothing.

**Tags and categories belong to a folder**, not to the whole library.
Why: they live in the folder's JSON sidecar next to the media, so a folder can be
moved or copied and keeps its metadata.
Consequence: a tag created in one folder does not appear in another; moving a file
into another folder carries its tag with it only if the target folder does not
already define that tag differently.

**The tag undo only reaches back over the current folder, and only one folder at a time.**
Why: it works by snapshotting the folder's JSON sidecar before each change, and a snapshot
is only meaningful for the folder it came from. Opening another folder therefore clears the
stack, and a step whose folder is no longer the open one is discarded rather than written
back. The same holds for the second pane: each half has its own stack.
Workaround / status: undo what you want to undo before you navigate away. Up to 20 steps
are kept (16 MB of snapshots in total); older steps fall off the bottom.

**Undoing a tag deletion that swept a very large tree may not restore every subfolder.**
Why: the sweep keeps the previous content of each sidecar it rewrites, capped at 512 folders
or 8 MB (RAM is priority 1 in this project). Beyond that cap the open folder is still fully
restored, but the subfolders below it are not.
Workaround / status: the status line says so explicitly when it happens ("the subfolders
could only be restored in part") instead of pretending the tree came back. In practice a
sidecar is a few kilobytes, so the cap corresponds to a tree of several hundred tagged
folders.

**The tag undo and redo have no keyboard shortcut.**
Why: `Ctrl+Z` in the gallery is the *file* undo (move, rename, delete). Binding a second
`Ctrl+Z` to tags would make the key unpredictable - you could not tell in advance whether it
brings back a file or a tag. The two stacks are deliberately separate (user's decision,
2026-09-03).
Workaround / status: the bar at the foot of the tag panel is the way; its two marks name what
each button would do, so nothing happens blindly.

**Tags and categories share one undo history, not one each.**
Why: a great many operations touch both - deleting a tag also strips it from every category,
and the converter rewrites both - so a separate stack per section could not be kept honest
(user's decision, 2026-09-03). The bar therefore always shows the newest step of the whole
folder, whichever section it came from.
Workaround / status: nothing to work around; the two sides of the bar name exactly what each
button would do, and hovering spells the notation out in full.

---

## PDF

**A text-to-PDF page made up only of very short lines cannot be searched in
PDFium-based viewers** (Chrome, and this app's own PDF view).
Why: measured - from about 30 characters of line width upwards everything is
fine; below that, those viewers read the narrow column as vertically written text
and hand out every character on its own line, so a word search finds nothing.
Status: the file itself is correct (every character carries its proper Unicode)
and other PDF readers are unaffected. Widening the text block from our side did
not change the viewer's guess.

**Form fields are drawn by the app, not by PDFium.**
Why: PDFium only renders widget annotations through an API Qt does not expose, so
the fields are drawn as a QML overlay.
Consequence: text, checkbox, radio and choice fields work; push buttons are shown
but inert, and exotic field types may look plainer than in Acrobat.

**Tracked changes cover adding and deleting an annotation**, not editing an
existing one (moving, recolouring, retyping).
Why: tracking that would mean storing the state before every single change.

**Page changes cannot be undone once you close the file.** Moving, rotating,
removing or inserting pages is written into the PDF straight away; `Ctrl+Z`
works only while the document is open.
Why: deliberate (user's decision, 2026-08-23). The undo source is a temporary
copy `<name>.mgorig` next to the file, and that copy is deleted when the
document is closed - keeping it would mean a second full copy of every PDF you
ever reordered lying next to it.
Workaround / status: while the file is open, `Ctrl+Z` reaches back to the state
it had when you opened it. If you need a safety net beyond that, copy the file
first.

**Typing into the page text is made permanent by a page change in the same
session.** Text you typed with the *edit text* tool normally stays reversible
through the sidecar; if you also moved, rotated or removed a page, it is baked
into the file when you close it.
Why: the reordered file is assembled from the edited text layer, so the typed
characters are already in it - keeping the edit as a pending change too would
apply it a second time. Notes, drawings, highlights and redactions are not
affected: they are drawn only on export and stay reversible either way.

**Mixing pages from more than eight PDFs in one extraction makes the result
larger and slower to write.**
Why: while writing, the assembler keeps the parsed structure and the object map
of at most eight sources, so shared objects (fonts, resources) of those go into
the output exactly once. Beyond that the least recently used source is dropped;
when it comes up again it is parsed anew and its shared objects are written a
second time. Measured with two sources at 150 interleaved pages: 3.3 ms and
268 KB inside the cap, 86.2 ms and 1177 KB without it.
Workaround / status: nothing to do - the result is always correct, only bigger.
The cap keeps memory bounded while a job runs.

**A scanned page has no text until you make the document searchable.**
Selecting text, the document search, line snapping and the *Replace text*
prefill all read the PDF's own text layer - a scan has none.
Why: deliberate. The app no longer keeps a per-page recognition in memory;
instead *Document -> Make document searchable* writes the words into the file
once, and everything then works through the normal path.
Workaround / status: run that action once per scanned document.

**Making a document searchable takes about a second per page, and it is not
faster on more cores.**
Why: measured on A4 at 200 dpi - 49 ms to render, ~810 ms for Tesseract's LSTM
recognition. Recognising pages in parallel was built and measured: 16 pages went
from 13.4 s to 12.1 s with two threads (+10 %) and got *slower* again with four,
while peak memory rose from 241 MB to 427 MB. The threads demonstrably ran at the
same time, yet each page took 2.8 s instead of 0.87 s - the work is bound by
memory bandwidth, not by CPU, and switching off Tesseract's own OpenMP changed
nothing. So it stays serial: memory is the higher priority here.
Workaround / status: it reports progress per page and can be cancelled.

**The invisible text layer is written in Latin script only.** Words containing
characters outside WinAnsi/Latin-1 - Arabic, Japanese, Cyrillic - are skipped,
and the app says how many were.
Why: those need a CID font with a `/ToUnicode` map in the file; writing one
correctly is a separate piece of work. Skipping is the honest option - a wrong
byte would make the search find the wrong word.

**OCR mistakes become part of the file.** If a word is read wrongly, that is what
the search will find from then on, and correcting it means redoing the document.
Why: inherent to every OCR PDF. Measured on a clean 32-page test scan, 30 of the
32 pages returned the searched word.

**On a scanned page, blacking out cannot remove words**, because there are no
words in the file - only pixels in an image. A text layer does not change that:
it tells the app where a word sits, but the pixels underneath stay.
Workaround / status: cover the spot and export with rasterising, so the covered
pixels are gone from the output.

**Selecting text on a page rotated by 90 or 180 degrees loses the last
character.**
Why: `QPdfDocument::getSelection` behaves that way in Qt 6.11 - measured with
ordinary, visible text that has nothing to do with this app's OCR; the same file
reports the full text through `getSelectionAtIndex`, and the character positions
are correct. Reproduce it with `bench_rotselect`.
Workaround / status: drag a little past the word, or select the whole page.

---

## Audio player

**A track change is gapless only when the next track is known in advance.**
Why: the chain pre-decodes the following track into the same output stream. At the
very end of the queue there is nothing to prepare, and with *shuffle + repeat all*
the next round is only shuffled once you reach it - so the first track of the new
round still starts the old way, with a pause of roughly 0.8 s (measured).
Workaround / status: inside a list, and with repeat one, changes are seamless.

**Tags are read, but never written.**
Why: the reader covers ID3v2/ID3v1, MP4 `ilst`, and Vorbis comments (FLAC, OGG,
Opus) - writing them back would be a different piece of work with a much higher
risk (a wrong byte damages the file).
Workaround / status: use a tag editor; the app picks up the change on the next
read.

**WMA/ASF files show the file name.**
Why: their tag format (ASF objects) is not implemented - the four families above
cover what the app's own formats need.

**The equalizer applies to audio files only, never to video.**
Why: `QMediaPlayer` does not hand out its samples, so the app runs its own
decode -> ring buffer -> equalizer -> sink chain for audio. Video keeps using
`QMediaPlayer` unchanged.

**In M4A, OGG, FLAC and WAV - and for Opus or Vorbis inside a Matroska file -
a jump still restarts the decoder.**
Why: `QAudioDecoder` cannot seek in Qt 6. The app works around it in two ways:
self-framing streams (MP3, MP2, AC-3, E-AC-3, AAC) are entered at the target
frame, and a Matroska file is read from the cluster the target sits in. Both
need a stream a decoder can pick up mid-way. Opus and Vorbis cannot do that -
they need an Ogg wrapper with its header packets - and the remaining containers
have not been taught yet.
Measured: jumping to 45 min in an E-AC-3 track 2.8 s -> 21 ms, to 150 min in a
1.5 GB MKV 9.4 s -> 95 ms; the untouched cases still decode from the start.
Consequence: unnoticeable at song length (~100 ms for a three-minute file), but
an hours-long audiobook as M4A would still show the old wait.
Workaround / status: the index of MP4 and of Ogg is already understood elsewhere
in the app; using it here is the next step, noted in `NEXT.md`.

**One playback for the whole app.**
Why: the player belongs to the half that started it; opening a second track
replaces the first, and changing that half's folder ends the queue (the queue
belongs to the folder).

**Saving a video's sound works for MP4/M4V/MOV and MKV/WEBM/MKA - not for AVI or WMV.**
Why: the sound is lifted out byte for byte, which needs a reader per container.
Two are written (ISO-BMFF and Matroska); AVI and WMV are not planned.
Workaround / status: other containers say which one they are; their sound still
*plays* in player mode (Settings -> Audio -> show videos), it just cannot be saved.

**AAC from MKV/WEBM is saved as `.aac` (ADTS), not as `.m4a`.**
Why: the frames in Matroska carry no headers; the app builds one ADTS header per
frame from the file's own `CodecPrivate`. That is a valid, playable file, but a
different container than the `.m4a` an MP4 source produces.
Measured on a real file: the decoded waveform is identical to the source
(deviation 0.000000), with the usual one-frame encoder priming at the start.
Workaround / status: none needed - every player opens `.aac`. HE-AAC is written
as its LC core, which is what an ADTS header can express; the decoder still
picks up the extension from the stream itself.

**A raw stream (`.ac3`, `.eac3`, `.mp3`, `.aac`) starts at its first frame, not at
the video's timeline zero.**
Why: those files have no container and therefore no timestamps - the decoder
starts where the first frame starts. Measured against the source (waveform
identical, deviation 0.000000): AC-3 leads by 256 samples (5 ms), MP3 by 1105
samples (23 ms), E-AC-3 by 0.
Workaround / status: nothing to do; the offset is far below anything audible.
AAC leads by 1024 samples (21 ms) for the same reason.

**A 5.1 track stays 5.1.**
Why: the sound is copied, not re-encoded - mixing it down to stereo would mean
decoding and encoding again, which is exactly what this feature avoids.
Consequence: a `.eac3` from a film is a six-channel file; the player mixes it
down for playback, but the file keeps all channels (and its size).

**For Vorbis the timing of the written file is derived from the source's block
timestamps.**
Why: the exact value would need the block sizes from the setup header and the mode
of every packet. Audible difference: none; the displayed duration can be off by a
fraction of a second. Opus is exact (computed per packet). Measured on real
files (`bench_mkvextract`, 12 s of a WEB-DL): Vorbis +1 ms, Opus +14 ms against
the source, and the decoded waveform is identical in both cases (offset 0,
largest deviation 0.000000).

**After a jump, the saved file lands a few milliseconds away from where the
video would.**
Why: the two containers carry their timing differently - Matroska seeks to a
cluster, Ogg to a page granule. Measured with a foreign decoder (`ffmpeg`,
jump to 8 s): Opus 1 ms, Vorbis 18 ms earlier than the same jump in the source;
from the landing point onwards the samples are identical (Vorbis bit for bit,
Opus 1 of 32768 through the 16-bit rounding).
Workaround / status: nothing to do - the sound itself is unchanged, only the
point a player lands on differs.

**A fragmented MP4 whose fragments are missing is still refused.**
Why: fragmented files themselves work now (the sample tables are read from
`moof`/`traf`/`trun`), but a file that announces fragments (`mvex`) and contains
none is a head without a body - writing an empty sound file would be a lie.
Workaround / status: the message names the reason; such a file is usually a
truncated download.

**A sound track whose frames carry no length of their own cannot be saved
outside a container.**
Why: AC-3, E-AC-3, MP3 and AAC each land in a form that players accept (raw
stream, or ADTS headers built from the file's own description). DTS, ALAC and
raw PCM have no such target - they would need a container this app does not
write.
Workaround / status: the message names the codec case (`unsupported codec`).

**The saved file keeps the video's audio format.**
Why: nothing is re-encoded - that is the point (no quality loss, no encoder, no
extra dependency). What was AAC in the video is AAC in the `.m4a`.
Workaround / status: converting to MP3 or FLAC would need an encoder library; not
planned.

---

## Two-pane mode

**A boosted equalizer plays quieter than a flat one.**
What you notice: raise a band and the music gets noticeably quieter rather than
louder - one band at +12 dB costs about 12 dB of level.
Why: boosting cannot add headroom, it can only use it up. Either the peaks are
cut off (harsh, measured at 13-35 % distortion) or the whole signal is lowered to
make room. *Prevent clipping* does the latter, and it aims at the worst case: the
loudest frequency the filter chain can produce, not the average.
Workaround / status: turn the volume up, or drag the preamp back yourself - the
slider keeps working. Turning *Prevent clipping* off in Settings -> Audio brings
the old behaviour back, distortion included.

**Two halves, at most four open files** (two per half when split).
Why: a deliberate cap - beyond that the tiles are too small to work in.

**In true fullscreen a tile cannot be re-docked.**
Why: the header of a tile is also its drag handle for docking; while the chrome is
hidden there is nothing to grab.
Workaround: `F` or `Esc` brings it back.

---

**A folder full of very large PNGs takes a moment longer than one of JPEGs.**
Why: a JPEG is shrunk while it is being read (libjpeg decodes in DCT steps), a
PNG cannot be - it is always decoded at full size first (48 MB for a 12-megapixel
image) and only then scaled down. To keep that from filling memory, the app lets
only as many full-size decodes run at once as fit in a 192 MB budget: measured on
200 twelve-megapixel PNGs, peak memory drops from 421 MB to 323 MB and the first
screenful of 40 tiles takes 400 ms instead of 350 ms. With ten 27-megapixel PNGs
the peak drops from 809 MB to 187 MB, and there the first tile even appears twice
as fast.
Workaround / status: deliberate; JPEG folders are untouched (1.5 ms per tile) and
the second visit to any folder comes from the thumbnail cache (0.04 ms per tile).

## Editors

**A text file over 8 MB opens read-only.**
What you notice: the Save button is replaced by an orange *Read only* marker,
and typing does nothing. `Ctrl+S` is silent.
Why: `ViewerController::readTextFile` loads at most 8 MB so a huge log does not
freeze the window - only the BEGINNING of the file is in the editor. Writing that
back would delete everything past the cap. Measured before the lock existed: a
9,860,000-byte log lost 1,471,361 bytes (14.9 %) after a single keystroke plus
save, and the notice line "… [Datei gekürzt: > 8 MB]" was written into the file
along with it. Two locks now stand in the way - the surface refuses to edit
(`Viewer.textFileTruncated`), and `writeTextFile` refuses the write outright.
Workaround / status: use an external editor for such files. Raising the cap is
not the fix - the same 9.6 MB file already costs 2.3 s of frozen window just to
be laid out by `TextArea`, before any colouring. Loading such a file in pieces is
its own piece of work and has not been started.

**Typing in a very large file gets slower the larger the file is.**
What you notice: in a file of a few hundred thousand lines each keystroke lags.
Why: measured with the highlighter attached, ONE block is re-coloured per
keystroke - the syntax colouring is not the cost. What grows is Qt's own re-layout
of the document: 2 ms at 20,000 lines, 15 ms at 100,000, 41 ms at 240,000, and
the same numbers appear with the highlighter switched off. This predates the
syntax colouring.
Workaround / status: none inside the editor; the 8 MB cap above keeps it from
getting worse.


**The margin rulers move the page margins, not the paper.**
What you notice: pulling a margin in gives you a narrower column of text and
usually MORE pages - it does not shrink the document to fit.
Why: that is what a page margin is, and it is what Word does. The paper size
still comes from the document (`w:sectPr/w:pgSz`) and is left alone - a growing
sheet would no longer be A4 and would be rescaled by printers and viewers. The
earlier setting "extra margin for PDF export", which drew the page smaller into a
rectangle and left the page count untouched, has been removed: it was the
stopgap for exactly this, and the two would have added up.

**Scrolling while you hold a ruler handle counts towards the margin.**
What you notice: grab a margin handle, keep the button down and turn the wheel -
the handle stays under your pointer and the margin changes by exactly the
distance you scrolled, on top of whatever the mouse itself moved.
Why: deliberate, and necessary - an A4 page is taller than the window, so the
bottom margin cannot be reached without scrolling. The vertical ruler's scale
follows the scroll for as long as you hold a handle, instead of following the
page under your eye (which jumps by a whole page at a time). Measured
(`bench_docxruler`): the handle sits 1 px from the pointer, scrolling 367 px
adds exactly 97 mm to the margin, and letting go moves nothing (0 px).
Workaround / status: it means a long scroll makes a large margin - scroll only
as far as you need while holding, or let go, scroll, and grab again. The value
is still clamped so at least 10 mm of writing area remains. The horizontal ruler
is deliberately unaffected: vertical scrolling must not change the left and
right margins (measured: 35.5 mm with and without the wheel).

**Dragging a ruler changes the document.**
What you notice: the file is marked as modified and the margins are saved with
it - Word then shows the same margins.
Why: they live in the document (`w:sectPr/w:pgMar`), which is the only place
Word reads them from. `Ctrl+Z` takes a drag back as one step, and the reset
button on each ruler restores what the file came with. A document that had no
`w:sectPr` at all gets one written the first time you drag.

**A tab in a document becomes a space in the exported PDF's text.**
Why: Qt maps the space glyph to U+0009 in the PDF's `ToUnicode` table, so every
space in an exported file would be read back as a tab. That is corrected on the
way out (`core/PdfGlyphRuns`), and a real tab - which a page description draws as
blank space anyway, never as a glyph - is read back as a space along with it.
Workaround / status: deliberate; the alternative was tabs instead of spaces in
every exported file.

**A PDF made by another Qt program can still read back with its words split.**
Why: Qt's PDF engine writes one text object per glyph. On a page of short,
tightly spaced lines PDFium - which drives this app's search and Chrome's PDF
viewer - then takes the page for vertically written text and puts a line break
between the letters, so "Hallo" is read as "H" + "allo". Files written *by this
app* are repaired on the way out (measured on 44 lines of "Hallo wie geht es":
searching "Hallo" went from 0 hits to 132, and the rendered page is unchanged
pixel for pixel). A file that arrives from elsewhere is not touched.
Workaround / status: for a foreign file, a Poppler-based reader (Okular,
Evince, `pdftotext`) reads it correctly.

**The page number in the text-to-PDF export is fixed.**
Why: that export writes a centred "1/3" footer by design; only the DOCX export got
the choice of position and style.

**The DOCX editor rewrites only what you touch.**
Why: the file is kept as it came, so unknown parts survive untouched. Practical
limit: features the editor does not know are preserved but not editable.

**Dragging a margin ruler gets sluggish in a long document.**
What you notice: on a short document the margin follows the mouse smoothly; the
longer the document, the more the drag stutters.
Why: a margin change re-flows the whole document, and the ruler reports on every
mouse movement, so that work runs once per movement on the UI thread. Measured
(`bench_docxruler`, per mouse movement): 5.2 ms at 100 paragraphs, 18.4 ms at
400, 53.7 ms at 1600, **73.1 ms at 4000** - about 14 updates per second at the
top end.
Workaround / status: the view itself no longer moves while you drag (that was a
separate bug and is fixed), so the stutter is the only remaining cost; drag in
short steps, or set the margin and let go. Coalescing the updates was considered
and not built: the window system already merges mouse movements, so the
measurement says it would save nothing that is not already saved.

**A very long DOCX keeps its whole layout in memory.**
Why: the editor measures every paragraph to know where the pages break, and it
keeps the measured heights and line bands for all of them (only the shaped text
runs of the visible area are held). Measured on a generated 648-page document:
166 MB resident, of which 29 MB is the document itself. A 5-page file costs
15 MB.
Workaround / status: no upper limit is enforced; typing and paging stay fast at
that size (0.3 ms per keystroke), so the memory is the only cost.

**A very large photo is decoded at the size you are looking at, not at its own.**
Why: a 27-megapixel image is 108 MB in memory, and fitted into a window it shows
fewer than a twentieth of those pixels - with four files side by side that adds up
fast. The viewer therefore decodes in steps (the next power of two above the
displayed size) and switches to the full image at 100 % zoom, where it is sharp
again. Measured: 250 MB down to 157 MB for one open 27-megapixel photo; the fitted
view differs from the old one by 0.18 of 255 per pixel on average.
Workaround / status: deliberate. A JPEG even opens faster this way (55 ms -> 33 ms);
a PNG opens about 30 % slower (179 ms -> 233 ms), because it has to be decoded at
full size anyway and is then scaled down - the memory is the trade.
The decode step only ever *grows* while a file stays open, so making the window
large and then small again keeps the larger step in memory until you open
another file. That is the deliberate half of the trade: shrinking the window
would otherwise re-decode the picture and make it blink.

**What Word makes of the app's files has never been checked.**
Why: every test reads the file back with the app's own parser. Opening one in Word
is on the list (see `NEXT.md`).

**A search pattern never reaches across a line break.**
Why: every search runs per line (text editor) or per paragraph (DOCX) - as
`QTextDocument::find` always did, since it does not cross block boundaries
either. So `\d\n\d` finds nothing, and `.*` stops at the end of the line.
Workaround / status: deliberate. Searching across blocks would mean holding the
whole document as one string - at the 8 MB read cap that is a second copy of the
file for every keystroke in the search field.

**A replacement is inserted literally - `\1` does not put back what was found.**
Why: a hit can come from the literal branch or from the pattern branch (see
Settings -> General -> *Search with patterns*), and a back-reference has no
meaning for a literal hit. Making it work would require the mode switch the
whole design avoids.
Workaround / status: deliberate.

**A pathological pattern can make the editor hang for a moment.**
Why: Qt's regular expressions have no time limit, and a pattern such as
`(a+)+$` on a long line can take exponentially long (catastrophic
backtracking). This is a property of the engine, not of the file.
Workaround / status: none built. Such patterns are written on purpose rather
than by accident; the literal branch of the same search is unaffected, and the
pattern branch only runs at all when the term contains regex characters.

**In a PDF, the pattern branch stops after 500 hits per page.**
Why: each hit costs one `getSelectionAtIndex` call to get its rectangles, and a
pattern like `.` matches every character on the page. The literal branch (Qt's
own search model) is not capped.
Workaround / status: deliberate; write a more specific pattern.

---

## CSV and TSV files

**A `.txt` never becomes a table, even when it is one.**
What you notice: a semicolon-separated export saved as `.txt` - DATEV's own
*individual ASCII format* among them - opens in the text editor, and no table
button appears.
Why: the decision is made on the extension (`.csv`/`.tsv`) on purpose. Sniffing
the content of every `.txt` would turn log files, key-value dumps and anything
else with separators into tables, and a `.txt` is a text file first.
Workaround / status: rename the file to `.csv`, and it opens as a table. Whether
`.txt` should get an opt-in switch is an open question, kept in `NEXT.md` §3c.

**The table shows; it does not edit.**
What you notice: there is no cell editing, no inserting or deleting rows, no
sorting, no search, and no saving.
Why: display was built first on purpose. Editing needs a mutable model, undo,
and a writer that leaves the untouched parts of the file byte-for-byte alone -
each of those is its own piece of work.
Workaround / status: edit in the text view, which is a full editor. The order of
the next steps is recorded in `NEXT.md` §3c: search, then sorting, then editing.

**Very wide or very tall files are read up to 32 MB.**
What you notice: the footer says the file was too large and only the beginning
is shown.
Why: the same deliberate cap as the DATEV view. Measured with `bench_datev`:
835 bytes per row at 125 columns, so the cap is roughly 100,000 rows and 82 MB
of memory.
Workaround / status: the text view has its own, lower cap of 8 MB. Streaming the
table instead of holding it in memory has not been built.

**The separator and the header row are guessed, and cannot be corrected.**
What you notice: a file whose first lines are untypical - a long free-text
preamble, say - can end up split on the wrong character, or its first row is
taken for data when it is a heading. The footer states what was found, but there
is no switch to change it.
Why: the guess scores `;` `,` tab and `|` over the first 20 lines by how
consistent a field count each produces (`;` wins any tie), and the header row is
taken when line 1 carries no numbers and line 2 does. Manual switches for both
were built and then removed on request - they occupied the footer permanently
for a case that rarely arises.
Workaround / status: the text view shows the file as it is. If the guess turns
out to miss in practice, the switches are a small addition - the properties are
still there, only the buttons are gone.

**Tables in one file are split at blank lines only.**
What you notice: an export that stacks several tables without an empty line
between them stays one table. Conversely, a blank line in the middle of a single
table splits it into two tabs.
Why: the blank line is the only separator that is actually *in* the file. Every
other rule - a change in field count, a row without numbers - would be a guess,
and guessing wrong tears apart a file that was fine.
Workaround / status: the **All** tab always shows the file flat, exactly as it
stands, so nothing is hidden by a wrong split. `tests/uni_datenbank.csv` is the
sample this was built against (5 blocks, 167 rows including the 4 gaps).

**A block title is assumed, not known.**
What you notice: a data row that happens to hold a single field and sits at the
top of a block is taken for the block's name, and the row below it for its
column headings.
Why: that is the shape these exports have (title, headings, rows), and it is the
only way a text-only table can be recognised at all - a list of names, rooms and
office hours contains no number that would give the heading row away.
Workaround / status: the **All** tab shows every row as data. The rule is pinned
down in `tests/table/tst_delimited.cpp`, including the cases where it must *not*
fire (a single-column list, a title with only one row under it).

---

## DATEV files

**Most header fields are shown as "Dateikopf 7", "Dateikopf 8" and so on.**
What you notice: the fold-out header block names five fields (identifier, version
number, format name, creation time, currency) and shows the other 26 by position
only, even though they carry real values.
Why: only those five can be read off the file itself - the identifier spells
itself out, the timestamp parses as a timestamp, the currency as an ISO code. The
official field catalogue lives on `developer.datev.de`, and that page is a
JavaScript application: fetched as HTML it returns no content at all, so the
catalogue could not be taken from the authoritative source. Naming the rest from
memory would put unverified claims in front of a bookkeeper.
Workaround / status: every value is visible, only its label is missing. The
catalogue is a table (`src/datev/DatevFormat.cpp`, one entry per field and format
version) - filling it in is a single edit once the official list is at hand.

**A booking batch larger than 32 MB is cut off.**
What you notice: the footer says the file was too large and only the beginning is
shown; the totals then cover only the rows that were read.
Why: a deliberate cap in `DatevController`, separate from the 8 MB cap of the text
editor. Measured with `bench_datev`: 50,000 bookings are a 16.3 MB file, take
987 ms to parse and cost 40.8 MB of RSS (835 bytes per booking - the rows keep
only the fields that are filled; storing all 125 slots cost 4178 bytes per
booking, i.e. 204 MB for the same file). At the cap that is roughly 100,000
bookings and 82 MB.
Workaround / status: split the batch, or read it in the raw text view, which has
its own 8 MB cap. Streaming the table instead of holding it in memory has not
been built.

**No writing, no editing, no export.**
What you notice: the table has no edit mode and no save button, and the text view
of a DATEV file behaves like any other text file.
Why: deliberate. One wrongly written field in a bookkeeping file is a damage no
convenience makes up for.
Workaround / status: not planned to change.

**Only files that start with `"EXTF";` or `"DTVF";` become a table.**
What you notice: a DATEV export that does not carry that identifier in its first
line - notably a file written in DATEV's separate, user-configured *individual
ASCII format* - opens as plain text like any other `.csv` or `.txt`.
Why: the decision is made on that identifier alone, deliberately. Recognising a
booking batch by "it has semicolons and numbers" would hide every ordinary CSV in
a folder behind a bookkeeping view. A file that *does* carry the identifier works
regardless of how small it is: neither the number of columns nor their names are
hard-coded, and the totals columns are located by their heading (pinned down in
`tests/datev/tst_datevcsv.cpp` with a reduced batch of 5 header fields and 5
columns).
Workaround / status: read such a file in the text view. Supporting the individual
ASCII format needs its actual shape first - DATEV's own description of it could
not be retrieved (all three of their documentation hosts render their content in
the browser and return an empty document when fetched).

**A quote inside an unquoted region is guessed, not resolved.**
What you notice: a field written as `" "Normalabschr. immater. VermG" "` keeps its
inner quotes; other tools may show it without them.
Why: the sample contains exactly this, and DATEV's own writer produced it. The
reader treats a single `"` as a field end only when a separator or the line end
follows, so the inner quotes stay part of the text rather than truncating it
after `" "`. That is a decision, not a certainty: the file is genuinely ambiguous
at this point.
Workaround / status: the raw view shows the line as it stands. The case is pinned
down in `tests/datev/tst_datevcsv.cpp` so it does not change unnoticed.

---

## Not built yet

Not limits of the built thing - **planned work**, kept here so there is one place
to look. Once something ships, its entry moves out (into **[FEATURES.md](FEATURES.md)**), and only
what it still cannot do stays behind in the sections above.

- **Spell checking for Japanese** - there is no Hunspell dictionary for it, and
  the approach does not fit: Japanese does not separate words by spaces, while
  Hunspell checks word by word. It would need a different engine altogether (a
  morphological analyser such as MeCab, which first has to split the sentence
  into words), i.e. a new dependency plus its own dictionary and a second
  checking path next to Hunspell - a separate piece of work, not started. Arabic, by contrast, only needs the `hunspell-ar` dictionary
  installed - no code change.
- **Whitespace markers** (Kate's `»` for tabs and dots for spaces) are
  deliberately not built - the user decided against them on 2026-09-02.
- **More languages.** 27 are covered. Each new one is a table entry in
  `src/editor/LanguageTable.cpp` plus a section in the test driver; no scanner
  code changes as long as one of the five scanner kinds fits.
- **Writing tags** (changing title or artist of an audio file) - reading is solid,
  writing is deliberately not built: one wrong byte damages the file.
- **Searching inside a table.** The next step for the CSV view, and the only one
  already decided: same shape as the editor's find bar (`Ctrl+F` opens, `Esc`
  closes). Still open: whether it searches every column or a chosen one, whether
  it jumps to the row or filters, and whether it stays inside the selected block.
- **Sorting, hiding columns, freezing the first column, copying a cell.** All
  deliberately deferred - each is its own piece of work, and the order they are
  built in is a decision to take rather than to drift into.
- **Editing a table and writing it back.** Reading is solid; editing needs a
  mutable model, undo, and a writer that leaves the untouched part of the file
  byte-for-byte alone. Three constraints are already fixed for whenever it
  starts: every cell stays text (guessing a type and reformatting on save
  destroys data), only changed rows get rewritten, and the changes live as an
  overlay beside the compact rows rather than replacing them. A formula engine
  is explicitly *not* part of this - a CSV cannot store one anyway.
- **`.txt` as a table.** Only `.csv` and `.tsv` open as tables; a semicolon
  export saved as `.txt` stays text. An opt-in switch would be small and needs
  no guessing, but it has not been asked for.


---

*Found something that belongs here? It goes into this file with reason and, where
possible, a measurement - not into the changelog.*
