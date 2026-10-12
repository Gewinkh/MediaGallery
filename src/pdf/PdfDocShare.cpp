#include "pdf/PdfDocShare.h"
#include "pdf/PdfRawScan.h"

#include <QDateTime>
#include <QFileInfo>
#include <QMultiHash>
#include <QPdfDocument>
#include <QThread>
#include <QUrl>
#include <QVariant>
#include <algorithm>
#include <cstring>
#include <mutex>

namespace PdfDocShare {

// Jede Instanz haelt eine gescannte Datei fast ganz im Speicher (110 MB Datei: +106 MB anon schon nach load()).
struct Entry {
    QPdfDocument* doc = nullptr;
    QObject*      quick = nullptr;
    QString       path;
    qint64        size = -1;
    QDateTime     modified;
    quint64       serial = 0;
    bool          detached = false;   // aus dem QML-Dokument geloest: der letzte Halter loescht es

    ~Entry() {
        if (!detached || !doc) return;
        if (QThread::currentThread() == doc->thread()) delete doc;
        else doc->deleteLater();
    }
};

namespace {

struct Registry {
    std::mutex m;
    QMultiHash<QString, std::shared_ptr<Entry>> byPath;
    quint64 serial = 0;
};

Registry& registry() {
    static Registry r;
    return r;
}

// PDFium prueft beim Laden alles, worauf eine Seite zeigt, und hielt so jeden Tonstrom im Speicher (110 MB Datei mit
// 59 Toenen: +106 MB anon, mit der Sicht 1,5 MB; alle Seiten pixelgleich). Ein QFile, weil QPdfDocument::fileName() den
// Namen nur von einem QFile nimmt - PdfPageImage oeffnet ihn als Traeger. Gelesen wird ohne Abbildung (zaehlte zum RSS).
class LoadView : public QFile {
public:
    explicit LoadView(const QString& path) : QFile(path) {}

    bool start() {
        {
            QFile f(fileName());
            if (!f.open(QIODevice::ReadOnly)) return false;
            const QByteArray d = mg::pdfraw::mapWhole(f);
            m_masks = mg::pdfraw::mediaRefMasks(d.isEmpty() ? f.readAll() : d);
        }
        return open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

protected:
    qint64 readData(char* out, qint64 max) override {
        const qint64 p = pos();
        const qint64 n = QFile::readData(out, max);
        if (n <= 0) return n;
        auto it = std::lower_bound(m_masks.cbegin(), m_masks.cend(), p,
                                   [](const QPair<qsizetype,qsizetype>& m, qint64 v) { return m.first + m.second <= v; });
        for (; it != m_masks.cend() && it->first < p + n; ++it)
            for (qint64 k = qMax<qint64>(it->first, p); k < qMin<qint64>(it->first + it->second, p + n); ++k)
                out[k - p] = (k - it->first) < 4 ? "null"[k - it->first] : ' ';
        return n;
    }

private:
    QVector<QPair<qsizetype,qsizetype>> m_masks;
};

// Laedt `doc` ueber die Sicht (Rueckfall: der Pfad); eine fruehere Sicht faellt danach weg.
void loadInto(QPdfDocument* doc, const QString& path) {
    QList<LoadView*> alt;
    for (QObject* k : doc->children())
        if (auto* v = dynamic_cast<LoadView*>(k)) alt << v;
    auto* view = new LoadView(path);
    if (view->start()) {
        view->setParent(doc);
        doc->load(view);
    } else {
        delete view;
        doc->load(path);
    }
    qDeleteAll(alt);
}

void withdraw(const std::weak_ptr<Entry>& weak) {
    std::shared_ptr<Entry> e = weak.lock();
    if (!e) return;
    Registry& r = registry();
    std::lock_guard lock(r.m);
    r.byPath.remove(e->path, e);
    // Noch geliehen: vor dem Loeschen der Kinder herausloesen. destroyed() kommt in ~QObject vor deleteChildren().
    if (e.use_count() > 1) {
        e->doc->setParent(nullptr);
        e->detached = true;
    }
}

} // namespace

Lease::Lease(Lease&&) noexcept = default;
Lease& Lease::operator=(Lease&&) noexcept = default;
Lease::~Lease() = default;

QPdfDocument* Lease::doc() const {
    return m_entry ? m_entry->doc : m_own.get();
}

void Lease::reset() {
    m_entry.reset();
    m_own.reset();
}

Lease Lease::adopt(std::unique_ptr<QPdfDocument> own) {
    Lease l;
    l.m_own = std::move(own);
    return l;
}

namespace {

void offer(QObject* quickDoc, QPdfDocument* doc, const QString& path) {
    auto e = std::make_shared<Entry>();
    e->doc = doc;
    e->quick = quickDoc;
    e->path = path;
    const QFileInfo fi(path);
    e->size = fi.size();
    e->modified = fi.lastModified();

    Registry& r = registry();
    {
        std::lock_guard lock(r.m);
        for (auto it = r.byPath.constFind(path); it != r.byPath.cend() && it.key() == path; ++it)
            if ((*it)->quick == quickDoc) return;
        e->serial = ++r.serial;
        r.byPath.insert(path, e);
    }
    QObject::connect(quickDoc, &QObject::destroyed, [w = std::weak_ptr<Entry>(e)] { withdraw(w); });
}

}  // namespace

bool load(QObject* quickDoc, const QString& localPath) {
    auto* doc = quickDoc ? quickDoc->findChild<QPdfDocument*>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
    if (!doc || localPath.isEmpty()) return false;
    loadInto(doc, localPath);
    if (doc->status() != QPdfDocument::Status::Ready) return false;
    offer(quickDoc, doc, localPath);
    return true;
}

Lease borrow(const QString& localPath) {
    Lease l;
    if (localPath.isEmpty()) return l;
    std::shared_ptr<Entry> best;
    {
        Registry& r = registry();
        std::lock_guard lock(r.m);
        for (auto it = r.byPath.constFind(localPath); it != r.byPath.cend() && it.key() == localPath; ++it)
            if (!best || (*it)->serial > best->serial) best = *it;
    }
    // Umgeschrieben (Seitenoperation, Speichern): wer neu lesen will, bekommt nicht den alten Stand.
    if (best) {
        const QFileInfo fi(localPath);
        if (fi.size() == best->size && fi.lastModified() == best->modified)
            l.m_entry = std::move(best);
    }
    return l;
}

Lease open(const QString& localPath) {
    Lease l = borrow(localPath);
    if (l) return l;
    if (localPath.isEmpty()) return l;
    auto own = std::make_unique<QPdfDocument>();
    loadInto(own.get(), localPath);
    if (own->status() != QPdfDocument::Status::Ready)
        return l;
    return Lease::adopt(std::move(own));
}

} // namespace PdfDocShare
