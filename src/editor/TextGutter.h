#pragma once
#include <QColor>
#include <QFont>
#include <QQuickPaintedItem>
#include <QQuickTextDocument>

class QTextLayout;

// ─────────────────────────────────────────────────────────────────────────────
//  TextGutter.h - die Zeilennummern-Spalte des Texteditors.
//
//  WARUM C++ und nicht QML: eine Spalte aus QML waere ein `Repeater` ueber alle
//  Zeilen, also 20 000 Items fuer eine mittlere Logdatei. Hier entsteht kein
//  einziges Item - gemalt werden nur die Bloecke, die gerade im Fenster stehen
//  (rund vierzig, unabhaengig von der Dateigroesse). Vorbild ist `DocxPageThumb`,
//  das ebenfalls ein `QQuickPaintedItem` ist.
//
//  DER KERN IST DIE UMBRUCH-REGEL: bei sichtbarem („weichem") Umbruch belegt EIN
//  Absatz mehrere Bildschirmzeilen. Die Nummer steht dann nur an der ERSTEN,
//  die Fortsetzungszeilen bleiben leer - genau so haelt es Kate, und daran
//  erkennt man, dass dort kein Enter steht. Das ergibt sich hier von selbst,
//  weil je BLOCK einmal gemalt wird, nicht je Bildschirmzeile.
//
//  Die Spalte liest dasselbe `QTextDocument` wie die `TextArea` daneben. Sie
//  aendert es nie - sie fragt nur nach Positionen.
// ─────────────────────────────────────────────────────────────────────────────
namespace mg::editor {

class TextGutter : public QQuickPaintedItem {
    Q_OBJECT

    //  Das Dokument der `TextArea` daneben.
    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    //  Scrollstand der `Flickable`, in der die `TextArea` sitzt.
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    //  Innenabstand der `TextArea` oben - ohne ihn saessen die Nummern um genau
    //  diesen Betrag zu hoch.
    Q_PROPERTY(qreal topPadding READ topPadding WRITE setTopPadding NOTIFY topPaddingChanged)
    //  Cursorposition: bestimmt, welche Zeile hervorgehoben wird.
    Q_PROPERTY(int cursorPosition READ cursorPosition WRITE setCursorPosition
                   NOTIFY cursorPositionChanged)

    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY styleChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY styleChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY styleChanged)
    Q_PROPERTY(QColor activeColor READ activeColor WRITE setActiveColor NOTIFY styleChanged)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY styleChanged)

    //  Breite, die die Spalte braucht - richtet sich nach der hoechsten Nummer.
    //  QML bindet die Breite darauf; die Spalte bestimmt sie selbst, weil nur
    //  sie die Schriftmasse kennt.
    Q_PROPERTY(qreal requiredWidth READ requiredWidth NOTIFY requiredWidthChanged)

    //  Zeile und Spalte des Cursors (1-basiert) fuer die Statuszeile. Sie stehen
    //  hier, weil dieselbe Blockrechnung sie ohnehin liefert - QML muesste
    //  sonst den ganzen Text vor dem Cursor durchzaehlen.
    Q_PROPERTY(int cursorLine READ cursorLine NOTIFY cursorPositionChanged)
    Q_PROPERTY(int cursorColumn READ cursorColumn NOTIFY cursorPositionChanged)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY requiredWidthChanged)

public:
    explicit TextGutter(QQuickItem* parent = nullptr);

    void paint(QPainter* p) override;

    QQuickTextDocument* document() const { return m_quickDoc; }
    void setDocument(QQuickTextDocument* d);

    qreal contentY() const { return m_contentY; }
    void  setContentY(qreal y);
    qreal topPadding() const { return m_topPadding; }
    void  setTopPadding(qreal v);
    int   cursorPosition() const { return m_cursorPosition; }
    void  setCursorPosition(int p);

    QFont  font() const { return m_font; }
    void   setFont(const QFont& f);
    QColor backgroundColor() const { return m_background; }
    void   setBackgroundColor(const QColor& c);
    QColor textColor() const { return m_textColor; }
    void   setTextColor(const QColor& c);
    QColor activeColor() const { return m_activeColor; }
    void   setActiveColor(const QColor& c);
    QColor borderColor() const { return m_borderColor; }
    void   setBorderColor(const QColor& c);

    qreal requiredWidth() const { return m_requiredWidth; }
    int   cursorLine() const;
    int   cursorColumn() const;
    int   lineCount() const;

signals:
    void documentChanged();
    void contentYChanged();
    void topPaddingChanged();
    void cursorPositionChanged();
    void styleChanged();
    void requiredWidthChanged();

private:
    QTextDocument* doc() const;
    void breiteNeuRechnen();
    //  Der Ast einer weich umgebrochenen Zeile (senkrechter Strich + Abzweige).
    void zeichneAst(QPainter* p, const QTextLayout& tl, qreal blockOben,
                    const QColor& farbe);

    QQuickTextDocument* m_quickDoc = nullptr;
    qreal  m_contentY = 0;
    qreal  m_topPadding = 0;
    int    m_cursorPosition = 0;
    QFont  m_font;
    QColor m_background  = QColor(20, 24, 29);
    QColor m_textColor   = QColor(94, 104, 120);
    QColor m_activeColor = QColor(200, 210, 226);
    QColor m_borderColor = QColor(0, 0, 0, 0);
    qreal  m_requiredWidth = 40;
    int    m_letzteZeilenzahl = -1;
};

}  // namespace mg::editor
