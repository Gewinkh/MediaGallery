#pragma once
#include "editor/MarkdownParser.h"
#include "editor/SyntaxPalette.h"

#include <QFont>
#include <QRectF>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFormat>
#include <QTextOption>
#include <atomic>

class QTextDocument;

// MarkdownRender - baut aus dem zerlegten Markdown ein QTextDocument; laeuft im Arbeitsfaden, ausgelegt wird spaeter.
namespace mg::editor::md {

struct RenderStyle {
    QFont body;                  // Fliesstext; die Pixelgroesse ist die Grundgroesse aller Abstaende
    QFont mono;
    SyntaxPalette palette;
    QString baseDir;             // Ordner der Datei: Bilder und relative Verweise
    QSet<int> flipped;           // Details, die gegen ihre Vorgabe auf- bzw. zugeklappt sind
    QString showLabel;           // Zusatz hinter einer zugeklappten Summary
    QString hideLabel;
    int maxImageWidth = 1600;    // groessere Bilder werden beim Laden verkleinert
    qint64 imageBudget = 64LL * 1024 * 1024;   // dekodierte Bytes je Dokument, danach nur Alternativtext
};

inline constexpr int kAnchorProperty = QTextFormat::UserProperty + 1;   // Anker einer Ueberschrift am Block
// Hoehe der Kopfleiste ueber einem Code-Block; den Kasten selbst zeichnet die Oberflaeche.
inline constexpr int kCodeHeader = 28;
inline constexpr QLatin1StringView kDetailsScheme("mg-details:");

// Bloecke im Fluss, kein Rahmen: Qt weitet eine Markierung an Rahmengrenzen auf den ganzen Rahmen.
struct CodeBox {
    QTextBlock first;
    QTextBlock last;
    qreal padding = 0;
    QString language;
};

class RenderedDocument : public QTextDocument {
public:
    QList<CodeBox> codeBoxes;     // von oben nach unten
    QTextFrame* metaFrame = nullptr;
};

RenderedDocument* buildDocument(const Document& doc, const RenderStyle& style,
                                const std::atomic<bool>* abort = nullptr);

// Was die TextEdit beim Einhaengen setzt: weicht die Option ab, wirft Qt das ganze Layout weg und legt im GUI-Faden neu aus.
struct LayoutVorgabe {
    qreal width = 0;
    QTextOption option;
    Qt::LayoutDirection ohneRichtung = Qt::LeftToRight;   // Eingaberichtung, gilt fuer Text ohne starkes Zeichen
};
// Legt im Arbeitsfaden aus: das Formen ist der teuerste Schritt, und die TextEdit fragt beim Einhaengen nach der Groesse.
void layoutDocument(RenderedDocument& doc, const LayoutVorgabe& vorgabe, const std::atomic<bool>* abort);

QRectF codeBoxRect(const RenderedDocument& doc, const CodeBox& box);
QString codeBoxText(const CodeBox& box);

// Von Zeilenfenster [a,b) nach [c,d), a <= b und c <= d: bleibende Zeilen behalten ihre QML-Elemente.
struct FensterWechsel {
    bool springe;     // nichts bleibt: das Fenster beginnt leer an c
    int vorneWeg;
    int hintenWeg;
    int vorneNeu;
    int hintenNeu;
};
constexpr FensterWechsel fensterWechsel(int a, int b, int c, int d) {
    if (b <= c || d <= a) return {true, 0, b - a, 0, d - c};
    return {false, c > a ? c - a : 0, b > d ? b - d : 0, a > c ? a - c : 0, d > b ? d - b : 0};
}

// Sprache eines Code-Zauns (`c`, `asm`, `sh`, `make` …) als Bezeichner der LanguageTable; leer = ungefaerbt.
QString languageForFence(const QString& info);

}  // namespace mg::editor::md
