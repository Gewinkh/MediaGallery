#pragma once
#include "editor/SyntaxTypes.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

// Die Farben des Texteditors, GETRENNT vom App-Theme: eigene Profile, eigener Konfigurator, keine gemeinsame
// Farbe. Deshalb eine eigene Struktur statt eines weiteren Feldes in `ThemeColors`.
namespace mg::editor {

enum class EditorProfile { Nightfall, Paper, Ember, Custom };
inline constexpr int kEditorProfileCount = 4;

struct SyntaxPalette {
    QString name;

    //  Die Flaeche selbst - alles, was nicht ein einzelnes Token ist.
    QColor background       = QColor(24, 28, 34);
    QColor text             = QColor(216, 222, 233);
    QColor currentLine      = QColor(35, 41, 50);   // Streifen hinter der Cursorzeile
    QColor selection        = QColor(58, 84, 122);
    QColor gutterBackground = QColor(20, 24, 29);
    QColor gutterText       = QColor(94, 104, 120);
    QColor gutterTextActive = QColor(200, 210, 226);

    //  Je Token-Klasse eine Farbe. `Normal` bleibt ungenutzt (der Text nimmt
    //  dann `text`), steht aber im Feld, damit der Index direkt passt.
    QColor tok[int(Tok::Count)];

    QJsonObject        toJson() const;
    static SyntaxPalette fromJson(const QJsonObject& o);

    //  Farbe einer Klasse; `Normal` liefert `text`.
    QColor colorFor(Tok t) const {
        return t == Tok::Normal ? text : tok[int(t)];
    }
};

//  Die mitgelieferten Profile. `Custom` liefert die Vorgabe von `Nightfall` -
//  wer ein eigenes Profil baut, faengt damit an statt bei Schwarz.
SyntaxPalette paletteForProfile(EditorProfile p);

//  Anzeigename eines Profils (nicht uebersetzt - wie bei den App-Profilen).
QString profileName(EditorProfile p);

//  Bezeichner einer Token-Klasse fuer JSON und Einstellungen ("keyword").
//  Auch der Schluessel, unter dem die Oberflaeche ihre Farbfelder fuehrt.
QLatin1StringView tokenKey(Tok t);

//  Rueckweg: "keyword" -> Tok::Keyword. `Tok::Count` bei unbekanntem Namen.
Tok tokenFromKey(QStringView key);

}  // namespace mg::editor
