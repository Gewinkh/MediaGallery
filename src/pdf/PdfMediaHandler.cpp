// QPdfDocument bietet keine API fuer Annotationen oder eingebettete Stroeme. Deshalb wird die Datei
// gelesen, jedes Objekt mit /Subtype /Sound, /Screen, /Movie oder /Link ausgewertet und das Medium
// ueber die Verweise des Objekts gesucht. Verschluesselte und gefilterte Stroeme bleiben leer.

#include "pdf/PdfMediaHandler.h"
#include "pdf/PdfRawScan.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QPdfLinkModel>
#include <QStandardPaths>
#include <QDebug>
#include <QSet>
#include <algorithm>

using namespace mg::pdfraw;

namespace {

//  Wie weit die Suche nach dem Medium den Verweisen folgt: Aktion -> Rendition -> Medienclip -> Dateiangabe.
constexpr int kMaxTiefe   = 4;
constexpr int kMaxObjekte = 32;

QByteArray nameValue(const QByteArray& dict, const char* key) {
    const qsizetype kp = keyPos(dict, key);
    if (kp < 0) return {};
    qsizetype v = kp + qsizetype(qstrlen(key));
    skipWs(dict, v);
    if (v >= dict.size() || dict[v] != '/') return {};
    qsizetype e = v + 1;
    while (e < dict.size() && !isDelim(dict[e])) ++e;
    return dict.mid(v + 1, e - v - 1);
}

//  Alle Verweise `N G R` in einem Text, in Lesereihenfolge.
QVector<int> alleVerweise(const QByteArray& d) {
    QVector<int> out;
    qsizetype i = 0;
    while (i < d.size()) {
        if (d[i] >= '0' && d[i] <= '9' && (i == 0 || isDelim(d[i-1]))) {
            qsizetype j = i;
            const long a = readUInt(d, j);
            skipWs(d, j);
            const long b = readUInt(d, j);
            skipWs(d, j);
            if (a > 0 && b >= 0 && j < d.size() && d[j] == 'R' && (j + 1 >= d.size() || isDelim(d[j+1]))) {
                out.append(int(a));
                i = j + 1;
                continue;
            }
            i = (j > i) ? j : i + 1;
        } else {
            ++i;
        }
    }
    return out;
}

//  Seiten und Annotationen sind Nachbarn, keine Teile des Mediums - ihnen zu folgen fuehrte zu fremden Adressen.
bool istNachbar(const QByteArray& dict) {
    return keyPos(dict, "/Rect") >= 0 || keyPos(dict, "/Kids") >= 0 || keyPos(dict, "/MediaBox") >= 0
           || isPageObject(dict);
}

MediaAnnotation::Type typFuerEndung(const QString& ext) {
    static const QStringList video = { "mp4", "avi", "mkv", "mov", "webm", "ogv", "wmv", "flv", "m4v" };
    static const QStringList audio = { "mp3", "wav", "ogg", "flac", "aac", "m4a", "opus", "wma", "aiff", "aif" };
    if (video.contains(ext, Qt::CaseInsensitive)) return MediaAnnotation::Type::Video;
    if (audio.contains(ext, Qt::CaseInsensitive)) return MediaAnnotation::Type::Audio;
    return MediaAnnotation::Type::Unknown;
}

MediaAnnotation::Type typFuerAdresse(const QString& url) {
    const QString endung = QFileInfo(QString(url).section(u'?', 0, 0)).suffix();
    return typFuerEndung(endung);
}

//  Seite einer Annotation: wo sie in /Annots steht, sonst ihr /P, sonst die erste.
int seiteFuer(const QHash<int,int>& annotSeite, const QHash<int,int>& seiteVon, int num, const QByteArray& dict) {
    if (annotSeite.contains(num)) return annotSeite.value(num);
    return seiteVon.value(firstRefForKey(dict, "/P"), 0);
}

}  // namespace

struct PdfMediaHandler::Scan {
    const QByteArray&    data;
    QHash<int,qsizetype> off;
    QVector<int>         pages;
    QHash<int,int>       seiteVon;    // Seitenobjekt -> Index
    QHash<int,int>       annotSeite;  // Annotationsobjekt -> Index, aus /Annots der Seite
};

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

    Scan s{data, buildObjectOffsets(data), {}, {}, {}};
    s.pages = pageObjects(data, s.off);
    for (int i = 0; i < s.pages.size(); ++i) {
        s.seiteVon.insert(s.pages[i], i);
        const QByteArray pd = objectDict(data, s.off, s.pages[i]);
        QByteArray annots = bracketValue(pd, "/Annots");
        if (annots.isEmpty()) {                                  // /Annots als eigenes Objekt
            const int ref = firstRefForKey(pd, "/Annots");
            if (ref > 0 && s.off.contains(ref)) {
                qsizetype p = s.off.value(ref);
                skipWs(data, p);
                const qsizetype e = (p < data.size() && data[p] == '[') ? data.indexOf(']', p) : -1;
                if (e > p) annots = data.mid(p, e - p + 1);
            }
        }
        for (int a : alleVerweise(annots))
            if (!s.annotSeite.contains(a)) s.annotSeite.insert(a, i);
    }

    QVector<QPair<qsizetype,int>> folge;
    folge.reserve(s.off.size());
    for (auto it = s.off.constBegin(); it != s.off.constEnd(); ++it) folge.append({ it.value(), it.key() });
    std::sort(folge.begin(), folge.end());

    for (const auto& eintrag : std::as_const(folge)) {
        const int num = eintrag.second;
        const QByteArray dict = objectDict(data, s.off, num);
        if (dict.isEmpty()) continue;
        const QByteArray sub = nameValue(dict, "/Subtype");
        if (sub == "Link")                          parseLink(s, num, dict);
        else if (sub == "Sound")                    parseMedia(s, num, dict, false);
        else if (sub == "Screen" || sub == "Movie") parseMedia(s, num, dict, true);
    }

    //  Spruenge innerhalb der Datei (`/Dest`, `/GoTo`, auch benannte Ziele) loest PDFium schon auf; der Rohstrom
    //  muesste dafuer den Namensbaum lesen. Webadressen kommen oben aus dem Rohstrom.
    if (m_doc && m_doc->status() == QPdfDocument::Status::Ready) {
        QPdfLinkModel links;
        links.setDocument(m_doc);
        for (int seite = 0; seite < m_doc->pageCount(); ++seite) {
            links.setPage(seite);
            const QSizeF ps = m_doc->pagePointSize(seite);
            if (ps.isEmpty()) continue;
            for (int i = 0; i < links.rowCount({}); ++i) {
                const QModelIndex ix = links.index(i);
                const int ziel = links.data(ix, int(QPdfLinkModel::Role::Page)).toInt();
                if (!links.data(ix, int(QPdfLinkModel::Role::Url)).toUrl().isEmpty() || ziel < 0) continue;
                const QRectF r = links.data(ix, int(QPdfLinkModel::Role::Rectangle)).toRectF();
                MediaAnnotation ann;
                ann.page = seite;
                ann.rect = QRectF(r.x() / ps.width(), r.y() / ps.height(), r.width() / ps.width(),
                                  r.height() / ps.height());
                ann.type = MediaAnnotation::Type::Link;
                ann.targetPage = ziel;
                m_annotations.append(ann);
            }
        }
    }

    qDebug() << "PdfMediaHandler: found" << m_annotations.size()
             << "media annotation(s) in" << QFileInfo(pdfPath).fileName();
}

QSizeF PdfMediaHandler::pageSize(const Scan& s, int page) const {
    if (m_doc && page < m_doc->pageCount()) {
        const QSizeF ps = m_doc->pagePointSize(page);
        if (!ps.isEmpty()) return ps;
    }
    if (page >= 0 && page < s.pages.size()) return mediaBoxSize(objectDict(s.data, s.off, s.pages[page]));
    return {};
}

void PdfMediaHandler::parseLink(const Scan& s, int num, const QByteArray& dict) {
    QByteArray aktion = nestedDictForKey(dict, "/A");
    if (aktion.isEmpty()) aktion = objectDict(s.data, s.off, firstRefForKey(dict, "/A"));
    QString url = stringForKey(aktion, "/URI").trimmed();
    const QString schema = QUrl(url).scheme().toLower();
    if (schema != u"http" && schema != u"https" && schema != u"mailto" && schema != u"ftp") {
        //  Verweis auf eine Datei (`FEATURES.md`, `file:///…`): relativ zum Ordner der PDF, und nur, wenn es sie gibt.
        QString pfad;
        if (schema == u"file") pfad = QUrl(url).toLocalFile();
        else if (schema.isEmpty() || (schema.size() == 1 && QDir::isAbsolutePath(url)))
            pfad = QUrl::fromPercentEncoding(url.section(u'#', 0, 0).toUtf8());
        if (pfad.isEmpty()) return;
        if (QDir::isRelativePath(pfad)) pfad = QFileInfo(m_pdfPath).dir().absoluteFilePath(pfad);
        pfad = QDir::cleanPath(pfad);
        if (!QFileInfo(pfad).isFile()) return;
        url = pfad;
    }

    const int page = seiteFuer(s.annotSeite, s.seiteVon, num, dict);
    const QSizeF ps = pageSize(s, page);
    if (ps.isEmpty()) return;
    const QRectF r = parseNormalisedRect(bracketValue(dict, "/Rect"), ps);
    if (!r.isValid() || r.isEmpty()) return;

    QString label = stringForKey(dict, "/Contents").trimmed();
    if (label.isEmpty()) label = stringForKey(dict, "/NM").trimmed();
    if (label.isEmpty()) label = url;

    MediaAnnotation ann;
    ann.page      = page;
    ann.rect      = r;
    ann.sourceUrl = url;
    ann.type      = MediaAnnotation::Type::Link;
    ann.label     = label;
    m_annotations.append(ann);
}

void PdfMediaHandler::parseMedia(const Scan& s, int num, const QByteArray& dict, bool isVideo) {
    MediaAnnotation ann;
    ann.page = seiteFuer(s.annotSeite, s.seiteVon, num, dict);
    if (m_doc && m_doc->pageCount() > 0) ann.page = qBound(0, ann.page, m_doc->pageCount() - 1);
    ann.rect = parseNormalisedRect(bracketValue(dict, "/Rect"), pageSize(s, ann.page));
    if (!ann.rect.isValid() || ann.rect.isEmpty())
        ann.rect = QRectF(0.02, 0.02, 0.08, 0.08);
    ann.label = stringForKey(dict, "/Contents").trimmed();
    if (ann.label.isEmpty()) ann.label = stringForKey(dict, "/NM").trimmed();
    if (ann.label.isEmpty()) ann.label = tr("Media");

    //  Breitensuche ueber die Verweise: das erste erkannte Medium gewinnt, die Adresse kommt aus dem
    //  ersten Woerterbuch, das eine traegt. Die Annotation selbst steht vorn.
    QVector<QByteArray> woerterbuecher{dict};
    QVector<QPair<int,int>> warte;                      // Objekt, Tiefe
    for (int r : alleVerweise(dict)) warte.append({r, 1});
    QSet<int> gesehen{num};
    bool extrahiert = false;
    for (int i = 0; i < warte.size() && gesehen.size() <= kMaxObjekte; ++i) {
        const auto [obj, tiefe] = warte[i];
        if (gesehen.contains(obj)) continue;
        gesehen.insert(obj);
        const QByteArray d = objectDict(s.data, s.off, obj);
        if (d.isEmpty() || istNachbar(d)) continue;
        woerterbuecher.append(d);
        if (!extrahiert) {
            const QByteArray strom = streamData(s.data, s.off, obj);
            if (!strom.isEmpty()) extrahiert = extractStream(strom, ann);
        }
        if (tiefe < kMaxTiefe)
            for (int r : alleVerweise(d)) warte.append({r, tiefe + 1});
    }

    if (!extrahiert && isVideo) {
        QString url;
        for (const char* key : {"/URI", "/F", "/UF"}) {
            for (const QByteArray& w : woerterbuecher) {
                url = stringForKey(w, key).trimmed();
                if (!url.isEmpty()) break;
            }
            if (!url.isEmpty()) break;
        }
        if (!url.isEmpty()) {
            ann.sourceUrl = url;
            ann.type = typFuerAdresse(url);
        }
    }
    if (ann.type == MediaAnnotation::Type::Unknown)
        ann.type = isVideo ? MediaAnnotation::Type::Video : MediaAnnotation::Type::Audio;

    // Avoid duplicates (same page + nearly identical rect)
    for (const auto& existing : std::as_const(m_annotations)) {
        if (existing.page == ann.page && existing.type != MediaAnnotation::Type::Link) {
            const QPointF delta = existing.rect.center() - ann.rect.center();
            if (qAbs(delta.x()) < 0.01 && qAbs(delta.y()) < 0.01)
                return;
        }
    }
    m_annotations.append(ann);
}

bool PdfMediaHandler::extractStream(const QByteArray& bytes, MediaAnnotation& ann) {
    if (bytes.size() < 16) return false;
    const QString ext = guessMimeExt(bytes.left(16));
    if (ext.isEmpty()) return false;  // compressed / unrecognised – skip

    const QString tmpDir  = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString baseName = QFileInfo(m_pdfPath).completeBaseName();
    const QString tmpPath  = tmpDir + QString("/pdfmedia_%1_p%2_%3.%4")
                                         .arg(baseName, QString::number(ann.page),
                                              QString::number(m_annotations.size()), ext);
    QFile tmp(tmpPath);
    if (!tmp.open(QIODevice::WriteOnly)) return false;
    tmp.write(bytes);
    tmp.close();

    ann.sourcePath = tmpPath;
    ann.type       = typFuerEndung(ext);
    m_tempFiles.append(tmpPath);
    return true;
}

QString PdfMediaHandler::guessMimeExt(const QByteArray& header) {
    if (header.size() < 4) return {};
    const auto u = [&](int i) { return static_cast<unsigned char>(header[i]); };

    //  RIFF ist ein Behaelter: erst die Art in Byte 8..11 entscheidet, ob Ton oder Film.
    if (header.startsWith("RIFF")) {
        if (header.size() < 12) return {};
        const QByteArray art = header.mid(8, 4);
        if (art == "AVI ") return QStringLiteral("avi");
        if (art == "WAVE") return QStringLiteral("wav");
        return {};
    }
    if (header.startsWith("ID3"))  return QStringLiteral("mp3");
    if (header.startsWith("FORM")) return QStringLiteral("aiff");
    if (header.startsWith("OggS")) return QStringLiteral("ogg");
    if (header.startsWith("fLaC")) return QStringLiteral("flac");
    if (header.size() >= 8) {
        const QByteArray ftyp = header.mid(4, 4);
        if (ftyp == "ftyp" || ftyp == "moov") return QStringLiteral("mp4");
        if (ftyp == "wide" || ftyp == "mdat") return QStringLiteral("mov");
    }
    if (u(0) == 0x1A && u(1) == 0x45 && u(2) == 0xDF && u(3) == 0xA3) return QStringLiteral("webm");
    //  ADTS und MPEG-Audio teilen das Sync-Muster; nur die Layer-Bits trennen sie (ADTS: 00).
    if (u(0) == 0xFF && (u(1) & 0xF6) == 0xF0) return QStringLiteral("aac");
    if (u(0) == 0xFF && (u(1) & 0xE0) == 0xE0 && (u(1) & 0x06) != 0) return QStringLiteral("mp3");
    return {};
}
