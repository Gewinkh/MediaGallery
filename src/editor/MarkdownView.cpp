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
#include <QTextDocument>
#include <QTextFrame>
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
        //  Nur Bloecke im Sichtfenster zeichnen: Qt schaltet das fuer grosse Texte nur in setText ein, nicht beim
        //  Tausch des Dokuments - sonst Knoten fuer das ganze Dokument (762 KB Markdown: Heap +190 MB).
        if (auto* edit = qobject_cast<QQuickItem*>(m_quickDoc->parent()))
            edit->setFlag(QQuickItem::ItemObservesViewport);
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
