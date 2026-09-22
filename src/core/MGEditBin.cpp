#include "core/MGEditBin.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace mg::mgeb {
namespace {

//  Typbyte eines Wertes.
enum Typ : std::uint8_t {
    tNull = 0, tFalsch = 1, tWahr = 2,
    tDouble = 3, tGanz = 4, tText = 5,
    tListe = 6, tObjekt = 7, tDoubleFeld = 8, tDoubleFeldGeschichtet = 9
};

//  Typ 9 legt die Bytes der Zahlen GESCHICHTET ab (erst alle nullten, dann alle
//  ersten). Das wird nur noch GELESEN, nicht mehr geschrieben: an echter
//  Handschrift sind 35 % der Koordinaten exakte Wiederholungen, und die erkennt
//  der Packer in der rohen Folge als einen Treffer, waehrend die Schichtung sie
//  auf acht Ebenen zerreisst. Gemessen an einem echten Strich mit 1515 Punkten:
//  Datei +48 %, Lesen +34 %. An erzeugten Punkten OHNE Wiederholungen sah es
//  umgekehrt aus - deshalb steht die Erkenntnis hier und nicht nur im Log.
void entschichte(const char* quelle, double* ziel, std::size_t n) {
    char* z = reinterpret_cast<char*>(ziel);
    for (std::size_t b = 0; b < 8; ++b)
        for (std::size_t i = 0; i < n; ++i)
            z[i * 8 + b] = quelle[b * n + i];
}

const char* const kNamen[k_Anzahl] = {
    "format", "version", "recording",
    "boxes", "chains", "growBase", "pageplan", "textops", "formvals", "anns",
    "page", "tr", "kind", "x", "y", "w", "h", "pts",
    "srcobj", "mstyle", "img", "hilite", "orig",
    "stroke", "lw", "fill",
    "text", "font", "size", "bold", "italic", "under", "color",
    "align", "valign", "anchor",
    "src", "key", "doc", "rot", "at", "del",
    "spaltenbreiten"
};

std::uint32_t leseFest32(const char* p) {
    std::uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

//  Varint lesen, mit Ende. Gibt false zurueck, statt darueber hinauszulaufen.
bool leseVarint(const char*& p, const char* ende, std::uint32_t& aus) {
    std::uint32_t w = 0;
    for (int s = 0; s < 5; ++s) {
        if (p >= ende) return false;
        const std::uint8_t b = static_cast<std::uint8_t>(*p++);
        w |= std::uint32_t(b & 0x7Fu) << (7 * s);
        if (!(b & 0x80u)) { aus = w; return true; }
    }
    return false;
}

bool leseZigzag(const char*& p, const char* ende, qint64& aus) {
    std::uint64_t w = 0;
    for (int s = 0; s < 10; ++s) {
        if (p >= ende) return false;
        const std::uint8_t b = static_cast<std::uint8_t>(*p++);
        w |= std::uint64_t(b & 0x7Fu) << (7 * s);
        if (!(b & 0x80u)) {
            aus = static_cast<qint64>((w >> 1) ^ (~(w & 1) + 1));
            return true;
        }
    }
    return false;
}

}  // namespace

const char* keyName(Key k) { return kNamen[k]; }

//  ── Schreiben ───────────────────────────────────────────────────────────────

Schreiber::Schreiber() { m_daten.reserve(4096); }

void Schreiber::varint(std::uint32_t w) {
    while (true) {
        const std::uint8_t b = w & 0x7Fu;
        w >>= 7;
        m_daten.append(char(w ? (b | 0x80u) : b));
        if (!w) return;
    }
}

void Schreiber::zigzag(qint64 v) {
    std::uint64_t w = (static_cast<std::uint64_t>(v) << 1) ^ static_cast<std::uint64_t>(v >> 63);
    while (true) {
        const std::uint8_t b = w & 0x7Fu;
        w >>= 7;
        m_daten.append(char(w ? (b | 0x80u) : b));
        if (!w) return;
    }
}

std::uint32_t Schreiber::nummer(const QString& name) {
    const auto it = m_nummern.constFind(name);
    if (it != m_nummern.cend()) return *it;
    const quint32 n = quint32(m_namen.size());
    m_namen.append(name.toUtf8());
    m_nummern.insert(name, n);
    return n;
}

void Schreiber::beginneObjekt() {
    if (!m_offen.isEmpty() && !m_objekt.back()) ++m_zahl.back();
    m_daten.append(char(tObjekt));
    m_offen.append(m_daten.size());
    m_daten.append(4, '\0');
    m_zahl.append(0);
    m_objekt.append(true);
}

void Schreiber::beendeObjekt() {
    if (m_offen.isEmpty()) return;
    const int stelle = m_offen.back();
    const std::uint32_t n = std::uint32_t(m_zahl.back());
    std::memcpy(m_daten.data() + stelle, &n, 4);
    m_offen.removeLast();
    m_zahl.removeLast();
    m_objekt.removeLast();
}

void Schreiber::beginneListe() {
    if (!m_offen.isEmpty() && !m_objekt.back()) ++m_zahl.back();
    m_daten.append(char(tListe));
    m_offen.append(m_daten.size());
    m_daten.append(4, '\0');
    m_zahl.append(0);
    m_objekt.append(false);
}

void Schreiber::beendeListe() { beendeObjekt(); }

void Schreiber::schluessel(Key k) {
    if (m_zahl.isEmpty()) return;
    ++m_zahl.back();
    varint(nummer(QLatin1String(kNamen[k])));
}

void Schreiber::schluessel(const QString& name) {
    if (m_zahl.isEmpty()) return;
    ++m_zahl.back();
    varint(nummer(name));
}

//  Ein Wert in einer Liste zaehlt sich selbst; in einem Objekt hat ihn schon
//  sein Schluessel gezaehlt.
#define MG_ZAEHLE() do { if (!m_offen.isEmpty() && !m_objekt.back()) ++m_zahl.back(); } while (0)

void Schreiber::gib(double v) {
    MG_ZAEHLE();
    m_daten.append(char(tDouble));
    m_daten.append(reinterpret_cast<const char*>(&v), 8);
}

void Schreiber::gib(int v)    { gib(qint64(v)); }

void Schreiber::gib(qint64 v) {
    MG_ZAEHLE();
    m_daten.append(char(tGanz));
    zigzag(v);
}

void Schreiber::gib(bool v) {
    MG_ZAEHLE();
    m_daten.append(char(v ? tWahr : tFalsch));
}

void Schreiber::gib(const QString& v) {
    MG_ZAEHLE();
    m_daten.append(char(tText));
    const QByteArray u = v.toUtf8();
    varint(std::uint32_t(u.size()));
    m_daten.append(u);
}

void Schreiber::gibNull() {
    MG_ZAEHLE();
    m_daten.append(char(tNull));
}

//  ROH, nicht geschichtet - siehe die Messung am Typ 9 oben.
void Schreiber::gibDoubles(const double* p, int n) {
    MG_ZAEHLE();
    m_daten.append(char(tDoubleFeld));
    varint(std::uint32_t(n));
    if (n > 0) m_daten.append(reinterpret_cast<const char*>(p), qsizetype(n) * 8);
}

#undef MG_ZAEHLE

void Schreiber::uebernimm(const Wert& v) {
    if (v.istLeer()) { gibNull(); return; }
    const char* p = v.roh();
    switch (static_cast<std::uint8_t>(*p)) {
        case tNull:   gibNull(); return;
        case tWahr:   gib(true); return;
        case tFalsch: gib(false); return;
        case tDouble: gib(v.toDouble()); return;
        case tGanz:   gib(v.toLongLong()); return;
        case tText:   gib(v.toString()); return;
        case tDoubleFeld:
        case tDoubleFeldGeschichtet:
        case tListe: {
            const Liste l = v.toArray();
            if (l.istDoubleFeld()) {
                QVarLengthArray<double, 64> puffer(l.size());
                for (int i = 0; i < l.size(); ++i) puffer[i] = l.doubleAt(i);
                gibDoubles(puffer.data(), l.size());
                return;
            }
            beginneListe();
            for (int i = 0; i < l.size(); ++i) uebernimm(l.at(i));
            beendeListe();
            return;
        }
        case tObjekt: {
            const Objekt o = v.toObject();
            beginneObjekt();
            for (int i = 0; i < o.count(); ++i) {
                schluessel(o.schluesselBei(i));
                uebernimm(o.wertBei(i));
            }
            beendeObjekt();
            return;
        }
        default: gibNull(); return;
    }
}

void Schreiber::uebernimm(const QJsonValue& v) {
    switch (v.type()) {
        case QJsonValue::Null:   gibNull(); return;
        case QJsonValue::Bool:   gib(v.toBool()); return;
        case QJsonValue::String: gib(v.toString()); return;
        case QJsonValue::Double: {
            const double d = v.toDouble();
            //  Ganze Zahlen als Ganzzahl: sie stehen so kuerzer da und kommen
            //  beim Lesen genauso wieder heraus.
            if (d == double(qint64(d)) && qAbs(d) < 9.0e15) gib(qint64(d));
            else gib(d);
            return;
        }
        case QJsonValue::Array: {
            const QJsonArray a = v.toArray();
            beginneListe();
            for (const QJsonValue& e : a) uebernimm(e);
            beendeListe();
            return;
        }
        case QJsonValue::Object: {
            const QJsonObject o = v.toObject();
            beginneObjekt();
            for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
                schluessel(it.key());
                uebernimm(it.value());
            }
            beendeObjekt();
            return;
        }
        default: gibNull(); return;
    }
}

QByteArray Schreiber::fertig() {
    QByteArray aus;
    aus.reserve(m_daten.size() + 256);
    aus.append(kKennung, 4);
    aus.append(char(kFassung));
    //  Die Schluesseltabelle steht VOR den Werten, damit der Leser sie kennt,
    //  bevor die erste Nummer auftaucht.
    QByteArray kopf;
    std::uint32_t n = std::uint32_t(m_namen.size());
    while (true) {
        const std::uint8_t b = n & 0x7Fu;
        n >>= 7;
        kopf.append(char(n ? (b | 0x80u) : b));
        if (!n) break;
    }
    for (const QByteArray& name : std::as_const(m_namen)) {
        std::uint32_t l = std::uint32_t(name.size());
        while (true) {
            const std::uint8_t b = l & 0x7Fu;
            l >>= 7;
            kopf.append(char(l ? (b | 0x80u) : b));
            if (!l) break;
        }
        kopf.append(name);
    }
    aus.append(kopf);
    aus.append(m_daten);
    return aus;
}

//  ── Lesen ───────────────────────────────────────────────────────────────────

namespace {

//  Laeuft EINEN Wert ab und prueft dabei jede Laenge. Der Rueckgabewert ist die
//  Stelle dahinter, oder nullptr. Weil `lies` den ganzen Baum einmal so abgeht,
//  duerfen die Zugriffe danach ohne weitere Pruefung arbeiten.
const char* pruefeWert(const char* p, const char* ende, int tiefe) {
    if (tiefe > kMaxTiefe || p >= ende) return nullptr;
    const std::uint8_t typ = static_cast<std::uint8_t>(*p++);
    switch (typ) {
        case tNull: case tWahr: case tFalsch:
            return p;
        case tDouble:
            return (ende - p >= 8) ? p + 8 : nullptr;
        case tGanz: {
            qint64 wert = 0;
            return leseZigzag(p, ende, wert) ? p : nullptr;
        }
        case tText: {
            std::uint32_t n = 0;
            if (!leseVarint(p, ende, n)) return nullptr;
            if (n > kMaxTextLaenge || std::size_t(ende - p) < n) return nullptr;
            return p + n;
        }
        case tDoubleFeld:
        case tDoubleFeldGeschichtet: {
            std::uint32_t n = 0;
            if (!leseVarint(p, ende, n)) return nullptr;
            if (n > kMaxEintraege) return nullptr;
            const std::size_t bytes = std::size_t(n) * 8;
            if (std::size_t(ende - p) < bytes) return nullptr;
            return p + bytes;
        }
        case tListe: {
            if (ende - p < 4) return nullptr;
            const std::uint32_t n = leseFest32(p);
            p += 4;
            if (n > kMaxEintraege) return nullptr;
            for (std::uint32_t i = 0; i < n; ++i) {
                p = pruefeWert(p, ende, tiefe + 1);
                if (!p) return nullptr;
            }
            return p;
        }
        case tObjekt: {
            if (ende - p < 4) return nullptr;
            const std::uint32_t n = leseFest32(p);
            p += 4;
            if (n > kMaxEintraege) return nullptr;
            for (std::uint32_t i = 0; i < n; ++i) {
                std::uint32_t nr = 0;
                if (!leseVarint(p, ende, nr)) return nullptr;
                p = pruefeWert(p, ende, tiefe + 1);
                if (!p) return nullptr;
            }
            return p;
        }
        default:
            return nullptr;
    }
}

//  Wie `pruefeWert`, nur ohne Pruefungen - der Baum ist beim Laden abgegangen
//  worden. Wird gebraucht, um in einem Objekt zum naechsten Feld zu kommen.
const char* hinter(const char* p) {
    const std::uint8_t typ = static_cast<std::uint8_t>(*p++);
    switch (typ) {
        case tNull: case tWahr: case tFalsch: return p;
        case tDouble: return p + 8;
        case tGanz: {
            while (static_cast<std::uint8_t>(*p) & 0x80u) ++p;
            return p + 1;
        }
        case tText: {
            std::uint32_t n = 0;
            int s = 0;
            while (true) {
                const std::uint8_t b = static_cast<std::uint8_t>(*p++);
                n |= std::uint32_t(b & 0x7Fu) << (7 * s++);
                if (!(b & 0x80u)) break;
            }
            return p + n;
        }
        case tDoubleFeld:
        case tDoubleFeldGeschichtet: {
            std::uint32_t n = 0;
            int s = 0;
            while (true) {
                const std::uint8_t b = static_cast<std::uint8_t>(*p++);
                n |= std::uint32_t(b & 0x7Fu) << (7 * s++);
                if (!(b & 0x80u)) break;
            }
            return p + std::size_t(n) * 8;
        }
        case tListe: {
            const std::uint32_t n = leseFest32(p);
            p += 4;
            for (std::uint32_t i = 0; i < n; ++i) p = hinter(p);
            return p;
        }
        case tObjekt: {
            const std::uint32_t n = leseFest32(p);
            p += 4;
            for (std::uint32_t i = 0; i < n; ++i) {
                while (static_cast<std::uint8_t>(*p) & 0x80u) ++p;
                ++p;
                p = hinter(p);
            }
            return p;
        }
        default: return p;
    }
}

}  // namespace

bool Doku::lies(const QByteArray& roh) {
    m_ok = false;
    m_namen.clear();
    for (std::uint32_t& n : m_vonKey) n = kKeine;

    if (roh.size() < 6) return false;
    if (std::memcmp(roh.constData(), kKennung, 4) != 0) return false;
    if (static_cast<std::uint8_t>(roh.at(4)) > kFassung) return false;

    //  Die Bytes werden FESTGEHALTEN: jeder Wert ist nur ein Zeiger hinein.
    m_daten = roh;
    const char* p = m_daten.constData() + 5;
    m_ende = m_daten.constData() + m_daten.size();

    std::uint32_t anzahl = 0;
    if (!leseVarint(p, m_ende, anzahl)) return false;
    if (anzahl > 65535u) return false;
    m_namen.reserve(int(anzahl));
    for (std::uint32_t i = 0; i < anzahl; ++i) {
        std::uint32_t laenge = 0;
        if (!leseVarint(p, m_ende, laenge)) return false;
        if (laenge > kMaxTextLaenge || std::size_t(m_ende - p) < laenge) return false;
        m_namen.append(QByteArray(p, int(laenge)));
        p += laenge;
    }
    for (int k = 0; k < k_Anzahl; ++k) {
        const QByteArray gesucht(kNamen[k]);
        for (int i = 0; i < m_namen.size(); ++i)
            if (m_namen.at(i) == gesucht) { m_vonKey[k] = std::uint32_t(i); break; }
    }

    m_wurzel = p;
    const char* hinten = pruefeWert(p, m_ende, 0);
    if (!hinten) return false;

    m_ok = true;
    return true;
}

std::uint32_t Doku::nummerVon(const QString& name) const {
    const QByteArray u = name.toUtf8();
    for (int i = 0; i < m_namen.size(); ++i)
        if (m_namen.at(i) == u) return std::uint32_t(i);
    return kKeine;
}

QString Doku::nameVon(std::uint32_t nummer) const {
    return (nummer < std::uint32_t(m_namen.size()))
               ? QString::fromUtf8(m_namen.at(int(nummer)))
               : QString();
}

Objekt Doku::wurzel() const {
    if (!m_ok || !m_wurzel || static_cast<std::uint8_t>(*m_wurzel) != tObjekt) return {};
    return Objekt(this, m_wurzel + 5, leseFest32(m_wurzel + 1));
}

Objekt::Objekt(const Doku* d, const char* p, std::uint32_t n) : m_d(d) {
    m_felder.reserve(int(n));
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint32_t nr = 0;
        int s = 0;
        while (true) {
            const std::uint8_t b = static_cast<std::uint8_t>(*p++);
            nr |= std::uint32_t(b & 0x7Fu) << (7 * s++);
            if (!(b & 0x80u)) break;
        }
        m_felder.append(Feld{ nr, p });
        p = hinter(p);
    }
}

Wert Objekt::value(Key k) const {
    if (!m_d) return {};
    const std::uint32_t nr = m_d->nummerVon(k);
    if (nr == Doku::kKeine) return {};
    for (const Feld& f : m_felder)
        if (f.nummer == nr) return Wert(m_d, f.wert);
    return {};
}

Wert Objekt::value(const QString& name) const {
    if (!m_d) return {};
    const std::uint32_t nr = m_d->nummerVon(name);
    if (nr == Doku::kKeine) return {};
    for (const Feld& f : m_felder)
        if (f.nummer == nr) return Wert(m_d, f.wert);
    return {};
}

QString Objekt::schluesselBei(int i) const {
    return (m_d && i >= 0 && i < m_felder.size()) ? m_d->nameVon(m_felder.at(i).nummer) : QString();
}

Wert Objekt::wertBei(int i) const {
    return (i >= 0 && i < m_felder.size()) ? Wert(m_d, m_felder.at(i).wert) : Wert();
}

bool Wert::isObject() const { return m_p && static_cast<std::uint8_t>(*m_p) == tObjekt; }
bool Wert::isString() const { return m_p && static_cast<std::uint8_t>(*m_p) == tText; }

namespace {

//  Zigzag-Varint in voller Breite; der Baum ist beim Laden abgegangen worden,
//  hier wird deshalb nicht mehr gegen das Ende geprueft.
qint64 leseGanz(const char* p) {
    std::uint64_t w = 0;
    for (int s = 0; s < 10; ++s) {
        const std::uint8_t b = static_cast<std::uint8_t>(*p++);
        w |= std::uint64_t(b & 0x7Fu) << (7 * s);
        if (!(b & 0x80u)) break;
    }
    return static_cast<qint64>((w >> 1) ^ (~(w & 1) + 1));
}

}  // namespace

qint64 Wert::toLongLong(qint64 def) const {
    if (!m_p) return m_direkt ? qint64(m_zahl) : def;
    const std::uint8_t typ = static_cast<std::uint8_t>(*m_p);
    if (typ == tGanz) return leseGanz(m_p + 1);
    if (typ == tDouble) {
        double d = 0;
        std::memcpy(&d, m_p + 1, 8);
        return qint64(d);
    }
    return def;
}

int Wert::toInt(int def) const {
    if (!m_p) return m_direkt ? int(m_zahl) : def;
    const std::uint8_t typ = static_cast<std::uint8_t>(*m_p);
    if (typ != tGanz && typ != tDouble) return def;
    const qint64 v = toLongLong(def);
    //  Was nicht hineinpasst, wird NICHT abgeschnitten - ein umgeschlagener
    //  Wert saehe gueltig aus und waere still falsch.
    if (v < qint64(std::numeric_limits<int>::min())
        || v > qint64(std::numeric_limits<int>::max()))
        return def;
    return int(v);
}

double Wert::toDouble(double def) const {
    if (!m_p) return m_direkt ? m_zahl : def;
    const std::uint8_t typ = static_cast<std::uint8_t>(*m_p);
    if (typ == tDouble) {
        double d = 0;
        std::memcpy(&d, m_p + 1, 8);
        return d;
    }
    if (typ == tGanz) return double(leseGanz(m_p + 1));
    return def;
}

bool Wert::toBool(bool def) const {
    if (!m_p) return def;
    const std::uint8_t typ = static_cast<std::uint8_t>(*m_p);
    if (typ == tWahr)   return true;
    if (typ == tFalsch) return false;
    return def;
}

QString Wert::toString(const QString& def) const {
    if (!m_p) return def;
    const char* p = m_p;
    if (static_cast<std::uint8_t>(*p++) != tText) return def;
    std::uint32_t n = 0;
    int s = 0;
    while (true) {
        const std::uint8_t b = static_cast<std::uint8_t>(*p++);
        n |= std::uint32_t(b & 0x7Fu) << (7 * s++);
        if (!(b & 0x80u)) break;
    }
    return QString::fromUtf8(p, qsizetype(n));
}

Liste Wert::toArray() const {
    if (!m_p) return {};
    const char* p = m_p;
    const std::uint8_t typ = static_cast<std::uint8_t>(*p++);
    if (typ == tListe) {
        const std::uint32_t n = leseFest32(p);
        return Liste(m_d, p + 4, n, false);
    }
    if (typ == tDoubleFeld || typ == tDoubleFeldGeschichtet) {
        std::uint32_t n = 0;
        int s = 0;
        while (true) {
            const std::uint8_t b = static_cast<std::uint8_t>(*p++);
            n |= std::uint32_t(b & 0x7Fu) << (7 * s++);
            if (!(b & 0x80u)) break;
        }
        return Liste(m_d, p, n, true, typ == tDoubleFeldGeschichtet);
    }
    return {};
}

Objekt Wert::toObject() const {
    if (!m_p || static_cast<std::uint8_t>(*m_p) != tObjekt) return {};
    return Objekt(m_d, m_p + 5, leseFest32(m_p + 1));
}

Wert Wert::ausDouble(double d) {
    Wert w;
    w.m_zahl = d;
    w.m_direkt = true;
    return w;
}

int Liste::inDoubles(double* ziel, int platz) const {
    if (!m_doubles || !ziel) return 0;
    //  Geschichtet wird nur der GANZE Block entschichtet - ein Teilstueck
    //  liesse sich nicht aus den Spalten zusammensetzen.
    if (m_geschichtet) {
        if (platz < int(m_n)) return 0;
        entschichte(m_anfang, ziel, std::size_t(m_n));
        return int(m_n);
    }
    const int n = std::min(platz, int(m_n));
    if (n > 0) std::memcpy(ziel, m_anfang, std::size_t(n) * 8);
    return n;
}

double Liste::doubleAt(int i) const {
    if (i < 0 || i >= int(m_n)) return 0.0;
    if (m_geschichtet) {
        double d = 0;
        char* z = reinterpret_cast<char*>(&d);
        for (std::size_t b = 0; b < 8; ++b)
            z[b] = m_anfang[b * std::size_t(m_n) + std::size_t(i)];
        return d;
    }
    if (m_doubles) {
        double d = 0;
        std::memcpy(&d, m_anfang + qsizetype(i) * 8, 8);
        return d;
    }
    return at(i).toDouble(0.0);
}

Wert Liste::at(int i) const {
    if (i < 0 || i >= int(m_n)) return {};
    if (m_doubles) return Wert::ausDouble(doubleAt(i));
    const char* p = m_anfang;
    int von = 0;
    if (m_letzterZeiger && i >= m_letzterIndex) { p = m_letzterZeiger; von = m_letzterIndex; }
    for (int k = von; k < i; ++k) p = hinter(p);
    m_letzterIndex = i;
    m_letzterZeiger = p;
    return Wert(m_d, p);
}

//  ── Ansehen ─────────────────────────────────────────────────────────────────

namespace {

void zeige(const Doku& d, const Wert& v, QString& aus, int tiefe);

void zeigeObjekt(const Doku& d, const Objekt& o, QString& aus, int tiefe) {
    const QString rand(tiefe * 2, QLatin1Char(' '));
    for (int i = 0; i < o.count(); ++i) {
        aus += rand + o.schluesselBei(i) + QLatin1String(": ");
        zeige(d, o.wertBei(i), aus, tiefe + 1);
    }
}

void zeige(const Doku& d, const Wert& v, QString& aus, int tiefe) {
    if (v.istLeer()) { aus += QLatin1String("-\n"); return; }
    const std::uint8_t typ = static_cast<std::uint8_t>(*v.roh());
    switch (typ) {
        case tNull:   aus += QLatin1String("null\n"); return;
        case tWahr:   aus += QLatin1String("ja\n"); return;
        case tFalsch: aus += QLatin1String("nein\n"); return;
        case tGanz:   aus += QString::number(v.toInt()) + QLatin1Char('\n'); return;
        case tDouble: aus += QString::number(v.toDouble(), 'g', 17) + QLatin1Char('\n'); return;
        case tText:   aus += QLatin1Char('"') + v.toString() + QLatin1String("\"\n"); return;
        case tDoubleFeld:
        case tDoubleFeldGeschichtet: {
            const Liste l = v.toArray();
            aus += QStringLiteral("[%1 Zahlen]").arg(l.size());
            for (int i = 0; i < l.size() && i < 8; ++i)
                aus += QLatin1Char(' ') + QString::number(l.doubleAt(i), 'g', 6);
            if (l.size() > 8) aus += QLatin1String(" …");
            aus += QLatin1Char('\n');
            return;
        }
        case tListe: {
            const Liste l = v.toArray();
            aus += QStringLiteral("[%1]\n").arg(l.size());
            const QString rand((tiefe + 1) * 2, QLatin1Char(' '));
            for (int i = 0; i < l.size(); ++i) {
                aus += rand + QStringLiteral("- ");
                zeige(d, l.at(i), aus, tiefe + 2);
            }
            return;
        }
        case tObjekt: {
            aus += QLatin1Char('\n');
            zeigeObjekt(d, v.toObject(), aus, tiefe + 1);
            return;
        }
        default: aus += QLatin1String("?\n"); return;
    }
}

}  // namespace

QString alsText(const QByteArray& roh) {
    Doku d;
    if (!d.lies(roh)) return QString();
    QString aus;
    aus.reserve(roh.size() * 2);
    zeigeObjekt(d, d.wurzel(), aus, 0);
    return aus;
}

}  // namespace mg::mgeb
