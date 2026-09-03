#pragma once
#include "core/SearchPattern.h"
#include "editor/SyntaxPalette.h"

#include <QObject>
#include <QString>
#include <QSyntaxHighlighter>
#include <QVariantMap>
//  VOLLSTAENDIG einbinden, nicht vorwaerts deklarieren: Qts Metatyp-System
//  verlangt fuer einen Zeiger in einem Q_PROPERTY den fertigen Typ
//  („Pointer Meta Types must either point to fully-defined types").
#include <QQuickTextDocument>

// ─────────────────────────────────────────────────────────────────────────────
//  CodeHighlighter.h - der Anschluss an Qt, und NUR der.
//
//  Alles Fachliche (Sprachtabelle, Zerleger, Palette) ist reines C++ und ohne
//  GUI testbar. Hier haengt es an Qts Dokumentmodell:
//    QML `TextArea.textDocument` -> QQuickTextDocument -> QTextDocument
//    -> QSyntaxHighlighter::highlightBlock (faerbt NUR geaenderte Bloecke)
//  Gemessen (Release, Sonde): ein Tastendruck faerbt genau EINEN Block nach;
//  das vollstaendige Faerben kostet ~5 µs je Zeile und faellt nur beim Laden an.
//
//  DEZENTRAL, eine Instanz je Kachel - wie `PdfEditController` und die anderen
//  Surface-Beigaben: in der geteilten Ansicht sind bis zu vier Dateien offen,
//  jede mit eigener Sprache und eigenem Dokument. Registriert wird die Klasse
//  deshalb mit `qmlRegisterType`, NICHT als Singleton.
//
//  Nutzung aus QML:
//      CodeHighlighter {
//          document: editor.textDocument
//          path:     root.currentPath      // bestimmt die Sprache
//      }
// ─────────────────────────────────────────────────────────────────────────────
namespace mg::editor {

//  Der eigentliche Faerber. Nicht in QML sichtbar - er haengt am Dokument.
class Highlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit Highlighter(QTextDocument* doc);

    void setLanguageId(const QString& id);
    void setPalette(const SyntaxPalette& p);
    QString languageId() const { return m_languageId; }

    //  Alle Fundstellen hinterlegen (Kates „Alle hervorheben"). Leerer Begriff
    //  hebt es auf. Die Faerbung passiert im selben Durchlauf wie die Syntax -
    //  ein zweiter Faerber waere ein zweiter voller Durchgang.
    void setSearchTerm(const QString& term, bool caseSensitive);

protected:
    void highlightBlock(const QString& text) override;

private:
    QString       m_languageId;
    SyntaxPalette m_palette;
    mg::search::Pattern m_searchPattern;
    //  Fertige Formate je Token-Klasse: `QTextCharFormat` bei JEDEM Block neu
    //  zu bauen waere die teuerste Zeile des ganzen Wegs.
    QTextCharFormat m_formats[int(Tok::Count)];
    void rebuildFormats();
    void markiereFundstellen(const QString& text);
};

//  Die QML-Fassade. Haelt genau einen `Highlighter` und haengt ihn um, wenn
//  QML ein anderes Dokument setzt.
class CodeHighlighter : public QObject {
    Q_OBJECT
    //  Das `textDocument` einer QML-`TextArea`. Ohne das gibt es keinen Zugriff
    //  auf den Text - `TextArea` baut darauf auf.
    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    //  Pfad der offenen Datei; daraus kommt die Sprache (Endungstabelle).
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY languageChanged)
    //  Was in der Statuszeile steht ("C++", "Markdown", "Text").
    Q_PROPERTY(QString languageLabel READ languageLabel NOTIFY languageChanged)
    //  Faerbt ueberhaupt? Falsch bei unbekannter Endung - die Oberflaeche kann
    //  die Sprachanzeige dann weglassen.
    Q_PROPERTY(bool active READ active NOTIFY languageChanged)
    //  Breite eines Tabulators in ZEICHEN (nicht in Pixeln). Qts Vorgabe ist ein
    //  fester Wert von 80 px, unabhaengig von der Schrift - bei 13-px-Monospace
    //  sind das gut ZEHN Zeichen statt der vier, die jeder Editor zeigt.
    //
    //  Die Umrechnung Zeichen -> Pixel passiert HIER und NICHT in QML: gemessen
    //  wird an `QTextDocument::defaultFont()`, also an genau der Schrift, mit
    //  der das Layout auch rechnet. Der Weg ueber QMLs `FontMetrics` lag um
    //  ein Pixel daneben (gemessen: 11,438 gegen 12,438) - ein Tabulator stand
    //  damit NICHT dort, wo vier Leerzeichen enden, und genau das war der
    //  Wunsch (Nutzer 2026-09-02: „mache meine Laenge genau so lang wie vier
    //  Leerzeichen"). Gemessen wird an ECHTEN Leerzeichen, nicht an Ziffern.
    //
    //  Sitzt bei DIESER Klasse, weil sie ohnehin die einzige ist, die das
    //  `QTextDocument` der Kachel in der Hand hat.
    Q_PROPERTY(int tabWidth READ tabWidth WRITE setTabWidth NOTIFY tabWidthChanged)

public:
    explicit CodeHighlighter(QObject* parent = nullptr);
    ~CodeHighlighter() override;

    QQuickTextDocument* document() const { return m_quickDoc; }
    void setDocument(QQuickTextDocument* d);

    QString path() const { return m_path; }
    void    setPath(const QString& p);

    QString languageLabel() const;
    bool    active() const;

    int  tabWidth() const { return m_tabWidth; }
    void setTabWidth(int zeichen);

    //  Spalte (0-basiert) einer Position innerhalb ihrer Zeile. Braucht die
    //  Tabulatortaste, um bis zum naechsten Halt aufzufuellen. In QML muesste
    //  man dafuer den ganzen Text vor dem Cursor durchsuchen - eine Vollkopie
    //  bei jedem Tastendruck; hier ist es eine Blockrechnung.
    Q_INVOKABLE int columnAt(int position) const;

    // ── Suchen und Ersetzen ─────────────────────────────────────────────────
    //  Alles ueber `QTextDocument::find` - das Dokument kann das selbst, und
    //  zwar auf seiner eigenen Struktur statt auf einer Vollkopie des Textes.
    //  Alle Rueckgaben tragen `{ found, start, length, index, total }`:
    //  `index` ist der 1-basierte Platz des Treffers, `total` ihre Zahl -
    //  daraus baut die Leiste ihr „3 / 17".
    //
    //  `from` ist die Position, AB der gesucht wird (bei rueckwaerts: bis zu
    //  der). Die Suche laeuft UM: hinter dem letzten Treffer geht es beim
    //  ersten weiter - so haelt es jede IDE.
    Q_INVOKABLE QVariantMap findNext(const QString& needle, int from,
                                     bool caseSensitive, bool wholeWords,
                                     bool backward) const;
    //  Nur zaehlen (fuer die Anzeige, ohne den Cursor zu bewegen).
    Q_INVOKABLE int countMatches(const QString& needle, bool caseSensitive,
                                 bool wholeWords) const;
    //  Ersetzt den Treffer, der bei `from` GENAU beginnt, und sucht weiter.
    //  Steht der Cursor nicht auf einem Treffer, wird nur gesucht - so kann
    //  „Ersetzen" nie etwas erwischen, das der Nutzer nicht sieht.
    Q_INVOKABLE QVariantMap replaceAndFind(const QString& needle,
                                           const QString& replacement, int from,
                                           bool caseSensitive, bool wholeWords);
    //  Alle Fundstellen ersetzen. Rueckgabe: wie viele. EIN Undo-Schritt.
    Q_INVOKABLE int replaceAll(const QString& needle, const QString& replacement,
                               bool caseSensitive, bool wholeWords);
    //  Fundstellen hinterlegen (QML-Seite von `setSearchTerm`).
    Q_INVOKABLE void highlightMatches(const QString& needle, bool caseSensitive);

    //  Wurde der Begriff (auch) als regulaerer Ausdruck gelesen? Die Leiste
    //  zeigt damit an, warum ploetzlich mehr Treffer da sind als Buchstaben
    //  im Feld stehen. Siehe `core/SearchPattern.h`.
    Q_INVOKABLE bool usesRegex(const QString& needle) const;

signals:
    void documentChanged();
    void languageChanged();
    void tabWidthChanged();

private:
    void neuAufbauen();
    //  Setzt die Tabulatorbreite am Dokument (idempotent).
    void tabBreiteAnwenden();

    QQuickTextDocument* m_quickDoc = nullptr;
    Highlighter*        m_highlighter = nullptr;
    QString             m_path;
    QString             m_languageId;
    int                 m_tabWidth = 0;  // 0 = nicht gesetzt, Qt-Vorgabe gilt
};

}  // namespace mg::editor
