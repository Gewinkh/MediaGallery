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

// Der Anschluss an Qts Dokumentmodell - alles Fachliche liegt GUI-frei daneben.
// highlightBlock faerbt nur geaenderte Bloecke; ein Tastendruck kostet genau einen,
// das volle Faerben ~5 us je Zeile. Eine Instanz je Kachel, kein Singleton.
namespace mg::editor {

class Highlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit Highlighter(QTextDocument* doc);

    void setLanguageId(const QString& id);
    void setPalette(const SyntaxPalette& p);
    QString languageId() const { return m_languageId; }

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
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY languageChanged)
    Q_PROPERTY(QString languageLabel READ languageLabel NOTIFY languageChanged)
    //  Faerbt ueberhaupt? Falsch bei unbekannter Endung - die Oberflaeche kann
    //  die Sprachanzeige dann weglassen.
    Q_PROPERTY(bool active READ active NOTIFY languageChanged)
    // Tabulatorbreite in ZEICHEN. Qts Vorgabe sind feste 80 px, bei 13-px-Monospace also
    // zehn Zeichen statt vier. Gerechnet wird an QTextDocument::defaultFont, nicht ueber
    // QMLs FontMetrics - das lag um ein Pixel daneben (11,438 gegen 12,438).
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

    Q_INVOKABLE int columnAt(int position) const;

    // Alles über `QTextDocument::find` - das Dokument kann das auf seiner eigenen Struktur statt auf einer Vollkopie.
    // Rückgabe `{ found, start, length, index, total }`; daraus baut die Leiste ihr "3 / 17". Die Suche läuft UM.
    Q_INVOKABLE QVariantMap findNext(const QString& needle, int from,
                                     bool caseSensitive, bool wholeWords,
                                     bool backward) const;
    Q_INVOKABLE int countMatches(const QString& needle, bool caseSensitive,
                                 bool wholeWords) const;
    Q_INVOKABLE QVariantMap replaceAndFind(const QString& needle,
                                           const QString& replacement, int from,
                                           bool caseSensitive, bool wholeWords);
    Q_INVOKABLE int replaceAll(const QString& needle, const QString& replacement,
                               bool caseSensitive, bool wholeWords);
    Q_INVOKABLE void highlightMatches(const QString& needle, bool caseSensitive);

    Q_INVOKABLE bool usesRegex(const QString& needle) const;

signals:
    void documentChanged();
    void languageChanged();
    void tabWidthChanged();

private:
    void neuAufbauen();
    void tabBreiteAnwenden();

    QQuickTextDocument* m_quickDoc = nullptr;
    Highlighter*        m_highlighter = nullptr;
    QString             m_path;
    QString             m_languageId;
    int                 m_tabWidth = 0;  // 0 = nicht gesetzt, Qt-Vorgabe gilt
};

}  // namespace mg::editor
