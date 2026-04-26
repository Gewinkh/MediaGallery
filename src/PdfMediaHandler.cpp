// ══════════════════════════════════════════════════════════════════════════════
// PdfMediaHandler.cpp
// ══════════════════════════════════════════════════════════════════════════════
//
// Qt Limitation Note
// ──────────────────
// QPdfDocument (Qt6 PDF module) exposes ONLY page rendering and text
// extraction.  There is intentionally no public API for:
//   • annotation enumeration
//   • annotation rect / type queries
//   • embedded file / stream access
//   • JavaScript / action parsing
//
// Workaround implemented here:
//   We memory-map the raw .pdf file and scan for known annotation dictionary
//   markers.  For each hit we extract /Rect and attempt to pull the raw
//   stream data into a temp file that QMediaPlayer can open.
//
//   Supported annotation subtypes:
//     /Sound   – traditional PDF audio annotation (ISO 32000-1 §12.6.4.9)
//     /Screen  – used by modern PDF authoring tools for RichMedia / Rendition
//                actions (ISO 32000-1 §12.6.4.13)
//     /Movie   – legacy QuickTime-based video annotation (deprecated but still seen)
//
// Limitations of the workaround:
//   1. Encrypted / linearised PDFs may have streams we cannot extract.
//   2. Filter chains (FlateDecode, LZWDecode, …) are NOT decoded – only raw
//      (no /Filter, or /Filter /FlateDecode with uncompressed header sniff)
//      streams are extracted.  Compressed streams are left empty; PdfViewer
//      then falls back to sidecar file search.
//   3. The page-reference resolver is a best-effort heuristic; it works for
//      the vast majority of PDFs produced by standard tools.
// ══════════════════════════════════════════════════════════════════════════════

#include "PdfMediaHandler.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>
#include <QSet>

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────
PdfMediaHandler::PdfMediaHandler(QPdfDocument* doc, QObject* parent)
    : QObject(parent), m_doc(doc)
{}

void PdfMediaHandler::cleanup() {
    for (const QString& p : std::as_const(m_tempFiles)) {
        QFile::remove(p);
    }
    m_tempFiles.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Public interface
// ─────────────────────────────────────────────────────────────────────────────
void PdfMediaHandler::scanDocument(const QString& pdfPath) {
    m_annotations.clear();
    m_pdfPath = pdfPath;
    if (pdfPath.isEmpty()) return;

    QFile f(pdfPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "PdfMediaHandler: cannot open" << pdfPath;
        return;
    }
    const QByteArray data = f.readAll();
    f.close();

    parseAnnotations(data);

    qDebug() << "PdfMediaHandler: found" << m_annotations.size()
             << "media annotation(s) in" << QFileInfo(pdfPath).fileName();
}

QVector<MediaAnnotation> PdfMediaHandler::annotationsForPage(int page) const {
    QVector<MediaAnnotation> result;
    for (const auto& ann : m_annotations)
        if (ann.page == page) result.append(ann);
    return result;
}

QVector<MediaAnnotation> PdfMediaHandler::allLinks() const {
    QVector<MediaAnnotation> result;
    for (const auto& ann : m_annotations)
        if (ann.type == MediaAnnotation::Type::Link) result.append(ann);
    return result;
}

bool PdfMediaHandler::hasMedia() const {
    for (const auto& ann : m_annotations)
        if (ann.type == MediaAnnotation::Type::Audio ||
            ann.type == MediaAnnotation::Type::Video)
            return true;
    return false;
}

bool PdfMediaHandler::hasLinks() const {
    for (const auto& ann : m_annotations)
        if (ann.type == MediaAnnotation::Type::Link) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Core raw-PDF scan
// ─────────────────────────────────────────────────────────────────────────────
void PdfMediaHandler::parseAnnotations(const QByteArray& data) {
    // We search for three annotation subtype markers. Each is tried with both
    // space and no-space variants because PDF producers are inconsistent.
    struct SubtypeSpec {
        QByteArray tag;
        bool isVideo;
    };
    const SubtypeSpec specs[] = {
                                  { "/Subtype /Sound",   false },
                                  { "/Subtype/Sound",    false },
                                  { "/Subtype /Screen",  true  },
                                  { "/Subtype/Screen",   true  },
                                  { "/Subtype /Movie",   true  },
                                  { "/Subtype/Movie",    true  },
                                  };

    for (const auto& spec : specs) {
        const QVector<qsizetype> hits = findAll(data, spec.tag);
        for (qsizetype hit : hits)
            parseOneAnnotation(data, hit, spec.tag);
    }

    // Also scan for hyperlink annotations (/Subtype /Link)
    parseLinkAnnotations(data);
}

// ─────────────────────────────────────────────────────────────────────────────
// Link annotation parsing  (/Subtype /Link)
// ─────────────────────────────────────────────────────────────────────────────
void PdfMediaHandler::parseLinkAnnotations(const QByteArray& data) {
    // PDF link annotations: /Subtype /Link  with  /A << /S /URI  /URI (url) >>
    // The /A value can be:
    //   (a) inline dict:   /A << /S /URI /URI (https://...) >>
    //   (b) indirect ref:  /A 14 0 R   →  object 14 contains the action dict
    // We handle both.

    const QByteArray tags[] = { "/Subtype /Link", "/Subtype/Link" };

    // Helper lambda: extract URI string from an action dict byte block
    auto extractUri = [](const QByteArray& block) -> QString {
        // Find /URI key — value is a PDF string: (url) or <hex>
        qsizetype uriKey = block.indexOf("/URI");
        if (uriKey < 0) return {};
        // Skip the key name itself (4 chars "/URI") + optional whitespace
        qsizetype valStart = uriKey + 4;
        while (valStart < block.size() && (block[valStart] == ' ' || block[valStart] == '\n' || block[valStart] == '\r'))
            ++valStart;
        if (valStart >= block.size()) return {};
        // PDF string literal: (...)
        if (block[valStart] == '(') {
            qsizetype end = block.indexOf(')', valStart + 1);
            if (end < 0) return {};
            return QString::fromUtf8(block.mid(valStart + 1, end - valStart - 1)).trimmed();
        }
        // PDF hex string: <...>
        if (block[valStart] == '<') {
            qsizetype end = block.indexOf('>', valStart + 1);
            if (end < 0) return {};
            const QByteArray hex = block.mid(valStart + 1, end - valStart - 1).trimmed();
            return QString::fromUtf8(QByteArray::fromHex(hex)).trimmed();
        }
        return {};
    };

    // Helper lambda: resolve an indirect PDF object reference "N G R" → object body
    auto resolveIndirect = [&](const QByteArray& ref) -> QByteArray {
        // ref looks like "14 0 R" — extract object number
        bool ok = false;
        const int objNum = ref.trimmed().split(' ').first().toInt(&ok);
        if (!ok || objNum <= 0) return {};
        // Find "N 0 obj" or "N G obj" in the file
        // Search for "<objNum> " followed by a digit and " obj"
        const QByteArray marker = QByteArray::number(objNum) + " ";
        qsizetype pos = 0;
        while (pos < data.size()) {
            pos = data.indexOf(marker, pos);
            if (pos < 0) break;
            // Make sure it's at a word boundary (preceded by newline or space)
            if (pos > 0 && data[pos-1] != '\n' && data[pos-1] != '\r' && data[pos-1] != ' ') {
                pos += marker.size();
                continue;
            }
            // After "N G " must come "obj"
            qsizetype afterRef = pos + marker.size();
            // skip generation number
            while (afterRef < data.size() && data[afterRef] >= '0' && data[afterRef] <= '9') ++afterRef;
            while (afterRef < data.size() && (data[afterRef] == ' ')) ++afterRef;
            if (data.mid(afterRef, 3) != "obj") { pos += marker.size(); continue; }
            // Found the object — return its body (between "obj\n" and "endobj")
            qsizetype bodyStart = afterRef + 3;
            qsizetype bodyEnd   = data.indexOf("endobj", bodyStart);
            if (bodyEnd < 0) bodyEnd = qMin(bodyStart + 4096, static_cast<qsizetype>(data.size()));
            return data.mid(bodyStart, bodyEnd - bodyStart);
        }
        return {};
    };

    QSet<qsizetype> seen; // avoid duplicates when both tag variants hit the same annotation

    for (const QByteArray& tag : tags) {
        const QVector<qsizetype> hits = findAll(data, tag);
        for (qsizetype hitPos : hits) {
            // Locate surrounding annotation dict (search backward for "<<")
            qsizetype dictStart = data.lastIndexOf("<<", hitPos);
            if (dictStart < 0) continue;
            // Find a reasonable end: look for "endobj" or ">>" well past the subtype
            qsizetype dictEnd = data.indexOf("endobj", hitPos);
            if (dictEnd < 0 || dictEnd > hitPos + 4096)
                dictEnd = hitPos + 2048;
            dictEnd = qMin(dictEnd, static_cast<qsizetype>(data.size()));

            if (seen.contains(dictStart)) continue;
            seen.insert(dictStart);

            const QByteArray dict = data.mid(dictStart, dictEnd - dictStart);

            // ── /Rect ──────────────────────────────────────────────────────
            const QByteArray rectVal = dictValue(dict, "/Rect");
            if (rectVal.isEmpty()) continue;

            // ── /P (page reference) ────────────────────────────────────────
            const QByteArray pageRef = dictValue(dict, "/P");

            // ── /A (action) — inline dict or indirect reference ────────────
            QString url;

            // Find "/A" key in the annotation dict
            // Could be "/A <<...>>" or "/A N G R"
            qsizetype aKeyPos = dict.indexOf("/A ");
            if (aKeyPos < 0) aKeyPos = dict.indexOf("/A\n");
            if (aKeyPos < 0) aKeyPos = dict.indexOf("/A\r");
            if (aKeyPos >= 0) {
                qsizetype valPos = aKeyPos + 2;
                while (valPos < dict.size() && (dict[valPos]==' '||dict[valPos]=='\n'||dict[valPos]=='\r'))
                    ++valPos;

                if (valPos < dict.size() && dict[valPos] == '<' &&
                    valPos+1 < dict.size() && dict[valPos+1] == '<') {
                    // Inline action dict
                    qsizetype aEnd = dict.indexOf(">>", valPos + 2);
                    if (aEnd > valPos) {
                        const QByteArray aBlock = dict.mid(valPos, aEnd - valPos + 2);
                        // Only handle URI actions (/S /URI)
                        if (aBlock.contains("/URI"))
                            url = extractUri(aBlock);
                    }
                } else {
                    // Likely an indirect reference: "14 0 R"
                    qsizetype refEnd = dict.indexOf('R', valPos);
                    if (refEnd > valPos && refEnd - valPos < 20) {
                        const QByteArray ref = dict.mid(valPos, refEnd - valPos + 1);
                        if (ref.contains(' ') && ref.endsWith('R')) {
                            const QByteArray objBody = resolveIndirect(ref.trimmed());
                            if (!objBody.isEmpty() && objBody.contains("/URI"))
                                url = extractUri(objBody);
                        }
                    }
                }
            }

            if (url.isEmpty()) continue;
            // Sanity: must look like a URL
            if (!url.startsWith("http") && !url.startsWith("mailto") && !url.startsWith("ftp"))
                continue;

            // ── Page resolution ────────────────────────────────────────────
            int page = 0;
            if (!pageRef.isEmpty())
                page = resolvePageIndex(data, pageRef);
            if (page < 0) page = 0;

            // ── Rect ───────────────────────────────────────────────────────
            const QSizeF ps = m_doc->pagePointSize(page);
            if (ps.isEmpty()) continue;
            const QRectF r = parseNormalisedRect(rectVal, ps);
            if (!r.isValid() || r.isEmpty()) continue;

            // ── Label ──────────────────────────────────────────────────────
            QByteArray labelVal = dictValue(dict, "/Contents");
            if (labelVal.isEmpty()) labelVal = dictValue(dict, "/NM");
            QString label = QString::fromLatin1(labelVal).remove('(').remove(')').trimmed();
            if (label.isEmpty()) label = url;

            MediaAnnotation ann;
            ann.page      = page;
            ann.rect      = r;
            ann.sourceUrl = url;
            ann.type      = MediaAnnotation::Type::Link;
            ann.label     = label;
            m_annotations.append(ann);
        }
    }
}

void PdfMediaHandler::parseOneAnnotation(const QByteArray& data,
                                         qsizetype hitPos,
                                         const QByteArray& subtypeTag) {
    // ── Locate the surrounding dictionary ────────────────────────────────────
    // Walk backward from hitPos to the nearest '<<' — that's (approximately)
    // the start of the annotation dictionary.
    qsizetype dictStart = data.lastIndexOf("<<", hitPos);
    if (dictStart < 0) return;

    // Find the end: search for ">>" or "endobj" following the subtype tag
    qsizetype dictEnd = data.indexOf(">>", hitPos);
    if (dictEnd < 0) return;
    // Extend a bit to capture /Rect which may follow the subtype key
    dictEnd = qMin(dictEnd + 512, static_cast<qsizetype>(data.size()));

    const QByteArray dict = data.mid(dictStart, dictEnd - dictStart);

    // ── Extract fields ────────────────────────────────────────────────────────
    const QByteArray rectVal   = dictValue(dict, "/Rect");
    const QByteArray pageRef   = dictValue(dict, "/P");
    QByteArray       labelVal  = dictValue(dict, "/Contents");
    if (labelVal.isEmpty()) labelVal = dictValue(dict, "/NM");

    QString label = QString::fromLatin1(labelVal)
                        .remove('(').remove(')').trimmed();
    if (label.isEmpty()) label = tr("Media");

    // ── Determine page index ──────────────────────────────────────────────────
    int pageIdx = 0;
    if (!pageRef.isEmpty())
        pageIdx = resolvePageIndex(data, pageRef);
    if (m_doc && m_doc->pageCount() > 0)
        pageIdx = qBound(0, pageIdx, m_doc->pageCount() - 1);

    // ── Parse rect ───────────────────────────────────────────────────────────
    QSizeF pageSize = m_doc ? m_doc->pagePointSize(pageIdx) : QSizeF(595, 842);
    QRectF normRect;
    if (!rectVal.isEmpty())
        normRect = parseNormalisedRect(rectVal, pageSize);
    if (!normRect.isValid() || normRect.isEmpty())
        normRect = QRectF(0.02, 0.02, 0.08, 0.08);  // safe fallback

    // ── Determine type from subtype tag ───────────────────────────────────────
    const bool isVideoSubtype = subtypeTag.contains("Screen")
                                || subtypeTag.contains("Movie");

    // ── Build annotation ─────────────────────────────────────────────────────
    MediaAnnotation ann;
    ann.page  = pageIdx;
    ann.rect  = normRect;
    ann.label = label;

    // ── Try to extract the embedded media stream ──────────────────────────────
    bool extracted = extractEmbeddedStream(data, hitPos, ann);

    // If no direct stream, look for a RichMedia/Rendition URL (Screen annots)
    if (!extracted && isVideoSubtype)
        resolveRichMediaUrl(data, hitPos, ann);

    // Determine final type
    if (ann.type == MediaAnnotation::Type::Unknown) {
        if (isVideoSubtype)
            ann.type = MediaAnnotation::Type::Video;
        else
            ann.type = MediaAnnotation::Type::Audio;
    }

    // Avoid duplicates (same page + nearly identical rect from space/no-space tag variants)
    for (const auto& existing : std::as_const(m_annotations)) {
        if (existing.page == ann.page) {
            const QPointF delta = existing.rect.center() - ann.rect.center();
            if (qAbs(delta.x()) < 0.01 && qAbs(delta.y()) < 0.01)
                return;
        }
    }

    m_annotations.append(ann);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stream extraction
// ─────────────────────────────────────────────────────────────────────────────
bool PdfMediaHandler::extractEmbeddedStream(const QByteArray& data,
                                            qsizetype searchFrom,
                                            MediaAnnotation& ann) {
    // Search for "stream" within a reasonable window after the annotation dict
    constexpr qsizetype kSearchWindow = 4096;
    qsizetype stPos = data.indexOf("stream", searchFrom);
    if (stPos < 0 || stPos - searchFrom > kSearchWindow) return false;

    qsizetype dataStart = stPos + 6; // skip "stream"
    if (dataStart < data.size() && data[dataStart] == '\r') ++dataStart;
    if (dataStart < data.size() && data[dataStart] == '\n') ++dataStart;

    qsizetype dataEnd = data.indexOf("endstream", dataStart);
    if (dataEnd < 0) return false;

    const QByteArray streamData = data.mid(dataStart, dataEnd - dataStart);
    if (streamData.size() < 16) return false;

    // Sniff magic bytes to determine format and whether it's usable raw
    const QString ext = guessMimeExt(streamData.left(16));
    if (ext.isEmpty()) return false;  // compressed / unrecognised – skip

    // Write temp file
    const QString tmpDir  = QStandardPaths::writableLocation(
        QStandardPaths::TempLocation);
    const QString baseName = QFileInfo(m_pdfPath).completeBaseName();
    const QString tmpPath  = tmpDir + QString("/pdfmedia_%1_p%2_%3.%4")
                                         .arg(baseName)
                                         .arg(ann.page)
                                         .arg(m_annotations.size())
                                         .arg(ext);

    QFile tmp(tmpPath);
    if (!tmp.open(QIODevice::WriteOnly)) return false;
    tmp.write(streamData);
    tmp.close();

    ann.sourcePath = tmpPath;
    ann.type       = detectType({}, ext);
    m_tempFiles.append(tmpPath);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// RichMedia / Rendition URL extraction
// ─────────────────────────────────────────────────────────────────────────────
void PdfMediaHandler::resolveRichMediaUrl(const QByteArray& data,
                                          qsizetype searchFrom,
                                          MediaAnnotation& ann) {
    // Look for /URI or /F (filename) in a small window after the annotation
    constexpr qsizetype kWindow = 8192;
    const QByteArray region = data.mid(searchFrom,
                                       qMin(kWindow,
                                            static_cast<qsizetype>(data.size()) - searchFrom));

    QByteArray uri = dictValue(region, "/URI");
    if (uri.isEmpty()) uri = dictValue(region, "/F");
    if (uri.isEmpty()) return;

    // Strip PDF string delimiters
    QString url = QString::fromLatin1(uri)
                      .remove('(').remove(')').trimmed();
    if (url.isEmpty()) return;

    ann.sourceUrl = url;
    // Guess type from extension
    const QString lower = url.toLower();
    if (lower.endsWith(".mp4") || lower.endsWith(".avi") ||
        lower.endsWith(".mkv") || lower.endsWith(".mov") ||
        lower.endsWith(".webm"))
        ann.type = MediaAnnotation::Type::Video;
    else if (lower.endsWith(".mp3") || lower.endsWith(".wav") ||
             lower.endsWith(".ogg") || lower.endsWith(".aac") ||
             lower.endsWith(".flac"))
        ann.type = MediaAnnotation::Type::Audio;
}

// ─────────────────────────────────────────────────────────────────────────────
// Page index resolver
// ─────────────────────────────────────────────────────────────────────────────
int PdfMediaHandler::resolvePageIndex(const QByteArray& data,
                                      const QByteArray& pageRef) const {
    // pageRef looks like "5 0 R" – the object number identifies the page object.
    int objNum = pageRef.split(' ').value(0).toInt();
    if (objNum <= 0) return 0;

    // Collect all page object numbers in document order by scanning /Type /Page
    QVector<int> pageObjNums;
    {
        const QByteArray typeTag = "/Type /Page";
        qsizetype pos = 0;
        while (true) {
            qsizetype tp = data.indexOf(typeTag, pos);
            if (tp < 0) {
                // also try without space
                const QByteArray altTag = "/Type/Page";
                tp = data.indexOf(altTag, pos);
                if (tp < 0) break;
                pos = tp + altTag.size();
            } else {
                pos = tp + typeTag.size();
            }

            // Walk back to find the object declaration "N G obj"
            qsizetype objDecl = data.lastIndexOf(" obj", tp);
            if (objDecl < 0) continue;
            qsizetype lineS = data.lastIndexOf('\n', objDecl);
            if (lineS < 0) lineS = 0;
            const QByteArray numPart = data.mid(lineS + 1, objDecl - lineS - 1)
                                           .trimmed();
            const int pObjNum = numPart.split(' ').value(0).toInt();
            if (pObjNum > 0 && !pageObjNums.contains(pObjNum))
                pageObjNums.append(pObjNum);
        }
    }

    for (int i = 0; i < pageObjNums.size(); ++i)
        if (pageObjNums[i] == objNum) return i;

    return 0;  // fallback: first page
}

// ─────────────────────────────────────────────────────────────────────────────
// Static helpers
// ─────────────────────────────────────────────────────────────────────────────

QVector<qsizetype> PdfMediaHandler::findAll(const QByteArray& data,
                                            const QByteArray& pattern) {
    QVector<qsizetype> result;
    qsizetype pos = 0;
    while ((pos = data.indexOf(pattern, pos)) != -1) {
        result.append(pos);
        pos += pattern.size();
    }
    return result;
}

QByteArray PdfMediaHandler::dictValue(const QByteArray& dict,
                                      const QByteArray& key) {
    qsizetype kpos = dict.indexOf(key);
    if (kpos < 0) return {};
    qsizetype vstart = kpos + key.size();
    while (vstart < dict.size() &&
           (dict[vstart] == ' ' || dict[vstart] == '\n' ||
            dict[vstart] == '\r' || dict[vstart] == '\t'))
        ++vstart;
    if (vstart >= dict.size()) return {};

    const char first = dict[vstart];

    if (first == '[') {
        qsizetype end = dict.indexOf(']', vstart);
        return end >= 0 ? dict.mid(vstart, end - vstart + 1) : QByteArray{};
    }
    if (first == '(') {
        int depth = 0;
        for (qsizetype i = vstart; i < dict.size(); ++i) {
            if (dict[i] == '(') ++depth;
            else if (dict[i] == ')') {
                if (--depth == 0) return dict.mid(vstart, i - vstart + 1);
            }
        }
        return {};
    }
    if (first == '<' && vstart + 1 < dict.size() && dict[vstart + 1] == '<')
        return QByteArray("<<...>>");  // nested dict, not expanded

    // Plain token
    qsizetype end = vstart;
    while (end < dict.size() &&
           dict[end] != ' ' && dict[end] != '\n' &&
           dict[end] != '\r' && dict[end] != '\t' &&
           dict[end] != '/'  && dict[end] != '>'  && dict[end] != ']')
        ++end;
    return dict.mid(vstart, end - vstart);
}

QRectF PdfMediaHandler::parseNormalisedRect(const QByteArray& rectBytes,
                                            const QSizeF& pageSize) {
    // rectBytes: "[x1 y1 x2 y2]"
    const QByteArray inner = rectBytes.mid(1, rectBytes.size() - 2).trimmed();
    const QList<QByteArray> parts = inner.split(' ');
    QList<double> vals;
    for (const auto& p : parts) {
        bool ok = false;
        double v = p.trimmed().toDouble(&ok);
        if (ok) vals << v;
    }
    if (vals.size() < 4) return {};

    const double x1 = vals[0], y1 = vals[1], x2 = vals[2], y2 = vals[3];
    const double pw = pageSize.width()  > 0 ? pageSize.width()  : 595;
    const double ph = pageSize.height() > 0 ? pageSize.height() : 842;

    // PDF coordinate origin is bottom-left; we flip to top-left
    return QRectF(x1 / pw,
                  1.0 - y2 / ph,
                  (x2 - x1) / pw,
                  (y2 - y1) / ph);
}

QString PdfMediaHandler::guessMimeExt(const QByteArray& header) {
    if (header.size() < 4) return {};

    const auto u = [&](int i) { return static_cast<unsigned char>(header[i]); };

    // MP3: sync word 0xFF 0xEx or ID3 tag
    if ((u(0) == 0xFF && (u(1) & 0xE0) == 0xE0)) return QStringLiteral("mp3");
    if (header.startsWith("ID3"))                   return QStringLiteral("mp3");

    // WAV / RIFF
    if (header.startsWith("RIFF"))                  return QStringLiteral("wav");

    // AIFF / AIFF-C
    if (header.startsWith("FORM"))                  return QStringLiteral("aiff");

    // OGG (audio or video)
    if (header.startsWith("OggS"))                  return QStringLiteral("ogg");

    // FLAC
    if (header.startsWith("fLaC"))                  return QStringLiteral("flac");

    // MP4 / MOV: check ftyp box (bytes 4-7)
    if (header.size() >= 8) {
        const QByteArray ftyp = header.mid(4, 4);
        if (ftyp == "ftyp" || ftyp == "moov")       return QStringLiteral("mp4");
        if (ftyp == "wide" || ftyp == "mdat")        return QStringLiteral("mov");
    }

    // WebM / MKV: EBML magic
    if (u(0)==0x1A && u(1)==0x45 && u(2)==0xDF && u(3)==0xA3)
        return QStringLiteral("webm");

    // AVI: RIFF….AVI
    if (header.startsWith("RIFF") && header.size() >= 12 &&
        header.mid(8, 4) == "AVI ")
        return QStringLiteral("avi");

    // AAC: ADTS sync 0xFF 0xF0 or 0xFF 0xF1
    if (u(0) == 0xFF && (u(1) & 0xF0) == 0xF0)      return QStringLiteral("aac");

    // Unknown / likely compressed PDF stream
    return {};
}

MediaAnnotation::Type PdfMediaHandler::detectType(const QByteArray& /*subtype*/,
                                                  const QString& ext) {
    static const QStringList videoExts = {
        "mp4", "avi", "mkv", "mov", "webm", "ogv", "wmv", "flv", "m4v"
    };
    static const QStringList audioExts = {
        "mp3", "wav", "ogg", "flac", "aac", "m4a", "opus", "wma",
        "aiff", "aif"
    };
    if (videoExts.contains(ext, Qt::CaseInsensitive))
        return MediaAnnotation::Type::Video;
    if (audioExts.contains(ext, Qt::CaseInsensitive))
        return MediaAnnotation::Type::Audio;
    return MediaAnnotation::Type::Unknown;
}