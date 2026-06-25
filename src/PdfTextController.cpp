#include "PdfTextController.h"
#include "PathUtils.h"

#include <QPdfDocument>
#include <QPdfSelection>
#include <QGuiApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QCoreApplication>
#include <QThread>
#include <QSizeF>
#include <QPointF>
#include <QRectF>
#include <QPolygonF>
#include <QVariantMap>
#include <QRunnable>
#include <utility>

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
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
PdfTextController::PdfTextController(QObject* parent) : QObject(parent) {
    // Genau EIN Worker: nie sind zwei der teuren QPdfDocument-Instanzen
    // gleichzeitig am Laden → RAM-Peak bleibt gedeckelt.
    m_pool.setMaxThreadCount(1);
}

PdfTextController::~PdfTextController() {
    // Laufende/anstehende Ladevorgaenge sauber beenden, bevor der Controller
    // (und sein Dokument-Kind) verschwindet.
    m_pool.clear();
    m_pool.waitForDone();
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
    m_pendingPath.clear();
    m_activePath.clear();

    if (m_doc) {
        delete m_doc;
        m_doc = nullptr;
        emit readyChanged();
    }
    clearSelection();
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
    return applySelection(sel, page, ps.width(), ps.height());
}

// ─────────────────────────────────────────────────────────────────────────────
QVariantList PdfTextController::selectAllOnPage(int page) {
    if (!m_doc || page < 0 || page >= m_doc->pageCount())
        return {};

    const QSizeF ps = m_doc->pagePointSize(page);
    if (ps.isEmpty())
        return {};

    const QPdfSelection sel = m_doc->getAllText(page);
    return applySelection(sel, page, ps.width(), ps.height());
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

    m_selPage = page;
    if (text != m_selText) {
        m_selText = text;
        emit selectedTextChanged();
    }
    return rects;
}

// ─────────────────────────────────────────────────────────────────────────────
void PdfTextController::clearSelection() {
    m_selPage = -1;
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
