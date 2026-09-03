#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  TagUndoMark - die MARKE eines Rueckgaengig-Schrittes der Tag-Seitenleiste.
//
//  Statt eines Satzes („Tag geloescht: Urlaub") traegt die Leiste eine kurze
//  Syntax, die auf einen Blick sagt, WAS passiert ist (Festlegung des Nutzers
//  2026-09-03):
//
//      T:a               Tag a
//      K:a               Kategorie a (Hauptebene)
//      U(K:b):a          Unterkategorie a von Kategorie b
//      T(K:b):a          Tag a in Kategorie b
//      T(+2(K:d)):a      Tag a, zwei Ebenen tief unter Kategorie d
//      T:a -> K:a        umgewandelt
//      +3 T:a  /  -3 T:a Dateien zugeordnet bzw. entfernt
//
//  Die Buchstaben folgen der SPRACHE (deutsch T/K/U, englisch T/C/S).
//
//  FARBEN nach Verb, nicht nach Gegenstand:
//      gruen  = erstellt / zugeordnet      rot   = geloescht / entfernt
//      blau   = umgewandelt / verschoben   gelb  = umbenannt / umgefaerbt
//
//  Die Marke ist eine LISTE VON STUECKEN, kein String: jedes Stueck hat seine
//  eigene Farbe, kann kursiv sein (= gekuerzt) und einen eigenen Hover-Text
//  tragen (= der volle Name bzw. der volle Pfad). Nur so lassen sich „alte
//  Farbe -> neue Farbe" und die gekuerzten Pfade ueberhaupt darstellen.
//  QML bekommt sie als `QVariantList` von Abbildungen mit den Schluesseln
//  `text` · `color` · `italic` · `full`.
// ─────────────────────────────────────────────────────────────────────────────
#include <QColor>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "tags/TagCategory.h"

namespace mg::tagmark {

//  Was fuer ein Ding die Marke bezeichnet.
enum class Thing { Tag, Category, Subcategory };

//  Das Verb - es entscheidet ueber die Farbe.
enum class Verb { Create, Delete, Convert, Move, Rename, Recolor, Add, Remove };

//  Die vier Verb-Farben. Bewusst FEST und nicht ueber den Theme-Editor
//  einstellbar: sonst waere Rot irgendwann gruen und die Marke unlesbar. Die
//  Toene sind so gewaehlt, dass sie auf hellem UND dunklem Grund tragen.
//  Ein GEKUERZTES Stueck bekommt KEINE eigene Farbe: es traegt ohnehin die
//  Verbfarbe und hebt sich damit schon vom uebrigen Text der Leiste ab; das
//  Kursive ist das Zeichen „hier steht mehr, wenn du darauf zeigst".
QColor verbColor(Verb v);

//  Ein Stueck der Marke.
struct Piece {
    QString text;
    QColor  color;          // ungueltig = Vorgabefarbe der Leiste
    bool    italic = false; // gekuerzt (Name oder Pfad)
    QString full;           // Hover-Text dieses Stueckes ("" = keiner)
};

//  ── Bausteine ────────────────────────────────────────────────────────────
//  Der Name, gekuerzt auf die Anfangsbuchstaben seiner Woerter
//  („Sommerurlaub 2024 Kroatien" -> „S2K"). Ein einzelnes langes Wort wird
//  hinten beschnitten. Gibt den Namen unveraendert zurueck, wenn er kurz genug
//  ist.
QString shortName(const QString& name);

//  Der Pfad eines Knotens, von der Wurzel her. Leer = Hauptebene.
QStringList pathOf(const QList<TagCategory>& tree, const QString& id);

//  Die Marke EINES Dinges, z. B. `U(+1(K:c)):a`. `path` = Namen der Vorfahren
//  von der Wurzel her (ohne das Ding selbst).
QString expr(Thing t, const QString& name, const QStringList& path, bool shorten);

//  Ist die Marke gekuerzt (Name oder Pfad)? Dann gehoert sie kursiv.
bool isShortened(Thing t, const QString& name, const QStringList& path);

//  Der volle Text zum Hover: Marke ungekuerzt, dazu der Pfad ausgeschrieben.
QString fullText(Thing t, const QString& name, const QStringList& path);

//  ── Eine Marke hat ZWEI Richtungen ───────────────────────────────────────
//  Die Leiste schreibt links, was ZURUECK bewirken wuerde, und rechts, was VOR
//  bewirken wuerde. Beides ist derselbe Vorgang, nur andersherum gelesen -
//  und genau das war vorher die Verwirrung: es stand die vergangene Tat da,
//  waehrend der Knopf daneben das Gegenteil tat (Festlegung des Nutzers
//  2026-09-03).
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

//  ── Fertige Marken ───────────────────────────────────────────────────────
//  Ein Ding, ein Verb: erstellt, geloescht, umbenannt …
QVariantList simple(Verb v, Thing t, const QString& name, const QStringList& path);
//  Von A nach B (umgewandelt, verschoben, umbenannt).
QVariantList transition(Verb v,
                        Thing fromT, const QString& fromName, const QStringList& fromPath,
                        Thing toT,   const QString& toName,   const QStringList& toPath);
//  Farbwechsel: dasselbe Ding zweimal, links in der alten, rechts in der neuen
//  Farbe.
QVariantList recolor(Thing t, const QString& name, const QStringList& path,
                     const QColor& before, const QColor& after);
//  Zuordnungen: `+n` gruen und/oder `-m` rot vor dem Ding.
QVariantList counted(int added, int removed,
                     Thing t, const QString& name, const QStringList& path);

//  Dieselben vier Bauformen, aber in BEIDEN Richtungen.
Mark mkSimple(Verb v, Thing t, const QString& name, const QStringList& path);
Mark mkTransition(Verb v,
                  Thing fromT, const QString& fromName, const QStringList& fromPath,
                  Thing toT,   const QString& toName,   const QStringList& toPath);
Mark mkRecolor(Thing t, const QString& name, const QStringList& path,
               const QColor& before, const QColor& after);
Mark mkCounted(int added, int removed,
               Thing t, const QString& name, const QStringList& path);

//  Der Text, den die ganze Leiste beim Hover zeigt (und den die Statuszeile
//  nach einem Rueckgaengig nennt): alle Stuecke ungekuerzt hintereinander.
QString plain(const QVariantList& pieces);

//  Gezeichnetes Symbol vor der Marke ("trash" beim Loeschen, sonst leer) -
//  Regel 28: gezeichnet, nie ein Zeichen aus einer Schrift.
QString iconFor(Verb v);

}  // namespace mg::tagmark
