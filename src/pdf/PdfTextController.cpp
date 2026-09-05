#include "pdf/PdfTextController.h"
#include "core/PathUtils.h"

#include <QPdfDocument>
#include <QPdfSearchModel>
#include <QPdfLink>
#include <QPdfDocumentRenderOptions>
#include <QPdfSelection>
#include <QGuiApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QCoreApplication>
#include <QSizeF>
#include <QSize>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QPolygonF>
#include <QVariantMap>
#include <QStringList>
#include <QRunnable>
#include <utility>
#include <algorithm>

// Lädt das Auswahl-Dokument ohne GUI-Thread: eigene QPdfDocument-Instanz, nach Erfolg auf den GUI-Thread
// verschoben und per QueuedConnection übergeben. Die Generationszahl verwirft veraltete Ergebnisse.
namespace {
class PdfLoadTask : public QRunnable {
public:
    PdfLoadTask(PdfTextController* owner, QString localPath, int generation)
        : m_owner(owner), m_path(std::move(localPath)), m_gen(generation) {
        setAutoDelete(true);
    }

    void run() override {
        auto* doc = new QPdfDocument;   // Affinitaet: dieser Pool-Thread
        const bool ok = (doc->load(m_path) == QPdfDocument::Error::None
                         && doc->status() == QPdfDocument::Status::Ready);
        if (ok) {
            doc->moveToThread(QCoreApplication::instance()->thread());
        } else {
            delete doc;                 // Loeschen auf eigenem Thread -> ok
            doc = nullptr;
        }

        PdfTextController* owner = m_owner;
        const QString path = m_path;
        const int     gen  = m_gen;
        // Das Dokument gehört dem Lambda: wird das gepostete Ereignis nie ausgeführt (Controller wird gerade zerstört),
        // gibt der `unique_ptr` es frei. Mit dem rohen Zeiger blieb ein geparstes QPdfDocument verwaist im Speicher.
        std::unique_ptr<QPdfDocument> owned(doc);
        QMetaObject::invokeMethod(owner, [owner, d = std::move(owned), path, gen]() mutable {
            owner->adoptDocument(d.release(), path, gen);
        }, Qt::QueuedConnection);
    }

private:
    PdfTextController* m_owner;
    QString            m_path;
    int                m_gen;
};

} // namespace

PdfTextController::PdfTextController(QObject* parent) : QObject(parent) {
    // Genau EIN Worker: nie sind zwei der teuren QPdfDocument-Instanzen
    // gleichzeitig am Laden -> RAM-Peak bleibt gedeckelt.
    m_pool.setMaxThreadCount(1);
}

PdfTextController::~PdfTextController() {
    // Laufende/anstehende Ladevorgaenge sauber beenden, bevor der Controller
    // (und sein Dokument-Kind) verschwindet.
    m_pool.clear();
    m_pool.waitForDone();
}

void PdfTextController::prepare(const QString& pathOrUrl) {
    const QString local = mg::toLocalPath(pathOrUrl);
    if (local.isEmpty() || !QFileInfo::exists(local))
        return;

    if ((local == m_activePath && m_doc) || local == m_pendingPath)
        return;

    ++m_generation;
    m_pendingPath = local;
    m_pool.start(new PdfLoadTask(this, local, m_generation));
}

void PdfTextController::reload() {
    // `prepare` kehrt bei gleichem Pfad sofort zurück. Wurde die DATEI selbst umgeschrieben (Seitenoperationen,
    // neue Textebene), muss sie trotzdem neu gelesen werden - sonst arbeiten Auswahl und Suche auf dem alten Stand.
    const QString path = m_activePath;
    if (path.isEmpty())
        return;
    releaseDocument();
    prepare(path);
}

void PdfTextController::adoptDocument(QPdfDocument* doc, const QString& localPath,
                                      int generation) {
    if (generation != m_generation) {
        if (doc)
            delete doc;     // doc lebt auf dem GUI-Thread -> direktes delete ok
        return;
    }

    m_pendingPath.clear();

    if (m_doc) {
        delete m_doc;
        m_doc = nullptr;
    }

    m_doc = doc;
    if (m_doc) {
        m_doc->setParent(this);     // Lebensdauer an den Controller binden
        m_activePath = localPath;
    } else {
        m_activePath.clear();
    }

    emit readyChanged();

    //  Wurde schon gesucht, BEVOR das Dokument da war (Begriff eingetippt,
    //  während die Textebene noch lud), läuft die Suche jetzt nach - sonst
    //  bliebe die Trefferliste stumm leer, obwohl der Begriff dasteht.
    if (m_doc && !m_searchTerm.isEmpty() && m_searchedDoc != m_doc) {
        const QString again = m_searchTerm;
        m_searchTerm.clear();       // Kurzschluss aushebeln
        search(again);
    }
}

void PdfTextController::releaseDocument() {
    ++m_generation;                 // evtl. laufenden Ladevorgang verwerfen
    //  Laufende Suche zuerst stoppen: ihr Modell hält das Dokument, das gleich
    //  gelöscht wird.
    m_searchTimer.stop();
    m_searchPage = -1;
    m_hits.clear();
    m_searchTerm.clear();
    //  Das Suchmodell hält das Dokument - es verschwindet mit ihm (ein
    //  setDocument(nullptr) meldet in Qt nur eine nutzlose connect-Warnung).
    delete m_searchModel;
    m_searchModel = nullptr;
    m_searchedDoc = nullptr;
    emit searchChanged();
    m_pendingPath.clear();
    m_activePath.clear();

    if (m_doc) {
        delete m_doc;
        m_doc = nullptr;
        emit readyChanged();
    }
    clearSelection();
}

QVariantList PdfTextController::selectionBetween(int page,
                                                 double nx0, double ny0,
                                                 double nx1, double ny1) {
    if (!m_doc || page < 0 || page >= m_doc->pageCount())
        return {};

    const QSizeF ps = m_doc->pagePointSize(page);
    if (ps.isEmpty())
        return {};

    const QPointF a(nx0 * ps.width(), ny0 * ps.height());
    const QPointF b(nx1 * ps.width(), ny1 * ps.height());

    const QPdfSelection sel = m_doc->getSelection(page, a, b);
    if (sel.isValid() && !sel.text().isEmpty())
        return applySelection(sel, page, ps.width(), ps.height());
    //  Ohne eingebettete Textebene gibt es hier nichts zu markieren. Für einen
    //  Scan schreibt „Dokument durchsuchbar machen" (Dokument-Menü) die
    //  Textebene EINMAL in die Datei - danach greift der Weg oben.
    return {};
}

QVariantList PdfTextController::selectAllOnPage(int page) {
    if (!m_doc || page < 0 || page >= m_doc->pageCount())
        return {};

    const QSizeF ps = m_doc->pagePointSize(page);
    if (ps.isEmpty())
        return {};

    const QPdfSelection sel = m_doc->getAllText(page);
    if (sel.isValid() && !sel.text().isEmpty())
        return applySelection(sel, page, ps.width(), ps.height());
    return {};
}

//  Aus der QPdfSelection die normalisierten Highlight-Rechtecke bauen und den
//  Text merken. bounds() liefert Rechteck-Polygone in Punkten (Ursprung
//  oben-links) -> boundingRect()/Seitengroesse ergibt normalisierte [0..1]-Rects.
QVariantList PdfTextController::applySelection(const QPdfSelection& sel, int page,
                                               double pageWidthPts,
                                               double pageHeightPts) {
    Q_UNUSED(page)
    QVariantList rects;
    QString text;

    if (sel.isValid() && pageWidthPts > 0.0 && pageHeightPts > 0.0) {
        text = sel.text();
        const QList<QPolygonF> bounds = sel.bounds();
        rects.reserve(bounds.size());
        for (const QPolygonF& poly : bounds) {
            const QRectF r = poly.boundingRect();
            QVariantMap m;
            m.insert(QStringLiteral("x"), r.x()      / pageWidthPts);
            m.insert(QStringLiteral("y"), r.y()      / pageHeightPts);
            m.insert(QStringLiteral("w"), r.width()  / pageWidthPts);
            m.insert(QStringLiteral("h"), r.height() / pageHeightPts);
            rects.append(m);
        }
    }

    if (text != m_selText) {
        m_selText = text;
        emit selectedTextChanged();
    }
    return rects;
}

// Zeilenfang: Fragment-Rechtecke nach vertikaler Mitte sortieren und zu ZEILEN vereinigen. Toleranz 60 % der
// kleineren Fragmenthöhe - robust gegen Hoch-/Tiefstellungen und leicht versetzte Runs derselben Zeile.
QList<QRectF> PdfTextController::lineRectsPts(int page) const {
    if (!m_doc || page < 0 || page >= m_doc->pageCount())
        return {};

    //  Ohne eingebettete Textebene gibt es keine Zeilen. Für einen Scan
    //  schreibt „Dokument durchsuchbar machen" sie EINMAL in die Datei - danach
    //  liest dieser Weg sie wie bei jedem anderen Dokument.
    const QPdfSelection sel = m_doc->getAllText(page);
    if (!sel.isValid())
        return {};

    QList<QRectF> frags;
    const QList<QPolygonF> bounds = sel.bounds();
    frags.reserve(bounds.size());
    for (const QPolygonF& poly : bounds) {
        const QRectF r = poly.boundingRect();
        if (r.width() > 0.5 && r.height() > 0.5)
            frags.append(r);
    }
    if (frags.isEmpty())
        return {};

    std::sort(frags.begin(), frags.end(), [](const QRectF& a, const QRectF& b) {
        return a.center().y() < b.center().y();
    });

    QList<QRectF> lines;
    for (const QRectF& r : std::as_const(frags)) {
        if (!lines.isEmpty()) {
            QRectF& last = lines.last();
            const qreal tol = qMax(2.0, qMin(last.height(), r.height()) * 0.6);
            if (qAbs(r.center().y() - last.center().y()) < tol) {
                last = last.united(r);
                continue;
            }
        }
        lines.append(r);
    }
    return lines;
}

QVariantList PdfTextController::textLineRects(int page) {
    if (!m_doc || page < 0 || page >= m_doc->pageCount())
        return {};

    const QSizeF ps = m_doc->pagePointSize(page);
    if (ps.isEmpty())
        return {};

    const QList<QRectF> lines = lineRectsPts(page);

    QVariantList out;
    out.reserve(lines.size());
    for (const QRectF& r : std::as_const(lines)) {
        QVariantMap m;
        m.insert(QStringLiteral("x"), r.x()      / ps.width());
        m.insert(QStringLiteral("y"), r.y()      / ps.height());
        m.insert(QStringLiteral("w"), r.width()  / ps.width());
        m.insert(QStringLiteral("h"), r.height() / ps.height());
        out.append(m);
    }
    return out;
}

// Sonde für "Text ersetzen": getroffene Zeilen vereinigen, Zeilenhöhe für die Schriftgröße ableiten und den
// eingebetteten Text unter der Fläche holen - bewusst OHNE die Nutzer-Auswahl (`m_selText`) anzufassen.
QVariantMap PdfTextController::replaceProbe(int page, double nx0, double ny0,
                                            double nx1, double ny1) {
    QVariantMap out;
    out.insert(QStringLiteral("found"), false);
    if (!m_doc || page < 0 || page >= m_doc->pageCount())
        return out;

    const QSizeF ps = m_doc->pagePointSize(page);
    if (ps.isEmpty())
        return out;

    const QRectF drag(QPointF(qMin(nx0, nx1) * ps.width(),  qMin(ny0, ny1) * ps.height()),
                      QPointF(qMax(nx0, nx1) * ps.width(),  qMax(ny0, ny1) * ps.height()));
    if (drag.width() < 2.0 || drag.height() < 2.0)
        return out;

    QRectF uni;
    qreal  hSum = 0.0;
    int    n    = 0;
    const QList<QRectF> lines = lineRectsPts(page);
    for (const QRectF& l : lines) {
        const qreal ovY = qMin(drag.bottom(), l.bottom()) - qMax(drag.top(), l.top());
        const qreal ovX = qMin(drag.right(),  l.right())  - qMax(drag.left(), l.left());
        // Treffer: horizontale Überlappung + vertikal mindestens 35 % der
        // Zeilenhöhe ODER 80 % der Aufzieh-Höhe (flacher Zug INNERHALB einer
        // hohen Zeile zählt ebenfalls) - Streifschüsse zählen nicht.
        const qreal need = qMin(l.height() * 0.35, drag.height() * 0.8);
        if (ovX <= 0.0 || ovY < qMax(0.5, need))
            continue;
        uni = (n == 0) ? l : uni.united(l);
        hSum += l.height();
        ++n;
    }
    if (n == 0 || uni.width() < 2.0 || uni.height() < 2.0)
        return out;

    // Eingebetteten Text unter der Union holen (Ecken minimal eingerückt,
    // damit angrenzende Nachbarzeichen nicht mitgegriffen werden).
    QString text;
    const QPdfSelection sel = m_doc->getSelection(page,
                                  QPointF(uni.left()  + 0.5, uni.top()    + 0.5),
                                  QPointF(uni.right() - 0.5, uni.bottom() - 0.5));
    if (sel.isValid())
        text = sel.text();
    out.insert(QStringLiteral("found"), true);
    out.insert(QStringLiteral("x"),     uni.x()          / ps.width());
    out.insert(QStringLiteral("y"),     uni.y()          / ps.height());
    out.insert(QStringLiteral("w"),     uni.width()      / ps.width());
    out.insert(QStringLiteral("h"),     uni.height()     / ps.height());
    out.insert(QStringLiteral("lineH"), (hSum / n)       / ps.height());
    out.insert(QStringLiteral("text"),  text);
    return out;
}

void PdfTextController::clearSelection() {
    if (!m_selText.isEmpty()) {
        m_selText.clear();
        emit selectedTextChanged();
    }
}


// `QPdfSearchModel` liefert seine Rechtecke in PDF-Punkten mit Ursprung oben-links (gemessen), also dieselbe
// Konvention wie der Editor. Es arbeitet aber lazy - deshalb holt ein Timer die Seiten STÜCKWEISE.
void PdfTextController::search(const QString& needle) {
    const QString term = needle.trimmed();
    if (term == m_searchTerm && !term.isEmpty() && m_doc && m_searchedDoc == m_doc)
        return;
    m_hits.clear();
    m_searchTerm = term;
    m_searchPattern = mg::search::Pattern(term, false, false);
    m_searchTimer.stop();
    m_searchPage = -1;

    if (term.isEmpty() || !m_doc) {
        if (m_searchModel)
            m_searchModel->setSearchString(QString());
        emit searchChanged();
        return;
    }
    if (!m_searchModel) {
        m_searchModel = new QPdfSearchModel(this);
        m_searchTimer.setSingleShot(false);
        m_searchTimer.setInterval(0);            // so bald wie möglich, aber NACH den Ereignissen
        connect(&m_searchTimer, &QTimer::timeout, this, &PdfTextController::stepSearch);
    }
    m_searchModel->setDocument(m_doc);
    m_searchModel->setSearchString(term);
    m_searchedDoc = m_doc;
    m_searchPage = 0;
    emit searchChanged();
    m_searchTimer.start();
}

void PdfTextController::clearSearch() {
    search(QString());
}

void PdfTextController::stepSearch() {
    if (!m_doc || !m_searchModel || m_searchPage < 0) {
        m_searchTimer.stop();
        return;
    }
    const int pageCount = m_doc->pageCount();
    //  Ein Stück je Durchlauf: klein genug, dass die Oberfläche zwischendurch
    //  zeichnet, groß genug, dass eine normale Datei sofort fertig ist.
    constexpr int kPagesPerStep = 4;
    const int end = qMin(pageCount, m_searchPage + kPagesPerStep);
    for (int p = m_searchPage; p < end; ++p) {
        const QList<QPdfLink> found = m_searchModel->resultsOnPage(p);
        for (const QPdfLink& l : found) {
            //  ALLE Rechtecke einer Fundstelle gehören zu EINEM Treffer - sonst
            //  zählt ein Wort so oft, wie der Erzeuger es in Zeige-Operatoren
            //  zerlegt hat (s. SearchHit im Header).
            SearchHit h;
            h.page   = p;
            h.before = l.contextBefore();
            h.after  = l.contextAfter();
            for (const QRectF& r : l.rectangles())
                if (r.width() > 0.0 && r.height() > 0.0)
                    h.rects.push_back(r);
            if (!h.rects.isEmpty())
                m_hits.push_back(h);
        }
    }
    // Muster-Zweig nur, wenn der Begriff Sonderzeichen trägt und übersetzbar war. Der Seitentext liegt im SELBEN
    // Indexraum wie `getSelectionAtIndex` (nachgemessen), daraus kommen die Rechtecke.
    if (m_searchPattern.usesRegex()) {
        for (int p = m_searchPage; p < end; ++p) {
            const QString seite = m_doc->getAllText(p).text();
            if (seite.isEmpty()) continue;
            //  Nur der MUSTER-Zweig: den Wortlaut hat `QPdfSearchModel` oben
            //  bereits erledigt, samt Kontext.
            auto it = m_searchPattern.regex().globalMatch(seite);
            int genommen = 0;
            while (it.hasNext() && genommen < 500) {
                const QRegularExpressionMatch m = it.next();
                if (m.capturedLength() <= 0) continue;
                const QPdfSelection sel =
                    m_doc->getSelectionAtIndex(p, int(m.capturedStart()),
                                               int(m.capturedLength()));
                SearchHit h;
                h.page = p;
                //  `bounds()` liefert Vielecke (gedrehter Text) - fuer die
                //  Markierung genuegt ihr umschliessendes Rechteck.
                for (const QPolygonF& poly : sel.bounds()) {
                    const QRectF r = poly.boundingRect();
                    if (r.width() > 0.0 && r.height() > 0.0)
                        h.rects.push_back(r);
                }
                if (h.rects.isEmpty()) continue;

                //  Doppelt? Ein Muster kann dasselbe treffen wie der Wortlaut.
                //  Verglichen wird die FLAECHE, weil der Wortlaut-Zweig keine
                //  Zeichenindizes meldet.
                bool schon = false;
                const QRectF neu = h.bounds();
                for (const SearchHit& alt : std::as_const(m_hits))
                    if (alt.page == p && alt.bounds().intersects(neu)) { schon = true; break; }
                if (schon) continue;

                constexpr int kKontext = 30;
                const int von = int(m.capturedStart());
                h.before = seite.mid(qMax(0, von - kKontext),
                                     qMin(kKontext, von));
                h.after  = seite.mid(von + int(m.capturedLength()), kKontext);
                m_hits.push_back(h);
                ++genommen;
            }
        }
        //  Nach Seite und Lage sortieren, damit ▲/▼ die Treffer in der
        //  Reihenfolge des Dokuments abschreitet und nicht erst alle
        //  woertlichen und dann alle Muster-Treffer.
        std::stable_sort(m_hits.begin(), m_hits.end(),
                         [](const SearchHit& a, const SearchHit& b) {
                             if (a.page != b.page) return a.page < b.page;
                             const QRectF ra = a.bounds(), rb = b.bounds();
                             if (!qFuzzyCompare(ra.top() + 1, rb.top() + 1))
                                 return ra.top() < rb.top();
                             return ra.left() < rb.left();
                         });
    }

    m_searchPage = end;
    if (m_searchPage >= pageCount) {
        m_searchPage = -1;                       // fertig
        m_searchTimer.stop();
    }
    emit searchChanged();
}


QVariantList PdfTextController::searchHitsOnPage(int page) const {
    QVariantList out;
    if (!m_doc || page < 0)
        return out;
    const QSizeF pts = m_doc->pagePointSize(page);
    if (pts.width() <= 0.0 || pts.height() <= 0.0)
        return out;
    for (const SearchHit& h : m_hits) {
        if (h.page != page)
            continue;
        //  Gezeichnet werden ALLE Teilstücke - gezählt wird die Fundstelle nur
        //  einmal (s. searchCount).
        for (const QRectF& r : h.rects) {
            QVariantMap m;
            m.insert(QStringLiteral("x"), r.x()      / pts.width());
            m.insert(QStringLiteral("y"), r.y()      / pts.height());
            m.insert(QStringLiteral("w"), r.width()  / pts.width());
            m.insert(QStringLiteral("h"), r.height() / pts.height());
            out.push_back(m);
        }
    }
    return out;
}

QVariantMap PdfTextController::searchHit(int index) const {
    QVariantMap m;
    if (index < 0 || index >= m_hits.size())
        return m;
    const SearchHit& h = m_hits.at(index);
    const QRectF b = h.bounds();               // umschließt alle Teilstücke
    m.insert(QStringLiteral("page"),   h.page);
    m.insert(QStringLiteral("x"),      b.x());
    m.insert(QStringLiteral("y"),      b.y());
    m.insert(QStringLiteral("w"),      b.width());
    m.insert(QStringLiteral("h"),      b.height());
    m.insert(QStringLiteral("before"), h.before);
    m.insert(QStringLiteral("after"),  h.after);
    return m;
}

void PdfTextController::copyToClipboard() {
    if (m_selText.isEmpty())
        return;
    if (QClipboard* cb = QGuiApplication::clipboard())
        cb->setText(m_selText);
}
