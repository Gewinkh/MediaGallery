#include "html/HtmlBild.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageIOHandler>
#include <QImageReader>
#include <QSvgRenderer>
#include <QUrl>

#include <cctype>

namespace mg::html {

namespace {

constexpr qint64 kMaxDatei = 64 << 20;

bool gleich(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if ((a[i] | 0x20) != (b[i] | 0x20)) return false;
    return true;
}

std::string_view getrimmt(std::string_view v) {
    while (!v.empty() && std::isspace(uint8_t(v.front()))) v.remove_prefix(1);
    while (!v.empty() && std::isspace(uint8_t(v.back()))) v.remove_suffix(1);
    return v;
}

std::string_view ohneAnfuehrung(std::string_view v) {
    v = getrimmt(v);
    if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()) v = v.substr(1, v.size() - 2);
    return v;
}

bool datenUrl(std::string_view url, QByteArray& typ, QByteArray& aus) {
    if (url.size() < 5 || !gleich(url.substr(0, 5), "data:")) return false;
    const size_t komma = url.find(',');
    if (komma == std::string_view::npos) return false;
    std::string_view kopf = url.substr(5, komma - 5);
    const bool b64 = kopf.size() >= 7 && gleich(kopf.substr(kopf.size() - 7), ";base64");
    if (b64) kopf.remove_suffix(7);
    kopf = getrimmt(kopf.substr(0, kopf.find(';')));
    typ = QByteArray(kopf.data(), qsizetype(kopf.size())).toLower();
    const QByteArray roh = QByteArray::fromRawData(url.data() + komma + 1, qsizetype(url.size() - komma - 1));
    aus = b64 ? QByteArray::fromBase64(roh) : QByteArray::fromPercentEncoding(roh);
    return !aus.isEmpty();
}

//  Bytes einer data:-URL oder der Pfad einer lokalen Datei, beide mit Medientyp; Netz und Unbekanntes nichts.
struct Quelle {
    QByteArray typ, daten;
    QString pfad;
};

bool quelle(std::string_view url, std::string_view basis, Quelle& q) {
    url = ohneAnfuehrung(url);
    if (datenUrl(url, q.typ, q.daten)) return true;
    const QUrl roh(QString::fromUtf8(url.data(), qsizetype(url.size())));
    if (url.empty() || !roh.isValid()) return false;
    const QUrl u = basis.empty() ? roh
        : QUrl::fromLocalFile(QString::fromUtf8(basis.data(), qsizetype(basis.size())) + u'/').resolved(roh);
    if (!u.isLocalFile()) return false;
    const QFileInfo f(u.toLocalFile());
    if (!f.isFile() || f.size() > kMaxDatei) return false;
    q.pfad = f.absoluteFilePath();
    const QString endung = f.suffix().toLower();
    q.typ = endung == u"svg" || endung == u"svgz" ? "image/svg+xml" : "image/";
    if (q.typ == "image/svg+xml") {
        QFile d(q.pfad);
        if (!d.open(QIODevice::ReadOnly)) return false;
        q.daten = d.readAll();
    }
    return true;
}

Masse svgMasse(const QSvgRenderer& r, std::string_view xml) {
    Masse m;
    const QRectF vb = r.viewBoxF();
    const QSize d = r.defaultSize();
    const std::string_view wb = wurzelAttribut(xml, "width"), wh = wurzelAttribut(xml, "height");
    if (!wb.empty() && wb.back() != '%') m.b = float(d.width());
    if (!wh.empty() && wh.back() != '%') m.h = float(d.height());
    if (vb.width() > 0 && vb.height() > 0) m.verhaeltnis = float(vb.width() / vb.height());
    else if (m.b > 0 && m.h > 0) m.verhaeltnis = m.b / m.h;
    return m;
}

std::shared_ptr<QSvgRenderer> svgAus(const QByteArray& daten, Masse& m) {
    auto r = std::make_shared<QSvgRenderer>(daten);
    if (!r->isValid()) return {};
    const std::string_view xml(daten.constData(), size_t(daten.size()));
    m = svgMasse(*r, xml);
    if (!gleich(wurzelAttribut(xml, "preserveAspectRatio"), "none")) r->setAspectRatioMode(Qt::KeepAspectRatio);
    return r;
}

//  EXIF-Drehung wie im Browser: die Masse gelten nach der Drehung.
Masse rasterMasse(QSize n, const QImageReader& leser) {
    if (leser.transformation() & QImageIOHandler::TransformationRotate90) n.transpose();
    return Masse{ float(n.width()), float(n.height()), float(n.width()) / float(n.height()) };
}

}   // namespace

std::string_view wurzelAttribut(std::string_view xml, std::string_view name) {
    const size_t a = xml.find("<svg");
    if (a == std::string_view::npos) return {};
    const std::string_view tag = xml.substr(a, xml.find('>', a) - a);
    for (size_t p = tag.find(name); p != std::string_view::npos; p = tag.find(name, p + 1)) {
        size_t e = p + name.size();
        if (!std::isspace(uint8_t(tag[p - 1]))) continue;
        while (e < tag.size() && std::isspace(uint8_t(tag[e]))) ++e;
        if (e >= tag.size() || tag[e] != '=') continue;
        do ++e; while (e < tag.size() && std::isspace(uint8_t(tag[e])));
        if (e >= tag.size()) return {};
        const char q = tag[e];
        if (q != '"' && q != '\'') return {};
        const size_t z = tag.find(q, e + 1);
        return z == std::string_view::npos ? std::string_view() : tag.substr(e + 1, z - e - 1);
    }
    return {};
}

Geladen ladeBild(std::string_view url, std::string_view basis, int kante) {
    Geladen g;
    Quelle q;
    if (!quelle(url, basis, q)) return g;
    if (q.typ == "image/svg+xml") {
        g.svg = svgAus(q.daten, g.masse);
        return g;
    }
    if (!q.typ.startsWith("image/")) return g;
    QBuffer puffer(&q.daten);
    QImageReader leser;
    if (q.pfad.isEmpty()) {
        puffer.open(QIODevice::ReadOnly);
        leser.setDevice(&puffer);
    } else {
        leser.setFileName(q.pfad);
    }
    leser.setAutoTransform(true);
    const QSize n = leser.size();
    if (kante > 0 && n.isValid() && std::max(n.width(), n.height()) > kante)
        leser.setScaledSize(n.scaled(kante, kante, Qt::KeepAspectRatio));
    g.raster = leser.read();
    if (g.raster.isNull()) return g;
    g.masse = n.isValid() ? rasterMasse(n, leser)
                          : Masse{ float(g.raster.width()), float(g.raster.height()),
                                   float(g.raster.width()) / float(g.raster.height()) };
    return g;
}

Masse bildMasse(std::string_view url, std::string_view basis) {
    Quelle q;
    if (!quelle(url, basis, q)) return {};
    Masse m;
    if (q.typ == "image/svg+xml") {
        svgAus(q.daten, m);
        return m;
    }
    if (!q.typ.startsWith("image/")) return {};
    QBuffer puffer(&q.daten);
    QImageReader leser;
    if (q.pfad.isEmpty()) {
        puffer.open(QIODevice::ReadOnly);
        leser.setDevice(&puffer);
    } else {
        leser.setFileName(q.pfad);
    }
    leser.setAutoTransform(true);
    const QSize n = leser.size();
    return n.isValid() && !n.isEmpty() ? rasterMasse(n, leser) : Masse{};
}

}   // namespace mg::html
