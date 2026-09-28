#pragma once
#include "editor/MarkdownParser.h"
#include "editor/SyntaxPalette.h"

#include <QFont>
#include <QSet>
#include <QTextFormat>
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
inline constexpr int kCodeTextProperty = QTextFormat::UserProperty + 2; // Inhalt eines Code-Rahmens (Kopieren)
inline constexpr int kCodeLangProperty = QTextFormat::UserProperty + 3; // Sprache eines Code-Rahmens (Kopfleiste)
inline constexpr int kMetaProperty = QTextFormat::UserProperty + 4;     // Rahmen der Frontmatter-Karte
// Hoehe der Kopfleiste ueber einem Code-Block; den Kasten selbst zeichnet die Oberflaeche.
inline constexpr int kCodeHeader = 28;
inline constexpr QLatin1StringView kDetailsScheme("mg-details:");

QTextDocument* buildDocument(const Document& doc, const RenderStyle& style,
                             const std::atomic<bool>* abort = nullptr);

// Sprache eines Code-Zauns (`c`, `asm`, `sh`, `make` …) als Bezeichner der LanguageTable; leer = ungefaerbt.
QString languageForFence(const QString& info);

}  // namespace mg::editor::md
