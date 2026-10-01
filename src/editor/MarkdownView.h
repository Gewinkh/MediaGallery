#pragma once
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QVariantMap>
#include <atomic>
#include <memory>
// Vollstaendig einbinden: ein Zeiger in einem Q_PROPERTY verlangt den fertigen Typ.
#include <QQuickTextDocument>

class QTextDocument;
class QTextFrame;
namespace mg::editor::md { struct RenderStyle; }

// MarkdownView - gerenderte Markdown-Ansicht je Kachel: baut das Dokument im Arbeitsfaden, schreibt nie.
namespace mg::editor {

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

    Q_INVOKABLE void reload();
    Q_INVOKABLE void toggleDetails(int id);
    //  { kind: details|anchor|file|url|none, id, position, path, url }; relativ gilt ab dem Ordner der Datei.
    Q_INVOKABLE QVariantMap resolveLink(const QString& href) const;
    Q_INVOKABLE bool copyCode(int index) const;

    //  Als PDF, so wie die Ansicht gerade steht (aufgeklappte Hinweise eingeschlossen). `print` = Schwarz/Weiss.
    bool exporting() const { return m_exporting; }
    Q_INVOKABLE QString pdfTarget() const;
    Q_INVOKABLE void countPdfPages(bool print, bool landscape);
    Q_INVOKABLE void exportPdf(const QString& target, bool print, bool landscape, int firstPage, int lastPage);
    //  Schriften, Palette und Beschriftungen fuer eine Datei - Ansicht, PDF und der Export aus der Galerie.
    static md::RenderStyle styleFor(const QString& source, const QSet<int>& flipped);

signals:
    void sourceChanged();
    void documentChanged();
    void stateChanged();
    void decorationsChanged();
    void exportingChanged();
    //  Ohne weitere Parameter: einer namens `print` verdeckt in QML die gleichnamige Funktion, der Empfaenger lief nie.
    void pdfPagesCounted(int pages);
    void pdfExportFinished(bool ok, const QString& target, const QString& error);

private:
    QString m_source;
    QPointer<QQuickTextDocument> m_quickDoc;
    //  Das zuletzt uebergebene Dokument. Kind der TextEdit, damit es nie vor ihr stirbt.
    QPointer<QTextDocument> m_ownDoc;
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
    void runPdf(const QString& target, bool print, bool landscape, int first, int last, int countGeneration);
    void adopt(QTextDocument* doc, int generation, const QString& text, bool readFile, bool ok,
               bool truncated);
    int anchorPosition(const QString& name) const;
    QList<QTextFrame*> codeFrames() const;
};

}  // namespace mg::editor
