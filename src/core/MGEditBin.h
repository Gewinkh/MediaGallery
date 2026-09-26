#pragma once
//  MGEditBin - der Inhalt einer Beidatei als Bytes statt als JSON-Text.
//  Aufbau: "MGEB" · Fassung · Schluesseltabelle · Wurzelwert.
//  `Objekt`/`Wert` tragen dieselben Rufe wie QJsonObject/QJsonValue - dadurch
//  liest jeder Codec aus beiden Quellen mit demselben Code (s. `JsonObjekt`).
#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVarLengthArray>
#include <QVector>

#include <cstdint>

namespace mg::mgeb {

inline constexpr char kKennung[4] = { 'M', 'G', 'E', 'B' };
inline constexpr std::uint8_t kFassung = 1;

//  Deckel gegen praeparierte Dateien.
inline constexpr std::uint32_t kMaxEintraege  = 20'000'000;
inline constexpr std::uint32_t kMaxTextLaenge = 1u << 24;
inline constexpr int           kMaxTiefe      = 64;

//  Feste Feldnamen; dynamische (Spaltennummern) laufen ueber die
//  Zeichenketten-Ueberladung.
enum Key : std::uint16_t {
    k_format, k_version, k_recording,
    k_boxes, k_chains, k_growBase, k_pageplan, k_textops, k_formvals, k_anns,
    k_page, k_tr, k_kind, k_x, k_y, k_w, k_h, k_pts,
    k_srcobj, k_mstyle, k_img, k_hilite, k_orig,
    k_stroke, k_lw, k_fill,
    k_text, k_font, k_size, k_bold, k_italic, k_under, k_color,
    k_align, k_valign, k_anchor,
    k_src, k_key, k_doc, k_rot, k_at, k_del,
    k_spaltenbreiten, k_spaltenformate,
    k_Anzahl
};

const char* keyName(Key k);

//  ── Schreiben ───────────────────────────────────────────────────────────────

class Wert;

//  Ein Strom, kein Baum: die Werte stehen in der Reihenfolge, in der sie kommen.
class Schreiber {
public:
    Schreiber();

    void beginneObjekt();
    void beendeObjekt();
    void beginneListe();
    void beendeListe();

    void schluessel(Key k);
    void schluessel(const QString& name);

    void gib(double v);
    void gib(int v);
    void gib(qint64 v);
    void gib(bool v);
    void gib(const QString& v);
    void gibNull();
    //  Punktlisten am Stueck: ein Laengenfeld, dann die rohen Doubles.
    void gibDoubles(const double* p, int n);

    //  Einen fremden Abschnitt uebernehmen - der Schreiber einer Tabelle darf
    //  die Notizen der Editoren nicht verlieren; die zweite Fassung holt ihn
    //  aus einer alten Beidatei.
    void uebernimm(const Wert& v);
    void uebernimm(const QJsonValue& v);

    template <class S, class V>
    void feld(S s, V v) { schluessel(s); gib(v); }

    QByteArray fertig();

private:
    void varint(std::uint32_t w);
    void zigzag(qint64 v);
    std::uint32_t nummer(const QString& name);

    QByteArray                m_daten;
    QVector<QByteArray>       m_namen;     // Schluesseltabelle in Reihenfolge
    QHash<QString, quint32>   m_nummern;
    QVarLengthArray<int, 16>  m_offen;     // Stelle des Zaehlers je offener Ebene
    QVarLengthArray<int, 16>  m_zahl;
    QVarLengthArray<bool, 16> m_objekt;    // zaehlt diese Ebene Schluessel oder Werte?
};

//  ── Lesen ───────────────────────────────────────────────────────────────────

class Objekt;
class Liste;
class Doku;

class Wert {
public:
    Wert() = default;
    Wert(const Doku* d, const char* p) : m_d(d), m_p(p) {}

    bool istLeer() const { return m_p == nullptr; }
    bool isUndefined() const { return m_p == nullptr; }

    int     toInt(int def = 0) const;
    //  Die volle Breite. `toInt` gibt `def` zurueck, wenn der Wert nicht in ein
    //  int passt - abschneiden waere ein stiller Datenverlust.
    qint64  toLongLong(qint64 def = 0) const;
    double  toDouble(double def = 0.0) const;
    bool    toBool(bool def = false) const;
    QString toString(const QString& def = QString()) const;
    Liste   toArray() const;
    Objekt  toObject() const;

    //  Gleiche Namen wie bei QJsonValue - die Codecs fragen beide gleich.
    bool isObject() const;
    bool isString() const;

    const char* roh() const { return m_p; }
    const Doku* doku() const { return m_d; }

    //  Ein Element eines Doublefeldes hat kein eigenes Typbyte in der Datei.
    static Wert ausDouble(double d);

private:
    const Doku* m_d = nullptr;
    const char* m_p = nullptr;
    double      m_zahl = 0.0;
    bool        m_direkt = false;
};

class Liste {
public:
    Liste() = default;
    Liste(const Doku* d, const char* p, std::uint32_t n, bool doubles, bool geschichtet = false)
        : m_d(d), m_anfang(p), m_n(n), m_doubles(doubles), m_geschichtet(geschichtet) {}

    int  size() const { return int(m_n); }
    bool isEmpty() const { return m_n == 0; }
    //  Fortlaufender Zugriff bleibt linear; ein Sprung zurueck laeuft von vorn.
    Wert at(int i) const;
    double doubleAt(int i) const;
    bool istDoubleFeld() const { return m_doubles; }
    //  Ein Punktfeld am Stueck. Bei der geschichteten Ablage muss `platz` fuer
    //  ALLE Werte reichen - ein Teilstueck laesst sich nicht entschichten.
    int inDoubles(double* ziel, int platz) const;

private:
    const Doku*         m_d = nullptr;
    const char*         m_anfang = nullptr;
    std::uint32_t       m_n = 0;
    bool                m_doubles = false;
    bool                m_geschichtet = false;
    mutable int         m_letzterIndex = -1;
    mutable const char* m_letzterZeiger = nullptr;
};

class Objekt {
public:
    Objekt() = default;
    Objekt(const Doku* d, const char* p, std::uint32_t n);

    bool isEmpty() const { return m_felder.isEmpty(); }
    int  count() const { return m_felder.size(); }

    Wert value(Key k) const;
    Wert value(const QString& name) const;
    bool contains(Key k) const { return !value(k).istLeer(); }

    QString  schluesselBei(int i) const;
    Wert     wertBei(int i) const;

private:
    struct Feld { std::uint32_t nummer; const char* wert; };
    const Doku*                m_d = nullptr;
    QVarLengthArray<Feld, 32>  m_felder;
};

//  Haelt die Bytes UND die Schluesseltabelle; jeder `Wert` zeigt in diese Bytes.
class Doku {
public:
    bool lies(const QByteArray& roh);
    bool ok() const { return m_ok; }
    Objekt wurzel() const;

    const char* ende() const { return m_ende; }
    //  Nummer eines festen Schluessels in DIESER Datei, oder kKeine.
    static constexpr std::uint32_t kKeine = 0xFFFFFFFFu;
    std::uint32_t nummerVon(Key k) const { return m_vonKey[k]; }
    std::uint32_t nummerVon(const QString& name) const;
    QString nameVon(std::uint32_t nummer) const;

private:
    QByteArray            m_daten;
    const char*           m_ende = nullptr;
    QVector<QByteArray>   m_namen;
    std::uint32_t         m_vonKey[k_Anzahl] = {};
    const char*           m_wurzel = nullptr;
    bool                  m_ok = false;
};

//  Damit die Codecs nur EINMAL dastehen: QJsonObject in derselben Form.
class JsonObjekt {
public:
    explicit JsonObjekt(QJsonObject o) : m_o(std::move(o)) {}
    QJsonValue value(Key k) const { return m_o.value(QLatin1String(keyName(k))); }
    QJsonValue value(const QString& name) const { return m_o.value(name); }
    bool contains(Key k) const { return m_o.contains(QLatin1String(keyName(k))); }
    bool isEmpty() const { return m_o.isEmpty(); }
    //  `keys()` je Ruf ist teuer, faellt aber nur auf dem Alt-Weg an.
    int count() const { return int(m_o.size()); }
    QString schluesselBei(int i) const { return m_o.keys().value(i); }
    QJsonValue wertBei(int i) const { return m_o.value(schluesselBei(i)); }

private:
    QJsonObject m_o;
};

//  Aus einem Listenelement wird in beiden Faellen etwas mit `value(Key)`.
inline JsonObjekt alsObjekt(const QJsonValue& v) { return JsonObjekt(v.toObject()); }
inline Objekt     alsObjekt(const Wert& v)       { return v.toObject(); }

//  Punkte am Stueck, wo die Quelle es hergibt; der Rest wird einzeln geholt.
template <class Ziel>
int holePunkte(const Liste& l, Ziel& ziel) {
    if (!l.istDoubleFeld() || ziel.isEmpty()) return 0;
    return l.inDoubles(&ziel[0].rx(), ziel.size() * 2);
}
template <class Liste2, class Ziel>
int holePunkte(const Liste2&, Ziel&) { return 0; }

//  Der Inhalt lesbar, zum Ansehen in der App - nie zum Einlesen.
QString alsText(const QByteArray& roh);

}  // namespace mg::mgeb
