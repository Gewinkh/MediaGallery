#include "pdf/PdfTextController.h"
#include "core/PathUtils.h"

#include <QPdfDocument>
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

// ─────────────────────────────────────────────────────────────────────────────
//  PdfLoadTask — laedt das Auswahl-Dokument OHNE GUI-Thread.
//
//  Oeffnet eine EIGENE QPdfDocument-Instanz (Parsen blockiert nie die GUI),
//  verschiebt sie nach Erfolg auf den GUI-Thread und reicht sie per
//  QueuedConnection an den Controller. Bei Fehler wird nullptr uebergeben.
//  Die mitgefuehrte Generationszahl erlaubt dem Controller, veraltete Ergebnisse
//  (schnelles Vor/Zurueck) zu verwerfen.
//
//  Hinweis: Die QPointer-/Context-Form von invokeMethod verwirft den Aufruf
//  automatisch, falls der Controller zwischenzeitlich zerstoert wurde.
// ─────────────────────────────────────────────────────────────────────────────
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
            // Kuenftige Nutzung erfolgt ausschliesslich auf dem GUI-Thread.
            doc->moveToThread(QCoreApplication::instance()->thread());
        } else {
            delete doc;                 // Loeschen auf eigenem Thread → ok
            doc = nullptr;
        }

        PdfTextController* owner = m_owner;
        const QString path = m_path;
        const int     gen  = m_gen;
        QMetaObject::invokeMethod(owner, [owner, doc, path, gen]() {
            owner->adoptDocument(doc, path, gen);
        }, Qt::QueuedConnection);
    }

private:
    PdfTextController* m_owner;
    QString            m_path;
    int                m_gen;
};

// ─────────────────────────────────────────────────────────────────────────────
//  PdfOcrTask — rendert EINE Seite und erkennt ihren Text (Tesseract) OHNE
//  GUI-Thread. Öffnet dazu eine EIGENE, transiente QPdfDocument-Instanz aus dem
//  Pfad (thread-sicher, unabhängig vom GUI-Thread-Dokument), rendert bei
//  kOcrDpi und reicht die Zeilen per QueuedConnection an den Controller. Die
//  Generationszahl verwirft veraltete Läufe (Pfadwechsel/Freigabe).
// ─────────────────────────────────────────────────────────────────────────────
namespace { constexpr double kOcrDpi = 200.0; }

class PdfOcrTask : public QRunnable {
public:
    PdfOcrTask(PdfTextController* owner, QString localPath, int page, int generation)
        : m_owner(owner), m_path(std::move(localPath)), m_page(page), m_gen(generation) {
        setAutoDelete(true);
    }

    void run() override {
        QList<mg::OcrLine> lines;
        {
            QPdfDocument doc;   // transient, nur dieser Thread
            if (doc.load(m_path) == QPdfDocument::Error::None
                && doc.status() == QPdfDocument::Status::Ready
                && m_page >= 0 && m_page < doc.pageCount()) {
                const QSizeF ps = doc.pagePointSize(m_page);
                if (!ps.isEmpty()) {
                    const QSize px(qRound(ps.width()  * kOcrDpi / 72.0),
                                   qRound(ps.height() * kOcrDpi / 72.0));
                    const QImage img = doc.render(m_page, px);
                    if (!img.isNull())
                        lines = mg::ocr::recognize(img, kOcrDpi);
                }
            }
        }
        PdfTextController* owner = m_owner;
        const int page = m_page;
        const int gen  = m_gen;
        QMetaObject::invokeMethod(owner, [owner, page, lines, gen]() {
            owner->adoptOcr(page, lines, gen);
        }, Qt::QueuedConnection);
    }

private:
    PdfTextController* m_owner;
    QString            m_path;
    int                m_page;
    int                m_gen;
};
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
PdfTextController::PdfTextController(QObject* parent) : QObject(parent) {
    // Genau EIN Worker: nie sind zwei der teuren QPdfDocument-Instanzen
    // gleichzeitig am Laden → RAM-Peak bleibt gedeckelt.
    m_pool.setMaxThreadCount(1);
    m_ocrPool.setMaxThreadCount(1);   // OCR seriell, getrennt vom Ladepool
}

PdfTextController::~PdfTextController() {
    // Laufende/anstehende Ladevorgaenge sauber beenden, bevor der Controller
    // (und sein Dokument-Kind) verschwindet.
    m_pool.clear();
    m_pool.waitForDone();
    m_ocrPool.clear();
    m_ocrPool.waitForDone();
    // m_doc ist als Kind von 'this' geparented → wird automatisch geloescht.
}

// ─────────────────────────────────────────────────────────────────────────────
//  Lazy, asynchron: stoesst (nur falls noetig) das Laden des Auswahl-Dokuments an.
// ─────────────────────────────────────────────────────────────────────────────
void PdfTextController::prepare(const QString& pathOrUrl) {
    const QString local = mg::toLocalPath(pathOrUrl);
    if (local.isEmpty() || !QFileInfo::exists(local))
        return;

    // Bereits aktiv oder bereits am Laden → nichts zu tun (idempotent).
    if ((local == m_activePath && m_doc) || local == m_pendingPath)
        return;

    // Neues Ziel: vorheriges Ergebnis (falls noch unterwegs) wird verworfen.
    ++m_generation;
    m_pendingPath = local;
    m_pool.start(new PdfLoadTask(this, local, m_generation));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Ergebnis-Uebernahme auf dem GUI-Thread (vom Worker via QueuedConnection).
// ─────────────────────────────────────────────────────────────────────────────
void PdfTextController::adoptDocument(QPdfDocument* doc, const QString& localPath,
                                      int generation) {
    if (generation != m_generation) {
        // Veraltet (zwischenzeitlich prepare()/releaseDocument()) → verwerfen.
        if (doc)
            delete doc;     // doc lebt auf dem GUI-Thread → direktes delete ok
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
}

// ─────────────────────────────────────────────────────────────────────────────
void PdfTextController::releaseDocument() {
    ++m_generation;                 // evtl. laufenden Ladevorgang verwerfen
    ++m_ocrGen;                     // evtl. laufende OCR verwerfen
    m_pendingPath.clear();
    m_activePath.clear();
    m_ocrCache.clear();
    if (m_ocrBusy) { m_ocrBusy = false; emit ocrBusyChanged(); }

    if (m_doc) {
        delete m_doc;
        m_doc = nullptr;
        emit readyChanged();
    }
    clearSelection();
}

// ─────────────────────────────────────────────────────────────────────────────
//  OCR (gescannte PDFs)
// ─────────────────────────────────────────────────────────────────────────────
bool PdfTextController::ocrAvailable() const { return mg::ocr::available(); }

bool PdfTextController::hasOcr(int page) const { return m_ocrCache.contains(page); }

void PdfTextController::ocrPage(int page) {
    if (!mg::ocr::available() || m_activePath.isEmpty() || page < 0)
        return;
    if (m_ocrCache.contains(page) || m_ocrBusy)
        return;
    // Hat die Seite eine EINGEBETTETE Textebene, ist OCR unnötig (schneller,
    // exakter Weg bleibt): dann nur ein leeres Ergebnis cachen wäre falsch —
    // wir starten OCR gar nicht und lassen den eingebetteten Pfad greifen.
    if (m_doc && !lineRectsPts(page).isEmpty())
        return;
    m_ocrBusy = true;
    emit ocrBusyChanged();
    m_ocrPool.start(new PdfOcrTask(this, m_activePath, page, m_ocrGen));
}

void PdfTextController::adoptOcr(int page, const QList<mg::OcrLine>& lines,
                                 int generation) {
    if (generation != m_ocrGen)     // veraltet (Pfadwechsel/Freigabe) → verwerfen
        return;
    m_ocrCache.insert(page, lines);
    if (m_ocrBusy) { m_ocrBusy = false; emit ocrBusyChanged(); }
    emit ocrReady(page, lines.size());
}

// ─────────────────────────────────────────────────────────────────────────────
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
    // Gescannte Seite → OCR-basierte (zeilengranulare) Auswahl.
    return ocrSelection(page, QRectF(a, b).normalized(), ps.width(), ps.height(), false);
}

// ─────────────────────────────────────────────────────────────────────────────
QVariantList PdfTextController::selectAllOnPage(int page) {
    if (!m_doc || page < 0 || page >= m_doc->pageCount())
        return {};

    const QSizeF ps = m_doc->pagePointSize(page);
    if (ps.isEmpty())
        return {};

    const QPdfSelection sel = m_doc->getAllText(page);
    if (sel.isValid() && !sel.text().isEmpty())
        return applySelection(sel, page, ps.width(), ps.height());
    // Gescannte Seite → alle OCR-Zeilen auswählen.
    return ocrSelection(page, QRectF(), ps.width(), ps.height(), true);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Aus der QPdfSelection die normalisierten Highlight-Rechtecke bauen und den
//  Text merken. bounds() liefert Rechteck-Polygone in Punkten (Ursprung
//  oben-links) → boundingRect()/Seitengroesse ergibt normalisierte [0..1]-Rects.
// ─────────────────────────────────────────────────────────────────────────────
QVariantList PdfTextController::applySelection(const QPdfSelection& sel, int page,
                                               double pageWidthPts,
                                               double pageHeightPts) {
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

// ─────────────────────────────────────────────────────────────────────────────
//  OCR-Textauswahl (gescannte Seiten): OCR-Zeilen, die die Aufziehfläche
//  schneiden (oder alle bei selectAll), als normalisierte Highlights + Text.
// ─────────────────────────────────────────────────────────────────────────────
QVariantList PdfTextController::ocrSelection(int page, const QRectF& dragPts,
                                             double pageWidthPts, double pageHeightPts,
                                             bool selectAll) {
    QVariantList rects;
    const auto it = m_ocrCache.constFind(page);
    if (it == m_ocrCache.constEnd() || pageWidthPts <= 0.0 || pageHeightPts <= 0.0) {
        if (!m_selText.isEmpty()) { m_selText.clear(); emit selectedTextChanged(); }
        return rects;
    }
    QList<const mg::OcrLine*> hits;
    for (const mg::OcrLine& l : *it)
        if (selectAll || l.rectPts.intersects(dragPts))
            hits.append(&l);
    std::sort(hits.begin(), hits.end(), [](const mg::OcrLine* a, const mg::OcrLine* b) {
        return a->rectPts.center().y() < b->rectPts.center().y();
    });
    QStringList parts;
    rects.reserve(hits.size());
    for (const mg::OcrLine* l : std::as_const(hits)) {
        parts << l->text;
        QVariantMap m;
        m.insert(QStringLiteral("x"), l->rectPts.x()      / pageWidthPts);
        m.insert(QStringLiteral("y"), l->rectPts.y()      / pageHeightPts);
        m.insert(QStringLiteral("w"), l->rectPts.width()  / pageWidthPts);
        m.insert(QStringLiteral("h"), l->rectPts.height() / pageHeightPts);
        rects.append(m);
    }
    const QString text = parts.join(QLatin1Char('\n'));
    if (text != m_selText) {
        m_selText = text;
        emit selectedTextChanged();
    }
    return rects;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Zeilenfang des PDF-Editors: Fragment-Rechtecke aus getAllText() nach
//  vertikaler Mitte sortieren und zu ZEILEN vereinigen. Toleranz = 60 % der
//  kleineren Fragmenthöhe (robust gegen Hoch-/Tiefstellungen und leicht
//  versetzte Runs derselben Zeile).
// ─────────────────────────────────────────────────────────────────────────────
QList<QRectF> PdfTextController::lineRectsPts(int page) const {
    if (!m_doc || page < 0 || page >= m_doc->pageCount())
        return {};

    // Fallback für gescannte Seiten (keine/leere eingebettete Textebene):
    // die per OCR erkannten Zeilenrechtecke aus dem Cache.
    auto ocrRects = [this, page]() -> QList<QRectF> {
        const auto it = m_ocrCache.constFind(page);
        if (it == m_ocrCache.constEnd()) return {};
        QList<QRectF> lines;
        lines.reserve(it->size());
        for (const mg::OcrLine& l : *it) lines.append(l.rectPts);
        return lines;
    };

    const QPdfSelection sel = m_doc->getAllText(page);
    if (!sel.isValid())
        return ocrRects();

    QList<QRectF> frags;
    const QList<QPolygonF> bounds = sel.bounds();
    frags.reserve(bounds.size());
    for (const QPolygonF& poly : bounds) {
        const QRectF r = poly.boundingRect();
        if (r.width() > 0.5 && r.height() > 0.5)
            frags.append(r);
    }
    if (frags.isEmpty())
        return ocrRects();

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

// ─────────────────────────────────────────────────────────────────────────────
//  „Text ersetzen"-Sonde: getroffene Zeilen vereinigen (Zeilen-Einschnappen),
//  Ø-Zeilenhöhe für die Schriftgröße ableiten und den EINGEBETTETEN Text unter
//  der vereinigten Fläche extrahieren. Der Text wird über getSelection zwischen
//  den (leicht eingerückten) Ecken der Union geholt — bewusst OHNE die
//  Nutzer-Auswahl (m_selText) anzufassen.
// ─────────────────────────────────────────────────────────────────────────────
QVariantMap PdfTextController::replaceProbe(int page, double nx0, double ny0,
                                            double nx1, double ny1) {
    QVariantMap out;
    out.insert(QStringLiteral("found"), false);
    if (!m_doc || page < 0 || page >= m_doc->pageCount())
        return out;

    const QSizeF ps = m_doc->pagePointSize(page);
    if (ps.isEmpty())
        return out;

    // Aufgezogener Bereich in Punkten (normalisiert → Punkte, Ecken sortiert).
    const QRectF drag(QPointF(qMin(nx0, nx1) * ps.width(),  qMin(ny0, ny1) * ps.height()),
                      QPointF(qMax(nx0, nx1) * ps.width(),  qMax(ny0, ny1) * ps.height()));
    // Entartete Klicks (kein echtes Aufziehen) treffen bewusst nichts.
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
        // hohen Zeile zählt ebenfalls) — Streifschüsse zählen nicht.
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
    // Gescannte Seite (kein eingebetteter Text): Text aus den OCR-Zeilen holen,
    // die die Union vertikal überlappen (Reihenfolge nach y).
    if (text.isEmpty()) {
        const auto it = m_ocrCache.constFind(page);
        if (it != m_ocrCache.constEnd()) {
            QList<const mg::OcrLine*> hits;
            for (const mg::OcrLine& l : *it) {
                const qreal ovY = qMin(uni.bottom(), l.rectPts.bottom())
                                  - qMax(uni.top(), l.rectPts.top());
                if (ovY > qMax(0.5, l.rectPts.height() * 0.35))
                    hits.append(&l);
            }
            std::sort(hits.begin(), hits.end(), [](const mg::OcrLine* a, const mg::OcrLine* b) {
                return a->rectPts.center().y() < b->rectPts.center().y();
            });
            QStringList parts;
            for (const mg::OcrLine* l : std::as_const(hits)) parts << l->text;
            text = parts.join(QLatin1Char('\n'));
        }
    }

    out.insert(QStringLiteral("found"), true);
    out.insert(QStringLiteral("x"),     uni.x()          / ps.width());
    out.insert(QStringLiteral("y"),     uni.y()          / ps.height());
    out.insert(QStringLiteral("w"),     uni.width()      / ps.width());
    out.insert(QStringLiteral("h"),     uni.height()     / ps.height());
    out.insert(QStringLiteral("lineH"), (hSum / n)       / ps.height());
    out.insert(QStringLiteral("text"),  text);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
void PdfTextController::clearSelection() {
    if (!m_selText.isEmpty()) {
        m_selText.clear();
        emit selectedTextChanged();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void PdfTextController::copyToClipboard() {
    if (m_selText.isEmpty())
        return;
    if (QClipboard* cb = QGuiApplication::clipboard())
        cb->setText(m_selText);
}
