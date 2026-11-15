#include "editor/MarkdownView.h"

#include "core/AppSettings.h"
#include "core/PathUtils.h"
#include "core/Strings.h"
#include "core/TextEncoding.h"
#include "editor/EditorController.h"
#include "editor/MarkdownRender.h"
#include "editor/TextPdfExporter.h"

#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QInputMethod>
#include <QRunnable>
#include <QTextBlock>
#include <QQuickItem>
#include <QQuickWindow>
#include <QFontMetricsF>
#include <QMetaMethod>
#include <QSGImageNode>
#include <QSGRectangleNode>
#include <QSGTextNode>
#include <QSGTransformNode>
#include <QSGTexture>
#include <QTextCursor>
#include <QTextLayout>
#include <QTextList>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>
#include <QThread>
#include <QUrl>
#include <algorithm>
#include <functional>
#include <tuple>

namespace mg::editor {
namespace {

constexpr qint64 kMaxBytes = 8LL * 1024 * 1024;   // derselbe Deckel wie im Texteditor
constexpr int kFontPx = 15;
// Mitschnitt jedes Einhaengens - fuer Darstellungsfehler, die nur in der laufenden App auftreten.
const bool kLog = qEnvironmentVariableIntValue("MG_LOG_MD") == 1;

struct Result {
    md::RenderedDocument* doc = nullptr;
    QString text;
    bool ok = true;
    bool truncated = false;
    // Kommt das Ergebnis nie an (Kachel geschlossen), faellt das Dokument hier - im GUI-Faden, dem es gehoert.
    ~Result() { delete doc; }
};

class RenderTask : public QRunnable {
public:
    RenderTask(QObject* owner, QString path, QString text, bool readFile, md::RenderStyle style, md::LayoutVorgabe vorgabe,
               std::shared_ptr<std::atomic<bool>> abort, std::function<void(std::shared_ptr<Result>)> back)
        : m_owner(owner), m_target(owner->thread()), m_path(std::move(path)), m_text(std::move(text)),
          m_readFile(readFile), m_style(std::move(style)), m_vorgabe(vorgabe), m_abort(std::move(abort)),
          m_back(std::move(back)) {
        setAutoDelete(true);
    }

    void run() override {
        auto r = std::make_shared<Result>();
        if (m_readFile) {
            QFile f(m_path);
            if (!f.open(QIODevice::ReadOnly)) {
                r->ok = false;
            } else {
                const QByteArray roh = f.read(kMaxBytes);
                r->truncated = f.size() > kMaxBytes;
                r->text = mg::decodeUnknownText(roh);
            }
        } else {
            r->text = std::move(m_text);
        }
        if (m_abort->load()) return;
        if (r->ok) {
            const md::Document doc = md::parse(r->text);
            if (m_abort->load()) return;
            md::RenderedDocument* d = md::buildDocument(doc, m_style, m_abort.get());
            md::layoutDocument(*d, m_vorgabe, m_abort.get());
            if (m_abort->load()) {
                delete d;
                return;
            }
            d->moveToThread(m_target);
            r->doc = d;
        }
        auto back = m_back;
        QMetaObject::invokeMethod(m_owner, [back, r] { back(r); }, Qt::QueuedConnection);
    }

private:
    QObject* m_owner;
    QThread* m_target;
    QString m_path;
    QString m_text;
    bool m_readFile;
    md::RenderStyle m_style;
    md::LayoutVorgabe m_vorgabe;
    std::shared_ptr<std::atomic<bool>> m_abort;
    std::function<void(std::shared_ptr<Result>)> m_back;
};

}  // namespace

int MarkdownCodeBoxes::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_end - m_first; }

QVariant MarkdownCodeBoxes::data(const QModelIndex& index, int role) const {
    const int i = m_first + index.row();
    if (!index.isValid() || i < m_first || i >= m_end) return {};
    const Box& b = m_boxes[i];
    switch (role) {
    case IndexRole:    return i;
    case XRole:        return b.rect.x();
    case YRole:        return b.rect.y();
    case WidthRole:    return b.rect.width();
    case HeightRole:   return b.rect.height();
    case HeaderRole:   return md::kCodeHeader;
    case LanguageRole: return b.language;
    default:           return {};
    }
}

QHash<int, QByteArray> MarkdownCodeBoxes::roleNames() const {
    return {{IndexRole, "boxIndex"}, {XRole, "boxX"}, {YRole, "boxY"}, {WidthRole, "boxWidth"},
            {HeightRole, "boxHeight"}, {HeaderRole, "boxHeader"}, {LanguageRole, "boxLanguage"}};
}

//  Beide Kanten wachsen mit dem Index: die Kaesten stehen in Dokumentreihenfolge und ueberlappen nicht. Der Saum baut
//  einen Kasten, bevor er ins Bild rollt.
std::pair<int, int> MarkdownCodeBoxes::fenster() const {
    constexpr qreal kSaum = 200;
    const qreal oben = m_top - kSaum;
    const qreal unten = m_top + m_height + kSaum;
    const auto anfang = std::partition_point(m_boxes.cbegin(), m_boxes.cend(),
                                             [&](const Box& b) { return b.rect.bottom() <= oben; });
    const auto ende = std::partition_point(anfang, m_boxes.cend(), [&](const Box& b) { return b.rect.top() < unten; });
    return {int(anfang - m_boxes.cbegin()), int(ende - m_boxes.cbegin())};
}

void MarkdownCodeBoxes::geheZu(int first, int end) {
    const md::FensterWechsel w = md::fensterWechsel(m_first, m_end, first, end);
    if (w.vorneWeg > 0) {
        beginRemoveRows({}, 0, w.vorneWeg - 1);
        m_first += w.vorneWeg;
        endRemoveRows();
    }
    if (w.hintenWeg > 0) {
        beginRemoveRows({}, rowCount() - w.hintenWeg, rowCount() - 1);
        m_end -= w.hintenWeg;
        endRemoveRows();
    }
    if (w.springe) m_first = m_end = first;
    if (w.vorneNeu > 0) {
        beginInsertRows({}, 0, w.vorneNeu - 1);
        m_first -= w.vorneNeu;
        endInsertRows();
    }
    if (w.hintenNeu > 0) {
        beginInsertRows({}, rowCount(), rowCount() + w.hintenNeu - 1);
        m_end += w.hintenNeu;
        endInsertRows();
    }
}

void MarkdownCodeBoxes::setBoxes(QList<Box> boxes, bool neu) {
    if (neu) {
        beginResetModel();
        m_boxes = std::move(boxes);
        std::tie(m_first, m_end) = fenster();
        endResetModel();
        return;
    }
    m_boxes = std::move(boxes);
    const auto [first, end] = fenster();
    geheZu(first, end);
    if (rowCount() > 0) emit dataChanged(index(0), index(rowCount() - 1));
}

void MarkdownCodeBoxes::setViewport(qreal top, qreal height) {
    m_top = top;
    m_height = height;
    const auto [first, end] = fenster();
    geheZu(first, end);
}

MarkdownView::MarkdownView(QObject* parent) : QObject(parent) {
    m_pool.setMaxThreadCount(1);
    //  Jeder Faden haelt eigene Schriften samt Formungstabellen: sie gehen mit ihm, nicht erst nach Qts 30 s.
    m_pool.setExpiryTimeout(0);
    m_pdfPool.setMaxThreadCount(1);
    m_pdfPool.setExpiryTimeout(0);
    connect(&AppSettings::instance(), &AppSettings::languageChanged, this, [this] { start(false); });
    if (EditorController* c = activeController())
        connect(c, &EditorController::paletteChanged, this, [this] { start(false); });
}

MarkdownView::~MarkdownView() {
    if (m_abort) m_abort->store(true);
    m_pdfAbort->store(true);
    m_pool.waitForDone();
    m_pdfPool.waitForDone();
}

void MarkdownView::setSource(const QString& pathOrUrl) {
    const QString pfad = mg::toLocalPath(pathOrUrl);
    if (pfad == m_source) return;
    m_source = pfad;
    m_text.clear();
    m_textValid = false;
    m_flipped.clear();
    m_truncated = false;
    m_failed = false;
    emit sourceChanged();
    if (pfad.isEmpty()) {
        //  Die Kachel wird freigegeben: der Inhalt geht sofort, nicht erst mit der naechsten Datei.
        //  Erst die Griffe in die Bloecke loslassen: `clear` meldet eine neue Groesse, und die fragt nach den Kaesten.
        m_ready = false;
        updateBoxes(true);
        if (m_ownDoc) {
            m_ownDoc->codeBoxes.clear();
            m_ownDoc->tables.clear();
            m_ownDoc->metaFrame = nullptr;
            m_ownDoc->clear();
        }
        ++m_vorschauGen;
        m_vorschau.leere();
        emit decorationsChanged();
    }
    start(true);
}

void MarkdownView::setDocument(QQuickTextDocument* d) {
    if (d == m_quickDoc) return;
    m_quickDoc = d;
    m_ownDoc = nullptr;
    //  Qt baut beim Auswahlwechsel Kind-Rahmen (Code, Tabellen) ab Position 0 neu und laesst die alten Knoten stehen: Text
    //  liegt doppelt und wirkt fett. Privater Slot, daher die Textform; selectionStart/End schweigen beim Loslassen.
    if (QObject* edit = d ? d->parent() : nullptr) {
        connect(edit, SIGNAL(selectedTextChanged()), edit, SLOT(updateWholeDocument()), Qt::UniqueConnection);
        connect(edit, SIGNAL(cursorPositionChanged()), edit, SLOT(updateWholeDocument()), Qt::UniqueConnection);
    }
    emit documentChanged();
    start(!m_textValid);
}

void MarkdownView::reload() { start(true); }

void MarkdownView::toggleDetails(int id) {
    if (!m_flipped.remove(id)) m_flipped.insert(id);
    start(false);
}

void MarkdownView::start(bool readFile) {
    if (m_abort) m_abort->store(true);
    ++m_generation;
    if (m_source.isEmpty() || !m_quickDoc) {
        if (m_busy) {
            m_busy = false;
            emit stateChanged();
        }
        return;
    }
    if (!m_textValid) readFile = true;

    md::RenderStyle st = renderStyle();
    st.qtTableLines = false;

    if (kLog) qInfo("[md] start lesen=%d gen=%d %s", int(readFile), m_generation, qPrintable(m_source));
    m_abort = std::make_shared<std::atomic<bool>>(false);
    m_busy = true;
    emit stateChanged();
    const int gen = m_generation;
    auto* self = this;
    //  Stand von jetzt: stimmen Breite und Option beim Einhaengen noch, legt die TextEdit nichts mehr aus.
    md::LayoutVorgabe vorgabe;
    if (const auto* edit = qobject_cast<QQuickItem*>(m_quickDoc->parent())) vorgabe.width = edit->width();
    vorgabe.option = m_quickDoc->textDocument()->defaultTextOption();
    vorgabe.ohneRichtung = QGuiApplication::inputMethod()->inputDirection();
    m_pool.start(new RenderTask(this, m_source, readFile ? QString() : m_text, readFile, std::move(st), vorgabe, m_abort,
                                [self, gen, readFile](std::shared_ptr<Result> r) {
                                    self->adopt(std::exchange(r->doc, nullptr), gen, r->text, readFile, r->ok,
                                                r->truncated);
                                }));
}

md::RenderStyle MarkdownView::renderStyle() const { return styleFor(m_source, m_flipped); }

md::RenderStyle MarkdownView::styleFor(const QString& source, const QSet<int>& flipped) {
    md::RenderStyle st;
    st.body = QGuiApplication::font();
    st.body.setPixelSize(kFontPx);
    //  Latein aus der Festbreitenschrift, alles andere faellt je Zeichen auf die Familien der App zurueck.
    st.mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    QStringList familien{st.mono.family()};
    familien += st.body.families().isEmpty() ? QStringList{st.body.family()} : st.body.families();
    st.mono.setFamilies(familien);
    st.mono.setPixelSize(kFontPx);
    st.palette = activeController() ? activeController()->palette() : paletteForProfile(EditorProfile::Nightfall);
    st.baseDir = QFileInfo(source).absolutePath();
    st.flipped = flipped;
    st.showLabel = Strings::get(StringKey::MarkdownDetailsShow);
    st.hideLabel = Strings::get(StringKey::MarkdownDetailsHide);
    return st;
}

QString MarkdownView::pdfTarget() const { return TextPdf::targetPathFor(m_source); }

void MarkdownView::countPdfPages(bool print, bool landscape) {
    md::PdfOptions o;
    o.print = print;
    o.landscape = landscape;
    runPdf(PdfArt::Zaehlen, QString(), o, ++m_countGeneration);
}

void MarkdownView::previewPdf(bool print, bool landscape) {
    const QString ziel = m_vorschau.neu();
    if (ziel.isEmpty()) return;
    md::PdfOptions o;
    o.print = print;
    o.landscape = landscape;
    runPdf(PdfArt::Vorschau, ziel, o, ++m_vorschauGen);
}

void MarkdownView::exportPdf(const QString& target, bool print, bool landscape, int firstPage, int lastPage,
                             const QList<int>& pages) {
    if (m_exporting || target.isEmpty() || !m_textValid) {
        QMetaObject::invokeMethod(this, [this, target] {
            emit pdfExportFinished(false, target, Strings::get(StringKey::MarkdownPdfNotReady));
        }, Qt::QueuedConnection);
        return;
    }
    m_exporting = true;
    emit exportingChanged();
    md::PdfOptions o;
    o.print = print;
    o.landscape = landscape;
    o.firstPage = firstPage;
    o.lastPage = lastPage;
    o.pages = pages;
    runPdf(PdfArt::Schreiben, mg::toLocalPath(target), o, 0);
}

// Zaehlen, Vorschau und Schreiben teilen sich den Faden: alle legen das Dokument aus, und zwei auf einmal
// kosteten nur RAM.
void MarkdownView::runPdf(PdfArt art, const QString& target, const md::PdfOptions& opt, int generation) {
    class PdfTask : public QRunnable {
    public:
        std::function<void()> work;
        void run() override { work(); }
    };
    auto* t = new PdfTask;
    t->setAutoDelete(true);
    QPointer<MarkdownView> self(this);
    t->work = [self, art, text = m_text, st = renderStyle(), opt, target, generation, abort = m_pdfAbort] {
        QString err;
        const int seiten = md::writePdf(md::parse(text), st, opt, target, &err, abort.get());
        if (abort->load()) return;
        QMetaObject::invokeMethod(self, [self, art, seiten, err, target, generation] {
            if (!self) return;
            switch (art) {
            case PdfArt::Zaehlen:
                //  Nur die juengste Zaehlung zaehlt - der Dialog fragt bei jedem Umschalten neu.
                if (generation == self->m_countGeneration) emit self->pdfPagesCounted(seiten);
                return;
            case PdfArt::Vorschau:
                if (generation != self->m_vorschauGen || seiten <= 0) {
                    self->m_vorschau.verwerfe(target);
                    return;
                }
                self->m_vorschau.uebernehme(target);
                emit self->pdfPreviewReady(target, seiten);
                return;
            case PdfArt::Schreiben:
                self->m_exporting = false;
                emit self->exportingChanged();
                emit self->pdfExportFinished(seiten > 0, target, err);
                return;
            }
        }, Qt::QueuedConnection);
    };
    m_pdfPool.start(t);
}

void MarkdownView::adopt(md::RenderedDocument* doc, int generation, const QString& text, bool readFile, bool ok,
                         bool truncated) {
    if (generation != m_generation) {
        delete doc;
        return;
    }
    m_busy = false;
    m_failed = !ok;
    if (readFile) {
        m_text = text;
        m_textValid = ok;
        m_truncated = truncated;
    }
    if (doc && m_quickDoc) {
        //  Kind der TextEdit: sie raeumt es mit sich ab. Das eigene Dokument der TextEdit loescht Qt beim ersten
        //  Tausch selbst - geloescht wird hier nur ein zuvor uebergebenes.
        doc->setParent(m_quickDoc->parent());
        md::RenderedDocument* alt = m_ownDoc;
        m_quickDoc->setTextDocument(doc);
        m_ownDoc = doc;
        delete alt;
        m_ready = true;
        ++m_revision;
        connect(doc->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, [this] {
            updateBoxes(false);
            emit decorationsChanged();
        });
        if (kLog) {
            const auto* edit = qobject_cast<QQuickItem*>(doc->parent());
            qInfo("[md] eingehaengt rev=%d zeichen=%d rahmen=%lld breite=%.1f textbreite=%.1f x=%.2f", m_revision,
                  doc->characterCount(), static_cast<long long>(doc->rootFrame()->childFrames().size()),
                  edit ? edit->width() : -1.0, doc->textWidth(), edit ? edit->x() : -1.0);
        }
        updateBoxes(true);
        emit decorationsChanged();
    } else {
        delete doc;
    }
    emit stateChanged();
}

QVariantList MarkdownView::codeBlocks() const {
    QVariantList out;
    if (!m_ownDoc || !m_ready) return out;
    for (const md::CodeBox& k : m_ownDoc->codeBoxes) {
        const QRectF r = md::codeBoxRect(*m_ownDoc, k);
        QVariantMap m;
        m.insert(QStringLiteral("x"), r.x());
        m.insert(QStringLiteral("y"), r.y());
        m.insert(QStringLiteral("width"), r.width());
        m.insert(QStringLiteral("height"), r.height());
        m.insert(QStringLiteral("language"), k.language);
        m.insert(QStringLiteral("header"), md::kCodeHeader);
        out << m;
    }
    return out;
}

void MarkdownView::updateBoxes(bool neu) {
    QList<MarkdownCodeBoxes::Box> boxes;
    if (m_ownDoc && m_ready) {
        boxes.reserve(m_ownDoc->codeBoxes.size());
        for (const md::CodeBox& k : m_ownDoc->codeBoxes) boxes.append({md::codeBoxRect(*m_ownDoc, k), k.language});
    }
    m_boxes.setBoxes(std::move(boxes), neu);
    m_grids.clear();
    if (m_ownDoc && m_ready) {
        m_grids.reserve(m_ownDoc->tables.size());
        for (QTextTable* t : m_ownDoc->tables) m_grids.append(md::tableGrid(*m_ownDoc, t));
    }
}

QColor MarkdownView::tableLineColor() const { return m_ownDoc ? m_ownDoc->tableLine : QColor(); }

MarkdownGrid::MarkdownGrid(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents); }

void MarkdownGrid::setView(MarkdownView* v) {
    if (v == m_view) return;
    disconnect(m_conn);
    m_view = v;
    if (v) m_conn = connect(v, &MarkdownView::decorationsChanged, this, [this] { rebuild(true); });
    emit viewChanged();
    rebuild(true);
}

void MarkdownGrid::setViewTop(qreal v) {
    if (qFuzzyCompare(v, m_top)) return;
    m_top = v;
    emit viewportChanged();
    rebuild(false);
}

void MarkdownGrid::setViewHeight(qreal v) {
    if (qFuzzyCompare(v, m_height)) return;
    m_height = v;
    emit viewportChanged();
    rebuild(false);
}

//  Ein Fenster Vorlauf nach oben und unten: beim Rollen entsteht der Knoten nur alle paar Bildschirme neu.
void MarkdownGrid::rebuild(bool erzwungen) {
    if (!erzwungen && m_top >= m_von && m_top + m_height <= m_bis) return;
    const bool hatte = !m_lines.isEmpty();
    m_von = m_top - m_height;
    m_bis = m_top + 2 * m_height;
    m_lines.clear();
    if (m_view) {
        md::gridLines(m_view->tableGrids(), m_von, m_bis, m_lines);
        m_color = m_view->tableLineColor();
    }
    if (hatte || !m_lines.isEmpty()) {
        update();
        emit linesChanged();
    }
}

//  Rechteckknoten wie Qts eigene Raender: eigene Geometrie liesse der Software-Renderer still weg.
QSGNode* MarkdownGrid::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    if (m_lines.isEmpty() || !window()) {
        delete old;
        return nullptr;
    }
    QSGNode* wurzel = old ? old : new QSGNode;
    QSGNode* k = wurzel->firstChild();
    for (const QRectF& r : m_lines) {
        auto* linie = static_cast<QSGRectangleNode*>(k);
        if (!linie) {
            linie = window()->createRectangleNode();
            wurzel->appendChildNode(linie);
        }
        linie->setRect(r);
        linie->setColor(m_color);
        k = linie->nextSibling();
    }
    while (k) {
        QSGNode* weiter = k->nextSibling();
        wurzel->removeChildNode(k);
        delete k;
        k = weiter;
    }
    return wurzel;
}

const md::RenderedDocument* MarkdownView::shownDocument() const { return m_ready ? m_ownDoc.data() : nullptr; }

MarkdownViewport::MarkdownViewport(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents); }

MarkdownViewport::~MarkdownViewport() { setTextItem(nullptr); }

void MarkdownViewport::setView(MarkdownView* v) {
    if (v == m_view) return;
    for (const auto& c : std::as_const(m_conns)) disconnect(c);
    m_conns.clear();
    m_view = v;
    if (v) {
        //  Neues Dokument oder neue Breite: die Liste zeigte sonst auf verworfene Bloecke bzw. alte Lagen.
        m_conns << connect(v, &MarkdownView::decorationsChanged, this, [this] { follow(true); })
                << connect(v, &MarkdownView::stateChanged, this, [this] {
                       if (m_view && (!m_view->shownDocument() || m_view->revision() != m_revision)) follow(true);
                   });
    }
    emit viewChanged();
    follow(true);
}

void MarkdownViewport::setTextItem(QQuickItem* t) {
    if (t == m_text) return;
    for (const auto& c : std::as_const(m_textConns)) disconnect(c);
    m_textConns.clear();
    if (m_text) m_text->setFlag(ItemHasContents, true);
    m_text = t;
    if (t) {
        t->setFlag(ItemHasContents, false);
        const QMetaObject* mo = t->metaObject();
        const QMetaMethod ziel = metaObject()->method(metaObject()->indexOfSlot("readText()"));
        for (const char* name : {"selectionStart", "selectionEnd", "selectionColor", "selectedTextColor", "renderType"}) {
            const QMetaProperty p = mo->property(mo->indexOfProperty(name));
            if (p.hasNotifySignal()) m_textConns << connect(t, p.notifySignal(), this, ziel);
        }
        readText();
    }
    emit textItemChanged();
}

void MarkdownViewport::readText() {
    if (!m_text) return;
    m_selVon = m_text->property("selectionStart").toInt();
    m_selBis = m_text->property("selectionEnd").toInt();
    const QColor farbe = m_text->property("selectionColor").value<QColor>();
    const QColor text = m_text->property("selectedTextColor").value<QColor>();
    const int art = m_text->property("renderType").toInt();
    if (farbe != m_selFarbe || text != m_selText || art != m_renderType) ++m_stand;
    m_selFarbe = farbe;
    m_selText = text;
    m_renderType = art;
    update();
}

void MarkdownViewport::setViewTop(qreal v) {
    if (qFuzzyCompare(v, m_top)) return;
    m_top = v;
    emit viewportChanged();
    follow(false);
}

void MarkdownViewport::setViewHeight(qreal v) {
    if (qFuzzyCompare(v, m_height)) return;
    m_height = v;
    emit viewportChanged();
    follow(false);
}

//  Je ein Fenster Vorlauf; der Bereich rueckt schon ein halbes Fenster vor seinem Rand weiter.
void MarkdownViewport::follow(bool erzwungen) {
    const md::RenderedDocument* doc = m_view ? m_view->shownDocument() : nullptr;
    if (!doc || m_height <= 0) {
        if (!m_bloecke.isEmpty() || !m_flaechen.isEmpty()) update();
        m_bloecke.clear();
        m_flaechen.clear();
        m_von = 0;
        m_bis = -1;
        return;
    }
    const qreal rand = m_height / 2;
    if (!erzwungen && m_top - rand >= m_von && m_top + m_height + rand <= m_bis) return;
    //  Erzwungen heisst neue Lage der Bloecke (Dokument, Breite): bestehende Teile taugen nicht mehr.
    if (erzwungen) ++m_stand;
    m_von = m_top - m_height;
    m_bis = m_top + 2 * m_height;
    sammle(*doc);
    ++m_moves;
    emit movesChanged();
    update();
}

void MarkdownViewport::sammle(const md::RenderedDocument& doc) {
    const QAbstractTextDocumentLayout* lay = doc.documentLayout();
    if (m_revision != m_view->revision()) {
        m_revision = m_view->revision();
        m_rahmen.clear();
        std::function<void(QTextFrame*)> rein = [&](QTextFrame* f) {
            for (QTextFrame* k : f->childFrames()) {
                if (!qobject_cast<QTextTable*>(k) && k->frameFormat().background().style() != Qt::NoBrush)
                    m_rahmen.append(k);
                rein(k);
            }
        };
        rein(doc.rootFrame());
    }
    m_flaechen.clear();
    for (QTextFrame* f : std::as_const(m_rahmen)) {
        const QTextFrameFormat ff = f->frameFormat();
        const QRectF r = lay->frameBoundingRect(f).adjusted(ff.leftMargin(), ff.topMargin(), -ff.rightMargin(),
                                                             -ff.bottomMargin());
        //  Auf ganze Pixel wie Qts Textflaeche: eine Linie bei y = 146,25 fuellt beide Zeilen statt zwei halber.
        if (r.bottom() >= m_von && r.top() <= m_bis) m_flaechen.append({r.toAlignedRect(), ff.background().color()});
    }

    m_bloecke.clear();
    QTextBlock b = doc.findBlock(qMax(0, lay->hitTest(QPointF(0, qMax<qreal>(0, m_von)), Qt::FuzzyHit)));
    //  Zurueck zu Zellen weiter vorn in derselben Tabellenzeile; im Fluss endet das nach einem Block.
    for (int n = 0; n < 1024 && b.previous().isValid() && lay->blockBoundingRect(b.previous()).bottom() > m_von; ++n)
        b = b.previous();
    for (; b.isValid(); b = b.next()) {
        const QRectF r = lay->blockBoundingRect(b);
        if (r.top() > m_bis) {
            //  Eine Zelle mit mehreren Bloecken ragt unten hinaus, die Nachbarzellen derselben Zeile beginnen noch im Bereich.
            const QTextTable* t = QTextCursor(b).currentTable();
            if (!t || lay->blockBoundingRect(t->cellAt(b.position()).firstCursorPosition().block()).top() > m_bis) break;
            continue;
        }
        if (r.bottom() < m_von || !b.isVisible()) continue;
        m_bloecke.append({b, r.topLeft(), b.blockNumber()});
    }
}

namespace {

// Wurzel der eigenen Zeichnung: Flaechen unten, darueber je Block ein Teil, der bleibt, solange der Block im Bereich
// liegt. Transformknoten statt schlichter: Qts Software-Renderer kennt nur deren Lage, neue Kinder eines schlichten
// zeichnete er ohne die Lage der Ansicht. Die Bildtexturen bleiben bis zum naechsten Dokument.
class Zeichnung : public QSGTransformNode {
public:
    Zeichnung() {
        appendChildNode(flaechen);
        appendChildNode(text);
    }
    ~Zeichnung() override { qDeleteAll(bilder); }
    struct Teil {
        QSGNode* knoten;
        int von, bis, erste, anzahl;   // Markierung und Zeilenausschnitt, mit denen er gebaut ist
    };
    QSGTransformNode* flaechen = new QSGTransformNode;
    QSGTransformNode* text = new QSGTransformNode;
    QHash<int, Teil> teile;            // nach Blocknummer
    QHash<QString, QSGTexture*> bilder;
    int revision = -1;
    int stand = -1;

    void weg(QHash<int, Teil>::iterator it) {
        text->removeChildNode(it->knoten);
        delete it->knoten;
    }
};

// Aufzaehlungszeichen bzw. Nummer vor der ersten Zeile, auf deren Grundlinie; wie Qts Textflaeche als Zeichen.
void listenMarke(QSGTextNode* text, const QTextBlock& b, QPointF pos, const QColor& grund) {
    const QTextList* liste = b.textList();
    QString s;
    switch (liste->format().style()) {
    case QTextListFormat::ListDisc:   s = QChar(0x2022); break;
    case QTextListFormat::ListCircle: s = QChar(0x25E6); break;
    case QTextListFormat::ListSquare: s = QChar(0x25AA); break;
    default:                          s = liste->itemText(b); break;
    }
    const QTextCharFormat cf = b.charFormat();
    const QFont schrift = cf.font().resolve(b.document()->defaultFont());
    QTextLayout marke(s, schrift);
    QTextLayout::FormatRange farbe{0, int(s.size()), {}};
    farbe.format.setForeground(cf.hasProperty(QTextFormat::ForegroundBrush) ? cf.foreground().color() : grund);
    marke.setFormats({farbe});
    marke.beginLayout();
    const QTextLine z = marke.createLine();
    marke.endLayout();
    const QTextLine erste = b.layout()->lineAt(0);
    const qreal abstand = QFontMetricsF(schrift).horizontalAdvance(QLatin1Char(' '));
    const qreal x = b.textDirection() == Qt::RightToLeft ? erste.x() + erste.width() + abstand
                                                        : erste.x() - abstand - z.naturalTextWidth();
    text->addTextLayout(pos + QPointF(x, erste.y() + erste.ascent() - z.ascent()), &marke);
}

// Hoch- und tiefgestellte Stuecke: im Zeilenlayout durchsichtig, hier verschoben nachgezeichnet. Versatz wie Qts
// Textflaeche, aus der ungekuerzten Schrift; die Groesse wie Qts Layout, zwei Drittel.
void hochTief(QSGTextNode* text, QPointF pos, const QTextLayout* tl, const QTextFragment& frag, int blockPos,
              const QTextDocument& doc) {
    const QTextCharFormat cf = frag.charFormat();
    const QFont voll = cf.font().resolve(doc.defaultFont());
    const QColor grund = static_cast<const md::RenderedDocument&>(doc).text;
    QFont klein = voll;
    if (klein.pointSize() != -1)
        klein.setPointSize(klein.pointSize() * 2 / 3);
    else
        klein.setPixelSize(klein.pixelSize() * 2 / 3);
    const QFontMetricsF fm(voll);
    const int hoehe = int(fm.ascent() + fm.descent());
    const int schub = cf.verticalAlignment() == QTextCharFormat::AlignSuperScript ? -(hoehe / 2) : hoehe / 6;
    const int rel = frag.position() - blockPos;
    const QTextLine z = tl->lineForTextPosition(rel);
    if (!z.isValid()) return;
    QTextLayout stueck(frag.text(), klein);
    QTextLayout::FormatRange farbe{0, frag.length(), {}};
    farbe.format.setForeground(cf.hasProperty(QTextFormat::ForegroundBrush) ? cf.foreground().color() : grund);
    stueck.setFormats({farbe});
    stueck.beginLayout();
    const QTextLine eine = stueck.createLine();
    stueck.endLayout();
    const qreal x = qMin(z.cursorToX(rel), z.cursorToX(rel + frag.length()));
    text->addTextLayout(pos + QPointF(x, z.y() + z.ascent() + schub - eine.ascent()), &stueck);
}

// Ein eingebettetes Bild: Qt verkleinert breite Bilder beim Auslegen; die Breite steht in der Zeile, die Hoehe folgt
// dem Seitenverhaeltnis. Die Textur bleibt bis zum naechsten Dokument.
void bild(QQuickWindow* win, QHash<QString, QSGTexture*>& bilder, QSGNode* wurzel, QPointF pos, const QTextLayout* tl,
          const QTextFragment& frag, int blockPos, const QTextDocument& doc) {
    const QTextImageFormat bf = frag.charFormat().toImageFormat();
    QSGTexture*& tex = bilder[bf.name()];
    if (!tex) {
        const QImage img = doc.resource(QTextDocument::ImageResource, QUrl(bf.name())).value<QImage>();
        if (img.isNull()) return;
        tex = win->createTextureFromImage(img);
    }
    for (int i = frag.position() - blockPos; i < frag.position() - blockPos + frag.length(); ++i) {
        const QTextLine z = tl->lineForTextPosition(i);
        if (!z.isValid()) continue;
        const qreal a = z.cursorToX(i);
        const qreal e = z.cursorToX(i + 1);
        const qreal w = std::abs(e - a);
        const qreal h = bf.width() > 0 ? bf.height() * w / bf.width() : bf.height();
        QSGImageNode* n = win->createImageNode();
        n->setTexture(tex);
        n->setFiltering(QSGTexture::Linear);
        n->setRect(QRectF(pos.x() + qMin(a, e), pos.y() + z.y() + z.ascent() - h, w, h));
        wurzel->appendChildNode(n);
    }
}

// Ein Block samt Markierung, Aufzaehlungszeichen, Hochgestelltem und Bildern.
QSGNode* teil(QQuickWindow* win, QHash<QString, QSGTexture*>& bilder, const md::RenderedDocument& doc,
              const QTextBlock& block, QPointF pos, int von, int bis, int erste, int anzahl, const QColor& selFarbe,
              const QColor& selText, int renderType) {
    QTextLayout* tl = block.layout();
    auto* knoten = new QSGNode;
    QSGTextNode* text = win->createTextNode();
    text->setColor(doc.text);
    text->setSelectionColor(selFarbe);
    text->setSelectionTextColor(selText);
    text->setRenderType(QSGTextNode::RenderType(renderType));
    QList<QTextFragment> hochtief;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        const QTextCharFormat cf = frag.charFormat();
        if (cf.isImageFormat()) {
            bild(win, bilder, knoten, pos, tl, frag, block.position(), doc);
            continue;
        }
        const int lage = cf.verticalAlignment();
        if (lage == QTextCharFormat::AlignSuperScript || lage == QTextCharFormat::AlignSubScript) hochtief << frag;
    }
    const bool markiert = bis > von;
    //  Mit Hochgestelltem malte Qt die Markierung samt markiertem Text an die Grundlinie; dann nur die Flaeche selbst.
    if (markiert && !hochtief.isEmpty()) {
        const int ende = anzahl < 0 ? tl->lineCount() : erste + anzahl;
        for (int i = erste; i < ende; ++i) {
            const QTextLine z = tl->lineAt(i);
            const int a = qMax(von, z.textStart());
            const int e = qMin(bis, z.textStart() + z.textLength());
            if (e <= a) continue;
            const qreal x1 = z.cursorToX(a);
            const qreal x2 = z.cursorToX(e);
            QSGRectangleNode* n = win->createRectangleNode();
            n->setRect(QRectF(pos.x() + qMin(x1, x2), pos.y() + z.y(), std::abs(x2 - x1), z.height()));
            n->setColor(selFarbe);
            knoten->appendChildNode(n);
        }
    }
    const bool qtMarkiert = markiert && hochtief.isEmpty();
    text->addTextLayout(pos, tl, qtMarkiert ? von : -1, qtMarkiert ? bis - von : -1, erste, anzahl);
    if (block.textList()) listenMarke(text, block, pos, doc.text);
    for (const QTextFragment& frag : std::as_const(hochtief)) hochTief(text, pos, tl, frag, block.position(), doc);
    knoten->appendChildNode(text);
    return knoten;
}

}  // namespace

QSGNode* MarkdownViewport::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    const md::RenderedDocument* doc = m_view ? m_view->shownDocument() : nullptr;
    if (!doc || !window() || m_revision != m_view->revision() || (m_bloecke.isEmpty() && m_flaechen.isEmpty())) {
        delete old;
        return nullptr;
    }
    auto* w = old ? static_cast<Zeichnung*>(old) : new Zeichnung;
    //  Neues Dokument, neue Breite oder Farben: alle Teile neu, die Texturen nur mit dem Dokument.
    if (w->revision != m_revision || w->stand != m_stand) {
        if (w->revision != m_revision) {
            qDeleteAll(w->bilder);
            w->bilder.clear();
        }
        for (auto it = w->teile.begin(); it != w->teile.end(); ++it) w->weg(it);
        w->teile.clear();
        w->revision = m_revision;
        w->stand = m_stand;
    }
    while (QSGNode* k = w->flaechen->firstChild()) {
        w->flaechen->removeChildNode(k);
        delete k;
    }
    //  Rechteckknoten: eigene Geometrie liesse der Software-Renderer still weg.
    for (const auto& [r, farbe] : std::as_const(m_flaechen)) {
        QSGRectangleNode* n = window()->createRectangleNode();
        n->setRect(r);
        n->setColor(farbe);
        w->flaechen->appendChildNode(n);
    }
    const qreal bereich = m_bis - m_von;
    QSet<int> da;
    da.reserve(m_bloecke.size());
    for (const Stueck& s : std::as_const(m_bloecke)) {
        QTextLayout* tl = s.block.layout();
        if (!tl || tl->lineCount() == 0) continue;
        const int pos = s.block.position();
        int von = qMax(m_selVon, pos) - pos;
        int bis = qMin(m_selBis, pos + s.block.length()) - pos;
        if (bis <= von) von = bis = -1;
        //  Ein riesiger Absatz zeichnet nur seine Zeilen im Bereich.
        int erste = 0;
        int anzahl = -1;
        if (tl->boundingRect().height() > bereich) {
            const int n = tl->lineCount();
            while (erste < n && s.pos.y() + tl->lineAt(erste).rect().bottom() < m_von) ++erste;
            int letzte = erste;
            while (letzte < n && s.pos.y() + tl->lineAt(letzte).y() <= m_bis) ++letzte;
            anzahl = letzte - erste;
            if (anzahl <= 0) continue;
        }
        da.insert(s.nummer);
        auto it = w->teile.find(s.nummer);
        if (it != w->teile.end()) {
            if (it->von == von && it->bis == bis && it->erste == erste && it->anzahl == anzahl) continue;
            w->weg(it);
        }
        QSGNode* k = teil(window(), w->bilder, *doc, s.block, s.pos, von, bis, erste, anzahl, m_selFarbe, m_selText,
                          m_renderType);
        w->text->appendChildNode(k);
        w->teile.insert(s.nummer, {k, von, bis, erste, anzahl});
    }
    for (auto it = w->teile.begin(); it != w->teile.end();) {
        if (da.contains(it.key())) {
            ++it;
            continue;
        }
        w->weg(it);
        it = w->teile.erase(it);
    }
    return w;
}

QVariantMap MarkdownView::frontMatterBox() const {
    if (!m_ownDoc || !m_ready || !m_ownDoc->metaFrame) return {};
    const QRectF r = m_ownDoc->documentLayout()->frameBoundingRect(m_ownDoc->metaFrame);
    return {{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()},
            {QStringLiteral("width"), r.width()}, {QStringLiteral("height"), r.height()}};
}

bool MarkdownView::copyCode(int index) const {
    if (!m_ownDoc || index < 0 || index >= m_ownDoc->codeBoxes.size()) return false;
    QGuiApplication::clipboard()->setText(md::codeBoxText(m_ownDoc->codeBoxes[index]));
    return true;
}

int MarkdownView::anchorPosition(const QString& name) const {
    if (!m_ownDoc || name.isEmpty()) return -1;
    const QString gesucht = QUrl::fromPercentEncoding(name.toUtf8());
    const QString slug = md::slugify(gesucht);
    for (QTextBlock b = m_ownDoc->begin(); b.isValid(); b = b.next()) {
        const QString a = b.blockFormat().property(md::kAnchorProperty).toString();
        if (!a.isEmpty() && (a == gesucht || a == slug)) return b.position();
    }
    return -1;
}

int MarkdownView::sourceLineAt(int position) const {
    const md::RenderedDocument* d = shownDocument();
    return d ? md::sourceLineAt(*d, position) : -1;
}

int MarkdownView::positionOfSourceLine(int line) const {
    const md::RenderedDocument* d = shownDocument();
    return d ? md::positionOfSourceLine(*d, line) : -1;
}

QVariantMap MarkdownView::resolveLink(const QString& href) const {
    QVariantMap m;
    m.insert(QStringLiteral("kind"), QStringLiteral("none"));
    if (href.startsWith(md::kDetailsScheme)) {
        m.insert(QStringLiteral("kind"), QStringLiteral("details"));
        m.insert(QStringLiteral("id"), href.mid(md::kDetailsScheme.size()).toInt());
        return m;
    }
    if (href.startsWith(u'#')) {
        m.insert(QStringLiteral("kind"), QStringLiteral("anchor"));
        m.insert(QStringLiteral("position"), anchorPosition(href.mid(1)));
        return m;
    }
    const QUrl url(href);
    const QString schema = url.scheme().toLower();
    if (schema == u"http" || schema == u"https" || schema == u"mailto" || schema == u"ftp") {
        m.insert(QStringLiteral("kind"), QStringLiteral("url"));
        m.insert(QStringLiteral("url"), href);
        return m;
    }
    QString pfad;
    QString anker;
    if (schema == u"file") {
        pfad = url.toLocalFile();
        anker = url.fragment();
    } else if (schema.isEmpty() || (schema.size() == 1 && QDir::isAbsolutePath(href))) {
        const int raute = href.indexOf(u'#');
        pfad = QUrl::fromPercentEncoding((raute >= 0 ? href.left(raute) : href).toUtf8());
        anker = raute >= 0 ? href.mid(raute + 1) : QString();
        if (!pfad.isEmpty() && QDir::isRelativePath(pfad))
            pfad = QDir(QFileInfo(m_source).absolutePath()).absoluteFilePath(pfad);
    } else {
        return m;
    }
    if (pfad.isEmpty() || QDir::cleanPath(pfad) == QDir::cleanPath(m_source)) {
        m.insert(QStringLiteral("kind"), QStringLiteral("anchor"));
        m.insert(QStringLiteral("position"), anchorPosition(anker));
        return m;
    }
    pfad = QDir::cleanPath(pfad);
    if (!QFileInfo(pfad).isFile()) return m;
    m.insert(QStringLiteral("kind"), QStringLiteral("file"));
    m.insert(QStringLiteral("path"), pfad);
    return m;
}

}  // namespace mg::editor
