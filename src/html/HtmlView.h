#pragma once
//  HtmlView - Anzeige des eigenen HTML-Renderers: legt und malt Kacheln im Arbeitsfaden, gezeigt wird nur das
//  Sichtfenster samt Vorrat in Rollrichtung. Rollen verschiebt nur Texturen; Markierung und Treffer liegen als
//  Flaechen darueber, damit eine Mausbewegung keine Kachel neu malt.
#include "core/PdfVorschau.h"
#include "html/HtmlAuswahl.h"
#include "html/HtmlPdf.h"

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QQuickItem>
#include <QThreadPool>
#include <QTimer>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <set>

class QSGSimpleTextureNode;
class QSGRectangleNode;

namespace mg::html {

struct Dokument;
struct Seite;

class HtmlView : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY layoutChanged)
    Q_PROPERTY(QColor canvasColor READ canvasColor NOTIFY layoutChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool failed READ failed NOTIFY stateChanged)
    Q_PROPERTY(bool hasScript READ hasScript NOTIFY stateChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QColor selectionColor MEMBER m_farbeAuswahl NOTIFY selectionChanged)
    //  Dieselbe Such-API wie TableController, damit der Suchbalken der Tabellen sie bedient.
    Q_PROPERTY(int matchCount READ matchCount NOTIFY searchChanged)
    Q_PROPERTY(int matchIndex READ matchIndex NOTIFY searchChanged)
    Q_PROPERTY(bool matchOverflow READ matchOverflow NOTIFY searchChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchChanged)
    Q_PROPERTY(int searchRevision READ searchRevision NOTIFY searchChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)

public:
    explicit HtmlView(QQuickItem* parent = nullptr);
    ~HtmlView() override;

    QString source() const { return m_source; }
    void setSource(const QString& pathOrUrl);
    qreal contentY() const { return m_contentY; }
    void setContentY(qreal y);
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal z);
    qreal contentHeight() const { return m_hoehe * m_zoom; }
    QColor canvasColor() const { return m_leinwand; }
    bool busy() const { return m_busy; }
    bool failed() const { return m_failed; }
    bool hasScript() const { return m_skript; }
    bool hasSelection() const { return m_anker.gueltig() && m_fokus.gueltig() && !(m_anker == m_fokus); }
    int matchCount() const { return int(m_treffer.size()); }
    int matchIndex() const { return m_trefferIndex; }
    bool matchOverflow() const { return m_trefferUeber; }
    bool searching() const { return m_sucht; }
    int searchRevision() const { return m_suchRevision; }

    Q_INVOKABLE QString selectedText() const;
    Q_INVOKABLE void copy();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    //  fromRow: Inhalts-y in Item-Pixeln, ab dem der erste Treffer gewaehlt wird.
    Q_INVOKABLE void search(const QString& text, bool caseSensitive, bool wholeWords, int fromRow);
    Q_INVOKABLE void stepMatch(int richtung);
    Q_INVOKABLE void clearSearch();
    //  PDF wie MarkdownView: zaehlen, Vorschau zum Auswaehlen, schreiben - in eigenem Faden, damit Kacheln weiterlaufen.
    bool exporting() const { return m_exporting; }
    Q_INVOKABLE QString pdfTarget() const;
    Q_INVOKABLE void countPdfPages(bool print, bool landscape);
    Q_INVOKABLE void previewPdf(bool print, bool landscape);
    Q_INVOKABLE void exportPdf(const QString& target, bool print, bool landscape, int firstPage, int lastPage,
                               const QList<int>& pages = {});

    //  Kachelhoehe in Item-Pixeln.
    static constexpr int kKachel = 384;
    //  Zuletzt bestellter Kachelbereich; der Malfaden ueberspringt, was nicht mehr darin liegt.
    struct Gewollt { std::atomic<int> von{ 0 }, bis{ -1 }; };

signals:
    void sourceChanged();
    void contentYChanged();
    void zoomChanged();
    void layoutChanged();
    void stateChanged();
    void selectionChanged();
    void searchChanged();
    //  Die Flickable haelt contentY; die Ansicht bittet nur darum (Treffer, Ziehen ueber den Rand).
    void scrollRequested(qreal contentY);
    //  Verweis nach aussen (http, https, mailto, ftp); Spruenge in derselben Datei rollt die Ansicht selbst.
    void linkActivated(const QString& url);
    void openFileRequested(const QString& path);
    void exportingChanged();
    void pdfPagesCounted(int pages);
    void pdfPreviewReady(const QString& path, int pages);
    void pdfExportFinished(bool ok, const QString& target, const QString& error);

protected:
    QSGNode* updatePaintNode(QSGNode* alt, UpdatePaintNodeData*) override;
    void geometryChange(const QRectF& neu, const QRectF& alt) override;
    void updatePolish() override;
    void itemChange(ItemChange c, const ItemChangeData& d) override;
    void releaseResources() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void hoverMoveEvent(QHoverEvent* e) override;

private:
    struct Kachel {
        QImage bild;            // nach dem Hochladen leer, die Textur haelt den Inhalt
        int gen = 0;
        bool neu = true;
        qreal verschub = 0;     // alte Kachel nach einer Verankerung: so weit tiefer, bis die neuen stehen
    };

    void starte(bool lesen);
    void annehmen(int gen, std::shared_ptr<const Dokument> dok, std::shared_ptr<const Seite> seite, bool ok);
    void kachelFertig(int gen, int index, QImage bild);
    void schichtFertig(int gen, int index, QImage bild);
    uint32_t elementUnter(QPointF p) const;
    void legeNach();
    void zeigeKacheln();
    std::pair<int, int> sicht() const;
    bool sichtOffen() const;
    bool imFenster(int index, const Kachel& k) const;
    Stelle stelleBei(QPointF p, bool glyphe = false) const;
    uint32_t verweisBei(QPointF p) const;
    void folge(uint32_t verweis);
    void springe(uint32_t ziel);
    void setzeFokus(const Stelle& s);
    void rolle();
    void zeigeTreffer();
    void trefferAngenommen(int gen, std::vector<Bereich> t, bool ueber, int abY);
    void rechneFlaechen();
    void entsorge(std::shared_ptr<const Seite> seite);
    qreal faktor() const;
    enum class PdfArt { Schreiben, Zaehlen, Vorschau };
    void runPdf(PdfArt art, const QString& ziel, const PdfOptionen& o, int gen);

    //  Zuerst deklariert, also zuletzt zerstoert: Schriften des Layouts gehoeren dem Faden dieses Pools und
    //  werden dort freigegeben (Qts Schrift-Caches leben je Faden).
    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_abbruch;
    std::shared_ptr<const Dokument> m_dok;
    std::vector<uint8_t> m_zustand;     // :checked und details[open] nach Klicks; leer bis zum ersten
    std::shared_ptr<const Seite> m_seite;
    QString m_source;
    qreal m_contentY = 0, m_zoom = 1;
    int m_richtung = 0;                 // letzte Rollrichtung, +1 abwaerts
    std::shared_ptr<Gewollt> m_gewollt = std::make_shared<Gewollt>();
    float m_hoehe = 0;
    float m_gelegtBreite = -1;
    //  Im Viewer kommt die Hoehe nach der Breite: gelegt wird erst mit ihr (vh, fixed unten).
    bool m_ohneHoehe = false;
    qreal m_gelegtFaktor = 0;
    QColor m_leinwand = Qt::white;
    bool m_busy = false, m_failed = false, m_skript = false;
    //  Breite geaendert, waehrend gelegt oder gemalt wurde: danach die zuletzt gemeldete Breite legen.
    bool m_nachlegen = false;
    uint32_t m_sprung = kKein;          // nach dem Legen anspringen (Ziel im eben aufgeklappten details)
    int m_layoutGen = 0, m_seiteGen = 0;
    std::map<int, Kachel> m_kacheln;
    std::map<int, Kachel> m_wartend;    // neu gelegt, gezeigt erst mit allen sichtbaren zugleich
    std::set<int> m_inArbeit;
    //  fixed und sticky: je Schicht ein Bild, gezeigt mit der Lage aus `m_schichten`; nach neuem Legen alle zugleich getauscht.
    std::vector<Schicht> m_schichten;
    std::vector<QImage> m_schichtBilder, m_schichtWartend;
    int m_schichtGen = -1, m_schichtOffen = 0;
    bool m_schichtNeu = false;
    Stelle m_anker, m_fokus;
    bool m_zieht = false;
    bool m_ueberVerweis = false;
    QPointF m_maus;
    QElapsedTimer m_doppelUhr;          // seit dem letzten Doppelklick, ungueltig = keiner offen
    QPointF m_doppelOrt;
    QTimer m_rollTakt;
    QColor m_farbeAuswahl = QColor(51, 142, 255, 90);
    std::vector<Bereich> m_treffer;
    std::vector<float> m_trefferY;      // Oberkante der ersten Zeile je Treffer, Dokumentkoordinaten
    QString m_suchText;
    bool m_suchGross = false, m_suchWoerter = false, m_trefferUeber = false, m_sucht = false;
    int m_trefferIndex = -1, m_suchGen = 0, m_suchRevision = 0;
    std::shared_ptr<std::atomic<bool>> m_suchAbbruch;
    //  Item-Koordinaten: alle Treffer, der aktuelle, die Markierung.
    std::vector<QRectF> m_flaechen[3];
    bool m_flaechenNeu = false;
    //  Nur in updatePaintNode, waehrend der GUI-Faden steht.
    struct Knoten { QSGSimpleTextureNode* bild; QSGSimpleTextureNode* rand; };
    std::map<int, Knoten> m_knoten;
    std::vector<std::pair<size_t, QSGSimpleTextureNode*>> m_schichtKnoten;     // Schicht, Knoten
    QSGRectangleNode* m_grund = nullptr;
    std::vector<QSGRectangleNode*> m_flaechenKnoten[3];
    bool m_knotenVerloren = false;
    //  Legt in Seitenbreite neu, mit eigenen Schriften, die im selben Lauf wieder fallen.
    QThreadPool m_pdfPool;
    std::shared_ptr<std::atomic<bool>> m_pdfAbbruch = std::make_shared<std::atomic<bool>>(false);
    bool m_exporting = false;
    int m_zaehlGen = 0, m_vorschauGen = 0;
    PdfVorschau m_vorschau;
    //  Zaehlen oder Vorschau, bevor das Dokument da ist (Dialog waehrend des Ladens): laeuft, sobald es kommt.
    std::optional<std::pair<PdfArt, PdfOptionen>> m_pdfWartet;
};

}   // namespace mg::html
