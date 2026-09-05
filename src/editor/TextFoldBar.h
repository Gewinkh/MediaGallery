#pragma once
#include "editor/FoldScanner.h"

#include <QColor>
#include <QFont>
#include <QQuickPaintedItem>
#include <QQuickTextDocument>
#include <QSet>
#include <QTimer>

// Die Leiste ist KLEBRIG: hatte eine Datei einmal einen Bereich, bleibt sie stehen - sonst wanderte der Text
// bei jedem gelöschten `}` hin und her. Gefaltet wird über `setVisible`; angestoßen wird die TextArea durch
// kurzes Umlegen von `wrapMode`, nicht über `contentsChanged` (das markierte die Datei fälschlich als geändert).
namespace mg::editor {

class TextFoldBar : public QQuickPaintedItem {
    Q_OBJECT

    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QQuickItem* textArea READ textArea WRITE setTextArea NOTIFY textAreaChanged)
    //  Die `Flickable` darum - nur, um den Scrollstand nachzufuehren, wenn
    //  ueber der Sicht etwas auf- oder zugeklappt wird.
    Q_PROPERTY(QQuickItem* flickable READ flickable WRITE setFlickable NOTIFY textAreaChanged)
    // Höhe des Dokuments OHNE die verborgenen Zeilen: `TextArea::paintedHeight` merkt vom Falten nichts (gemessen:
    // Dokument 68017 px, `paintedHeight` blieb bei 85017), und es dazu zu bewegen kostete 66 ms gegen 1 ms.
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

    Q_INVOKABLE bool ensureVisible(int position);
    Q_INVOKABLE int foldedStartFor(int position) const;
    Q_INVOKABLE void unfoldAll();
    Q_INVOKABLE bool toggleFold(int blockNumber);
    Q_INVOKABLE bool isFolded(int blockNumber) const { return m_gefaltet.contains(blockNumber); }
    Q_INVOKABLE int  regionCount() const { return int(m_bereiche.size()); }
    Q_INVOKABLE void rescanNow() { neuErfassen(); }

    const QList<FoldRegion>& regions() const { return m_bereiche; }
    const QSet<int>&         foldedStarts() const { return m_gefaltet; }

signals:
    void documentChanged();
    void pathChanged();
    void textAreaChanged();
    void viewChanged();
    void styleChanged();
    void regionsChanged();
    void foldingChanged();
    void documentHeightChanged();

protected:
    void mousePressEvent(QMouseEvent* e) override;

private:
    QTextDocument* doc() const;
public:
    void neuErfassen();                 // Bereiche neu einlesen
private:
    void bereichAnwenden(const FoldRegion& r, bool verbergen);
    void sichtbarkeitNeuSetzen();
    // `standHalten` = der Scrollstand soll bleiben (Klick auf einen Pfeil). Beim Aufklappen FÜR einen Suchtreffer
    // ist das falsch: dort springt der Aufrufer gleich danach zum Treffer.
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
