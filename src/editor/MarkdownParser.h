#pragma once
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

// MarkdownParser - zerlegt Markdown (CommonMark + GitHub-Erweiterungen) in Bloecke und Inline-Laeufe.
// Laeuft im Arbeitsfaden, ohne Qt-Markdown-Import: der verliert nach Inline-HTML wie `<dir>` den Rest des Absatzes.
namespace mg::editor::md {

enum class Align : quint8 { None, Left, Center, Right };

struct Block {
    enum Kind : quint8 { Paragraph, Heading, Code, Quote, List, Item, Table, Rule, Details, FrontMatter };
    Kind kind = Paragraph;
    int  level = 0;                  // Heading 1-6
    QString text;                    // Absatz/Ueberschrift/Summary als Inline-Rohtext, Code als Inhalt
    QString info;                    // Sprache eines Code-Zauns
    bool ordered = false;            // List
    int  start = 1;
    bool tight = true;
    int  task = -1;                  // Item: -1 keine Aufgabe, 0 offen, 1 erledigt
    bool open = false;               // Details: per `<details open>` aufgeklappt
    int  id = -1;                    // Details: laufende Nummer im Dokument
    QList<Align> aligns;             // Table
    QList<QStringList> rows;         // Table: rows[0] ist der Kopf
    QList<QPair<QString, QString>> pairs;   // FrontMatter
    QList<Block> children;
};

struct LinkRef {
    QString url;
    QString title;
};

struct Footnote {
    QString label;
    QString text;
};

struct Document {
    QList<Block> blocks;
    QHash<QString, LinkRef> refs;    // Schluessel normalisiert (s. normalizeLabel)
    QList<Footnote> footnotes;
    int detailsCount = 0;
};

Document parse(QStringView source);

enum InlineFlag : quint16 {
    Bold = 1, Italic = 2, Strike = 4, Code = 8, Sub = 16, Sup = 32,
    Kbd = 64, Underline = 128, Mark = 256
};

struct Run {
    enum Kind : quint8 { Text, Break, Image };
    Kind    kind = Text;
    QString text;                    // bei Image der Alternativtext
    quint16 flags = 0;
    QString href;                    // nicht leer = Verweis
    QString src;                     // Image
};

// `footnotes` nur fuer die Nummer eines `[^x]`: Position in der Liste + 1.
QList<Run> parseInlines(QStringView text, const QHash<QString, LinkRef>& refs,
                        const QList<Footnote>& footnotes = {});

QString normalizeLabel(QStringView label);

// Anker einer Ueberschrift wie auf GitHub: klein, Satzzeichen weg, Leerzeichen -> '-'.
QString slugify(QStringView text);

// Reiner Text der Inline-Laeufe (Alternativtext, Anker, Suche).
QString plainText(const QList<Run>& runs);

}  // namespace mg::editor::md
