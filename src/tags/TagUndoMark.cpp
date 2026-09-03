#include "tags/TagUndoMark.h"

#include "core/Strings.h"

#include <QRegularExpression>
#include <QVariantMap>

#include <functional>

namespace mg::tagmark {
namespace {

//  Ab hier wird ein Name gekuerzt. 14 Zeichen sind die Grenze, ab der eine
//  Marke mit Pfad in der rund 280 px breiten Seitenleiste nicht mehr aufgeht
//  (11 px Schrift, gemessen an `bench_tagpanel`).
constexpr int kMaxName = 14;

QString letter(Thing t) {
    switch (t) {
    case Thing::Tag:         return Strings::get(StringKey::TagMarkTag);
    case Thing::Category:    return Strings::get(StringKey::TagMarkCat);
    case Thing::Subcategory: return Strings::get(StringKey::TagMarkSub);
    }
    return {};
}

//  Der Klammerausdruck vor dem Doppelpunkt: wo das Ding sitzt.
//  Kein Pfad -> leer. Eine Ebene -> `K:wurzel`.
//
//  Ab zwei Ebenen gehen die beiden Fassungen auseinander:
//   • GEKUERZT (`shorten`): `+n(K:wurzel)` - gezeigt werden erste und letzte
//     Ebene, die dazwischen werden gezaehlt (Festlegung des Nutzers).
//   • VOLL: die ganze Verschachtelung, `U(U(K:c):b):a`. Der Hover soll die
//     KOMPLETTE Notation zeigen, nicht noch einmal die gekuerzte samt einer
//     zerlegten Kette daneben (Nutzerbefund 2026-09-03).
QString parentExpr(const QStringList& path, bool shorten) {
    if (path.isEmpty()) return {};
    if (shorten) {
        const QString rootExpr = letter(Thing::Category) + QLatin1Char(':')
                               + shortName(path.first());
        if (path.size() == 1) return rootExpr;
        return QStringLiteral("+%1(%2)").arg(path.size() - 1).arg(rootExpr);
    }
    //  Von der Wurzel nach innen aufbauen: K:a -> U(K:a):b -> U(U(K:a):b):c
    QString out = letter(Thing::Category) + QLatin1Char(':') + path.first();
    for (int i = 1; i < path.size(); ++i)
        out = letter(Thing::Subcategory) + QLatin1Char('(') + out
            + QStringLiteral("):") + path.at(i);
    return out;
}

QVariantMap piece(const QString& text, const QColor& c = QColor(),
                  bool italic = false, const QString& full = QString()) {
    QVariantMap m;
    m.insert(QStringLiteral("text"),   text);
    m.insert(QStringLiteral("color"),  c.isValid() ? QVariant(c) : QVariant());
    m.insert(QStringLiteral("italic"), italic);
    m.insert(QStringLiteral("full"),   full);
    return m;
}

//  Ein Ding als EIN Stueck - mit Farbe, Kursivstellung und Hover-Text.
QVariantMap thingPiece(Verb v, Thing t, const QString& name, const QStringList& path) {
    const bool kurz = isShortened(t, name, path);
    return piece(expr(t, name, path, /*shorten=*/true), verbColor(v), kurz,
                 kurz ? fullText(t, name, path) : QString());
}

}  // namespace

QColor verbColor(Verb v) {
    switch (v) {
    case Verb::Create:  case Verb::Add:     return QColor(0x3F, 0xC3, 0x6B);  // gruen
    case Verb::Delete:  case Verb::Remove:  return QColor(0xE5, 0x5A, 0x5A);  // rot
    case Verb::Convert: case Verb::Move:    return QColor(0x4E, 0x9B, 0xF5);  // blau
    case Verb::Rename:  case Verb::Recolor: return QColor(0xE2, 0xB3, 0x3C);  // gelb
    }
    return {};
}

QString shortName(const QString& name) {
    const QString n = name.trimmed();
    if (n.size() <= kMaxName) return n;

    //  Mehrere Woerter -> Anfangsbuchstaben, Zahlen zaehlen wie Woerter
    //  („Sommerurlaub 2024 Kroatien" -> „S2K").
    const QStringList woerter = n.split(QRegularExpression(QStringLiteral("[\\s_\\-/]+")),
                                        Qt::SkipEmptyParts);
    if (woerter.size() >= 2) {
        QString out;
        for (const QString& w : woerter) out.append(w.at(0));
        return out;
    }
    //  Ein einziges langes Wort laesst sich nicht sinnvoll abkuerzen - dann
    //  wird hinten beschnitten.
    return n.left(kMaxName - 1) + QStringLiteral("…");
}

QStringList pathOf(const QList<TagCategory>& tree, const QString& id) {
    QStringList out;
    std::function<bool(const QList<TagCategory>&, QStringList&)> walk =
        [&](const QList<TagCategory>& list, QStringList& acc) {
            for (const TagCategory& c : list) {
                if (c.id == id) return true;
                acc.append(c.name);
                if (walk(c.children, acc)) return true;
                acc.removeLast();
            }
            return false;
        };
    walk(tree, out);
    return out;
}

QString expr(Thing t, const QString& name, const QStringList& path, bool shorten) {
    const QString n = shorten ? shortName(name) : name;
    const QString p = parentExpr(path, shorten);
    return p.isEmpty() ? letter(t) + QLatin1Char(':') + n
                       : letter(t) + QLatin1Char('(') + p + QStringLiteral("):") + n;
}

bool isShortened(Thing t, const QString& name, const QStringList& path) {
    Q_UNUSED(t);
    if (shortName(name) != name) return true;
    if (path.size() > 1) return true;                       // Ebenen versimpelt
    if (!path.isEmpty() && shortName(path.first()) != path.first()) return true;
    return false;
}

//  Der Hover zeigt die KOMPLETTE Notation - ungekuerzte Namen UND die ganze
//  Verschachtelung. Nichts daneben: die Notation traegt bereits alles.
QString fullText(Thing t, const QString& name, const QStringList& path) {
    return expr(t, name, path, /*shorten=*/false);
}

QVariantList simple(Verb v, Thing t, const QString& name, const QStringList& path) {
    return { thingPiece(v, t, name, path) };
}

QVariantList transition(Verb v,
                        Thing fromT, const QString& fromName, const QStringList& fromPath,
                        Thing toT,   const QString& toName,   const QStringList& toPath) {
    return { thingPiece(v, fromT, fromName, fromPath),
             piece(QStringLiteral(" -> "), verbColor(v)),
             thingPiece(v, toT, toName, toPath) };
}

QVariantList recolor(Thing t, const QString& name, const QStringList& path,
                     const QColor& before, const QColor& after) {
    const bool kurz = isShortened(t, name, path);
    const QString e = expr(t, name, path, /*shorten=*/true);
    const QString f = kurz ? fullText(t, name, path) : QString();
    return { piece(e, before.isValid() ? before : verbColor(Verb::Recolor), kurz, f),
             piece(QStringLiteral(" -> "), verbColor(Verb::Recolor)),
             piece(e, after.isValid() ? after : verbColor(Verb::Recolor), kurz, f) };
}

QVariantList counted(int added, int removed,
                     Thing t, const QString& name, const QStringList& path) {
    QVariantList out;
    if (added > 0)
        out.append(piece(QStringLiteral("+%1 ").arg(added), verbColor(Verb::Add)));
    if (removed > 0)
        out.append(piece(QStringLiteral("-%1 ").arg(removed), verbColor(Verb::Remove)));
    //  Ohne Namen bleibt es beim reinen Zaehler - so, wenn EIN Schritt mehrere
    //  Gegenstaende betraf (`+5` ist dann ehrlicher als `+5 T:x`).
    if (name.isEmpty()) {
        if (out.isEmpty())
            out.append(piece(QStringLiteral("+0"), verbColor(Verb::Add)));
        return out;
    }
    //  Das Ding selbst traegt die Farbe der ueberwiegenden Richtung; sind beide
    //  im Spiel, gewinnt das Hinzufuegen (es ist der haeufigere Fall und die
    //  freundlichere Vorgabe).
    out.append(thingPiece(added >= removed ? Verb::Add : Verb::Remove, t, name, path));
    return out;
}

QString plain(const QVariantList& pieces) {
    QString out;
    for (const QVariant& v : pieces) {
        const QVariantMap m = v.toMap();
        const QString full = m.value(QStringLiteral("full")).toString();
        out += full.isEmpty() ? m.value(QStringLiteral("text")).toString() : full;
    }
    return out;
}

Verb inverseVerb(Verb v) {
    switch (v) {
    case Verb::Create:  return Verb::Delete;
    case Verb::Delete:  return Verb::Create;
    case Verb::Add:     return Verb::Remove;
    case Verb::Remove:  return Verb::Add;
    //  Diese vier kehren sich nicht im VERB um, sondern in ihren Seiten.
    case Verb::Convert: case Verb::Move:
    case Verb::Rename:  case Verb::Recolor: return v;
    }
    return v;
}

Mark mkSimple(Verb v, Thing t, const QString& name, const QStringList& path) {
    const Verb inv = inverseVerb(v);
    return { simple(v,   t, name, path), simple(inv, t, name, path),
             iconFor(v), iconFor(inv) };
}

Mark mkTransition(Verb v,
                  Thing fromT, const QString& fromName, const QStringList& fromPath,
                  Thing toT,   const QString& toName,   const QStringList& toPath) {
    return { transition(v, fromT, fromName, fromPath, toT,   toName,   toPath),
             transition(v, toT,   toName,   toPath,   fromT, fromName, fromPath),
             iconFor(v), iconFor(v) };
}

Mark mkRecolor(Thing t, const QString& name, const QStringList& path,
               const QColor& before, const QColor& after) {
    return { recolor(t, name, path, before, after),
             recolor(t, name, path, after,  before), {}, {} };
}

Mark mkCounted(int added, int removed,
               Thing t, const QString& name, const QStringList& path) {
    return { counted(added, removed, t, name, path),
             counted(removed, added, t, name, path), {}, {} };
}

QString iconFor(Verb v) {
    return (v == Verb::Delete) ? QStringLiteral("trash") : QString();
}

}  // namespace mg::tagmark
