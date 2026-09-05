#pragma once
// Die MARKE eines Rückgängig-Schrittes der Tag-Seitenleiste: statt eines Satzes eine kurze Syntax (`T:a`,
// `U(K:b):a`, `+3 T:a`), Buchstaben je nach Sprache, Farben nach VERB statt nach Gegenstand. Eine LISTE VON
// STÜCKEN, kein String - nur so gehen "alte Farbe -> neue Farbe" und gekürzte Pfade. QML: text/color/italic/full.
#include <QColor>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "tags/TagCategory.h"

namespace mg::tagmark {

enum class Thing { Tag, Category, Subcategory };

enum class Verb { Create, Delete, Convert, Move, Rename, Recolor, Add, Remove };

// Die vier Verb-Farben, bewusst FEST und nicht über den Theme-Editor einstellbar: sonst wäre Rot irgendwann grün
// und die Marke unlesbar. Ein gekürztes Stück bekommt keine eigene Farbe - das Kursive ist das Zeichen dafür.
QColor verbColor(Verb v);

struct Piece {
    QString text;
    QColor  color;          // ungueltig = Vorgabefarbe der Leiste
    bool    italic = false; // gekuerzt (Name oder Pfad)
    QString full;           // Hover-Text dieses Stueckes ("" = keiner)
};

// Der Name, gekürzt auf die Anfangsbuchstaben seiner Wörter ("Sommerurlaub 2024 Kroatien" -> "S2K"). Ein
// einzelnes langes Wort wird hinten beschnitten; kurze Namen kommen unverändert zurück.
QString shortName(const QString& name);

QStringList pathOf(const QList<TagCategory>& tree, const QString& id);

//  Die Marke EINES Dinges, z. B. `U(+1(K:c)):a`. `path` = Namen der Vorfahren
//  von der Wurzel her (ohne das Ding selbst).
QString expr(Thing t, const QString& name, const QStringList& path, bool shorten);

bool isShortened(Thing t, const QString& name, const QStringList& path);

QString fullText(Thing t, const QString& name, const QStringList& path);

// Eine Marke hat ZWEI Richtungen: die Leiste schreibt links, was ZURÜCK bewirken würde, und rechts, was VOR
// bewirken würde - derselbe Vorgang, nur andersherum gelesen.
struct Mark {
    QVariantList forward;        // was der Vorgang tat  = was VOR taete
    QVariantList backward;       // seine Umkehrung      = was ZURUECK taete
    QString      iconForward;
    QString      iconBackward;
};

//  Das Gegenverb: erstellen <-> loeschen, zuordnen <-> entfernen. Umwandeln,
//  Verschieben, Umbenennen und Umfaerben sind ihre eigene Umkehrung - dort
//  drehen sich nur die beiden Seiten des Pfeils.
Verb inverseVerb(Verb v);

QVariantList simple(Verb v, Thing t, const QString& name, const QStringList& path);
QVariantList transition(Verb v,
                        Thing fromT, const QString& fromName, const QStringList& fromPath,
                        Thing toT,   const QString& toName,   const QStringList& toPath);
QVariantList recolor(Thing t, const QString& name, const QStringList& path,
                     const QColor& before, const QColor& after);
QVariantList counted(int added, int removed,
                     Thing t, const QString& name, const QStringList& path);

Mark mkSimple(Verb v, Thing t, const QString& name, const QStringList& path);
Mark mkTransition(Verb v,
                  Thing fromT, const QString& fromName, const QStringList& fromPath,
                  Thing toT,   const QString& toName,   const QStringList& toPath);
Mark mkRecolor(Thing t, const QString& name, const QStringList& path,
               const QColor& before, const QColor& after);
Mark mkCounted(int added, int removed,
               Thing t, const QString& name, const QStringList& path);

QString plain(const QVariantList& pieces);

QString iconFor(Verb v);

}  // namespace mg::tagmark
