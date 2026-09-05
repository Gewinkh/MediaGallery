# Third-Party Licenses

MediaGallery itself is licensed under the MIT License (see [`LICENSE`](LICENSE)).
This document lists the third-party software MediaGallery builds against or uses
at run time, together with the license that applies to each of them.

**The MIT License of MediaGallery applies only to MediaGallery's own source code**
- the files tracked in this repository. It does not extend to anything listed
below, and nothing in this file places any third-party component under the MIT
License. In particular:

- Qt and the other direct dependencies are **not** covered by MediaGallery's MIT
  license; each stays under its own license and copyright.
- Third-party code **contained inside** a dependency - PDFium inside Qt PDF,
  Chromium inside Qt WebEngine, and the components Qt bundles in its base
  modules - likewise stays under its own license. Being reached only indirectly,
  through a dependency's public API, changes nothing about that.
- Runtime backend dependencies used by a dependency - such as FFmpeg used by
  Qt Multimedia - likewise remain under their own licenses. Their presence,
  version and applicable license depend on the selected backend and the concrete
  deployment configuration.
- Distributing MediaGallery's own source code under MIT grants no rights in any
  of these components.

Layering used throughout this document:

```text
MediaGallery source code        -> MIT
Direct dependencies             -> their own license
  Qt module (Qt-specific code)  -> the license of that Qt module
  third-party code inside it    -> its own license, per component
Transitive dependencies         -> their own license
  runtime backend dependencies  -> their own license, per component
```

---

## Scope and how to read this file

**Third-party source code in this repository:** none was identified in the
repository state examined. Verified against the repository state: no git
submodules and no `.gitmodules`, no `FetchContent` or `ExternalProject`
declaration in any CMake file, no package-manager manifest
(`vcpkg.json`, `conanfile.*`), and no vendored third-party directory.

**How the dependencies reach the binary - three different paths.** Keeping them
apart matters, because only the first is visible in MediaGallery's build files:

1. **Direct dependencies.** The libraries MediaGallery's own build resolves and
   links, and whose API its source code calls: the Qt modules named in
   `find_package(Qt6 ...)`, plus zlib, Tesseract and Hunspell. Each is resolved
   from the build environment; none is copied into this tree.
   Leptonica is a transitive dependency of Tesseract: the build names it because
   Tesseract's pkg-config call requests it, but MediaGallery's own code never calls
   it; it is documented as [transitive](#leptonica).
2. **Third-party code contained inside a direct dependency.** PDFium inside
   Qt PDF, Chromium inside Qt WebEngine, and the third-party components Qt
   bundles in its base modules. MediaGallery's build files do not mention these
   components directly; they arrive with the corresponding Qt libraries.
3. **Runtime backend dependencies of a direct dependency.** For example,
   FFmpeg may be used by the Qt Multimedia backend. These components are not
   necessarily contained inside the direct dependency itself; their presence,
   version and applicable license depend on the selected backend and the
   deployment configuration.

The statement "no third-party source code in this repository" therefore says
nothing about categories 2 and 3. Those components are not in this tree, but
they may be present in the binaries a distribution would carry or require.

The categories below are documented separately, because each carries different
consequences:

| Category | Present in this repository? | Where it is documented |
|---|---|---|
| Third-party **source code** in this tree | no | - |
| **Direct dependencies** (resolved by this project's build, linked) | linked, not contained | [Qt](#qt), [zlib](#zlib), [Tesseract OCR](#tesseract-ocr), [Hunspell](#hunspell) |
| **Transitive / embedded components** (inside a direct dependency) | not in this repository; part of the relevant dependency build | [Leptonica](#leptonica), [PDFium](#pdfium-inside-qt-pdf), [Chromium](#chromium-inside-qt-webengine), [inside Qt](#third-party-components-inside-qt) |
| **Runtime backend dependencies** of direct dependencies | deployment-dependent | [FFmpeg](#ffmpeg-the-backend-of-qt-multimedia) |
| **Externally sourced or derived data** in MediaGallery's own files | **yes** | [Externally sourced data](#externally-sourced-data) |
| **Implementations of published algorithms** in MediaGallery's own files | **yes** | [Acknowledgements](#acknowledgements) |

Three roles are distinguished throughout, because they carry different
obligations:

| Role | Meaning |
|---|---|
| **Build-time** | Must be present to compile and link MediaGallery. |
| **Run-time** | Loaded by the finished application, directly or through Qt. |
| **Shipped** | Actually redistributed inside a binary distribution of MediaGallery. |

**No binary distribution exists today.** The project's `CMakeLists.txt` contains
only `install(TARGETS MediaGallery ...)`; there is no `windeployqt`, `macdeployqt`,
bundling or packaging step. Which components end up being *shipped* therefore
depends entirely on how a future distribution is assembled, and the sections
below mark where that matters.

Version numbers below are those of the reference development environment
(Arch Linux distribution packages). Other build environments will use different
versions; the licenses named are those the respective upstream projects document
for the versions given.

---

## Overview

Each Qt row in the table states the license of the **Qt-specific code** of that
module. Qt modules additionally contain third-party code under separate licenses;
see [Third-party components inside Qt](#third-party-components-inside-qt).

| Component | Kind | Version | License | Role | Optional |
|---|---|---|---|---|---|
| Qt Core, Gui, Qml, Quick, Quick Controls, Svg | direct | 6.11.2 | LGPL-3.0 or GPL-2.0 | build + run | no |
| Qt Multimedia | direct | 6.11.2 | LGPL-3.0 or GPL-2.0 | build + run | no |
| Qt PDF | direct | 6.11.2 | LGPL-3.0 or GPL-2.0 | build + run | no |
| Qt WebEngine / WebEngineQuick | direct | 6.11.2 | LGPL-3.0, GPL-3.0 or GPL-2.0 (Qt-specific parts) | build + run | no |
| PDFium (inside Qt PDF) | embedded in Qt PDF | see Qt PDF | BSD (see below) | run | no |
| Chromium (inside Qt WebEngine) | embedded in Qt WebEngine | see Qt WebEngine | BSD-3-Clause, plus many further licenses inside Chromium | run | no |
| FFmpeg | runtime backend dependency | build-dependent | build-dependent; see [FFmpeg](#ffmpeg-the-backend-of-qt-multimedia) | run when the FFmpeg backend is used | deployment-dependent |
| zlib | direct | 1.3.2 | Zlib License | build + run | **yes** |
| Tesseract OCR | direct | 5.5.3 | Apache-2.0 | build + run | **yes** |
| Leptonica | transitive (via Tesseract) | 1.87.0 | BSD-2-Clause | build + run | **yes** |
| Hunspell | direct | 1.7.3 | MPL-1.1 / GPL-2.0 / LGPL-2.1 tri-license | build + run | **yes** |
| Adobe font metric data (derived numeric values) | data in this tree | - | origin: Adobe AFM data (`APAFML`); applicability to the reproduced values **`Needs verification`** | data in source | no |
| Adobe Glyph List names (subset) | data in this tree | - | BSD-3-Clause (AGL data as published by Adobe) | data in source | no |
| CP1252 code page mapping (32 values) | data in this tree | - | character encoding mapping; no code taken | data in source | no |
| DATEV file format (`EXTF`/`DTVF`) | format read by own code | 700 | no material taken; official field catalogue **`Needs verification`** | reading only | no |

---

## Qt

Qt is an external dependency of MediaGallery. MediaGallery contains no Qt source
code. It is built against **Qt 6.4 or newer** (`find_package(Qt6 6.4 REQUIRED ...)`)
and developed against **Qt 6.11.2**.

### Qt components MediaGallery actually uses

Declared in `CMakeLists.txt` (`find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui
Qml Quick QuickControls2 Multimedia Pdf Svg WebEngineQuick)`) and linked into the
application:

| Qt component | Used for | License (open source) |
|---|---|---|
| **Qt Core** | Base types, file I/O, threading, settings | LGPL-3.0 or GPL-2.0 |
| **Qt Gui** | Images, fonts, painting, `QGuiApplication` | LGPL-3.0 or GPL-2.0 |
| **Qt Qml** | QML engine, singleton and type registration | LGPL-3.0 or GPL-2.0 |
| **Qt Quick** | The entire user interface, scene graph | LGPL-3.0 or GPL-2.0 |
| **Qt Quick Controls** (`Qt6::QuickControls2`) | Standard controls; MediaGallery ships its own style for them | LGPL-3.0 or GPL-2.0 |
| **Qt Multimedia** | Audio and video playback, audio sink for the equalizer | LGPL-3.0 or GPL-2.0 |
| **Qt PDF** | PDF rendering, page and text access | LGPL-3.0 or GPL-2.0 |
| **Qt SVG** | SVG image support in the gallery | LGPL-3.0 or GPL-2.0 |
| **Qt WebEngineQuick** | HTML preview only, initialized lazily on first use | LGPL-3.0, GPL-3.0 or GPL-2.0 (Qt parts) |

**Qt Widgets is NOT used.** MediaGallery uses `QGuiApplication`; there is no
dependency on Qt Widgets anywhere in the tree.

`Qt6::GuiPrivate` is used **only** by the internal regression test drivers under
`tests/`, never by the application, and only when that component is present. The
test drivers are `EXCLUDE_FROM_ALL` targets built through the `mg_tests`
collector, so an ordinary `cmake --build` never links them.

For each of Qt Core, Gui, Qml, Quick, Quick Controls, Multimedia, PDF and SVG,
the Qt 6.11 documentation states that the module is available under commercial
licenses from The Qt Company and, in addition, under free software licenses: the
GNU Lesser General Public License, version 3, or the GNU General Public License,
version 2. Qt WebEngine is the one exception in this list: its Qt-specific parts
are documented as available under LGPL-3.0, **or GPL-3.0**, or GPL-2.0.

Two qualifications matter and are easy to lose:

- These statements cover the **Qt-specific code** of each module. Every module
  may additionally contain third-party code under its own license, which the Qt
  documentation lists separately per module.
- Qt notes that some Qt modules are available only under the GPL rather than the
  LGPL. None of the modules MediaGallery uses is in that group.

- Qt licensing overview: https://doc.qt.io/qt-6/licensing.html
- Third-party code used in Qt: https://doc.qt.io/qt-6/licenses-used-in-qt.html

### Third-party components inside Qt

These are not dependencies of MediaGallery's own code, but they are present in
the Qt libraries MediaGallery loads, and they matter for a binary distribution.

Qt's own summary of the rule is that you need to acknowledge and comply with the
licenses of the third-party components you actually ship. Qt documents these per
module, and from Qt 6.8 onwards also as SBOM documents in SPDX 2.3 format:
https://doc.qt.io/qt-6/licenses-used-in-qt.html

#### Third-party code in the base modules

The permissively licensed third-party code inside Qt is not limited to the three
large cases below. Qt GUI 6.11.2 documents multiple third-party modules/components
under permissive licenses, including FreeType, HarfBuzz, libjpeg-turbo, libpng,
Pixman, Vulkan-related components, and others. Qt Core, Qt QML and Qt Quick carry
their own third-party component lists as well. These components are part of the
corresponding Qt libraries when those libraries are distributed. The per-module
attribution pages and the SPDX SBOM for the exact Qt build are authoritative for
the complete set and their applicable licenses.

#### PDFium (inside Qt PDF)

**PDFium is not a dependency of MediaGallery.** It is embedded in Qt PDF, and
MediaGallery reaches it only through Qt PDF's public API: the source tree uses
`QPdfDocument` and its siblings in about 85 places and contains no PDFium header,
no `FPDF_*` call and no PDFium symbol. The two occurrences of `FPDF_FFLDraw` in
`src/pdf/` are comments explaining what Qt PDF does *not* expose. PDFium is
nevertheless part of the Qt PDF library a distribution ships, which is why it is
documented here.

For **Qt 6.11**, Qt's attribution pages list PDFium as **BSD** and reproduce
a notice reading `Copyright 2014 The PDFium Authors` with three conditions.
Qt also reproduces the text of the Apache License, Version 2.0, applicable to
parts of the PDFium snapshot. Upstream PDFium's own `LICENSE` carries the
same BSD-3-Clause text plus Apache-2.0.

Qt documents the following third-party components **inside the PDFium snapshot**.
They are one level further in - components of a component - and are listed here
so that a distribution's attribution set is not built from Qt PDF alone. The
licenses are those Qt states for the Qt 6.11 snapshot; another Qt version may
carry a different set, and the per-module attribution page for the exact Qt build
is authoritative:

| Inside the PDFium snapshot | License as documented by Qt |
|---|---|
| Abseil | Apache-2.0 |
| FreeType | FTL |
| The Chromium Project | BSD-3-Clause |
| fast_float | MIT |
| ICU | MIT |
| libjpeg-turbo | IJG, BSD-3-Clause, Zlib |
| libpng | Libpng-2.0, Libpng |
| zlib | Zlib |

zlib therefore appears twice in this document for different reasons: once as a
direct, optional dependency of MediaGallery, and once as a component inside
Qt PDF's PDFium snapshot. The two are separate copies with separate provenance.

- https://doc.qt.io/qt-6/qtpdf-licensing.html
- https://doc.qt.io/qt-6/qtpdf-3rdparty-pdfium.html
- https://doc.qt.io/qt-6/licenses-used-in-qt.html

#### Chromium (inside Qt WebEngine)

Qt WebEngine is built on Chromium. Qt documents Chromium itself as
**BSD 3-Clause "New" or "Revised" License**, and states that
*"The Chromium part contains code available under various licenses, with the most
restrictive license being the GNU Lesser General Public License v2.1 (LGPL 2.1)."*
Qt's attribution page lists well over a hundred distinct third-party components
inside Chromium.

MediaGallery initializes Qt WebEngine **lazily**, only when an HTML file is
previewed. The dependency exists nonetheless: the module is linked at build time
and its libraries are part of any distribution that keeps the HTML preview.

- https://doc.qt.io/qt-6/qtwebengine-licensing.html

#### FFmpeg (the backend of Qt Multimedia)

MediaGallery does **not** depend on FFmpeg directly: its build files never name
it and its source code contains no FFmpeg include and no call to any `libav*`
function (checked across `src/` and `main.cpp`). In MediaGallery's own source code,
FFmpeg is reached only indirectly through Qt Multimedia's backend plugin.

Qt Multimedia selects a backend at run time. Qt documents the **FFmpeg backend
as the default on desktop Linux**, and `QT_MEDIA_BACKEND` can select another one
(for example `gstreamer`). MediaGallery does not set that variable and does not
select a backend itself, so whichever backend Qt picks in a given environment is
the one that applies. On the reference environment the FFmpeg backend is present
as `libffmpegmediaplugin.so` and links `libavformat`, `libavcodec`,
`libswresample`, `libswscale` and `libavutil`.

**The applicable FFmpeg licensing depends on the concrete FFmpeg build used by
the deployment, including which optional components are enabled.** Three cases must be kept apart:

| Case | FFmpeg build | License |
|---|---|---|
| **(a) Qt's own binary packages** | Qt documents the version shipped with Qt as **7.1.3** | Qt documents it as *"GNU Lesser General Public License v2.1 or later and BSD 3-Clause 'New' or 'Revised' License and BSD 2-Clause 'Simplified' License and BSD Source Code Attribution and ISC License and MIT License and Mozilla Public License 2.0"*, and states that its pre-built FFmpeg libraries include only features that agree with those permissive licenses |
| **(b) A distribution's system FFmpeg** | whatever the distribution ships | whatever that build's configuration implies; see below |
| **(c) The build a given distribution of MediaGallery actually ships or requires** | not determinable from this repository | follows from (a) or (b), and must be established per distribution |

Case (b) is not hypothetical and is the case on the reference environment: there
the Qt FFmpeg plugin resolves to `libavcodec.so.63` from the distribution's own
`ffmpeg` package (9.0.1), and that package's `configure` line contains
`--enable-gpl` and `--enable-version3`. FFmpeg's own legal page states that its
base license is *"the GNU Lesser General Public License (LGPL) version 2.1 or
later"* and that *"FFmpeg incorporates several optional parts and optimizations
that are covered by the GNU General Public License (GPL) version 2 or later. If
those parts get used the GPL applies to all of FFmpeg."* A build configured with --enable-gpl may include GPL-covered components, in which case the applicable licensing obligations for that FFmpeg build include GPL-licensed code.

**This is a statement about that one build, not about FFmpeg in general.** A differently
configured build may have a different applicable license set.
Qt's own pre-built FFmpeg libraries are documented separately; a distribution
build must be assessed from the concrete FFmpeg build and the components it
contains. Qt's binary packages link FFmpeg dynamically, and Qt notes that applications
*"must either bundle FFmpeg binaries in their installer or depend on FFmpeg being installed
on the operating system"* - which is exactly why the applicable license follows the
deployment decision and cannot be fixed here. See
[Needs verification](#needs-verification).

Qt Multimedia also contains additional third-party components besides FFmpeg.
The complete set depends on the exact Qt build and is documented by Qt in the
module's third-party attribution information and corresponding SPDX SBOM.

Qt Multimedia's documentation additionally warns that video compression standards
such as H.264 may be covered by patents and can incur royalty fees, and that
*"The Qt licenses do not cover such fees."* This applies to MediaGallery's video
playback as well; patent questions are separate from license questions.

- https://doc.qt.io/qt-6/qtmultimedia-index.html
- https://doc.qt.io/qt-6/qtmultimedia-attribution-ffmpeg.html
- https://www.ffmpeg.org/legal.html

---

## zlib

**Optional.** Enables the DOCX editor. `find_package(ZLIB)`; when found,
`MG_HAVE_ZLIB` is defined and `mg_core` links `ZLIB::ZLIB`. When absent,
`src/core/ZCodec.cpp` falls back to Qt's `qCompress`/`qUncompress` and only DOCX
support is disabled - every PDF feature stays available.

- **License:** Zlib License
- **Copyright:** Jean-loup Gailly and Mark Adler
- **Version:** 1.3.2
- **Official source:** https://zlib.net/ - https://github.com/madler/zlib
- **License text:** https://zlib.net/zlib_license.html
- **Used for:** raw Deflate/Inflate for the OOXML (ZIP) entries of the DOCX
  container. ZIP stores entries as raw deflate streams, which is why Qt's
  zlib-wrapped `qCompress`/`qUncompress` cannot replace it for DOCX.
- **Note:** zlib also appears as a transitive component inside Qt PDF's PDFium
  snapshot.

**How MediaGallery uses it.** zlib is an external dependency, resolved from the
build environment and linked. Its source code is **not** contained in this
repository, and MediaGallery does not modify or fork it. No altered zlib source
is distributed by this project.

**Terms.** The Zlib License permits use for any purpose including commercial
applications, and permits altering and redistributing the software, subject to
three conditions:

1. The origin of the software must not be misrepresented; you must not claim you
   wrote the original software. An acknowledgment in product documentation is
   appreciated but not required.
2. Altered source versions must be plainly marked as such and must not be
   misrepresented as being the original software.
3. The license notice may not be removed or altered from any source distribution.

For MediaGallery in its current form, conditions 1 and 3 are the operative
ones: a distribution that redistributes zlib must comply with the Zlib License.
In particular, the license notice must not be removed or altered from any source
distribution. Condition 2 concerns altered source versions; this project ships
none today. This is a reading of the license text, not a legal determination.

Separately from the license, the zlib project states on its official page that
zlib is "legally unencumbered -- that is, not covered by any patents". This is an
assertion by the zlib authors about the library, not a license term, and it is
recorded here only because codec and compression components elsewhere in this
document do carry patent considerations.

---

## Tesseract OCR

**Optional.** Enables OCR for scanned PDFs. Found via
`pkg_check_modules(TESSERACT IMPORTED_TARGET tesseract lept)`; when found,
`MG_HAVE_TESSERACT` is defined and `mg_pdf` links `PkgConfig::TESSERACT`. When
absent, the OCR feature is disabled and the application states why.

- **Kind:** direct dependency - MediaGallery calls the Tesseract API itself.
- **License of the library:** Apache License, Version 2.0. Upstream states:
  *"The code in this repository is licensed under the Apache License, Version
  2.0."*
- **Version:** 5.5.3 (reference environment)
- **Official source:** https://github.com/tesseract-ocr/tesseract
- **Used by:** `src/pdf/OcrEngine.{h,cpp}` via `<tesseract/baseapi.h>` and
  `<tesseract/publictypes.h>`

**The trained data is a separate work with a separate license.** Tesseract needs
language data (`*.traineddata`) to recognise anything, and that data is *not*
part of the library:

- The Apache-2.0 license above covers the **Tesseract source code**. It says
  nothing about the license of any `*.traineddata` file, and no conclusion about
  the data may be drawn from it. Trained data is published in separate upstream
  repositories under its own terms, which differ by repository and by language.
- MediaGallery ships **no** trained data and contains none. It uses whatever the
  system provides, through Tesseract's own data path.
- Anyone shipping language data with a binary distribution has to establish the
  license of each data file used, from the source that data actually came from.

---

## Leptonica

**Transitive dependency.** Leptonica is a mandatory dependency of Tesseract, not
of MediaGallery: the source tree contains no Leptonica include and calls no
Leptonica function (checked across `src/` and `main.cpp`). It appears in this
project's build only because the same call requests it alongside Tesseract -
`pkg_check_modules(TESSERACT IMPORTED_TARGET tesseract lept)` - so it is named in
the build files while still being reached only through Tesseract.
In MediaGallery's current build configuration, Leptonica is resolved together
with Tesseract and is therefore present when the Tesseract feature is enabled.
Its exact presence in a binary distribution depends on how Tesseract is packaged
and linked.

- **Kind:** transitive (via Tesseract); named in the build files, not used by
  MediaGallery's own code.
- **License:** BSD 2-Clause License. The license text installed with version
  1.87.0 carries two numbered conditions and no "endorsement" clause.
- **Copyright:** `Copyright (C) 2001-2020 Leptonica. All rights reserved.`
- **Version:** 1.87.0 (reference environment)
- **Official source:** https://github.com/DanBloomberg/leptonica

---

## Hunspell

**Optional.** Enables spell checking in the DOCX editor. Found via
`pkg_check_modules(HUNSPELL IMPORTED_TARGET hunspell)`; when found,
`MG_HAVE_HUNSPELL` is defined and `mg_core` links `PkgConfig::HUNSPELL`. When
absent, spell checking stays off and the settings page states why.

- **License:** MPL-1.1 / GPL-2.0 / LGPL-2.1 tri-license. The upstream README
  states that Hunspell is *"licensed under LGPL/GPL/MPL tri-license"*; the
  `COPYING` file in the repository carries the GNU General Public License,
  Version 2.
- **Version:** 1.7.3
- **Official source:** https://github.com/hunspell/hunspell
- **Used by:** `src/core/SpellChecker` via `<hunspell/hunspell.hxx>`
- **External data - a separate work with a separate license:** Hunspell requires
  dictionaries (`.aff`/`.dic`), and the tri-license above covers the **library**,
  not any dictionary. No conclusion about a dictionary's license may be drawn
  from Hunspell's own. MediaGallery
  ships **no** dictionaries; it searches the system locations
  (`/usr/share/hunspell`, `/usr/share/myspell/dicts`, `/usr/local/share/hunspell`,
  `/Library/Spelling`) and the user's own `dictionaries` folder. Dictionaries
  carry their own licenses - frequently GPL, LGPL, MPL or LGPL/GPL/MPL
  tri-licensed - which are **not** covered by this document. Anyone shipping
  dictionaries with a binary distribution must check those licenses separately.

Under the tri-license a redistributor chooses one of the three licenses. Because
Hunspell is optional and MediaGallery contains no Hunspell code, a build made
without it has no Hunspell library to accompany. Obligations arising from a
dictionary someone adds are a separate matter (see above).

---

## Externally sourced data

MediaGallery's own source files contain a small number of data tables whose
values originate outside this project. This is data, not source code: no
third-party code is present in any of the files below.

### Adobe font metric data (`src/pdf/edit/PdfBaseFontWidths.cpp`)

The PDF specification permits a PDF to omit `/Widths` for the 14 standard fonts,
because a viewer is expected to know their metrics. To edit text on such pages,
MediaGallery carries a table of those character widths.

**Origin of the numbers:** they were generated from the `devps` font metric files
of **groff** (`/usr/share/groff/*/font/devps/`). Those files are themselves
generated by groff's `afmtodit` from Adobe's AFM files for the 14 standard fonts,
and they preserve the upstream notice
`Copyright (c) 1985, 1987, 1989, 1990, 1997, 1998, 1999 Adobe Systems Incorporated.
All Rights Reserved.`

- **License of the underlying AFM data:** Adobe Postscript AFM License, SPDX
  identifier **`APAFML`**. Its terms permit use, copying and distribution for any
  purpose and without charge, with or without modification, provided all copyright
  notices are retained, modifications are prominently noted, and the license
  paragraph itself is not modified. This is the license of the **AFM files**;
  whether it reaches the numeric values reproduced here is a separate question
  and is **not** treated as settled by this document.
- **SPDX reference:** https://spdx.org/licenses/preview/APAFML.html
- **groff:** GPL-3.0-or-later; groff is used only as the source of the metric
  files during development. It is **not** a build-time or run-time dependency of
  MediaGallery, and no groff code is present in this repository.
- **What is present in MediaGallery:** numeric width values only (character code
  to width in 1/1000 em). No AFM file, no font file, no font outline and no groff
  code is contained or shipped.
- **Open question:** whether reproducing these numeric metrics alone is subject
  to the `APAFML` conditions is not settled here. See
  [Needs verification](#needs-verification).

MediaGallery contains **no font files** of any kind.

### Adobe character encodings and glyph names (`src/pdf/edit/PdfEncodings.cpp`)

To map bytes of an embedded PDF text string to characters, MediaGallery carries
tables for `/WinAnsiEncoding` and `/MacRomanEncoding`, plus the subset of Adobe
Glyph List names those encodings and Latin-1 require.

- The encodings are defined in the PDF specification (ISO 32000-1, Annex D).
- The glyph names come from the **Adobe Glyph List**. Adobe publishes the AGL data
  at https://github.com/adobe-type-tools/agl-aglfn under the **BSD 3-Clause
  License**, copyright Adobe.
- Only the subset needed for those encodings is present. No Adobe source code is
  contained.

### CP1252 code page mapping (`src/core/TextEncoding.cpp`)

- **Origin of the values:** generated on the reference machine from Python's
  built-in `cp1252` codec (`bytes([b]).decode('cp1252')`), i.e. from the standard
  library present locally, not copied from a document or another project.
- **What is present in MediaGallery:** 32 numeric code points. No code and no
  table file from any third party.

### DATEV file format (`src/datev/`)

MediaGallery reads DATEV booking batches (`EXTF`/`DTVF`, format version 700). The
reader, the tokenizer and the table view are written for this project; **no DATEV
code, library, API or documentation file is contained or shipped**.

- The test files provide concrete examples of the DATEV format, including the identifier, separator,
 quoting and column names. The reader itself is MediaGallery's own implementation.
- No column table, no specification text and no other DATEV material has been
  copied into this repository.

---

## Other Components

### Operating system and toolchain

The C++ standard library, the C runtime and the platform's graphics stack are
supplied by the build and run environment and are not listed individually here.
One platform-specific call is used directly: `malloc_trim` from glibc on Linux
(`src/core/MemoryUtils.h`), guarded by `Q_OS_LINUX`.

### No network access from MediaGallery's own code

MediaGallery's own code performs no network access: `src/` and `main.cpp` contain
no `QNetworkAccessManager`, no `QTcpSocket`/`QUdpSocket`/`QSslSocket` and no HTTP
client, and the build downloads nothing (no `FetchContent`, no `ExternalProject`).
Qt WebEngine is initialized only for local HTML preview. This is a statement about
this project's code and build, not a guarantee about what the Qt libraries
themselves may do internally.

### Regression suite and test material

The regression suite under `tests/` is not tracked in this Git repository: it
appears in neither the index nor any commit, and `.gitignore` lists it so it stays
that way. Its drivers are `EXCLUDE_FROM_ALL` targets, so an ordinary `cmake --build` does
not build them and `install(TARGETS MediaGallery ...)` does not install them.

Local media files used for manual testing live under `tests/testfiles/`. They are
third-party works of external origin, are likewise untracked, and are neither
redistributed nor covered by this document. Should `tests/` ever be added to the
repository, those files must be reviewed before that happens.

---

## Development environment

The current development environment uses Qt under an Educational License.

This describes the development environment only. It is **not** the license of
MediaGallery, and it does **not** by itself permit distributing a finished
MediaGallery binary under those terms. The licensing of any future distribution
must be assessed separately, against the Qt license and the Qt build actually
used for it.

Independently of that, the Qt a given build links against is determined by the
build configuration. The reference environment resolves Qt from the Linux
distribution packages under `/usr` (`Qt6_DIR=/usr/lib/cmake/Qt6`), which are the
open-source Qt build; a build configured against a Qt Account installation would
use that one instead. Anyone preparing a distribution should confirm which Qt
their build actually used.

---

## Obligations for a binary distribution

No binary distribution exists yet. The table below is a working checklist of what
a distribution would have to account for, per component **actually shipped**. It
is a summary of the components' own terms, not a legal determination of what
suffices; see the [Note](#note) at the end.

| Shipped component | Reached how | What its terms call for |
|---|---|---|
| Qt libraries | direct | The applicable license for each Qt module, as documented by Qt for that module, plus the third-party attributions for the shipped modules. The LGPL's relinking and written-offer provisions are the ones usually cited here; their exact application depends on how the binary is built and linked. |
| Qt PDF / PDFium | PDFium embedded in Qt PDF | The BSD notice and, as reproduced by Qt, the Apache-2.0 text, plus the attributions for the components inside the PDFium snapshot. |
| Qt WebEngine / Chromium | Chromium embedded in Qt WebEngine | Chromium's BSD-3-Clause notice and the full Chromium third-party attribution set, including the LGPL-2.1 components Qt names as the most restrictive. |
| FFmpeg | loaded by the Qt Multimedia backend | Depends on the build that is shipped or required - Qt's own pre-built libraries carry the permissive set Qt lists; a build configured with `--enable-gpl` carries the GPL instead. Establish this per distribution. Patent/royalty considerations for codecs are separate from the licenses. |
| zlib | direct, optional | Comply with the Zlib License. In particular, the license notice must be retained in source distributions; product documentation acknowledgment is appreciated but not required. |
| Tesseract OCR | direct, optional | The Apache-2.0 license text and retained notices. |
| Leptonica | transitive, with Tesseract | The BSD-2-Clause copyright notice, conditions and disclaimer. |
| Hunspell | direct, optional | The text of the chosen license from the MPL-1.1 / GPL-2.0 / LGPL-2.1 tri-license. |
| Hunspell dictionaries | separate data, only if added | Their own licenses - check per dictionary; none is shipped or contained today. |
| Tesseract trained data | separate data, only if added | Its own license - check per language and per source repository; none is shipped or contained today. |
| MediaGallery itself | own code | The MIT text from `LICENSE`. |

zlib, Tesseract (with Leptonica) and Hunspell are optional in the build. A build
made without one of them has that library neither linked nor shipped. Components
embedded in a Qt module, such as PDFium in Qt PDF or Chromium in Qt WebEngine, are
part of that module's distribution when the module is shipped. Runtime backend
dependencies such as FFmpeg remain dependent on the selected backend and the
deployment configuration. A distribution therefore has to establish which
concrete Qt modules, embedded components and runtime backend libraries it actually
ships or requires, and then apply the terms applicable to those concrete builds.

---

## Acknowledgements

Parts of MediaGallery implement algorithms and mathematical methods published by
others. What follows distinguishes three things that are easy to conflate: the
**algorithm** (an idea or method, not itself subject to copyright), the
**published reference code** that expresses it, and **MediaGallery's own source
code**.

### Generators in `src/audio/Xoshiro.h` - close to the published reference code

`src/audio/Xoshiro.h` implements xoshiro128++ and, for seeding, SplitMix64. An
earlier version of this document described these as independent implementations
from which no code was copied or adapted. **That description was checked against
the published reference sources and does not hold**, and it has been corrected:

- The generator step (`next()`) and the `rotl` helper correspond
  **statement for statement**, in the same order and with the same local variable
  names (`result`, `t`), to the reference `xoshiro128plusplus.c`. The differences
  are the member name `m_s` instead of the file-scope `s`, and C++ class syntax.
- The seeding loop reproduces SplitMix64's step with the same three constants and
  the same shift widths as the reference `splitmix64.c`, adding a truncation to
  32 bits and a guard against an all-zero state, neither of which is in the
  reference.

The published reference files contain a public-domain dedication by their
authors, quoted below.

> *"Written in 2019 by David Blackman and Sebastiano Vigna (vigna@acm.org)"* /
> *"Written in 2015 by Sebastiano Vigna (vigna@acm.org)"* -
> *"To the extent possible under law, the author has dedicated all copyright and
> related and neighboring rights to this software to the public domain worldwide.
> Permission to use, copy, modify, and/or distribute this software for any purpose
> with or without fee is hereby granted."*

The attribution is given in recognition of the authors' work and to document the
provenance of the implementation.

| Where | Published work | Published by | Relationship of MediaGallery's code |
|---|---|---|---|
| `src/audio/Xoshiro.h` | **xoshiro128++**, used for the audio shuffle order | David Blackman and Sebastiano Vigna, https://prng.di.unimi.it/xoshiro128plusplus.c | closely follows the published reference implementation |
| `src/audio/Xoshiro.h` | **SplitMix64**, used to spread one seed across the generator state | Sebastiano Vigna, https://prng.di.unimi.it/splitmix64.c | follows the published reference C, with a 32-bit truncation and a zero-state guard added |
| `src/audio/Xoshiro.h` (`below`) | **Bounded random integers without division** | Daniel Lemire, *"Fast Random Integer Generation in an Interval"*, ACM TOMACS 29(1), 2019, https://doi.org/10.1145/3230636 | implements the method described in the paper; written in this project's own style, not taken from a particular implementation |

### Methods implemented from a specification or a published description

Here the published work is a formula, a specification or a constant - there is no
reference source file that MediaGallery's code follows:

| Where | Published method or value | Published by |
|---|---|---|
| `src/audio/AudioEqualizer.cpp` | **RBJ peaking biquad** ("Audio EQ Cookbook" formulas), the ten-band equalizer | Robert Bristow-Johnson, W3C Working Group Note, https://www.w3.org/TR/audio-eq-cookbook/ |
| `src/core/ZCodec.cpp` | **CRC-32**, polynomial `0xEDB88320`, for ZIP/DOCX entries | polynomial defined by the ZIP and PNG formats |
| `src/audio/MkvAudioExtract.cpp` | **Ogg CRC**, polynomial `0x04C11DB7` unreflected, for Ogg pages | polynomial defined by the Ogg format |

Both CRC lookup tables are computed from the polynomial rather than stored, so no
table was taken from any implementation.

### MediaGallery's Qt Quick Controls style (`qml/style/*.qml`)

The 14 QML files in `qml/style/` (plus a `qmldir`) implement a custom Qt Quick
Controls style. What can be verified from the repository:

- Every one of them is written against the public **`QtQuick.Templates`** API
  (`import QtQuick.Templates as T`), which is Qt's documented way to implement a
  custom style.
- None of them carries a Qt copyright header or an `SPDX-License-Identifier`
  line; no file is a copy of a Qt style file.
- Compared line by line against the Qt Basic and Fusion styles installed on the
  reference environment, the longest identical contiguous run in any file is
  **10 lines**, and those runs are the property plumbing a custom style has to
  write - binding `T.<Control>` properties to the control and the standard
  `implicitWidth`/`implicitHeight` sizing expressions Qt documents for this
  purpose. Individual values also coincide (for example a popup margin of 6),
  which is what one expects when a Qt style is used as the reference while
  writing a new one.

An earlier version of this document stated that the style *"contains no code from
any Qt-supplied style."* That is stronger than the repository can support: using
the public `QtQuick.Templates` API says nothing about where a given line came
from, and short passages do coincide with Qt's own styles for the reasons above.
What is stated here instead is what was actually measured. Qt's styles are part
of Qt and carry Qt's license; whether any of these files is a derivative work of
one of them is a question about their authorship history, not about their imports.
See [Needs verification](#needs-verification).

---

## Needs verification

Questions this document deliberately leaves open. They are recorded rather than
answered, because answering them requires information outside this repository:

1. **Applicability of `APAFML` to the derived width values.** The numeric font
   metrics in `src/pdf/edit/PdfBaseFontWidths.cpp` are derived from data that
   upstream carries an Adobe copyright notice and the Adobe Postscript AFM
   License. Whether reproducing the numbers alone - without any AFM file - is
   covered by that license, and what exactly its "retain all copyright notices"
   condition then requires, is a legal assessment. The origin is documented; the
   assessment is open.
2. **The FFmpeg build a given distribution ships or requires.** Qt documents
   FFmpeg 7.1.3 with permissive components only for the binaries *Qt* ships. A
   distribution's FFmpeg may be configured differently: on the reference
   environment it is built with `--enable-gpl --enable-version3`, so that build
   may include components that, per FFmpeg's own legal page, bring the GPL into
   play for the whole of FFmpeg. Which build a future
   MediaGallery distribution ships or depends on is a deployment decision that
   this repository does not record, so the applicable license cannot be fixed
   here. It has to be read off the FFmpeg build actually used.
3. **The Qt Educational License terms.** Their content is not derivable from this
   repository. What a distribution built under that license would require must be
   checked against the agreement itself.
4. **Third-party components inside Qt beyond those named here.** Qt publishes SBOM
   documents (SPDX 2.3) for its third-party components from Qt 6.8 onwards. For a
   concrete distribution, the SBOM of the exact Qt build used should be consulted
   rather than this summary.
5. **Versions in other build environments.** The versions given are those of the
   reference environment. A Windows (vcpkg) or macOS build resolves different
   versions, whose licenses should be confirmed for that build.
6. **Authorship history of `qml/style/*.qml`.** The measurements in
   [Acknowledgements](#acknowledgements) show no copied file, no Qt copyright
   header and only short coincident passages of unavoidable template plumbing.
   They cannot show how the files were written. Whether any of them is a
   derivative work of a Qt-supplied style is a question about their history, and
   is left open rather than answered by their imports.
7. **The DATEV field catalogue and its terms.** The header field names in
   `src/datev/DatevFormat.cpp` cover only what the sample files themselves state.
   Whether DATEV's published format description may be used as the source for the
   remaining field names, and under what conditions, has not been established -
   the official page could not be read (it renders its content in the browser, so
   fetching it returns an empty document). Nothing has been taken from it.
8. **The license of any trained data or dictionary a distribution adds.**
   Tesseract's Apache-2.0 and Hunspell's tri-license cover those libraries only.
   `*.traineddata` files and `.aff`/`.dic` dictionaries are separate works under
   their own terms, and none is shipped or contained today. Anything added later
   has to be checked against the source it came from.

---

## Note

This document is a technical license and compliance overview, compiled from the
project's build files and the official documentation of the components involved.
**It is not legal advice and does not warrant legal compliance.** Before
distributing MediaGallery in binary form, have the obligations of every component
actually shipped reviewed by qualified legal counsel.
