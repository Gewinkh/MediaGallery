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

**Two halves, at most four open files** (two per half when split).
Why: a deliberate cap - beyond that the tiles are too small to work in.

**A file cannot be dragged from one half into the other.** Not built yet.

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

**The extra margin for DOCX-to-PDF makes the content smaller, not the paper bigger.**
Why: the paper size comes from the document (`w:sectPr`) and stays - a growing
sheet would no longer be A4 and would be rescaled by printers and viewers. So the
page is drawn into a smaller rectangle instead, which shrinks the text by the same
factor.
Workaround / status: deliberate (your choice); 0 mm keeps the previous result.

**The page number in the text-to-PDF export is fixed.**
Why: that export writes a centred "1/3" footer by design; only the DOCX export got
the choice of position and style.

**The DOCX editor rewrites only what you touch.**
Why: the file is kept as it came, so unknown parts survive untouched. Practical
limit: features the editor does not know are preserved but not editable.

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

**What Word makes of the app's files has never been checked.**
Why: every test reads the file back with the app's own parser. Opening one in Word
is on the list (see `NEXT.md`).

---

## Not built yet

Not limits of the built thing - **planned work**, kept here so there is one place
to look. Once something ships, its entry moves out (into **[FEATURES.md](FEATURES.md)**), and only
what it still cannot do stays behind in the sections above.

- **Dragging a file from one half of the split view into the other** - dropping it
  on a *folder tile* in the other half already works (it is copied there); what is
  missing is dropping it into that half's open folder, and moving instead of
  copying.
- **Writing tags** (changing title or artist of an audio file) - reading is solid,
  writing is deliberately not built: one wrong byte damages the file.

---

*Found something that belongs here? It goes into this file with reason and, where
possible, a measurement - not into the changelog.*
