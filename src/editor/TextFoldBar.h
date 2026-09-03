#pragma once
#include "editor/FoldScanner.h"

#include <QColor>
#include <QFont>
#include <QQuickPaintedItem>
#include <QQuickTextDocument>
#include <QSet>
#include <QTimer>

// ─────────────────────────────────────────────────────────────────────────────
//  TextFoldBar.h - die schmale Leiste rechts von den Zeilennummern, in der man
//  Bloecke zu- und aufklappt (wie in Qt Creator).
//
//  DREI Entscheidungen, die man beim Lesen kennen sollte:
//
//  1. **Die Leiste erscheint nur, wenn es etwas zu falten GIBT** (`hasRegions`,
//     Festlegung des Nutzers). Sie ist dabei KLEBRIG: hat eine Datei einmal
//     einen Bereich gehabt, bleibt die Leiste stehen, auch wenn man die letzte
//     Klammer wieder loescht. Sonst wanderte der Text bei jedem geloeschten
//     `}` um die Leistenbreite hin und her.
//
//  2. **Gefaltet wird ueber `QTextBlock::setVisible`.** Das Dokument-Layout
//     beherrscht das vollstaendig (nachgemessen: zehn verborgene Zeilen aendern
//     die Dokumenthoehe von 357 auf 187 px); nur die QML-`TextArea` behaelt
//     ihre alte gemalte Hoehe, bis sie angestossen wird. Der Anstoss ist ein
//     kurzes Umlegen von `wrapMode` - NICHT `contentsChanged`: das feuert in
//     QML `onTextChanged` und markierte die Datei als geaendert, obwohl sich
//     kein Zeichen geaendert hat.
//
//  3. **Neu erfasst wird gebuendelt, nicht je Tastendruck.** Ein getipptes `{`
//     aendert die Struktur des ganzen Rests; die Erfassung laeuft deshalb
//     300 ms nach der letzten Eingabe - und nur, wenn das Eingefuegte
//     ueberhaupt ein Zeichen enthielt, das Bloecke oeffnen oder schliessen kann
//     (`touchesFolding`). Beim gewoehnlichen Tippen von Woertern passiert also
//     gar nichts.
//
//  Der Zustand „was ist zugeklappt" lebt NUR hier und NUR fuer diese Sitzung -
//  er wird nicht gespeichert (Festlegung des Nutzers).
// ─────────────────────────────────────────────────────────────────────────────
namespace mg::editor {

class TextFoldBar : public QQuickPaintedItem {
    Q_OBJECT

    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    //  Dateipfad - daraus kommt die Sprache und damit die Faltungsart.
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    //  Die `TextArea` daneben: sie muss nach jedem Falten angestossen werden.
    Q_PROPERTY(QQuickItem* textArea READ textArea WRITE setTextArea NOTIFY textAreaChanged)
    //  Die `Flickable` darum - nur, um den Scrollstand nachzufuehren, wenn
    //  ueber der Sicht etwas auf- oder zugeklappt wird.
    Q_PROPERTY(QQuickItem* flickable READ flickable WRITE setFlickable NOTIFY textAreaChanged)
    //  Hoehe des Dokuments OHNE die verborgenen Zeilen. Die `Flickable` bindet
    //  ihre `contentHeight` darauf: `TextArea::paintedHeight` merkt vom Falten
    //  nichts (gemessen: Dokument 68017 px, `paintedHeight` blieb bei 85017),
    //  und es dazu zu bewegen kostete eine volle Neuberechnung samt Sprung an
    //  den Dateianfang - 66 ms gegen 1 ms.
    Q_PROPERTY(qreal documentHeight READ documentHeight NOTIFY documentHeightChanged)

    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY viewChanged)
    Q_PROPERTY(qreal topPadding READ topPadding WRITE setTopPadding NOTIFY viewChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY styleChanged)
    Q_PROPERTY(int tabWidth READ tabWidth WRITE setTabWidth NOTIFY styleChanged)

    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY styleChanged)
    Q_PROPERTY(QColor markerColor READ markerColor WRITE setMarkerColor NOTIFY styleChanged)

    //  Hat diese Datei ueberhaupt faltbare Bereiche? QML bindet die Sichtbarkeit
    //  der Leiste darauf.
    Q_PROPERTY(bool hasRegions READ hasRegions NOTIFY regionsChanged)
    Q_PROPERTY(qreal requiredWidth READ requiredWidth NOTIFY styleChanged)

public:
    explicit TextFoldBar(QQuickItem* parent = nullptr);
    ~TextFoldBar() override;

    void paint(QPainter* p) override;

    QQuickTextDocument* document() const { return m_quickDoc; }
    void setDocument(QQuickTextDocument* d);
    QString path() const { return m_path; }
    void    setPath(const QString& p);
    QQuickItem* textArea() const { return m_textArea; }
    void        setTextArea(QQuickItem* i);
    QQuickItem* flickable() const { return m_flick; }
    void        setFlickable(QQuickItem* i);
    qreal       documentHeight() const;

    qreal contentY() const { return m_contentY; }
    void  setContentY(qreal v);
    qreal topPadding() const { return m_topPadding; }
    void  setTopPadding(qreal v);
    QFont font() const { return m_font; }
    void  setFont(const QFont& f);
    int   tabWidth() const { return m_tabWidth; }
    void  setTabWidth(int v);

    QColor backgroundColor() const { return m_background; }
    void   setBackgroundColor(const QColor& c);
    QColor markerColor() const { return m_marker; }
    void   setMarkerColor(const QColor& c);

    bool  hasRegions() const { return m_hatBereiche; }
    qreal requiredWidth() const;

    //  Klappt jeden zugeklappten Bereich auf, der `position` enthaelt - fuer
    //  einen Suchtreffer oder einen Cursor, der sonst im Verborgenen laege.
    //  Liefert true, wenn wirklich etwas aufgeklappt wurde.
    Q_INVOKABLE bool ensureVisible(int position);
    //  Blocknummer der Zeile, die einen zugeklappten Bereich um `position`
    //  eroeffnet; -1, wenn `position` sichtbar ist. Der aeusserste gewinnt.
    Q_INVOKABLE int foldedStartFor(int position) const;
    Q_INVOKABLE void unfoldAll();
    //  Bereich, der bei diesem Block beginnt, zu- bzw. aufklappen. Liefert
    //  false, wenn dort gar kein Bereich anfaengt. (Der Mausklick geht
    //  denselben Weg - so ist die Wirkung ohne Fenster pruefbar.)
    Q_INVOKABLE bool toggleFold(int blockNumber);
    Q_INVOKABLE bool isFolded(int blockNumber) const { return m_gefaltet.contains(blockNumber); }
    Q_INVOKABLE int  regionCount() const { return int(m_bereiche.size()); }
    //  Erzwingt die Neuerfassung sofort statt nach der Sammelzeit - fuer
    //  Pruefstaende und fuer den Augenblick, in dem eine Datei geladen wird.
    Q_INVOKABLE void rescanNow() { neuErfassen(); }

    //  Fuer Pruefstaende und die Zusatzzeichnung: die zugeklappten Bereiche.
    const QList<FoldRegion>& regions() const { return m_bereiche; }
    const QSet<int>&         foldedStarts() const { return m_gefaltet; }

signals:
    void documentChanged();
    void pathChanged();
    void textAreaChanged();
    void viewChanged();
    void styleChanged();
    void regionsChanged();
    //  Etwas wurde zu- oder aufgeklappt - die Zusatzzeichnung malt daraufhin
    //  ihre Punkte neu, und die Nummernspalte ihre Nummern.
    void foldingChanged();
    void documentHeightChanged();

protected:
    void mousePressEvent(QMouseEvent* e) override;

private:
    QTextDocument* doc() const;
public:
    void neuErfassen();                 // Bereiche neu einlesen
private:
    //  EINEN Bereich anwenden - nur dessen Bloecke werden angefasst. Ein
    //  Durchgang ueber das ganze Dokument je Klick waere bei 200 000 Zeilen
    //  spuerbar, und angefasst gehoert ohnehin nur, was sich aendert.
    void bereichAnwenden(const FoldRegion& r, bool verbergen);
    //  Notfallweg: alles neu setzen (nach einer Neuerfassung, wenn Bereiche
    //  verschwunden sind). Selten, deshalb darf er teuer sein.
    void sichtbarkeitNeuSetzen();
    //  Cursor aus dem Verborgenen holen und den Scrollstand nachfuehren.
    //  `standHalten` = der Scrollstand soll bleiben (Klick auf einen Pfeil).
    //  Beim Aufklappen FUER einen Suchtreffer ist das falsch: dort will der
    //  Aufrufer gleich danach zum Treffer springen.
    void nachDemFalten(int startBlock, qreal hoeheVorher, bool standHalten = true);
    int  blockBeiY(qreal y) const;      // Blocknummer unter dem Mauszeiger
    const FoldRegion* bereichAb(int block) const;

    QQuickTextDocument* m_quickDoc = nullptr;
    QQuickItem*         m_textArea = nullptr;
    QQuickItem*         m_flick = nullptr;
    QString  m_path;
    qreal    m_contentY = 0;
    qreal    m_topPadding = 0;
    QFont    m_font;
    int      m_tabWidth = 4;
    QColor   m_background = QColor(20, 24, 29);
    QColor   m_marker     = QColor(120, 132, 150);

    QList<FoldRegion> m_bereiche;
    QSet<int>         m_gefaltet;      // Startbloecke der zugeklappten Bereiche
    bool              m_hatBereiche = false;
    QTimer            m_timer;         // buendelt die Neuerfassung
};

}  // namespace mg::editor
