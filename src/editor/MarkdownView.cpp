#include "editor/MarkdownView.h"

#include "core/AppSettings.h"
#include "core/PathUtils.h"
#include "core/Strings.h"
#include "core/TextEncoding.h"
#include "editor/EditorController.h"
#include "editor/MarkdownRender.h"

#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QRunnable>
#include <QTextBlock>
#include <QQuickItem>
#include <QTextDocument>
#include <QTextFrame>
#include <QThread>
#include <QUrl>
#include <functional>

namespace mg::editor {
namespace {

constexpr qint64 kMaxBytes = 8LL * 1024 * 1024;   // derselbe Deckel wie im Texteditor
constexpr int kFontPx = 15;
// Mitschnitt jedes Einhaengens - fuer Darstellungsfehler, die nur in der laufenden App auftreten.
const bool kLog = qEnvironmentVariableIntValue("MG_LOG_MD") == 1;

struct Result {
    QTextDocument* doc = nullptr;
    QString text;
    bool ok = true;
    bool truncated = false;
    // Kommt das Ergebnis nie an (Kachel geschlossen), faellt das Dokument hier - im GUI-Faden, dem es gehoert.
    ~Result() { delete doc; }
};

class RenderTask : public QRunnable {
public:
    RenderTask(QObject* owner, QString path, QString text, bool readFile, md::RenderStyle style,
               std::shared_ptr<std::atomic<bool>> abort, std::function<void(std::shared_ptr<Result>)> back)
        : m_owner(owner), m_target(owner->thread()), m_path(std::move(path)), m_text(std::move(text)),
          m_readFile(readFile), m_style(std::move(style)), m_abort(std::move(abort)), m_back(std::move(back)) {
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
            QTextDocument* d = md::buildDocument(doc, m_style, m_abort.get());
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
    std::shared_ptr<std::atomic<bool>> m_abort;
    std::function<void(std::shared_ptr<Result>)> m_back;
};

}  // namespace

MarkdownView::MarkdownView(QObject* parent) : QObject(parent) {
    m_pool.setMaxThreadCount(1);
    connect(&AppSettings::instance(), &AppSettings::languageChanged, this, [this] { start(false); });
    if (EditorController* c = activeController())
        connect(c, &EditorController::paletteChanged, this, [this] { start(false); });
}

MarkdownView::~MarkdownView() {
    if (m_abort) m_abort->store(true);
    m_pool.waitForDone();
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
        if (m_ownDoc) m_ownDoc->clear();
        m_ready = false;
        emit decorationsChanged();
    }
    start(true);
}

void MarkdownView::setDocument(QQuickTextDocument* d) {
    if (d == m_quickDoc) return;
    m_quickDoc = d;
    m_ownDoc = nullptr;
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
    st.baseDir = QFileInfo(m_source).absolutePath();
    st.flipped = m_flipped;
    st.showLabel = Strings::get(StringKey::MarkdownDetailsShow);
    st.hideLabel = Strings::get(StringKey::MarkdownDetailsHide);

    if (kLog) qInfo("[md] start lesen=%d gen=%d %s", int(readFile), m_generation, qPrintable(m_source));
    m_abort = std::make_shared<std::atomic<bool>>(false);
    m_busy = true;
    emit stateChanged();
    const int gen = m_generation;
    auto* self = this;
    m_pool.start(new RenderTask(this, m_source, readFile ? QString() : m_text, readFile, std::move(st), m_abort,
                                [self, gen, readFile](std::shared_ptr<Result> r) {
                                    self->adopt(std::exchange(r->doc, nullptr), gen, r->text, readFile, r->ok,
                                                r->truncated);
                                }));
}

void MarkdownView::adopt(QTextDocument* doc, int generation, const QString& text, bool readFile, bool ok,
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
        QTextDocument* alt = m_ownDoc;
        m_quickDoc->setTextDocument(doc);
        m_ownDoc = doc;
        delete alt;
        m_ready = true;
        ++m_revision;
        connect(doc->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
                &MarkdownView::decorationsChanged);
        if (kLog) {
            const auto* edit = qobject_cast<QQuickItem*>(doc->parent());
            qInfo("[md] eingehaengt rev=%d zeichen=%d rahmen=%lld breite=%.1f x=%.2f", m_revision,
                  doc->characterCount(), static_cast<long long>(doc->rootFrame()->childFrames().size()),
                  edit ? edit->width() : -1.0, edit ? edit->x() : -1.0);
        }
        emit decorationsChanged();
    } else {
        delete doc;
    }
    emit stateChanged();
}

QList<QTextFrame*> MarkdownView::codeFrames() const {
    QList<QTextFrame*> out;
    if (!m_ownDoc) return out;
    QList<QTextFrame*> offen{m_ownDoc->rootFrame()};
    while (!offen.isEmpty()) {
        QTextFrame* f = offen.takeFirst();
        const QList<QTextFrame*> kinder = f->childFrames();
        for (int i = int(kinder.size()) - 1; i >= 0; --i) offen.prepend(kinder[i]);
        if (f->frameFormat().hasProperty(md::kCodeTextProperty)) out << f;
    }
    return out;
}

QVariantList MarkdownView::codeBlocks() const {
    QVariantList out;
    if (!m_ownDoc || !m_ready) return out;
    QAbstractTextDocumentLayout* lay = m_ownDoc->documentLayout();
    for (QTextFrame* f : codeFrames()) {
        const QTextFrameFormat ff = f->frameFormat();
        const QRectF r = lay->frameBoundingRect(f);
        //  Der obere Aussenabstand ist die Kopfleiste und gehoert zum Kasten, der linke die Einrueckung.
        QVariantMap m;
        m.insert(QStringLiteral("x"), r.x() + ff.leftMargin());
        m.insert(QStringLiteral("y"), r.y());
        m.insert(QStringLiteral("width"), r.width() - ff.leftMargin() - ff.rightMargin());
        m.insert(QStringLiteral("height"), r.height() - ff.bottomMargin());
        m.insert(QStringLiteral("language"), ff.stringProperty(md::kCodeLangProperty));
        m.insert(QStringLiteral("header"), ff.topMargin());
        out << m;
    }
    return out;
}

QVariantMap MarkdownView::frontMatterBox() const {
    if (!m_ownDoc || !m_ready) return {};
    for (QTextFrame* f : m_ownDoc->rootFrame()->childFrames()) {
        if (!f->frameFormat().hasProperty(md::kMetaProperty)) continue;
        const QRectF r = m_ownDoc->documentLayout()->frameBoundingRect(f);
        return {{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()},
                {QStringLiteral("width"), r.width()}, {QStringLiteral("height"), r.height()}};
    }
    return {};
}

bool MarkdownView::copyCode(int index) const {
    const QList<QTextFrame*> rahmen = codeFrames();
    if (index < 0 || index >= rahmen.size()) return false;
    QGuiApplication::clipboard()->setText(rahmen[index]->frameFormat().stringProperty(md::kCodeTextProperty));
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
