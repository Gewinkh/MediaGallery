// QPdfDocument bietet keine API fuer Annotationen oder eingebettete Stroeme. Deshalb
// wird die Datei gemappt und nach /Sound, /Screen und /Movie durchsucht; /Rect und
// Rohstrom werden herausgezogen. Verschluesselte und gefilterte Stroeme bleiben leer.

#include "pdf/PdfMediaHandler.h"

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>
#include <QSet>

PdfMediaHandler::PdfMediaHandler(QPdfDocument* doc, QObject* parent)
    : QObject(parent), m_doc(doc)
{}

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

    parseLinkAnnotations(data);
}

void PdfMediaHandler::parseLinkAnnotations(const QByteArray& data) {
    // PDF link annotations: /Subtype /Link with /A << /S /URI /URI (url) >>. The /A value may be an inline dict or
    // an indirect reference; both are handled.

    const QByteArray tags[] = { "/Subtype /Link", "/Subtype/Link" };

    auto extractUri = [](const QByteArray& block) -> QString {
        qsizetype uriKey = block.indexOf("/URI");
        if (uriKey < 0) return {};
        qsizetype valStart = uriKey + 4;
        while (valStart < block.size() && (block[valStart] == ' ' || block[valStart] == '\n' || block[valStart] == '\r'))
            ++valStart;
        if (valStart >= block.size()) return {};
        if (block[valStart] == '(') {
            qsizetype end = block.indexOf(')', valStart + 1);
            if (end < 0) return {};
            return QString::fromUtf8(block.mid(valStart + 1, end - valStart - 1)).trimmed();
        }
        if (block[valStart] == '<') {
            qsizetype end = block.indexOf('>', valStart + 1);
            if (end < 0) return {};
            const QByteArray hex = block.mid(valStart + 1, end - valStart - 1).trimmed();
            return QString::fromUtf8(QByteArray::fromHex(hex)).trimmed();
        }
        return {};
    };

    auto resolveIndirect = [&](const QByteArray& ref) -> QByteArray {
        bool ok = false;
        const int objNum = ref.trimmed().split(' ').first().toInt(&ok);
        if (!ok || objNum <= 0) return {};
        const QByteArray marker = QByteArray::number(objNum) + " ";
        qsizetype pos = 0;
        while (pos < data.size()) {
            pos = data.indexOf(marker, pos);
            if (pos < 0) break;
            if (pos > 0 && data[pos-1] != '\n' && data[pos-1] != '\r' && data[pos-1] != ' ') {
                pos += marker.size();
                continue;
            }
            qsizetype afterRef = pos + marker.size();
            while (afterRef < data.size() && data[afterRef] >= '0' && data[afterRef] <= '9') ++afterRef;
            while (afterRef < data.size() && (data[afterRef] == ' ')) ++afterRef;
            if (data.mid(afterRef, 3) != "obj") { pos += marker.size(); continue; }
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
            qsizetype dictStart = data.lastIndexOf("<<", hitPos);
            if (dictStart < 0) continue;
            qsizetype dictEnd = data.indexOf("endobj", hitPos);
            if (dictEnd < 0 || dictEnd > hitPos + 4096)
                dictEnd = hitPos + 2048;
            dictEnd = qMin(dictEnd, static_cast<qsizetype>(data.size()));

            if (seen.contains(dictStart)) continue;
            seen.insert(dictStart);

            const QByteArray dict = data.mid(dictStart, dictEnd - dictStart);

            const QByteArray rectVal = dictValue(dict, "/Rect");
            if (rectVal.isEmpty()) continue;

            const QByteArray pageRef = dictValue(dict, "/P");

            QString url;

            qsizetype aKeyPos = dict.indexOf("/A ");
            if (aKeyPos < 0) aKeyPos = dict.indexOf("/A\n");
            if (aKeyPos < 0) aKeyPos = dict.indexOf("/A\r");
            if (aKeyPos >= 0) {
                qsizetype valPos = aKeyPos + 2;
                while (valPos < dict.size() && (dict[valPos]==' '||dict[valPos]=='\n'||dict[valPos]=='\r'))
                    ++valPos;

                if (valPos < dict.size() && dict[valPos] == '<' &&
                    valPos+1 < dict.size() && dict[valPos+1] == '<') {
                    qsizetype aEnd = dict.indexOf(">>", valPos + 2);
                    if (aEnd > valPos) {
                        const QByteArray aBlock = dict.mid(valPos, aEnd - valPos + 2);
                        if (aBlock.contains("/URI"))
                            url = extractUri(aBlock);
                    }
                } else {
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
            if (!url.startsWith("http") && !url.startsWith("mailto") && !url.startsWith("ftp"))
                continue;

            int page = 0;
            if (!pageRef.isEmpty())
                page = resolvePageIndex(data, pageRef);
            if (page < 0) page = 0;

            const QSizeF ps = m_doc->pagePointSize(page);
            if (ps.isEmpty()) continue;
            const QRectF r = parseNormalisedRect(rectVal, ps);
            if (!r.isValid() || r.isEmpty()) continue;

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
    qsizetype dictStart = data.lastIndexOf("<<", hitPos);
    if (dictStart < 0) return;

    qsizetype dictEnd = data.indexOf(">>", hitPos);
    if (dictEnd < 0) return;
    dictEnd = qMin(dictEnd + 512, static_cast<qsizetype>(data.size()));

    const QByteArray dict = data.mid(dictStart, dictEnd - dictStart);

    const QByteArray rectVal   = dictValue(dict, "/Rect");
    const QByteArray pageRef   = dictValue(dict, "/P");
    QByteArray       labelVal  = dictValue(dict, "/Contents");
    if (labelVal.isEmpty()) labelVal = dictValue(dict, "/NM");

    QString label = QString::fromLatin1(labelVal)
                        .remove('(').remove(')').trimmed();
    if (label.isEmpty()) label = tr("Media");

    int pageIdx = 0;
    if (!pageRef.isEmpty())
        pageIdx = resolvePageIndex(data, pageRef);
    if (m_doc && m_doc->pageCount() > 0)
        pageIdx = qBound(0, pageIdx, m_doc->pageCount() - 1);

    QSizeF pageSize = m_doc ? m_doc->pagePointSize(pageIdx) : QSizeF(595, 842);
    QRectF normRect;
    if (!rectVal.isEmpty())
        normRect = parseNormalisedRect(rectVal, pageSize);
    if (!normRect.isValid() || normRect.isEmpty())
        normRect = QRectF(0.02, 0.02, 0.08, 0.08);  // safe fallback

    const bool isVideoSubtype = subtypeTag.contains("Screen")
                                || subtypeTag.contains("Movie");

    MediaAnnotation ann;
    ann.page  = pageIdx;
    ann.rect  = normRect;
    ann.label = label;

    bool extracted = extractEmbeddedStream(data, hitPos, ann);

    if (!extracted && isVideoSubtype)
        resolveRichMediaUrl(data, hitPos, ann);

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

bool PdfMediaHandler::extractEmbeddedStream(const QByteArray& data,
                                            qsizetype searchFrom,
                                            MediaAnnotation& ann) {
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

    const QString ext = guessMimeExt(streamData.left(16));
    if (ext.isEmpty()) return false;  // compressed / unrecognised – skip

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

void PdfMediaHandler::resolveRichMediaUrl(const QByteArray& data,
                                          qsizetype searchFrom,
                                          MediaAnnotation& ann) {
    constexpr qsizetype kWindow = 8192;
    const QByteArray region = data.mid(searchFrom,
                                       qMin(kWindow,
                                            static_cast<qsizetype>(data.size()) - searchFrom));

    QByteArray uri = dictValue(region, "/URI");
    if (uri.isEmpty()) uri = dictValue(region, "/F");
    if (uri.isEmpty()) return;

    QString url = QString::fromLatin1(uri)
                      .remove('(').remove(')').trimmed();
    if (url.isEmpty()) return;

    ann.sourceUrl = url;
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

int PdfMediaHandler::resolvePageIndex(const QByteArray& data,
                                      const QByteArray& pageRef) const {
    int objNum = pageRef.split(' ').value(0).toInt();
    if (objNum <= 0) return 0;

    QVector<int> pageObjNums;
    {
        const QByteArray typeTag = "/Type /Page";
        qsizetype pos = 0;
        while (true) {
            qsizetype tp = data.indexOf(typeTag, pos);
            if (tp < 0) {
                const QByteArray altTag = "/Type/Page";
                tp = data.indexOf(altTag, pos);
                if (tp < 0) break;
                pos = tp + altTag.size();
            } else {
                pos = tp + typeTag.size();
            }

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

    if ((u(0) == 0xFF && (u(1) & 0xE0) == 0xE0)) return QStringLiteral("mp3");
    if (header.startsWith("ID3"))                   return QStringLiteral("mp3");

    if (header.startsWith("RIFF"))                  return QStringLiteral("wav");

    if (header.startsWith("FORM"))                  return QStringLiteral("aiff");

    if (header.startsWith("OggS"))                  return QStringLiteral("ogg");

    if (header.startsWith("fLaC"))                  return QStringLiteral("flac");

    if (header.size() >= 8) {
        const QByteArray ftyp = header.mid(4, 4);
        if (ftyp == "ftyp" || ftyp == "moov")       return QStringLiteral("mp4");
        if (ftyp == "wide" || ftyp == "mdat")        return QStringLiteral("mov");
    }

    if (u(0)==0x1A && u(1)==0x45 && u(2)==0xDF && u(3)==0xA3)
        return QStringLiteral("webm");

    if (header.startsWith("RIFF") && header.size() >= 12 &&
        header.mid(8, 4) == "AVI ")
        return QStringLiteral("avi");

    if (u(0) == 0xFF && (u(1) & 0xF0) == 0xF0)      return QStringLiteral("aac");

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