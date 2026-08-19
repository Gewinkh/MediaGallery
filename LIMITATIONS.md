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

**Deleting goes to the trash - where there is one.**
Why: on systems without a working trash the app refuses to delete rather than
removing a file for good.

---

## Tags and categories

**Tags and categories belong to a folder**, not to the whole library.
Why: they live in the folder's JSON sidecar next to the media, so a folder can be
moved or copied and keeps its metadata.
Consequence: a tag created in one folder does not appear in another; moving a file
into another folder carries its tag with it only if the target folder does not
already define that tag differently.

**Deleting a tag removes it from every file in that folder.**
Why: there is one definition per folder; the panel asks before it does this.

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

---

## Audio player

**Track changes are not gapless.**
Why: the next track is not pre-decoded yet, so a change can leave a short pause on
slow media. Planned - see [Not built yet](#not-built-yet).

**No cover art, no artist - the player shows the file name.**
Why: reading those needs an ID3/Vorbis parser, which is not built yet. The large
view draws a placeholder instead.

**The equalizer applies to audio files only, never to video.**
Why: `QMediaPlayer` does not hand out its samples, so the app runs its own
decode -> ring buffer -> equalizer -> sink chain for audio. Video keeps using
`QMediaPlayer` unchanged.

**Seeking restarts the decoder.**
Why: `QAudioDecoder` cannot seek in Qt 6; a jump decodes from the start and
discards. Measured: 21 ms for a 5-second file, ~100 ms for a three-minute MP3 -
not noticeable in practice, but it is real work.

**One playback for the whole app.**
Why: the player belongs to the half that started it; opening a second track
replaces the first, and changing that half's folder ends the queue (the queue
belongs to the folder).

---

## Two-pane mode

**Two halves, at most four open files** (two per half when split).
Why: a deliberate cap - beyond that the tiles are too small to work in.

**A file cannot be dragged from one half into the other.** Not built yet.

**In true fullscreen a tile cannot be re-docked.**
Why: the header of a tile is also its drag handle for docking; while the chrome is
hidden there is nothing to grab.
Workaround: `F` or `Esc` brings it back.

---

## Editors

**The DOCX editor rewrites only what you touch.**
Why: the file is kept as it came, so unknown parts survive untouched. Practical
limit: features the editor does not know are preserved but not editable.

**What Word makes of the app's files has never been checked.**
Why: every test reads the file back with the app's own parser. Opening one in Word
is on the list (see `NEXT.md`).

---

## Not built yet

Not limits of the built thing - **planned work**, kept here so there is one place
to look. Once something ships, its entry moves out (into **[FEATURES.md](FEATURES.md)**), and only
what it still cannot do stays behind in the sections above.

- **Gapless track changes**: the next track is not pre-decoded yet, so a change can leave a short pause on slow media.
- **Video-to-audio converter**: extract the sound of a video file so it can join the player queue.
- **Cover art and track names from the file's own tags** (ID3 and friends): the player shows the file name and a drawn placeholder.
- **Dragging a file from one half of the split view into the other.**

---

*Found something that belongs here? It goes into this file with reason and, where
possible, a measurement - not into the changelog.*
