#include "core/EditSidecar.h"

#include "core/ZCodec.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace mg::editsidecar {
namespace {

constexpr char       kMagie[4]  = { 'M', 'G', 'E', 'D' };
constexpr quint8     kFassung   = 1;
constexpr quint8     kRoh       = 0;
constexpr quint8     kGepackt   = 1;
constexpr qsizetype  kKopf      = 10;

//  Deckel gegen praeparierte Dateien: schon der Deckel VOR dem Entpacken, sonst
//  fordert eine Handvoll Byte hunderte Megabyte an.
constexpr qint64 kMaxDatei = 8LL << 20;
constexpr qint64 kMaxText  = 64LL << 20;

quint32 leseU32(const QByteArray& b, qsizetype pos) {
    return quint32(quint8(b.at(pos)))
         | (quint32(quint8(b.at(pos + 1))) << 8)
         | (quint32(quint8(b.at(pos + 2))) << 16)
         | (quint32(quint8(b.at(pos + 3))) << 24);
}

QByteArray leseBytes(const QString& datei) {
    QFile f(datei);
    if (!f.exists() || f.size() > kMaxDatei || !f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

//  Rohbytes -> JSON-Text. Erkannt wird am Kopf; ohne ihn gilt der Inhalt als
//  die alte, unverpackte Fassung.
QByteArray auspacken(const QByteArray& b) {
    if (b.size() < kKopf || !b.startsWith(QByteArray::fromRawData(kMagie, 4)))
        return b;
    if (quint8(b.at(4)) != kFassung) return {};
    const quint8 art = quint8(b.at(5));
    const quint32 laenge = leseU32(b, 6);
    if (laenge > kMaxText) return {};
    const QByteArray nutz = b.mid(kKopf);
    if (art == kRoh)
        return qsizetype(laenge) == nutz.size() ? nutz : QByteArray();
    if (art != kGepackt) return {};
    bool ok = false;
    QByteArray text = mg::zcodec::inflate(nutz, mg::zcodec::Wrap::Zlib,
                                          qint64(laenge), false, &ok);
    if (!ok || qsizetype(laenge) != text.size()) return {};
    return text;
}

}  // namespace

QString pfad(const QString& dokument)    { return dokument + QStringLiteral(".mgedit"); }
QString altPfad(const QString& dokument) { return dokument + QStringLiteral(".mgedit.json"); }

QJsonObject lies(const QString& dokument) {
    QByteArray roh = leseBytes(pfad(dokument));
    if (roh.isEmpty()) roh = leseBytes(altPfad(dokument));
    if (roh.isEmpty()) return {};
    const QByteArray text = auspacken(roh);
    if (text.isEmpty()) return {};
    const QJsonDocument jd = QJsonDocument::fromJson(text);
    return jd.isObject() ? jd.object() : QJsonObject();
}

bool schreibe(const QString& dokument, const QJsonObject& wurzel) {
    const QByteArray text = QJsonDocument(wurzel).toJson(QJsonDocument::Compact);
    QByteArray nutz = text;
    quint8 art = kRoh;
    if (mg::zcodec::available()) {
        bool ok = false;
        const QByteArray p = mg::zcodec::deflate(text, mg::zcodec::Wrap::Zlib, 9, &ok);
        //  Ohne zlib - und bei winzigen Baeumen, die gepackt groesser werden -
        //  bleibt der rohe Text stehen; der Kopf sagt, was drinsteht.
        if (ok && !p.isEmpty() && p.size() < text.size()) { nutz = p; art = kGepackt; }
    }

    QByteArray aus(kMagie, 4);
    aus.append(char(kFassung));
    aus.append(char(art));
    const quint32 laenge = quint32(text.size());
    for (int i = 0; i < 4; ++i) aus.append(char((laenge >> (8 * i)) & 0xFF));
    aus.append(nutz);

    QSaveFile f(pfad(dokument));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    if (f.write(aus) != aus.size()) { f.cancelWriting(); return false; }
    if (!f.commit()) return false;
    //  Die JSON-Fassung von frueher steht sonst daneben und wuerde beim
    //  naechsten Lesen nie wieder angesehen.
    QFile::remove(altPfad(dokument));
    return true;
}

bool istBeidatei(const QByteArray& roh) {
    return roh.size() >= kKopf && roh.startsWith(QByteArray::fromRawData(kMagie, 4));
}

QString lesbar(const QString& beidatei) {
    const QByteArray text = auspacken(leseBytes(beidatei));
    if (text.isEmpty()) return {};
    const QJsonDocument jd = QJsonDocument::fromJson(text);
    if (!jd.isObject()) return {};
    return QString::fromUtf8(jd.toJson(QJsonDocument::Indented));
}

bool entferne(const QString& dokument) {
    bool ok = true;
    for (const QString& p : { pfad(dokument), altPfad(dokument) })
        if (QFile::exists(p) && !QFile::remove(p)) ok = false;
    return ok;
}

}  // namespace mg::editsidecar
