#pragma once
#include "core/SearchPattern.h"
#include "editor/TextFoldBar.h"

#include <QColor>
#include <QFont>
#include <QQuickPaintedItem>
#include <QQuickTextDocument>

// ─────────────────────────────────────────────────────────────────────────────
//  TextDecorations.h - alles, was der Editor UEBER den Text zeichnet.
//
//  Drei Dinge in EINEM Element, weil sie dieselbe Maschinerie brauchen
//  (Dokument, Layout, Scrollstand, Schriftmasse) und ein zweiter Durchgang
//  ueber die sichtbaren Bloecke nichts brachte ausser Kosten:
//
//   1. **Einrueckungshilfen** - senkrechte Linien je Einrueckungsstufe. Sie
//      beantworten „welches `}` gehoert wohin" in tief verschachteltem Code.
//   2. **Faltmarken** - die drei Punkte hinter einer zugeklappten Zeile
//      (`void f() {...}`). Enthaelt der verborgene Teil einen SUCHTREFFER,
//      wird die Marke hervorgehoben: sonst zaehlte die Suchleiste Treffer, die
//      man nirgends sieht.
//   3. **Klammernpaare** - ein Kasten hinter der Klammer am Cursor und hinter
//      ihrer Partnerin; fehlt die Partnerin, wird der Kasten rot.
//
//  Gezeichnet wird HINTER dem Text (`z: -1` in QML), wie der Streifen der
//  aktuellen Zeile. Die Klammernkaesten liegen damit unter den Glyphen - das
//  ist Absicht: die Schriftfarbe bleibt die des Faerbers, es kommt nur ein
//  Untergrund dazu. Damit muss der Faerber fuer die Klammern nicht angefasst
//  werden (er ist auf EINEN Durchgang je Block ausgelegt).
// ─────────────────────────────────────────────────────────────────────────────
namespace mg::editor {

class TextDecorations : public QQuickPaintedItem {
    Q_OBJECT

    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(mg::editor::TextFoldBar* foldBar READ foldBar WRITE setFoldBar NOTIFY foldBarChanged)

    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY viewChanged)
    Q_PROPERTY(qreal viewportHeight READ viewportHeight WRITE setViewportHeight NOTIFY viewChanged)
    Q_PROPERTY(int cursorPosition READ cursorPosition WRITE setCursorPosition NOTIFY viewChanged)
    //  Innenabstand der `TextArea`: ihr Text beginnt um diesen Betrag versetzt,
    //  waehrend `blockBoundingRect` bei 0/0 anfaengt. Ohne beides saessen
    //  Hilfslinien und Klammernkaesten um genau die Polsterung daneben.
    Q_PROPERTY(qreal leftPadding READ leftPadding WRITE setLeftPadding NOTIFY viewChanged)
    Q_PROPERTY(qreal topPadding READ topPadding WRITE setTopPadding NOTIFY viewChanged)

    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY styleChanged)
    Q_PROPERTY(int tabWidth READ tabWidth WRITE setTabWidth NOTIFY styleChanged)
    Q_PROPERTY(bool showGuides READ showGuides WRITE setShowGuides NOTIFY styleChanged)
    Q_PROPERTY(bool showBrackets READ showBrackets WRITE setShowBrackets NOTIFY styleChanged)

    Q_PROPERTY(QColor guideColor READ guideColor WRITE setGuideColor NOTIFY styleChanged)
    Q_PROPERTY(QColor markerColor READ markerColor WRITE setMarkerColor NOTIFY styleChanged)
    Q_PROPERTY(QColor bracketColor READ bracketColor WRITE setBracketColor NOTIFY styleChanged)
    Q_PROPERTY(QColor errorColor READ errorColor WRITE setErrorColor NOTIFY styleChanged)
    Q_PROPERTY(QColor matchColor READ matchColor WRITE setMatchColor NOTIFY styleChanged)

public:
    explicit TextDecorations(QQuickItem* parent = nullptr);

    void paint(QPainter* p) override;

    QQuickTextDocument* document() const { return m_quickDoc; }
    void setDocument(QQuickTextDocument* d);
    QString path() const { return m_path; }
    void    setPath(const QString& p);
    TextFoldBar* foldBar() const { return m_foldBar; }
    void         setFoldBar(TextFoldBar* b);

    qreal contentY() const { return m_contentY; }
    void  setContentY(qreal v);
    qreal viewportHeight() const { return m_viewportH; }
    void  setViewportHeight(qreal v);
    int   cursorPosition() const { return m_cursor; }
    void  setCursorPosition(int p);
    qreal leftPadding() const { return m_leftPad; }
    void  setLeftPadding(qreal v);
    qreal topPadding() const { return m_topPad; }
    void  setTopPadding(qreal v);

    QFont font() const { return m_font; }
    void  setFont(const QFont& f);
    int   tabWidth() const { return m_tabWidth; }
    void  setTabWidth(int v);
    bool  showGuides() const { return m_guides; }
    void  setShowGuides(bool v);
    bool  showBrackets() const { return m_brackets; }
    void  setShowBrackets(bool v);

    QColor guideColor() const { return m_guideColor; }
    void   setGuideColor(const QColor& c);
    QColor markerColor() const { return m_markerColor; }
    void   setMarkerColor(const QColor& c);
    QColor bracketColor() const { return m_bracketColor; }
    void   setBracketColor(const QColor& c);
    QColor errorColor() const { return m_errorColor; }
    void   setErrorColor(const QColor& c);
    QColor matchColor() const { return m_matchColor; }
    void   setMatchColor(const QColor& c);

    //  Suchbegriff der Leiste - nur dafuer, eine zugeklappte Stelle mit Treffer
    //  zu markieren. Leer = nichts markieren.
    Q_INVOKABLE void setSearchTerm(const QString& term, bool caseSensitive);

    //  Fuer Pruefstaende: Position der beiden Klammern (-1 = keine).
    Q_INVOKABLE int bracketA() const { return m_klammerA; }
    Q_INVOKABLE int bracketB() const { return m_klammerB; }

    //  Liegt (x, y) - in den Koordinaten DIESES Elements - auf den drei Punkten
    //  einer zugeklappten Zeile? Dann die Blocknummer, sonst -1. Die Oberflaeche
    //  klappt daraufhin wieder auf: die Punkte SIND der Knopf, nicht nur eine
    //  Marke (Festlegung des Nutzers 2026-09-03).
    Q_INVOKABLE int foldMarkerAt(qreal x, qreal y) const;

signals:
    void documentChanged();
    void pathChanged();
    void foldBarChanged();
    void viewChanged();
    void styleChanged();

private:
    QTextDocument* doc() const;
    void klammernSuchen();
    void trefferBereicheNeu();
    void zeichneHilfen(QPainter* p);
    void zeichneFaltmarken(QPainter* p);
    void zeichneKlammern(QPainter* p);
    //  Rechteck eines einzelnen Zeichens in Sicht-Koordinaten; ungueltig, wenn
    //  es nicht sichtbar ist.
    QRectF zeichenRect(int position) const;
    //  Rechteck der drei Punkte hinter einer zugeklappten Zeile - dieselbe
    //  Rechnung fuer das Malen UND fuer den Klick.
    QRectF markenRect(int startBlock) const;

    QQuickTextDocument* m_quickDoc = nullptr;
    TextFoldBar*        m_foldBar = nullptr;
    QString m_path;
    qreal   m_contentY = 0;
    qreal   m_viewportH = 0;
    int     m_cursor = 0;
    qreal   m_leftPad = 0;
    qreal   m_topPad = 0;
    QFont   m_font;
    int     m_tabWidth = 4;
    bool    m_guides = true;
    bool    m_brackets = true;

    QColor m_guideColor   = QColor(255, 255, 255, 26);
    QColor m_markerColor  = QColor(120, 132, 150);
    QColor m_bracketColor = QColor(255, 255, 255, 46);
    QColor m_errorColor   = QColor(220, 80, 80, 90);
    QColor m_matchColor   = QColor(255, 210, 80, 120);

    //  Die Klammernsuche laeuft ueber bis zu 5 000 Bloecke. Sie darf deshalb
    //  nur laufen, wenn sich wirklich etwas geaendert hat - sonst zahlt jeder
    //  Cursorschritt und jeder Tastendruck dafuer.
    int  m_letzterCursor = -1;
    int  m_letzteRevision = -1;
    int m_klammerA = -1;      // Klammer am Cursor
    int m_klammerB = -1;      // ihre Partnerin (-1 = keine gefunden)

    mg::search::Pattern m_suche;
    QSet<int>           m_trefferBereiche;   // Startbloecke mit Treffer darin
};

}  // namespace mg::editor
