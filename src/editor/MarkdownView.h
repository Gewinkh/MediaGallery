#pragma once
#include <QAbstractListModel>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QTextBlock>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QVariantMap>
#include <atomic>
#include <memory>
#include <QColor>
#include <QQuickItem>
// Vollstaendig einbinden: ein Zeiger in einem Q_PROPERTY verlangt den fertigen Typ.
#include <QQuickTextDocument>

#include "core/PdfVorschau.h"
#include "editor/MarkdownPdf.h"


// MarkdownView - gerenderte Markdown-Ansicht je Kachel: baut das Dokument im Arbeitsfaden, schreibt nie.
namespace mg::editor {

// Nur die Kaesten im Sichtfenster: ein Kasten sind rund 25 QML-Objekte.
class MarkdownCodeBoxes : public QAbstractListModel {
    Q_OBJECT
public:
    struct Box {
        QRectF rect;
        QString language;
    };
    enum Role { IndexRole = Qt::UserRole + 1, XRole, YRole, WidthRole, HeightRole, HeaderRole, LanguageRole };

    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setBoxes(QList<Box> boxes, bool neu);
    void setViewport(qreal top, qreal height);

private:
    QList<Box> m_boxes;
    int m_first = 0;
    int m_end = 0;
    qreal m_top = 0;
    qreal m_height = 0;

    std::pair<int, int> fenster() const;
    void geheZu(int first, int end);
};

class MarkdownView : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(bool failed READ failed NOTIFY stateChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY stateChanged)
    //  Zaehlt jedes neu uebergebene Dokument - die Flaeche haelt daran ihren Scrollstand.
    Q_PROPERTY(int revision READ revision NOTIFY stateChanged)
    //  In Koordinaten der TextEdit, neu je Layout; `frontMatterBox` ist leer ohne Frontmatter.
    Q_PROPERTY(QVariantList codeBlocks READ codeBlocks NOTIFY decorationsChanged)
    Q_PROPERTY(QVariantMap frontMatterBox READ frontMatterBox NOTIFY decorationsChanged)
    Q_PROPERTY(QAbstractItemModel* codeBoxes READ codeBoxes CONSTANT)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)

public:
    explicit MarkdownView(QObject* parent = nullptr);
    ~MarkdownView() override;

    QString source() const { return m_source; }
    void    setSource(const QString& pathOrUrl);

    QQuickTextDocument* document() const { return m_quickDoc; }
    void setDocument(QQuickTextDocument* d);

    bool busy() const { return m_busy; }
    bool ready() const { return m_ready; }
    bool failed() const { return m_failed; }
    bool truncated() const { return m_truncated; }
    int  revision() const { return m_revision; }
    QVariantList codeBlocks() const;
    QVariantMap frontMatterBox() const;
    QAbstractItemModel* codeBoxes() { return &m_boxes; }
    const QList<md::TableGrid>& tableGrids() const { return m_grids; }
    const md::RenderedDocument* shownDocument() const;
    QColor tableLineColor() const;

    Q_INVOKABLE void setViewport(qreal top, qreal height) { m_boxes.setViewport(top, height); }
    Q_INVOKABLE void reload();
    Q_INVOKABLE void toggleDetails(int id);
    //  { kind: details|anchor|file|url|none, id, position, path, url }; relativ gilt ab dem Ordner der Datei.
    Q_INVOKABLE QVariantMap resolveLink(const QString& href) const;
    Q_INVOKABLE bool copyCode(int index) const;
    //  Quellzeile an einer Dokumentstelle und zurueck, je -1 ohne Dokument: der Wechsel zum Rohtext haelt die Stelle.
    Q_INVOKABLE int sourceLineAt(int position) const;
    Q_INVOKABLE int positionOfSourceLine(int line) const;

    //  Als PDF, so wie die Ansicht gerade steht (aufgeklappte Hinweise eingeschlossen). `print` = Schwarz/Weiss.
    bool exporting() const { return m_exporting; }
    Q_INVOKABLE QString pdfTarget() const;
    Q_INVOKABLE void countPdfPages(bool print, bool landscape);
    Q_INVOKABLE void exportPdf(const QString& target, bool print, bool landscape, int firstPage, int lastPage,
                               const QList<int>& pages = {});
    //  Das ganze PDF in eine Vorschau-Datei, an deren Seiten man auswaehlt -> `pdfPreviewReady`.
    Q_INVOKABLE void previewPdf(bool print, bool landscape);
    //  Fuer Ansicht, PDF und den Export aus der Galerie.
    static md::RenderStyle styleFor(const QString& source, const QSet<int>& flipped);

signals:
    void sourceChanged();
    void documentChanged();
    void stateChanged();
    void decorationsChanged();
    void exportingChanged();
    //  Kein Parameter namens `print`: er verdeckt in QML die gleichnamige Funktion.
    void pdfPagesCounted(int pages);
    void pdfPreviewReady(const QString& path, int pages);
    void pdfExportFinished(bool ok, const QString& target, const QString& error);

private:
    QString m_source;
    QPointer<QQuickTextDocument> m_quickDoc;
    //  Das zuletzt uebergebene Dokument. Kind der TextEdit, damit es nie vor ihr stirbt.
    QPointer<md::RenderedDocument> m_ownDoc;
    MarkdownCodeBoxes m_boxes;
    QList<md::TableGrid> m_grids;
    bool m_busy = false;
    bool m_ready = false;
    bool m_failed = false;
    bool m_truncated = false;
    int  m_revision = 0;
    int  m_generation = 0;
    //  Rohtext der Datei; ein Umklappen oder Farbwechsel baut daraus neu, ohne die Platte zu fragen.
    QString m_text;
    bool m_textValid = false;
    QSet<int> m_flipped;

    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_abort;
    //  Eigener Faden: ein langer Export soll das Neuaufbauen der Ansicht nicht aufhalten.
    QThreadPool m_pdfPool;
    std::shared_ptr<std::atomic<bool>> m_pdfAbort = std::make_shared<std::atomic<bool>>(false);
    bool m_exporting = false;
    int  m_countGeneration = 0;

    void start(bool readFile);
    md::RenderStyle renderStyle() const;
    enum class PdfArt { Schreiben, Zaehlen, Vorschau };
    void runPdf(PdfArt art, const QString& target, const md::PdfOptions& opt, int generation);
    mg::PdfVorschau m_vorschau;
    int m_vorschauGen = 0;
    void adopt(md::RenderedDocument* doc, int generation, const QString& text, bool readFile, bool ok,
               bool truncated);
    void updateBoxes(bool neu);
    int anchorPosition(const QString& name) const;
};

// Die Tabellenlinien der Ansicht in EINEM Knoten, nur nahe dem Sichtfenster. Liegt deckungsgleich auf der TextEdit.
class MarkdownGrid : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(mg::editor::MarkdownView* markdownView READ view WRITE setView NOTIFY viewChanged)
    //  Sichtfenster in Koordinaten der TextEdit, wie `MarkdownView::setViewport`.
    Q_PROPERTY(qreal viewTop READ viewTop WRITE setViewTop NOTIFY viewportChanged)
    Q_PROPERTY(qreal viewHeight READ viewHeight WRITE setViewHeight NOTIFY viewportChanged)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY linesChanged)

public:
    explicit MarkdownGrid(QQuickItem* parent = nullptr);

    MarkdownView* view() const { return m_view; }
    void setView(MarkdownView* v);
    qreal viewTop() const { return m_top; }
    void  setViewTop(qreal v);
    qreal viewHeight() const { return m_height; }
    void  setViewHeight(qreal v);
    int lineCount() const { return int(m_lines.size()); }

signals:
    void viewChanged();
    void viewportChanged();
    void linesChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
    QPointer<MarkdownView> m_view;
    QMetaObject::Connection m_conn;
    qreal m_top = 0;
    qreal m_height = 0;
    //  Gebauter Bereich: erst wenn das Fenster ihn verlaesst, entstehen die Linien neu.
    qreal m_von = 0;
    qreal m_bis = -1;
    QList<QRectF> m_lines;
    QColor m_color;

    void rebuild(bool erzwungen);
};

// Zeichnet die Bloecke nahe dem Sichtfenster selbst; die TextEdit darin legt aus, markiert und kennt die Verweise, zeichnet
// aber nichts. Qts eigene baute in jeder Tabelle, auf die noch ein Rahmen folgt, alle Zellen bis zu deren Ende.
class MarkdownViewport : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(mg::editor::MarkdownView* markdownView READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(QQuickItem* textItem READ textItem WRITE setTextItem NOTIFY textItemChanged)
    Q_PROPERTY(qreal viewTop READ viewTop WRITE setViewTop NOTIFY viewportChanged)
    Q_PROPERTY(qreal viewHeight READ viewHeight WRITE setViewHeight NOTIFY viewportChanged)
    Q_PROPERTY(int moves READ moves NOTIFY movesChanged)
    Q_PROPERTY(int blockCount READ blockCount NOTIFY movesChanged)

public:
    explicit MarkdownViewport(QQuickItem* parent = nullptr);
    ~MarkdownViewport() override;

    MarkdownView* view() const { return m_view; }
    void setView(MarkdownView* v);
    QQuickItem* textItem() const { return m_text; }
    void setTextItem(QQuickItem* t);
    qreal viewTop() const { return m_top; }
    void  setViewTop(qreal v);
    qreal viewHeight() const { return m_height; }
    void  setViewHeight(qreal v);
    int moves() const { return m_moves; }
    int blockCount() const { return int(m_bloecke.size()); }

signals:
    void viewChanged();
    void textItemChanged();
    void viewportChanged();
    void movesChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private slots:
    void readText();

private:
    struct Stueck {
        QTextBlock block;
        QPointF pos;
        int nummer;
    };
    QPointer<MarkdownView> m_view;
    QPointer<QQuickItem> m_text;
    QList<QMetaObject::Connection> m_conns;
    QList<QMetaObject::Connection> m_textConns;
    qreal m_top = 0;
    qreal m_height = 0;
    qreal m_von = 0;
    qreal m_bis = -1;
    int m_moves = 0;
    int m_revision = -1;          // Dokument, aus dem die Liste stammt
    int m_stand = 0;              // erhoeht, wenn gebaute Teile nicht mehr taugen: neue Lage oder Farben
    QList<Stueck> m_bloecke;
    QList<QTextFrame*> m_rahmen;  // Rahmen mit Hintergrund: Linien, Zitate, Details
    QList<std::pair<QRectF, QColor>> m_flaechen;
    int m_selVon = 0;
    int m_selBis = 0;
    QColor m_selFarbe;
    QColor m_selText;
    int m_renderType = 0;

    void follow(bool erzwungen);
    void sammle(const md::RenderedDocument& doc);
};

}  // namespace mg::editor
