#include "editor/SyntaxPalette.h"

using namespace Qt::Literals::StringLiterals;

// Die drei mitgelieferten Profile sind EIGENE Zusammenstellungen - kein Nachbau eines bekannten Editor-Themas,
// dieselbe Lizenzfrage soll gar nicht erst entstehen. Kommentar am unauffälligsten, Operator dicht an der Textfarbe.
namespace mg::editor {
namespace {

struct KeyName { Tok tok; QLatin1StringView key; };

const KeyName s_keys[] = {
    { Tok::Normal,   "normal"_L1 },
    { Tok::Keyword,  "keyword"_L1 },
    { Tok::Type,     "type"_L1 },
    { Tok::String,   "string"_L1 },
    { Tok::Number,   "number"_L1 },
    { Tok::Comment,  "comment"_L1 },
    { Tok::Preproc,  "preproc"_L1 },
    { Tok::Function, "function"_L1 },
    { Tok::Operator, "operator"_L1 },
    { Tok::Heading,  "heading"_L1 },
    { Tok::Emphasis, "emphasis"_L1 },
    { Tok::Link,     "link"_L1 },
    { Tok::CodeSpan, "code"_L1 },
};
static_assert(sizeof(s_keys) / sizeof(s_keys[0]) == int(Tok::Count),
              "Zu jeder Token-Klasse gehoert genau ein Bezeichner - sonst "
              "verliert eine Farbe beim Speichern still ihren Schluessel.");

void setzeTokens(SyntaxPalette& p,
                 QColor keyword, QColor type, QColor string, QColor number,
                 QColor comment, QColor preproc, QColor function, QColor op,
                 QColor heading, QColor emphasis, QColor link, QColor code) {
    p.tok[int(Tok::Normal)]   = p.text;
    p.tok[int(Tok::Keyword)]  = keyword;
    p.tok[int(Tok::Type)]     = type;
    p.tok[int(Tok::String)]   = string;
    p.tok[int(Tok::Number)]   = number;
    p.tok[int(Tok::Comment)]  = comment;
    p.tok[int(Tok::Preproc)]  = preproc;
    p.tok[int(Tok::Function)] = function;
    p.tok[int(Tok::Operator)] = op;
    p.tok[int(Tok::Heading)]  = heading;
    p.tok[int(Tok::Emphasis)] = emphasis;
    p.tok[int(Tok::Link)]     = link;
    p.tok[int(Tok::CodeSpan)] = code;
}

}  // namespace

QLatin1StringView tokenKey(Tok t) {
    for (const KeyName& k : s_keys)
        if (k.tok == t) return k.key;
    return "normal"_L1;
}

Tok tokenFromKey(QStringView key) {
    for (const KeyName& k : s_keys)
        if (key.compare(k.key) == 0) return k.tok;
    return Tok::Count;
}

QString profileName(EditorProfile p) {
    switch (p) {
    case EditorProfile::Nightfall: return QStringLiteral("Nightfall");
    case EditorProfile::Paper:     return QStringLiteral("Paper");
    case EditorProfile::Ember:     return QStringLiteral("Ember");
    case EditorProfile::Custom:    return QStringLiteral("Custom");
    }
    return QStringLiteral("Nightfall");
}

SyntaxPalette paletteForProfile(EditorProfile p) {
    SyntaxPalette t;
    t.name = profileName(p);

    switch (p) {
    case EditorProfile::Paper:
        //  Helles Profil - fuer wen die App hell eingestellt ist oder wer bei
        //  Tageslicht arbeitet. Die Farben sind gesaettigter als im Dunklen,
        //  weil helle Flaechen Kontrast schlucken.
        t.background       = QColor(252, 251, 248);
        t.text             = QColor(38, 42, 48);
        t.currentLine      = QColor(240, 238, 232);
        t.selection        = QColor(184, 208, 240);
        t.gutterBackground = QColor(244, 242, 237);
        t.gutterText       = QColor(158, 158, 152);
        t.gutterTextActive = QColor(60, 64, 70);
        setzeTokens(t,
            /*keyword */ QColor(140,  30, 140),
            /*type    */ QColor( 20, 100, 160),
            /*string  */ QColor( 20, 120,  60),
            /*number  */ QColor(180,  80,  10),
            /*comment */ QColor(140, 140, 132),
            /*preproc */ QColor(150,  60,  30),
            /*function*/ QColor( 30,  70, 190),
            /*operator*/ QColor( 90,  96, 104),
            /*heading */ QColor( 20,  80, 150),
            /*emphasis*/ QColor(150,  50, 110),
            /*link    */ QColor( 25, 110, 180),
            /*code    */ QColor(160,  70,  40));
        break;

    case EditorProfile::Ember:
        //  Warmes dunkles Profil - gedaempfter als Nightfall, ohne Blaustich.
        t.background       = QColor(31, 27, 24);
        t.text             = QColor(230, 220, 206);
        t.currentLine      = QColor(45, 39, 34);
        t.selection        = QColor(104, 74, 44);
        t.gutterBackground = QColor(26, 23, 20);
        t.gutterText       = QColor(120, 106,  92);
        t.gutterTextActive = QColor(226, 202, 168);
        setzeTokens(t,
            /*keyword */ QColor(226, 132,  76),
            /*type    */ QColor(230, 186,  92),
            /*string  */ QColor(164, 190, 108),
            /*number  */ QColor(214, 150, 140),
            /*comment */ QColor(122, 110,  96),
            /*preproc */ QColor(196, 124, 160),
            /*function*/ QColor(238, 206, 128),
            /*operator*/ QColor(186, 174, 158),
            /*heading */ QColor(240, 168,  90),
            /*emphasis*/ QColor(212, 156, 196),
            /*link    */ QColor(140, 186, 190),
            /*code    */ QColor(180, 200, 140));
        break;

    case EditorProfile::Nightfall:
    case EditorProfile::Custom:
    default:
        //  Vorgabe. `Custom` faengt hier an, damit ein eigenes Profil nicht bei
        //  Schwarz auf Schwarz beginnt.
        t.background       = QColor( 24,  28,  34);
        t.text             = QColor(216, 222, 233);
        t.currentLine      = QColor( 35,  41,  50);
        t.selection        = QColor( 58,  84, 122);
        t.gutterBackground = QColor( 20,  24,  29);
        t.gutterText       = QColor( 94, 104, 120);
        t.gutterTextActive = QColor(200, 210, 226);
        setzeTokens(t,
            /*keyword */ QColor(198, 146, 232),
            /*type    */ QColor(122, 190, 236),
            /*string  */ QColor(148, 206, 140),
            /*number  */ QColor(232, 168, 118),
            /*comment */ QColor(110, 122, 140),
            /*preproc */ QColor(226, 140, 176),
            /*function*/ QColor(126, 200, 214),
            /*operator*/ QColor(170, 180, 196),
            /*heading */ QColor(130, 190, 255),
            /*emphasis*/ QColor(226, 186, 130),
            /*link    */ QColor(122, 196, 200),
            /*code    */ QColor(164, 210, 158));
        break;
    }
    return t;
}

QJsonObject SyntaxPalette::toJson() const {
    QJsonObject o;
    o["name"_L1]             = name;
    o["background"_L1]       = background.name();
    o["text"_L1]             = text.name();
    o["currentLine"_L1]      = currentLine.name();
    o["selection"_L1]        = selection.name();
    o["gutterBackground"_L1] = gutterBackground.name();
    o["gutterText"_L1]       = gutterText.name();
    o["gutterTextActive"_L1] = gutterTextActive.name();
    for (int i = 1; i < int(Tok::Count); ++i)
        o[tokenKey(Tok(i))] = tok[i].name();
    return o;
}

SyntaxPalette SyntaxPalette::fromJson(const QJsonObject& o) {
    //  Vorgabe als Grundlage: fehlt ein Schluessel (aeltere Datei, neue
    //  Token-Klasse), bleibt der eingebaute Wert stehen statt Schwarz.
    SyntaxPalette t = paletteForProfile(EditorProfile::Nightfall);

    const auto lies = [&o](QLatin1StringView k, QColor& ziel) {
        const QString s = o[k].toString();
        if (!s.isEmpty()) {
            const QColor c(s);
            if (c.isValid()) ziel = c;
        }
    };
    if (o.contains("name"_L1)) t.name = o["name"_L1].toString(t.name);
    lies("background"_L1,       t.background);
    lies("text"_L1,             t.text);
    lies("currentLine"_L1,      t.currentLine);
    lies("selection"_L1,        t.selection);
    lies("gutterBackground"_L1, t.gutterBackground);
    lies("gutterText"_L1,       t.gutterText);
    lies("gutterTextActive"_L1, t.gutterTextActive);
    for (int i = 1; i < int(Tok::Count); ++i)
        lies(tokenKey(Tok(i)), t.tok[i]);
    t.tok[int(Tok::Normal)] = t.text;
    return t;
}

}  // namespace mg::editor
