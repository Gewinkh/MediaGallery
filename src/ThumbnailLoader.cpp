#include "ThumbnailLoader.h"
#include "MediaItem.h"

#include <QPdfDocument>
#include <QPainter>
#include <QLinearGradient>
#include <QPixmap>
#include <QImage>
#include <QImageReader>
#include <QFont>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>
#include <QCryptographicHash>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QThread>
#include <QVideoFrame>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QFile>
#include <QTextStream>
#include <QPainterPath>
#include <QRegularExpression>
#include <QColor>
#include <QGuiApplication>
#include <QHash>

// ─── Disk-Cache-Helfer ───────────────────────────────────────────────────────
namespace {

QString cacheDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(base + QStringLiteral("/thumbs"));
    return base + QStringLiteral("/thumbs");
}

QString cacheKeyFor(const QString& path) {
    // mtime einbeziehen → ersetzte/bearbeitete Dateien erhalten frische Thumbnails.
    // v4: feste Generierungsgröße (kThumbDim) statt variabler Kachelgröße.
    // v5: HTML/HTM rendern jetzt als Design-Karte statt Quelltext → alte
    //     gecachte Quelltext-Thumbnails müssen einmalig invalidiert werden.
    // v6: verbesserte Extraktion (Hero-Klassen-Farben, Arabisch-Sterne,
    //     Bilingual-Titel-Split) → Karten erneut regenerieren.
    // v7: arabischer Display-Titel als Haupttitel + Sekundärzeile, div-Untertitel.
    // v8: PDF-Thumbnails werden jetzt immer auf weißen Untergrund compositet
    //     (statt ggf. transparent→schwarz beim JPEG-Export) → alte, fälschlich
    //     schwarze PDF-Thumbnails müssen einmalig neu generiert werden.
    //     (Bei künftigen Änderungen an der Thumbnail-Darstellung weiter hochzählen.)
    static const int kCacheVersion = 8;
    const qint64 mtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    const QByteArray raw =
        (path + QChar('|')
         + QString::number(ThumbnailLoader::kThumbDim) + QChar('|')
         + QString::number(mtime) + QChar('|')
         + QString::number(kCacheVersion)).toUtf8();
    return cacheDir() + QStringLiteral("/")
           + QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Md5).toHex())
           + QStringLiteral(".jpg");
}

} // namespace

// ─── ThumbnailLoader ─────────────────────────────────────────────────────────
ThumbnailLoader::ThumbnailLoader(QObject* parent)
    : QObject(parent)
    , m_pool(new QThreadPool(this))
{
    const int threads = qMin(8, qMax(2, QThread::idealThreadCount()));
    m_pool->setMaxThreadCount(threads);
    m_pool->setExpiryTimeout(30000);
}

ThumbnailLoader::~ThumbnailLoader() {
    // Alle laufenden Tasks kooperativ stoppen, dann auf den Pool warten.
    {
        QMutexLocker lk(&m_mutex);
        for (auto& f : m_flags)
            if (f) f->store(true, std::memory_order_relaxed);
    }
    m_pool->clear();
    m_pool->waitForDone(2000);
}

void ThumbnailLoader::requestThumbnail(const QString& filePath) {
    if (filePath.isEmpty()) return;
    {
        QMutexLocker lk(&m_mutex);
        if (m_pending.contains(filePath)) return;   // bereits in Arbeit
    }

    // ── Schneller Pfad: Cache-Datei existiert bereits ────────────────────────
    //  Kein Pool-Dispatch, kein Decode — nur eine Existenzprüfung und ein
    //  queued Signal. Das ist der Normalfall beim Scrollen über bereits
    //  generierte Thumbnails und hält den Pool für echte Misses frei.
    const QString cachePath = cacheKeyFor(filePath);
    if (QFileInfo::exists(cachePath)) {
        const QString url = QUrl::fromLocalFile(cachePath).toString();
        QMetaObject::invokeMethod(this, [this, filePath, url]() {
            emit thumbnailReady(filePath, url);
        }, Qt::QueuedConnection);
        return;
    }

    // ── Miss: Task mit Abbruch-Flag + Priorität einreihen ────────────────────
    const uint64_t gen = m_generation.load(std::memory_order_relaxed);
    auto flag = std::make_shared<std::atomic<bool>>(false);
    auto* task = new ThumbnailTask(filePath, QSize(kThumbDim, kThumbDim), gen, flag);
    task->setAutoDelete(true);

    {
        QMutexLocker lk(&m_mutex);
        m_pending.insert(filePath);
        m_queued.insert(filePath, task);
        m_flags.insert(filePath, flag);
    }

    connect(task, &ThumbnailTask::done, this,
            [this](const QString& path, const QString& thumbPath, bool ok, uint64_t taskGen) {
        bool stale, cancelled;
        {
            QMutexLocker lk(&m_mutex);
            const auto it = m_flags.constFind(path);
            cancelled = (it != m_flags.constEnd()) && it.value()
                        && it.value()->load(std::memory_order_relaxed);
            m_pending.remove(path);
            m_queued.remove(path);
            m_flags.remove(path);
            stale = (taskGen != m_generation.load(std::memory_order_relaxed));
        }
        // Nach cancelAll() (stale) oder gezieltem Abbruch: still verwerfen,
        // damit die Modell-Zeile NICHT als „failed“ markiert wird.
        if (stale || cancelled) return;

        if (ok)
            emit thumbnailReady(path, QUrl::fromLocalFile(thumbPath).toString());
        else
            emit thumbnailFailed(path);
    }, Qt::QueuedConnection);

    // Neuere Anforderung = höhere Priorität → gerade sichtbare Kacheln zuerst.
    m_pool->start(task, ++m_priority);
}

void ThumbnailLoader::cancelThumbnail(const QString& filePath) {
    if (filePath.isEmpty()) return;

    ThumbnailTask* task = nullptr;
    CancelFlag flag;
    {
        QMutexLocker lk(&m_mutex);
        if (const auto fit = m_flags.constFind(filePath); fit != m_flags.constEnd())
            flag = fit.value();
        if (const auto qit = m_queued.constFind(filePath); qit != m_queued.constEnd())
            task = qit.value();
    }

    // Laufende Tasks kooperativ abbrechen.
    if (flag) flag->store(true, std::memory_order_relaxed);

    // Noch nicht gestartete Tasks direkt aus der Queue nehmen → sie laufen nie.
    if (task && m_pool->tryTake(task)) {
        {
            QMutexLocker lk(&m_mutex);
            m_pending.remove(filePath);
            m_queued.remove(filePath);
            m_flags.remove(filePath);
        }
        delete task;   // autoDelete greift nicht, da aus dem Pool entnommen
    }
    // Falls bereits gestartet: Flag sorgt für frühen Abbruch; done() räumt auf.
}

void ThumbnailLoader::cancelAll() {
    // Noch nicht gestartete Tasks entfernen & löschen (autoDelete).
    m_pool->clear();
    // Laufende Ergebnisse über Generationswechsel verwerfen …
    m_generation.fetch_add(1, std::memory_order_relaxed);

    QMutexLocker lock(&m_mutex);
    // … und laufende Tasks zusätzlich kooperativ abbrechen.
    for (auto& f : m_flags)
        if (f) f->store(true, std::memory_order_relaxed);
    m_flags.clear();
    m_queued.clear();   // Zeiger gehören nun dem Pool (gelöscht) bzw. laufen aus
    m_pending.clear();
}

// ─── ThumbnailTask ───────────────────────────────────────────────────────────
ThumbnailTask::ThumbnailTask(const QString& path, const QSize& size, uint64_t generation,
                             std::shared_ptr<std::atomic<bool>> cancel)
    : m_path(path), m_size(size), m_generation(generation), m_cancel(std::move(cancel))
{
    setAutoDelete(true);
}

void ThumbnailTask::run() {
    // Früher Ausstieg, falls bereits vor dem Start abgebrochen.
    if (cancelled()) {
        emit done(m_path, QString(), false, m_generation);
        return;
    }

    const QString cachePath = cacheKeyFor(m_path);

    // Disk-Cache-Treffer: keine Dekodierung nötig (nur Existenzprüfung im Pool).
    if (QFileInfo::exists(cachePath)) {
        emit done(m_path, cachePath, true, m_generation);
        return;
    }

    // Vor dem teuren Decode erneut prüfen (Kachel evtl. schon weggescrollt).
    if (cancelled()) {
        emit done(m_path, QString(), false, m_generation);
        return;
    }

    const MediaType t = MediaItem::detectType(m_path);
    QPixmap pix;
    if      (t == MediaType::Image) pix = generateImageThumbnail(m_path, m_size);
    else if (t == MediaType::Video) pix = generateVideoThumbnail(m_path, m_size);
    else if (t == MediaType::Audio) pix = generateAudioThumbnail(m_path, m_size);
    else if (t == MediaType::Pdf)   pix = generatePdfThumbnail(m_path, m_size);
    else if (t == MediaType::Text)  pix = generateTextThumbnail(m_path, m_size);

    if (pix.isNull()) {
        emit done(m_path, QString(), false, m_generation);
        return;
    }

    // Nach dem Decode, vor dem Speichern: abgebrochene Ergebnisse verwerfen.
    if (cancelled()) {
        emit done(m_path, QString(), false, m_generation);
        return;
    }

    const bool saved = pix.save(cachePath, "JPG", 85);
    emit done(m_path, saved ? cachePath : QString(), saved, m_generation);
}

QPixmap ThumbnailTask::generateAudioThumbnail(const QString& path, const QSize& size) {
    QPixmap pix(size);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient grad(0, 0, 0, size.height());
    grad.setColorAt(0, QColor(55, 35, 90));
    grad.setColorAt(1, QColor(25, 15, 45));
    p.fillRect(pix.rect(), grad);

    const int barCount = 12;
    const int barW = qMax(2, size.width() / (barCount * 2));
    const int centerY = size.height() / 2;
    QColor waveColor(180, 140, 255, 160);
    p.setPen(Qt::NoPen);
    p.setBrush(waveColor);
    static const float heights[] = {0.3f,0.6f,0.9f,0.7f,1.0f,0.5f,0.8f,0.4f,0.95f,0.65f,0.75f,0.35f};
    const int totalW = barCount * barW * 2;
    const int startX = (size.width() - totalW) / 2;
    for (int i = 0; i < barCount; ++i) {
        const int h = int(heights[i] * size.height() * 0.35f);
        const int x = startX + i * barW * 2;
        p.drawRoundedRect(x, centerY - h, barW, h * 2, 1, 1);
    }

    const QString fmt = QFileInfo(path).suffix().toUpper();
    QFont fnt("Arial", qMax(10, size.width() / 8), QFont::Bold);
    p.setFont(fnt);
    p.setPen(QColor(220, 190, 255));
    p.drawText(pix.rect().adjusted(0, 0, 0, -size.height()/4), Qt::AlignHCenter | Qt::AlignBottom, fmt);

    p.setPen(QColor(180, 140, 255, 120));
    p.setFont(QFont("Arial", qMax(14, size.width() / 5)));
    p.drawText(pix.rect().adjusted(0, size.height()/10, 0, -size.height()/2), Qt::AlignCenter, QStringLiteral("\u266A"));

    p.end();
    return pix;
}

QPixmap ThumbnailTask::generateImageThumbnail(const QString& path, const QSize& size) {
    QImageReader reader(path);
    reader.setAutoTransform(true);

    const QSize imgSize = reader.size();
    if (imgSize.isValid() && (imgSize.width() > size.width() || imgSize.height() > size.height()))
        reader.setScaledSize(imgSize.scaled(size, Qt::KeepAspectRatio));

    QImage img = reader.read();
    if (img.isNull()) return QPixmap();

    if (img.width() > size.width() || img.height() > size.height())
        img = img.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    return QPixmap::fromImage(std::move(img));
}

QPixmap ThumbnailTask::generateVideoThumbnail(const QString& path, const QSize& size) {
    // QMediaPlayer benötigt eine Event-Loop und darf NICHT im generischen Pool-
    // Worker laufen → dedizierter QThread, plattformübergreifend korrekt.
    QPixmap result;

    QThread thread;
    QObject context;
    QMediaPlayer* player = nullptr;
    QVideoSink*   sink   = nullptr;
    bool          gotFrame = false;
    QMutex        mutex;
    QWaitCondition cond;

    player = new QMediaPlayer;
    sink   = new QVideoSink;
    player->moveToThread(&thread);
    sink->moveToThread(&thread);

    QObject::connect(sink, &QVideoSink::videoFrameChanged,
                     &context, [&](const QVideoFrame& frame) {
        QMutexLocker lk(&mutex);
        if (gotFrame || !frame.isValid()) return;
        gotFrame = true;
        QImage img = frame.toImage();
        if (!img.isNull())
            result = QPixmap::fromImage(
                img.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        cond.wakeAll();
    }, Qt::DirectConnection);

    QObject::connect(&thread, &QThread::started, player, [&]() {
        player->setVideoSink(sink);
        player->setSource(QUrl::fromLocalFile(path));
        player->play();
    });

    thread.start();

    {
        QMutexLocker lk(&mutex);
        cond.wait(&mutex, 4000);
    }

    QMetaObject::invokeMethod(player, [&]() {
        player->stop();
        player->deleteLater();
        sink->deleteLater();
        thread.quit();
    }, Qt::QueuedConnection);

    thread.wait(5000);
    return result;
}

QPixmap ThumbnailTask::generatePdfThumbnail(const QString& path, const QSize& size) {
    QPdfDocument doc;
    if (doc.load(path) != QPdfDocument::Error::None)
        return fallbackPdfThumbnail(size);

    const QSizeF pageSize = doc.pagePointSize(0);
    if (pageSize.isEmpty())
        return fallbackPdfThumbnail(size);

    const double scale = qMin(size.width() / pageSize.width(),
                              size.height() / pageSize.height());
    const QSize renderSize(qMax(1, static_cast<int>(pageSize.width()  * scale)),
                           qMax(1, static_cast<int>(pageSize.height() * scale)));

    QImage img = doc.render(0, renderSize);
    if (img.isNull())
        return fallbackPdfThumbnail(size);

    // QPdfDocument::render() liefert bei manchen Dokumenten (abhängig vom
    // internen PDFium-Renderpfad, z. B. schlanke, bildarme Text-PDFs) ein
    // Bild mit TRANSPARENTEM statt weißem Seitenhintergrund — nur tatsächlich
    // gezeichnete Inhalte (Text, farbige Emoji) sind opak. Da der Disk-Cache
    // als JPEG (kein Alphakanal) gespeichert wird, würde der transparente
    // Bereich beim Speichern zu SCHWARZ statt WEISS. Daher hier immer explizit
    // auf einen weißen Untergrund compositen — für bereits opake Renderings
    // (Normalfall) ändert das sichtbar nichts.
    QImage flattened(img.size(), QImage::Format_RGB32);
    flattened.fill(Qt::white);
    {
        QPainter fp(&flattened);
        fp.drawImage(0, 0, img);
    }

    QPixmap page = QPixmap::fromImage(std::move(flattened));
    {
        QPainter p(&page);
        QFont f = p.font();
        f.setPixelSize(qMax(9, renderSize.height() / 20));
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(200, 50, 50, 200));
        const int bh = qMax(14, renderSize.height() / 18);
        const int bw = bh * 2;
        const QRect badge(3, renderSize.height() - bh - 3, bw, bh);
        p.drawRoundedRect(badge, 3, 3);
        p.setPen(Qt::white);
        p.drawText(badge, Qt::AlignCenter, QStringLiteral("PDF"));
    }
    return page;
}

QPixmap ThumbnailTask::fallbackPdfThumbnail(const QSize& size) {
    QPixmap pix(size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient grad(0, 0, 0, size.height());
    grad.setColorAt(0, QColor(90, 20, 20));
    grad.setColorAt(1, QColor(40,  8,  8));
    p.fillRect(pix.rect(), grad);

    QFont f = p.font();
    f.setPixelSize(qMax(18, size.width() / 4));
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(255, 180, 160, 200));
    p.drawText(pix.rect(), Qt::AlignCenter, QStringLiteral("PDF"));
    p.end();
    return pix;
}

static void drawExtensionBadge(QPainter& p, const QSize& size, const QString& ext) {
    if (ext.isEmpty()) return;
    QFont bf("Monospace");
    bf.setStyleHint(QFont::Monospace);
    bf.setBold(true);
    bf.setPixelSize(qMax(9, size.height() / 22));
    p.setFont(bf);

    const QString label = ext.toUpper();
    QFontMetrics fm(bf);
    const int textW = fm.horizontalAdvance(label);
    const int bh = qMax(14, size.height() / 18);
    const int bw = textW + bh;
    const int margin = qMax(3, size.width() / 60);
    const QRect badge(size.width() - bw - margin, margin, bw, bh);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 150, 136, 210));
    p.drawRoundedRect(badge, 3, 3);
    p.setPen(Qt::white);
    p.drawText(badge, Qt::AlignCenter, label);
}

// ─────────────────────────────────────────────────────────────────────────────
//  HTML-„Design-Karte" — generische Hero-Nachbildung (statt Quelltext-Thumbnail)
//
//  Bildet den HERO-BLOCK der Seite als Mini-Karte nach: kein WebEngine, keine
//  Meta-Tags, kein Editieren der Dateien. Rein durch Parsen dessen, was die
//  Lern-Sheets gemeinsam haben — <header class="hero"> mit .eyebrow / <h1> /
//  .sub sowie CSS-Variablen (--bg/--ink/…). Funktioniert über verschiedene
//  Themes hinweg (hell/dunkel, LTR/RTL, mit/ohne Muster).
// ─────────────────────────────────────────────────────────────────────────────
namespace {

struct HtmlMeta {
    QString title, secondaryTitle, eyebrow, subtitle, chip;
    QColor  bg, text, accent, sub;
    bool    dark = true;
    bool    rtl  = false;
    int     texture = 0;            // 0 keine, 1 Gitter, 2 Sterne (Khatam)
    bool    valid() const { return !title.isEmpty(); }
};

// HTML-Entities dekodieren (numerisch + die in den Sheets üblichen benannten).
QString htmlDecodeEntities(QString s) {
    static const QRegularExpression reNum(QStringLiteral("&#(x?)([0-9a-fA-F]+);"));
    {
        QString out; out.reserve(s.size());
        qsizetype last = 0;
        auto it = reNum.globalMatch(s);
        while (it.hasNext()) {
            const auto m = it.next();
            out += s.mid(last, m.capturedStart() - last);
            bool ok = false;
            const uint cp = m.captured(2).toUInt(&ok, m.captured(1).isEmpty() ? 10 : 16);
            if (ok && cp) { const char32_t c = static_cast<char32_t>(cp); out += QString::fromUcs4(&c, 1); }
            last = m.capturedEnd();
        }
        out += s.mid(last);
        s = out;
    }
    static const QHash<QString, QString> ent = {
        {QStringLiteral("amp"),QStringLiteral("&")},   {QStringLiteral("lt"),QStringLiteral("<")},
        {QStringLiteral("gt"),QStringLiteral(">")},    {QStringLiteral("quot"),QStringLiteral("\"")},
        {QStringLiteral("apos"),QStringLiteral("'")},  {QStringLiteral("nbsp"),QStringLiteral(" ")},
        {QStringLiteral("shy"),QString()},             {QStringLiteral("mdash"),QStringLiteral("\u2014")},
        {QStringLiteral("ndash"),QStringLiteral("\u2013")},{QStringLiteral("middot"),QStringLiteral("\u00B7")},
        {QStringLiteral("hellip"),QStringLiteral("\u2026")},{QStringLiteral("times"),QStringLiteral("\u00D7")},
        {QStringLiteral("deg"),QStringLiteral("\u00B0")},{QStringLiteral("szlig"),QStringLiteral("\u00DF")},
        {QStringLiteral("auml"),QStringLiteral("\u00E4")},{QStringLiteral("ouml"),QStringLiteral("\u00F6")},
        {QStringLiteral("uuml"),QStringLiteral("\u00FC")},{QStringLiteral("Auml"),QStringLiteral("\u00C4")},
        {QStringLiteral("Ouml"),QStringLiteral("\u00D6")},{QStringLiteral("Uuml"),QStringLiteral("\u00DC")},
    };
    static const QRegularExpression reName(QStringLiteral("&([a-zA-Z]+);"));
    QString out; out.reserve(s.size());
    qsizetype last = 0;
    auto it = reName.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        out += ent.value(m.captured(1), m.captured(0));     // unbekannt → Original behalten
        last = m.capturedEnd();
    }
    out += s.mid(last);
    return out;
}

// Tags entfernen, Entities dekodieren, Weichtrennzeichen weg, Whitespace glätten.
QString htmlStrip(QString s) {
    static const QRegularExpression reTag(QStringLiteral("<[^>]*>"));
    s.remove(reTag);
    s = htmlDecodeEntities(s);
    s.remove(QChar(0x00AD));                  // &shy; (Weiches Trennzeichen)
    s.replace(QChar(0x00A0), QChar(u' '));    // &nbsp;
    return s.simplified();
}

// CSS-Custom-Properties einsammeln (erstes Vorkommen gewinnt).
QHash<QString, QString> cssVars(const QString& css) {
    QHash<QString, QString> v;
    static const QRegularExpression re(QStringLiteral("--([a-z0-9-]+)\\s*:\\s*([^;}]+)"),
                                       QRegularExpression::CaseInsensitiveOption);
    auto it = re.globalMatch(css);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString k = m.captured(1).toLower();
        if (!v.contains(k)) v.insert(k, m.captured(2).trimmed());
    }
    return v;
}

// var(--x[, fallback]) rekursiv auflösen.
QString cssResolve(QString val, const QHash<QString, QString>& v, int depth = 0) {
    val = val.trimmed();
    if (depth > 6) return val;
    static const QRegularExpression re(QStringLiteral("var\\(\\s*--([a-z0-9-]+)\\s*(?:,\\s*([^)]+))?\\)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(val);
    if (m.hasMatch()) {
        const QString name = m.captured(1).toLower();
        return cssResolve(v.contains(name) ? v.value(name) : m.captured(2), v, depth + 1);
    }
    return val;
}

QString cssRuleBody(const QString& css, const QString& selector) {
    const QRegularExpression re(QRegularExpression::escape(selector) + QStringLiteral("\\s*\\{([^}]*)\\}"),
                               QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(css);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString cssProp(const QString& body, const QString& name) {
    const QRegularExpression re(QStringLiteral("(?:^|[;{\\s])") + QRegularExpression::escape(name)
                                + QStringLiteral("\\s*:\\s*([^;}]+)"), QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(body);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

QColor parseCssColor(QString tok) {
    tok = tok.trimmed();
    if (tok.startsWith(QStringLiteral("rgb"), Qt::CaseInsensitive)) {
        static const QRegularExpression re(QStringLiteral("rgba?\\(([^)]*)\\)"), QRegularExpression::CaseInsensitiveOption);
        const auto m = re.match(tok);
        if (m.hasMatch()) {
            static const QRegularExpression sep(QStringLiteral("[\\s,/]+"));
            const QStringList parts = m.captured(1).split(sep, Qt::SkipEmptyParts);
            auto chan = [](const QString& s) -> int {
                return s.endsWith(QChar(u'%')) ? int(s.left(s.size() - 1).toDouble() * 2.55 + 0.5)
                                               : int(s.toDouble() + 0.5);
            };
            if (parts.size() >= 3) {
                const int a = parts.size() >= 4 ? int(parts[3].toDouble() * 255 + 0.5) : 255;
                return QColor(qBound(0, chan(parts[0]), 255), qBound(0, chan(parts[1]), 255),
                              qBound(0, chan(parts[2]), 255), qBound(0, a, 255));
            }
        }
        return QColor();
    }
    return QColor::fromString(tok);
}

// Erstes Farb-Token aus einer Deklaration (var() vorher aufgelöst).
QColor firstColor(QString decl, const QHash<QString, QString>& v) {
    if (decl.isEmpty()) return QColor();
    decl = cssResolve(decl, v);
    static const QRegularExpression re(QStringLiteral("#[0-9a-fA-F]{3,8}|rgba?\\([^)]*\\)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(decl);
    return m.hasMatch() ? parseCssColor(m.captured(0)) : QColor();
}

double relLuminance(const QColor& c) {
    return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
}

HtmlMeta extractHtmlMeta(const QString& path) {
    HtmlMeta r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return r;
    // Ganze Datei (bis 4 MB) — der Hero kann hinter großem <head>/<style> liegen.
    const QString t = QString::fromUtf8(f.read(4 * 1024 * 1024));
    f.close();
    if (t.isEmpty()) return r;

    const QHash<QString, QString> vars = cssVars(t);

    // Hero-Block + Hero-Klasse(n) isolieren (Klasse → Quelle für Farben)
    QString block, heroClass;
    {
        // 1) <header …class="…hero…">…</header>
        static const QRegularExpression reHeroHdr(QStringLiteral("<header[^>]*class=\"([^\"]*hero[^\"]*)\"[^>]*>(.*?)</header>"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        // 2) beliebiges Element mit Hero-artiger Klasse → Fenster ab dort (verschachtelte divs)
        static const QRegularExpression reHeroOpen(QStringLiteral("<(?:div|section|header|main)[^>]*class=\"([^\"]*(?:hero|masthead|banner|cover|intro|frontispiece|page-header|titlebar|kopf|titel)[^\"]*)\"[^>]*>"),
            QRegularExpression::CaseInsensitiveOption);
        // 3) beliebiger <header>
        static const QRegularExpression reAnyHdr(QStringLiteral("<header\\b[^>]*>(.*?)</header>"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        const auto m1 = reHeroHdr.match(t);
        if (m1.hasMatch()) { heroClass = m1.captured(1); block = m1.captured(2); }
        else {
            const auto m2 = reHeroOpen.match(t);
            if (m2.hasMatch()) { heroClass = m2.captured(1); block = t.mid(m2.capturedStart(), 4000); }
            else {
                const auto m3 = reAnyHdr.match(t);
                block = m3.hasMatch() ? m3.captured(1) : t;
            }
        }
    }

    // Titel = erstes <h1>; Fallback <title>
    {
        static const QRegularExpression re(QStringLiteral("<h1[^>]*>(.*?)</h1>"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        const auto m = re.match(block);
        if (m.hasMatch()) r.title = htmlStrip(m.captured(1));
    }
    if (r.title.isEmpty()) {
        static const QRegularExpression re(QStringLiteral("<title[^>]*>(.*?)</title>"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        const auto m = re.match(t);
        if (m.hasMatch()) r.title = htmlStrip(m.captured(1)).section(QChar(0x00B7), 0, 0).trimmed();
    }
    if (r.title.isEmpty()) {       // letzter Ausweg: erstes <h2> im Block
        static const QRegularExpression re(QStringLiteral("<h2[^>]*>(.*?)</h2>"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        const auto m = re.match(block);
        if (m.hasMatch()) r.title = htmlStrip(m.captured(1));
    }
    if (r.title.isEmpty()) return r;       // ungültig → Quelltext-Fallback

    // Prominenter arabischer Display-Titel im Hero (Element mit überwiegend
    // arabischem, kurzem Text) → Haupttitel; bisheriges lat. <h1> → Sekundärzeile.
    {
        static const QRegularExpression reNode(QStringLiteral(">([^<>]+)<"));
        QString arTitle;
        auto it = reNode.globalMatch(block);
        while (it.hasNext()) {
            const QString txt = htmlStrip(it.next().captured(1));
            if (txt.isEmpty() || txt.size() > 50) continue;
            int ar = 0, lat = 0;
            for (const QChar ch : txt) {
                const ushort u = ch.unicode();
                if (u >= 0x0600 && u <= 0x06FF) ++ar;
                else if ((u >= u'A' && u <= u'Z') || (u >= u'a' && u <= u'z')) ++lat;
            }
            if (ar >= 3 && ar >= lat) { arTitle = txt; break; }
        }
        if (!arTitle.isEmpty() && arTitle != r.title) {
            int arInTitle = 0;
            for (const QChar ch : r.title) { const ushort u = ch.unicode(); if (u >= 0x0600 && u <= 0x06FF) ++arInTitle; }
            if (arInTitle * 2 < r.title.size()) r.secondaryTitle = r.title;  // bisheriger Titel überwiegend nicht-arabisch
            r.title = arTitle;
        }
    }

    // Eyebrow / Kicker
    {
        static const QRegularExpression re(QStringLiteral("<(\\w+)[^>]*class=\"[^\"]*(?:eyebrow|kicker|overline|topline)[^\"]*\"[^>]*>(.*?)</\\1>"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        const auto m = re.match(block);
        if (m.hasMatch()) r.eyebrow = htmlStrip(m.captured(2));
    }

    // Untertitel
    {
        static const QRegularExpression re(QStringLiteral("<(p|div|h2|h3)[^>]*class=\"[^\"]*(?:subtitle|untertitel|sub|lead|deck|tagline|standfirst|intro)[^\"]*\"[^>]*>(.*?)</\\1>"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        const auto m = re.match(block);
        if (m.hasMatch()) r.subtitle = htmlStrip(m.captured(2));
        else {
            static const QRegularExpression re2(QStringLiteral("<h1[^>]*>.*?</h1>\\s*<p[^>]*>(.*?)</p>"),
                QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
            const auto m2 = re2.match(block);
            if (m2.hasMatch()) r.subtitle = htmlStrip(m2.captured(1));
        }
    }

    // Zweisprachiger Titel (lateinisch + CJK) → CJK-Teil als Untertitel abspalten
    {
        static const QRegularExpression reCjk(QStringLiteral("[\\x{3000}-\\x{30FF}\\x{4E00}-\\x{9FFF}\\x{FF00}-\\x{FFEF}]"));
        static const QRegularExpression reLat(QStringLiteral("[A-Za-z\\x{00C0}-\\x{024F}]"));
        const auto mc = reCjk.match(r.title);
        if (mc.hasMatch() && mc.capturedStart() >= 2) {
            const QString latin = r.title.left(mc.capturedStart()).trimmed();
            const QString cjk   = r.title.mid(mc.capturedStart()).trimmed();
            if (reLat.match(latin).hasMatch() && latin.size() >= 2 && !cjk.isEmpty()) {
                r.title = latin;
                if (r.subtitle.isEmpty()) r.subtitle = cjk;
            }
        }
    }

    // Farben — Quelle: Hero-Element-Klasse(n) → .hero → body → CSS-Variablen
    const QStringList heroToks = heroClass.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    auto ruleColor = [&](const QString& selector, const char* a, const char* b) -> QColor {
        const QString body = cssRuleBody(t, selector);
        QString d = cssProp(body, QString::fromLatin1(a));
        if (d.isEmpty() && b) d = cssProp(body, QString::fromLatin1(b));
        return firstColor(d, vars);
    };
    // Hintergrund
    for (const QString& tok : heroToks) {
        r.bg = ruleColor(QChar(u'.') + tok, "background", "background-color");
        if (r.bg.isValid()) break;
    }
    if (!r.bg.isValid()) r.bg = ruleColor(QStringLiteral(".hero"), "background", "background-color");
    if (!r.bg.isValid()) r.bg = ruleColor(QStringLiteral("body"), "background", "background-color");
    if (!r.bg.isValid()) r.bg = firstColor(vars.value(QStringLiteral("bg")), vars);
    if (!r.bg.isValid()) r.bg = QColor(0x15, 0x24, 0x2E);
    r.dark = relLuminance(r.bg) < 0.5;

    // Textfarbe
    for (const QString& tok : heroToks) {
        r.text = ruleColor(QChar(u'.') + tok, "color", nullptr);
        if (r.text.isValid()) break;
    }
    if (!r.text.isValid()) r.text = ruleColor(QStringLiteral(".hero"), "color", nullptr);
    if (!r.text.isValid()) {
        const QColor ink = firstColor(vars.value(QStringLiteral("ink")), vars);
        r.text = r.dark ? QColor(0xED, 0xF3, 0xF4) : (ink.isValid() ? ink : QColor(0x15, 0x24, 0x2E));
    }

    // Akzent: .eyebrow .dot {background} → h1 .tbl {color} → saturierteste Variable
    {
        QString dotBody = cssRuleBody(t, QStringLiteral(".eyebrow .dot"));
        if (dotBody.isEmpty()) dotBody = cssRuleBody(t, QStringLiteral(".dot"));
        r.accent = firstColor(cssProp(dotBody, QStringLiteral("background")), vars);
        if (!r.accent.isValid()) {
            QString tblBody = cssRuleBody(t, QStringLiteral("h1 .tbl"));
            if (tblBody.isEmpty()) tblBody = cssRuleBody(t, QStringLiteral(".tbl"));
            r.accent = firstColor(cssProp(tblBody, QStringLiteral("color")), vars);
        }
        if (!r.accent.isValid()) {
            for (const char* nm : {"accent","akzent","gold","brand","primary","highlight"}) {
                const QColor c = firstColor(vars.value(QString::fromLatin1(nm)), vars);
                if (c.isValid()) { r.accent = c; break; }
            }
        }
        if (!r.accent.isValid()) {
            QColor best; double bestSat = 0.25;
            for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
                const QColor c = firstColor(it.value(), vars);
                if (!c.isValid()) continue;
                const double s = c.hslSaturationF(), l = c.lightnessF();
                if (s > bestSat && l > 0.2 && l < 0.78) { bestSat = s; best = c; }
            }
            r.accent = best.isValid() ? best : r.text;
        }
    }

    r.sub = firstColor(cssProp(cssRuleBody(t, QStringLiteral(".sub")), QStringLiteral("color")), vars);
    if (!r.sub.isValid()) r.sub = r.dark ? QColor(0xA9, 0xBE, 0xC4) : QColor(0x7C, 0x76, 0x6B);

    // Arabischer Kontext (für Layout-Richtung & Sterndekor)
    static const QRegularExpression reArab(QStringLiteral("[\\x{0600}-\\x{06FF}]"));
    const bool arabicCtx = reArab.match(t).hasMatch();
    {
        static const QRegularExpression reHtmlRtl(QStringLiteral("<html[^>]*dir=\"rtl\""), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression reBodyRtl(QStringLiteral("<body[^>]*dir=\"rtl\""), QRegularExpression::CaseInsensitiveOption);
        // RTL-Layout nur, wenn der Titel selbst arabisch ist (lat. Titel bleibt LTR)
        r.rtl = reHtmlRtl.match(t).hasMatch() || reBodyRtl.match(t).hasMatch() || reArab.match(r.title).hasMatch();
    }

    // Textur: arabischer Kontext / ornamentales Hero-Muster → Sterne; linear-gradient → Gitter
    {
        QString heroCss;
        static const QRegularExpression reHeroRules(QStringLiteral("\\.hero(?:::?[a-z-]+)?\\s*\\{([^}]*)\\}"),
            QRegularExpression::CaseInsensitiveOption);
        auto it = reHeroRules.globalMatch(t);
        while (it.hasNext()) heroCss += it.next().captured(1) + QChar(u';');
        const bool ornamental = heroCss.contains(QStringLiteral("url("), Qt::CaseInsensitive)
                             || heroCss.contains(QStringLiteral("radial-gradient"), Qt::CaseInsensitive);
        const bool grid = heroCss.contains(QStringLiteral("linear-gradient"), Qt::CaseInsensitive);
        if (arabicCtx || ornamental)   r.texture = 2;
        else if (grid)                 r.texture = 1;
        else                           r.texture = 0;
    }

    // Niveau-Chip (CEFR) aus Titel/Untertitel/Eyebrow
    {
        static const QRegularExpression reLvl(QStringLiteral("\\b([ABC][12])(?:\\s*[\\x{2013}\\x{2014}-]\\s*([ABC][12]))?\\b"));
        const QString sources[3] = { r.title, r.subtitle, r.eyebrow };
        for (const QString& src : sources) {
            const auto m = reLvl.match(src);
            if (m.hasMatch()) { r.chip = m.captured(0).simplified(); break; }
        }
    }

    return r;
}

}  // namespace

QPixmap ThumbnailTask::generateHtmlCardThumbnail(const QString& path, const QSize& size) {
    const HtmlMeta m = extractHtmlMeta(path);
    if (!m.valid()) return QPixmap();      // → Quelltext-Fallback

    const int W = size.width(), H = size.height();
    QPixmap pix(size);
    pix.fill(m.bg);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // ── Textur ──────────────────────────────────────────────────────────────
    if (m.texture == 1) {                    // feines Gitter
        const int step = qMax(16, W / 16);
        p.setPen(QPen(m.dark ? QColor(255, 255, 255, 14) : QColor(0, 0, 0, 12), 1));
        for (int gx = step; gx < W; gx += step) p.drawLine(gx, 0, gx, H);
        for (int gy = step; gy < H; gy += step) p.drawLine(0, gy, W, gy);
    } else if (m.texture == 2) {             // 8-zackige Sterne (Khatam): zwei überlagerte Quadrate
        QColor c = m.accent; c.setAlpha(m.dark ? 30 : 22);
        p.setPen(QPen(c, qMax(1, W / 360)));
        p.setBrush(Qt::NoBrush);
        const int step = qMax(40, W / 8);
        const qreal s = step * 0.5;
        for (int cy = step / 2; cy < H + step; cy += step)
            for (int cx = step / 2; cx < W + step; cx += step) {
                const QRectF sq(cx - s / 2.0, cy - s / 2.0, s, s);
                p.drawRect(sq);
                p.save(); p.translate(cx, cy); p.rotate(45); p.translate(-cx, -cy);
                p.drawRect(sq); p.restore();
            }
    }

    const int margin   = qMax(20, W * 9 / 100);
    const int contentW = W - 2 * margin;
    const int x        = margin;
    const bool rtl     = m.rtl;
    const Qt::Alignment hAlign = (rtl ? Qt::AlignHCenter : Qt::AlignLeft);

    auto baseFont = [](int px, bool bold) -> QFont {
        QFont f = QGuiApplication::font();             // App-Font → CJK/Arabisch-Fallback (Noto)
        f.setPixelSize(qMax(8, px));
        f.setWeight(bold ? QFont::DemiBold : QFont::Normal);
        return f;
    };

    // ── Niveau-Chip (Ecke); Inhalt beginnt darunter → keine Überlappung ───────
    int contentTop = qMax(20, H * 10 / 100);
    if (!m.chip.isEmpty()) {
        QFont cf = baseFont(qMax(12, H / 30), true);
        p.setFont(cf);
        const QFontMetrics fm(cf);
        const int padX = qMax(7, W / 60);
        const int bh   = fm.height() + qMax(6, H / 80);
        const int bw   = fm.horizontalAdvance(m.chip) + 2 * padX;
        const int cmrg = qMax(12, W / 38);
        const int cx   = rtl ? cmrg : (W - bw - cmrg);
        const QRect chip(cx, cmrg, bw, bh);
        p.setPen(Qt::NoPen);
        p.setBrush(m.accent);
        p.drawRoundedRect(chip, qMax(5, bh / 3), qMax(5, bh / 3));
        p.setPen(relLuminance(m.accent) < 0.55 ? QColor(Qt::white) : QColor(0x20, 0x20, 0x20));
        p.drawText(chip, Qt::AlignCenter, m.chip);
        contentTop = qMax(contentTop, int(chip.bottom()) + qMax(10, H / 40));
    }

    // Linker Akzentbalken nur bei cleanen Karten (keine Textur), LTR
    if (m.texture == 0 && !rtl)
        p.fillRect(QRect(0, 0, qMax(4, W / 100), H), m.accent);

    int y = contentTop;

    // ── Eyebrow ───────────────────────────────────────────────────────────────
    if (!m.eyebrow.isEmpty()) {
        QFont ef = baseFont(qMax(11, H / 34), false);
        ef.setLetterSpacing(QFont::AbsoluteSpacing, qMax(1.0, H / 280.0));
        p.setFont(ef);
        QColor ec = m.accent; if (!m.dark) ec = ec.darker(112);
        p.setPen(ec);
        const QFontMetrics fm(ef);
        const QString eb = fm.elidedText(m.eyebrow.toUpper(), Qt::ElideRight, contentW);
        p.drawText(QRect(x, y, contentW, fm.height()), int(hAlign | Qt::AlignTop), eb);
        y += fm.height() + qMax(6, H / 42);
    }

    // ── Titel (umbrochen, ggf. verkleinert bis 3 Zeilen passen) ────────────────
    {
        const int maxPct = m.secondaryTitle.isEmpty() ? 40 : 30;
        int px = qMax(20, H / 11);
        QFont tf = baseFont(px, true);
        const int flags = int(hAlign | Qt::AlignTop) | int(Qt::TextWordWrap);
        const QRect box(x, y, contentW, H);
        for (int i = 0; i < 8 && px > 16; ++i) {
            const QFontMetrics fm(tf);
            if (fm.boundingRect(box, flags, m.title).height() <= H * maxPct / 100) break;
            px -= 2; tf.setPixelSize(px);
        }
        p.setFont(tf);
        p.setPen(m.secondaryTitle.isEmpty() ? m.text : m.accent);   // arab. Display-Titel im Akzent
        const QFontMetrics fm(tf);
        const int needH = qMin(fm.boundingRect(box, flags, m.title).height(), fm.lineSpacing() * 3);
        p.drawText(QRect(x, y, contentW, needH), flags, m.title);
        y += needH + qMax(6, H / (m.secondaryTitle.isEmpty() ? 38 : 60));
    }

    // ── Sekundärzeile (lat. Titel unter dem arab. Display-Titel) ───────────────
    if (!m.secondaryTitle.isEmpty()) {
        QFont stf = baseFont(qMax(14, H / 20), true);
        p.setFont(stf);
        p.setPen(m.text);
        const QFontMetrics fm(stf);
        const QString s = fm.elidedText(m.secondaryTitle, Qt::ElideRight, contentW);
        p.drawText(QRect(x, y, contentW, fm.height()), int(hAlign | Qt::AlignTop), s);
        y += fm.height() + qMax(8, H / 44);
    }

    // ── Akzentlinie ────────────────────────────────────────────────────────────
    {
        const int lw = qMax(28, W / 5);
        const int lh = qMax(2, H / 190);
        const int lx = rtl ? (W - lw) / 2 : x;
        p.fillRect(QRect(lx, y, lw, lh), m.accent);
        y += lh + qMax(8, H / 42);
    }

    // ── Untertitel (auf Platz über der Namensleiste begrenzt) ──────────────────
    if (!m.subtitle.isEmpty()) {
        QFont sf = baseFont(qMax(11, H / 30), false);
        p.setFont(sf);
        p.setPen(m.sub);
        const QFontMetrics fm(sf);
        const int safeBottom = H - qMax(28, H * 15 / 100);    // Platz für Namens-Overlay
        const int avail = safeBottom - y;
        if (avail > fm.lineSpacing()) {
            const int lines = qBound(1, avail / fm.lineSpacing(), 3);
            const int flags = int(hAlign | Qt::AlignTop) | int(Qt::TextWordWrap);
            p.drawText(QRect(x, y, contentW, lines * fm.lineSpacing()), flags, m.subtitle);
        }
    }

    p.end();
    return pix;
}

QPixmap ThumbnailTask::generateTextThumbnail(const QString& path, const QSize& size) {
    // HTML/HTM → gerenderte Design-Karte (Hero-Nachbildung) statt Quelltext.
    {
        const QString suf = QFileInfo(path).suffix().toLower();
        if (suf == QStringLiteral("html") || suf == QStringLiteral("htm")) {
            const QPixmap card = generateHtmlCardThumbnail(path, size);
            if (!card.isNull()) return card;
            // sonst: Quelltext-Fallback unten (z. B. wenn kein Hero/<h1> gefunden)
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fallbackTextThumbnail(path, size);

    QTextStream in(&file);
    QStringList lines;
    const int kMaxLines = 5;
    for (int i = 0; i < kMaxLines && !in.atEnd(); ++i)
        lines << in.readLine();
    file.close();

    QPixmap pix(size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient grad(0, 0, 0, size.height());
    grad.setColorAt(0, QColor(26, 34, 42));
    grad.setColorAt(1, QColor(16, 22, 28));
    p.fillRect(pix.rect(), grad);

    QFont mono("Monospace");
    mono.setStyleHint(QFont::Monospace);
    mono.setPixelSize(qMax(8, size.height() / 16));
    p.setFont(mono);
    p.setPen(QColor(180, 205, 200));

    QFontMetrics fm(mono);
    const int lineH  = fm.height();
    const int margin = qMax(6, size.width() / 18);
    int y            = margin + fm.ascent();
    const int avail  = size.width() - 2 * margin;

    for (const QString& raw : std::as_const(lines)) {
        if (y > size.height() - margin) break;
        QString line = raw;
        line.replace('\t', QStringLiteral("    "));
        p.drawText(margin, y, fm.elidedText(line, Qt::ElideRight, avail));
        y += lineH;
    }

    drawExtensionBadge(p, size, QFileInfo(path).suffix());
    p.end();
    return pix;
}

QPixmap ThumbnailTask::fallbackTextThumbnail(const QString& path, const QSize& size) {
    QPixmap pix(size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    QLinearGradient grad(0, 0, 0, size.height());
    grad.setColorAt(0, QColor(26, 34, 42));
    grad.setColorAt(1, QColor(16, 22, 28));
    p.fillRect(pix.rect(), grad);

    const int w = size.width(), h = size.height();
    const int dw = qMax(24, w / 3);
    const int dh = qMax(30, h / 3);
    const int fold = dw / 3;
    const QRect doc((w - dw) / 2, (h - dh) / 2, dw, dh);

    QPainterPath path2;
    path2.moveTo(doc.left(), doc.top());
    path2.lineTo(doc.right() - fold, doc.top());
    path2.lineTo(doc.right(), doc.top() + fold);
    path2.lineTo(doc.right(), doc.bottom());
    path2.lineTo(doc.left(), doc.bottom());
    path2.closeSubpath();

    p.setPen(QPen(QColor(150, 175, 170), 2));
    p.setBrush(QColor(40, 52, 60));
    p.drawPath(path2);

    p.setPen(QPen(QColor(120, 145, 140), 2));
    const int lx0 = doc.left() + dw / 6;
    const int lx1 = doc.right() - dw / 6;
    for (int i = 1; i <= 3; ++i) {
        const int ly = doc.top() + fold + i * (dh - fold) / 4;
        p.drawLine(lx0, ly, lx1, ly);
    }

    drawExtensionBadge(p, size, QFileInfo(path).suffix());
    p.end();
    return pix;
}
