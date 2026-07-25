#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  DocxTextArea — die selbstgebaute Anzeige-/Eingabefläche des DOCX-Editors
//  (QQuickPaintedItem; bewusst KEIN QML-TextEdit/RichText — Anforderung des
//  Auftrags: eigenes Absatz-/Zeilenmodell in C++; QTextLayout ist als reines
//  Shaping-/Zeilenumbruch-Primitiv UNTER diesem Modell ausdrücklich erlaubt).
//
//  RAM/Architektur:
//   • Das Item ist VIEWPORT-groß (nicht inhaltsgroß!) — QQuickPaintedItem
//     hält eine Textur in Item-Größe; ein 100-Seiten-Dokument als Textur
//     wäre ein VRAM-Desaster. Gescrollt wird über die contentY-Property
//     (QML-Scrollbar/Wheel), paint() zeichnet nur den sichtbaren Streifen.
//   • Layout-Cache je Absatz (QTextLayout + Höhe), Präfix-Offsets mit
//     dirtyFrom-Invalidierung → Bearbeitungen relayouten nur ab dem
//     betroffenen Block (inkrementell, Regel 17).
//   • Initial-Layout gechunkt über einen 0-ms-Timer (~300 Blöcke je Tick):
//     lange Dokumente blockieren den GUI-Thread nie; noch nicht vermessene
//     Blöcke tragen eine Zeilen-Schätzhöhe, die beim echten Layout ersetzt
//     wird (Scrollbalken bleibt dabei stabil nutzbar).
//
//  Eingabe: Maus (Cursor/Selektion/Doppelklick-Wort), Tastatur (Zeichen,
//  Pfeile/Home/End ± Shift, Enter/Backspace/Entf, Strg+A/C/X/V/Z/Shift+Z,
//  Strg+B/I/U, Strg+S → saveRequested) sowie Eingabemethoden-Commits
//  (tote Tasten/IME). Alle Mutationen laufen über den DocxEditController.
// ─────────────────────────────────────────────────────────────────────────────

#include <QQuickPaintedItem>
#include <QTextLayout>
#include <QTimer>
#include <QVector>
#include <memory>
#include <vector>

//  Vollständiger Typ nötig: Q_PROPERTY(DocxEditController*) registriert den
//  Metatyp im moc-Code.
#include "docx/edit/DocxEditController.h"

class DocxTextArea : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(DocxEditController* ctl READ ctl WRITE setCtl NOTIFY ctlChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentHeightChanged)
    //  Cursor-Rechteck in INHALTS-Koordinaten (QML scrollt es sichtbar).
    Q_PROPERTY(qreal cursorY READ cursorY NOTIFY cursorRectChanged)
    Q_PROPERTY(qreal cursorH READ cursorH NOTIFY cursorRectChanged)
    //  i18n-Texte kommen aus QML (App.uiText) — kein Strings-Coupling hier.
    Q_PROPERTY(QString tablePlaceholder MEMBER m_tablePlaceholder NOTIFY labelsChanged)
    Q_PROPERTY(QString pageBreakLabel MEMBER m_pageBreakLabel NOTIFY labelsChanged)
    //  Umgebungsgrund (Theme) um die WEISSE Word-Seite herum — QML bindet
    //  App.themeBase; die Seite selbst bleibt bewusst immer weiß (wie Word).
    Q_PROPERTY(QColor surroundColor MEMBER m_surroundColor NOTIFY labelsChanged)

public:
    explicit DocxTextArea(QQuickItem* parent = nullptr);
    ~DocxTextArea() override;

    DocxEditController* ctl() const { return m_ctl; }
    void setCtl(DocxEditController* c);

    qreal contentY() const { return m_contentY; }
    void  setContentY(qreal y);
    qreal contentHeight() const { return m_contentHeight; }
    qreal cursorY() const { return m_cursorRect.y(); }
    qreal cursorH() const { return m_cursorRect.height(); }

    void paint(QPainter* p) override;

signals:
    void ctlChanged();
    void contentYChanged();
    void contentHeightChanged();
    void cursorRectChanged();
    void labelsChanged();
    void saveRequested();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void inputMethodEvent(QInputMethodEvent* e) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery q) const override;
    void geometryChange(const QRectF& n, const QRectF& o) override;

private:
    struct BlockLayout {
        std::unique_ptr<QTextLayout> layout;   // nullptr = noch nicht vermessen
        qreal   height = 0.0;                  // inkl. Abstand davor/danach
        qreal   beforePx = 0.0;                // Versatz bis zur ersten Zeile
        qreal   indentPx = 0.0;                // Listen-Einzug
        QString marker;                        // "• " / "3. " (Listen)
        bool    laid = false;                  // height ist ECHT (nicht Schätzung)
    };

    //  Geometrie des Inhalts.
    qreal contentLeft() const;
    qreal contentWidth() const;

    void  rebuildAll();                        // Dokument (neu) geladen
    void  invalidateFrom(int first, int oldCount, int newCount);
    void  ensureLaid(int i);                   // Layout eines Blocks erzwingen
    void  ensureOffsetsTo(int i);              // Präfix-Offsets bis i gültig
    qreal blockTop(int i);                     // Inhalts-y des Blocks
    int   blockAtY(qreal y);                   // Block unter Inhalts-y
    void  layoutChunk();                       // Timer-Tick des Initial-Layouts
    void  updateContentHeight();
    void  rebuildMarkers();                    // Listen-Zähler (ganzes Dokument)
    void  buildLayout(int i);                  // QTextLayout eines Absatzes
    void  updateCursorRect();
    //  Leere Absätze werden mit dem am Cursor GELTENDEN Format vermessen
    //  (inkl. Pending) — beim Wechsel des Cursor-Blocks bzw. bei einer
    //  Format-Änderung ohne Selektion muss ihr Layout daher verfallen.
    void  invalidateEmptyBlock(int i);
    //  Maus (Item-Koordinaten) → (Block, Zeichenposition).
    void  hitTest(const QPointF& itemPos, int* block, int* pos);
    void  moveCursorVertical(int dir, bool keepAnchor);

    DocxEditController* m_ctl = nullptr;
    qreal   m_contentY = 0.0;
    qreal   m_contentHeight = 0.0;
    QRectF  m_cursorRect;                      // Inhalts-Koordinaten
    QString m_tablePlaceholder;
    QString m_pageBreakLabel;
    QColor  m_surroundColor = QColor(58, 62, 70);

    std::vector<BlockLayout> m_lay;   // move-only (unique_ptr) → std::vector
    QVector<qreal>       m_offsets;            // Präfix-Summen der Höhen
    int    m_offsetsValidTo = 0;               // Offsets [0..N] gültig
    int    m_layChunkAt = 0;                   // Fortschritt Initial-Layout
    QTimer m_chunkTimer;
    QTimer m_blinkTimer;
    bool   m_caretOn = true;
    bool   m_selecting = false;
    qreal  m_goalX = -1.0;                     // Wunsch-x für ↑/↓
    int    m_lastCursorBlock = -1;             // s. invalidateEmptyBlock()
};
