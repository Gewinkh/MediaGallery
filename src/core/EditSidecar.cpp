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
constexpr quint8     kBinaer    = 2;   // Koerper ist MGEB, gepackt
constexpr quint8     kBinaerRoh = 3;   // Koerper ist MGEB, ungepackt

//  Packstufe nach GROESSE, weil das Speichern im GUI-Faden laeuft. Bei 1,6 MB
//  Koerper: Stufe 1 in 2,2 ms auf 39,5 kB, Stufe 6 in 5,4 ms auf 34,4 kB - drei
//  Millisekunden fuer fuenf Kilobyte. Bei 40 kB kostet Stufe 6 nur 0,15 ms.
constexpr qsizetype  kGrossAb    = 64 << 10;
constexpr int        kPackKlein  = 6;
constexpr int        kPackGross  = 1;

int packStufe(qsizetype bytes) { return bytes >= kGrossAb ? kPackGross : kPackKlein; }
constexpr qsizetype  kKopf      = 10;

//  Deckel gegen praeparierte Dateien: schon der Deckel VOR dem Entpacken, sonst
//  fordert eine Handvoll Byte hunderte Megabyte an.
constexpr qint64 kMaxDatei = 8LL << 20;
constexpr qint64 kMaxText  = 64LL << 20;

void ziehUm(const QString& dokument, Inhalt& aus);

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

//  Rohbytes -> Koerper. Erkannt wird am Kopf; ohne ihn gilt der Inhalt als die
//  alte, unverpackte Fassung. `binaer` sagt, ob der Koerper MGEB ist.
QByteArray auspacken(const QByteArray& b, bool* binaer = nullptr) {
    if (binaer) *binaer = false;
    if (b.size() < kKopf || !b.startsWith(QByteArray::fromRawData(kMagie, 4)))
        return b;
    if (quint8(b.at(4)) != kFassung) return {};
    const quint8 art = quint8(b.at(5));
    const quint32 laenge = leseU32(b, 6);
    if (laenge > kMaxText) return {};
    if (art > kBinaerRoh) return {};
    //  Die Art sagt ZWEI Dinge: was im Koerper steht und ob er gepackt ist.
    //  Ohne die eigene Art fuer "binaer, ungepackt" laeuft eine kleine Beidatei,
    //  die sich nicht lohnt zu packen, beim Lesen in den JSON-Zweig.
    if (binaer) *binaer = (art == kBinaer || art == kBinaerRoh);
    const QByteArray nutz = b.mid(kKopf);
    if (art == kRoh || art == kBinaerRoh)
        return qsizetype(laenge) == nutz.size() ? nutz : QByteArray();
    bool ok = false;
    QByteArray text = mg::zcodec::inflate(nutz, mg::zcodec::Wrap::Zlib,
                                          qint64(laenge), false, &ok);
    if (!ok || qsizetype(laenge) != text.size()) return {};
    return text;
}

//  Kopf + Nutzlast. `art` sagt, was drinsteht; gepackt wird nur, wenn es kuerzer wird.
QByteArray verpacke(const QByteArray& koerper, quint8 artRoh, quint8 artGepackt) {
    QByteArray nutz = koerper;
    quint8 art = artRoh;
    if (mg::zcodec::available()) {
        bool ok = false;
        const QByteArray p = mg::zcodec::deflate(koerper, mg::zcodec::Wrap::Zlib,
                                                 packStufe(koerper.size()), &ok);
        if (ok && !p.isEmpty() && p.size() < koerper.size()) { nutz = p; art = artGepackt; }
    }
    QByteArray aus(kMagie, 4);
    aus.append(char(kFassung));
    aus.append(char(art));
    const quint32 laenge = quint32(koerper.size());
    for (int i = 0; i < 4; ++i) aus.append(char((laenge >> (8 * i)) & 0xFF));
    aus.append(nutz);
    return aus;
}

bool aufDiePlatte(const QString& ziel, const QByteArray& aus) {
    QSaveFile f(ziel);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    if (f.write(aus) != aus.size()) { f.cancelWriting(); return false; }
    return f.commit();
}

//  Die alte Form in die heutige uebersetzen. Geschrieben wird NUR, wenn das
//  Ergebnis vorher im Speicher gelesen werden konnte und dieselben Abschnitte
//  traegt - sonst bleibt die Datei, wie sie ist.
void ziehUm(const QString& dokument, Inhalt& aus) {
    if (aus.json.isEmpty()) return;

    mg::mgeb::Schreiber s;
    s.beginneObjekt();
    for (auto it = aus.json.constBegin(); it != aus.json.constEnd(); ++it) {
        s.schluessel(it.key());
        s.uebernimm(it.value());
    }
    s.beendeObjekt();
    const QByteArray koerper = s.fertig();

    mg::mgeb::Doku probe;
    if (!probe.lies(koerper)) return;
    if (probe.wurzel().count() != int(aus.json.size())) return;
    for (auto it = aus.json.constBegin(); it != aus.json.constEnd(); ++it)
        if (probe.wurzel().value(it.key()).istLeer()) return;

    if (!schreibeBin(dokument, koerper)) return;
    if (aus.bin.lies(koerper)) {
        aus.istBin = true;
        aus.json = QJsonObject();
    }
}

}  // namespace

QString pfad(const QString& dokument)    { return dokument + QStringLiteral(".mgedit"); }
QString altPfad(const QString& dokument) { return dokument + QStringLiteral(".mgedit.json"); }

QJsonObject lies(const QString& dokument) {
    QByteArray roh = leseBytes(pfad(dokument));
    if (roh.isEmpty()) roh = leseBytes(altPfad(dokument));
    if (roh.isEmpty()) return {};
    bool binaer = false;
    const QByteArray text = auspacken(roh, &binaer);
    if (text.isEmpty() || binaer) return {};
    const QJsonDocument jd = QJsonDocument::fromJson(text);
    return jd.isObject() ? jd.object() : QJsonObject();
}

Inhalt liesInhalt(const QString& dokument, bool zieheUm) {
    Inhalt aus;
    QByteArray roh = leseBytes(pfad(dokument));
    if (roh.isEmpty()) roh = leseBytes(altPfad(dokument));
    if (roh.isEmpty()) return aus;
    bool binaer = false;
    const QByteArray koerper = auspacken(roh, &binaer);
    if (koerper.isEmpty()) return aus;
    if (binaer) {
        aus.istBin = aus.bin.lies(koerper);
        return aus;
    }
    //  Alte Datei: Qts Leser laeuft genau hier, einmal je Datei.
    const QJsonDocument jd = QJsonDocument::fromJson(koerper);
    if (!jd.isObject()) return aus;
    aus.json = jd.object();
    if (zieheUm) ziehUm(dokument, aus);
    return aus;
}

bool schreibeBin(const QString& dokument, const QByteArray& koerper) {
    if (!aufDiePlatte(pfad(dokument), verpacke(koerper, kBinaerRoh, kBinaer))) return false;
    QFile::remove(altPfad(dokument));
    return true;
}

bool schreibe(const QString& dokument, const QJsonObject& wurzel) {
    //  Ohne zlib bleibt der rohe Text stehen; der Kopf sagt, was drinsteht.
    const QByteArray text = QJsonDocument(wurzel).toJson(QJsonDocument::Compact);
    if (!aufDiePlatte(pfad(dokument), verpacke(text, kRoh, kGepackt))) return false;
    //  Die JSON-Fassung von frueher steht sonst daneben und wuerde beim
    //  naechsten Lesen nie wieder angesehen.
    QFile::remove(altPfad(dokument));
    return true;
}

int uebernimmFremde(mg::mgeb::Schreiber& s, const Inhalt& in, const QString& eigener) {
    return uebernimmFremde(s, in, QStringList{ eigener });
}

int uebernimmFremde(mg::mgeb::Schreiber& s, const Inhalt& in, const QStringList& eigene) {
    int n = 0;
    if (in.istBin) {
        const mg::mgeb::Objekt w = in.bin.wurzel();
        for (int i = 0; i < w.count(); ++i) {
            const QString name = w.schluesselBei(i);
            if (eigene.contains(name)) continue;
            s.schluessel(name);
            s.uebernimm(w.wertBei(i));
            ++n;
        }
        return n;
    }
    for (auto it = in.json.constBegin(); it != in.json.constEnd(); ++it) {
        if (eigene.contains(it.key())) continue;
        s.schluessel(it.key());
        s.uebernimm(it.value());
        ++n;
    }
    return n;
}

bool istBeidatei(const QByteArray& roh) {
    return roh.size() >= kKopf && roh.startsWith(QByteArray::fromRawData(kMagie, 4));
}

QString lesbar(const QString& beidatei) {
    bool binaer = false;
    const QByteArray koerper = auspacken(leseBytes(beidatei), &binaer);
    if (koerper.isEmpty()) return {};
    if (binaer) return mg::mgeb::alsText(koerper);
    const QJsonDocument jd = QJsonDocument::fromJson(koerper);
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
