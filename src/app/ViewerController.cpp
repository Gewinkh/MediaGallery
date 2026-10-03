#include "app/ViewerController.h"
#include "core/EditSidecar.h"
#include "core/MediaLogs.h"
#include "core/MGAudioList.h"
#include "core/MGStorage.h"
#include "pdf/PdfMediaHandler.h"
#include "core/AppSettings.h"
#include "core/PathUtils.h"
#include "core/MemoryUtils.h"   // mg::trimHeap - RSS-Rückgabe nach Annotations-LRU-Eviction
#include "core/TextEncoding.h"
#include "editor/EditorController.h"
#include "editor/LanguageTable.h"
#include "editor/TextPdfExporter.h"
#include "datev/DatevCsv.h"
#include "editor/MarkdownPdf.h"
#include "editor/MarkdownView.h"
#include "media/MediaItem.h"
#include "table/DelimitedText.h"
#include "table/TablePdf.h"

#include <QPdfDocument>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QUrl>
#include <QDesktopServices>
#include <QStringDecoder>
#include <QStringEncoder>
#include <QVariantMap>
#include <QThreadPool>
#include <QRunnable>
#include <QPointer>
#include <utility>

//  Sieht die Datei nach Spalten aus? Gefragt wird nur bei `.txt` und nur, wenn
//  der Nutzer die Tabellenansicht dafuer verlangt hat. Bedingung: EIN
//  Trennzeichen ergibt ueber die ersten Zeilen durchgehend dieselbe Feldzahl
//  von mindestens zwei. Eine Notiz oder ein Logfile faellt damit durch, auch
//  wenn Semikolons darin vorkommen.
static bool siehtNachSpaltenAus(const QString& pfad) {
    QFile f(pfad);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QString text = mg::decodeUnknownText(f.read(64 * 1024));
    QStringList zeilen;
    for (const QString& z : text.split(QLatin1Char('\n'))) {
        const QString t = z.trimmed();
        if (!t.isEmpty()) zeilen.append(t);
        if (zeilen.size() >= 20) break;
    }
    //  Unter drei Zeilen ist "durchgehend dieselbe Feldzahl" keine Aussage.
    if (zeilen.size() < 3) return false;

    for (QChar trenner : { QLatin1Char(';'), QLatin1Char('\t'),
                           QLatin1Char(','), QLatin1Char('|') }) {
        QHash<int, int> haeufig;
        for (const QString& z : std::as_const(zeilen))
            haeufig[int(mg::table::splitRecord(z, trenner).size())]++;
        int felder = 0, treffer = 0;
        for (auto it = haeufig.cbegin(); it != haeufig.cend(); ++it)
            if (it.value() > treffer) { treffer = it.value(); felder = it.key(); }
        if (felder >= 2 && treffer * 5 >= zeilen.size() * 4) return true;
    }
    return false;
}

//  Hilfsfunktion: MediaAnnotation-Vektor -> QVariantList (QML-tauglich).
//  Frei (static), damit Worker-Task und synchrone Variante sie teilen.
static QVariantList annotationsToVariant(const QVector<MediaAnnotation>& anns) {
    QVariantList out;
    out.reserve(anns.size());
    for (const MediaAnnotation& a : anns) {
        QVariantMap m;
        m.insert("page",  a.page);
        m.insert("x",     a.rect.x());
        m.insert("y",     a.rect.y());
        m.insert("w",     a.rect.width());
        m.insert("h",     a.rect.height());
        m.insert("type",  static_cast<int>(a.type));
        m.insert("uri",   a.resolvedUri());
        m.insert("label", a.label);
        m.insert("targetPage", a.targetPage);
        out.append(m);
    }
    return out;
}

//  Roh-Scan eines PDFs ohne GUI-Thread. Laedt das Dokument LOKAL (lebt nur fuer
//  die Dauer des Scans -> kein RAM-Wachstum), scannt die Annotationen und reicht
//  das Ergebnis per QueuedConnection an den ViewerController zurueck.
namespace {
class PdfScanTask : public QRunnable {
public:
    PdfScanTask(ViewerController* owner, QString path)
        : m_owner(owner), m_path(std::move(path)) { setAutoDelete(true); }

    void run() override {
        QVariantList list;
        QStringList  temps;

        QPdfDocument doc;
        if (doc.load(m_path) == QPdfDocument::Error::None
            && doc.status() == QPdfDocument::Status::Ready) {
            PdfMediaHandler handler(&doc);
            handler.scanDocument(m_path);
            list  = annotationsToVariant(handler.allAnnotations());
            temps = handler.tempFiles();
            // Temp-Medien werden bewusst NICHT hier geloescht: sie werden zum
            // Abspielen gebraucht und erst beim App-Ende vom Owner entfernt.
        }

        // Der Owner wird als QPointer gehalten und im GUI-Thread erneut geprüft: ein roher Zeiger wäre beim Zerstören
        // des Owners während des Scans schon für den `invokeMethod`-Aufruf selbst ungültig.
        QPointer<ViewerController> owner = m_owner;
        if (!owner) return;
        const QString path = m_path;
        QMetaObject::invokeMethod(owner, [owner, path, list, temps]() {
            if (owner)
                owner->applyScanResult(path, list, temps);
        }, Qt::QueuedConnection);
    }

private:
    QPointer<ViewerController> m_owner;
    QString                    m_path;
};
} // namespace

ViewerController::ViewerController(QObject* parent) : QObject(parent) {}

ViewerController::~ViewerController() {
    m_pdfAbbruch->store(true);
    m_pdfPool.waitForDone();
    // Extrahierte Temp-Medien dieser Sitzung entfernen.
    for (const QString& p : std::as_const(m_sessionTempFiles))
        QFile::remove(p);
}

QString ViewerController::readTextFile(const QString& filePathOrUrl) const {
    const QString path = mg::toLocalPath(filePathOrUrl);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    const QByteArray raw = f.read(kMaxTextBytes);
    f.close();

    //  Die eigene Ablage ist binaer - roh angezeigt waere sie Zeichensalat.
    if (mg::storage::istMGStorage(raw.constData(), std::size_t(raw.size()))) {
        mg::storage::Ablage a;
        if (mg::storage::lies(raw.constData(), std::size_t(raw.size()), a, nullptr))
            return QString::fromStdString(
                mg::storage::alsText(a, QFileInfo(path).fileName().toStdString()));
        return {};
    }

    //  Die Wiedergabe-Reihenfolge ebenso - lesbar ist die nummerierte Liste.
    if (mg::audiolist::istMGAudioList(raw.constData(), std::size_t(raw.size()))) {
        std::vector<std::string> namen;
        if (mg::audiolist::lies(raw.constData(), std::size_t(raw.size()), namen, nullptr))
            return QString::fromStdString(
                mg::audiolist::alsText(namen, QFileInfo(path).fileName().toStdString()));
        return {};
    }

    //  Die Beidatei der Editoren ist ebenfalls binaer; lesbar ist der
    //  eingerueckte JSON-Baum darin.
    if (mg::editsidecar::istBeidatei(raw))
        return mg::editsidecar::lesbar(path);

    //  UTF-8 mit Fehlerpruefung, sonst CP1252 - nicht Latin-1: die beiden gehen
    //  bei 0x80-0x9F auseinander, und genau dort liegen Euro-Zeichen und
    //  typografische Anfuehrungszeichen.
    const QString text = mg::decodeUnknownText(raw);

    // BEWUSST kein Hinweistext im Inhalt: der Vermerk "Datei gekürzt" stand früher IM Puffer und wurde beim
    // Speichern mit in die Datei geschrieben. Der Hinweis gehört in die Oberfläche, nicht in die Daten.
    return text;
}

bool ViewerController::isStorageFile(const QString& filePathOrUrl) const {
    const QString path = mg::toLocalPath(filePathOrUrl);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray kopf = f.read(16);
    //  Beide eigenen Formate: nur anzeigen, dazu die Rohform anbieten.
    return mg::storage::istMGStorage(kopf.constData(), std::size_t(kopf.size()))
        || mg::audiolist::istMGAudioList(kopf.constData(), std::size_t(kopf.size()))
        || mg::editsidecar::istBeidatei(kopf);
}

void ViewerController::quietMediaLogs() const { mg::media::beQuiet(); }

bool ViewerController::isEditNotesFile(const QString& filePathOrUrl) const {
    const QString path = mg::toLocalPath(filePathOrUrl);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    return mg::editsidecar::istBeidatei(f.read(16));
}

QString ViewerController::readStorageRaw(const QString& filePathOrUrl) const {
    const QString path = mg::toLocalPath(filePathOrUrl);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray roh = f.read(kMaxTextBytes);
    f.close();

    //  Adresse, 16 Bytes, daneben die druckbaren Zeichen - so sind Kennung,
    //  Blockkennungen und Namen mit blossem Auge zu sehen.
    QString out;
    out.reserve(roh.size() * 5);
    for (qsizetype z = 0; z < roh.size(); z += 16) {
        out += QStringLiteral("%1  ").arg(z, 8, 16, QLatin1Char('0'));
        QString klartext;
        for (int i = 0; i < 16; ++i) {
            if (z + i < roh.size()) {
                const auto b = static_cast<unsigned char>(roh.at(z + i));
                out += QStringLiteral("%1 ").arg(b, 2, 16, QLatin1Char('0'));
                klartext += (b >= 32 && b < 127) ? QChar(b) : QChar(u'.');
            } else {
                out += QStringLiteral("   ");
            }
            if (i == 7) out += QLatin1Char(' ');
        }
        out += QLatin1Char(' ') + klartext + QLatin1Char('\n');
    }
    return out;
}

bool ViewerController::isDatevFile(const QString& filePathOrUrl) const {
    const QString path = mg::toLocalPath(filePathOrUrl);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    return mg::datev::looksLikeDatev(f.read(64));
}

bool ViewerController::isTableFile(const QString& filePathOrUrl) const {
    const QString e = QFileInfo(mg::toLocalPath(filePathOrUrl)).suffix().toLower();
    if (e == QLatin1String("csv") || e == QLatin1String("tsv")) return true;
    //  `.txt` nur auf ausdruecklichen Wunsch: ein Logfile mit Semikolons ginge
    //  sonst als Tabelle auf. Geprueft wird zusaetzlich, dass die Datei
    //  wirklich Spalten hat - eine gewoehnliche Notiz bleibt Text, auch wenn
    //  der Schalter an ist.
    if (e == QLatin1String("txt") && AppSettings::instance().tableOpensTxt())
        return siehtNachSpaltenAus(mg::toLocalPath(filePathOrUrl));
    return false;
}

bool ViewerController::textFileTruncated(const QString& filePathOrUrl) const {
    const QString path = mg::toLocalPath(filePathOrUrl);
    if (path.isEmpty())
        return false;
    const QFileInfo fi(path);
    return fi.exists() && fi.size() > kMaxTextBytes;
}

bool ViewerController::writeTextFile(const QString& filePathOrUrl, const QString& content) const {
    const QString path = mg::toLocalPath(filePathOrUrl);
    if (path.isEmpty())
        return false;

    //  Letzte Instanz gegen Datenverlust: von einer Datei jenseits des Deckels
    //  liegt nur der Anfang im Editor. Zurueckschreiben hiesse den Rest loeschen.
    if (QFileInfo(path).size() > kMaxTextBytes)
        return false;

    // Atomar schreiben (QSaveFile: erst Temp-Datei, dann atomarer Rename) - bei
    // einem Fehler bleibt die Originaldatei unangetastet.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    QStringEncoder enc(QStringEncoder::Utf8);
    const QByteArray bytes = enc.encode(content);
    if (f.write(bytes) != bytes.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

bool ViewerController::openExternally(const QString& filePathOrUrl) const {
    //  Webadressen unveraendert weiter - als Dateipfad gelesen wurde aus `https://…` ein Ordner, den es nicht gibt.
    const QUrl url(filePathOrUrl);
    const QString schema = url.scheme().toLower();
    if (schema == u"http" || schema == u"https" || schema == u"mailto" || schema == u"ftp")
        return QDesktopServices::openUrl(url);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(mg::toLocalPath(filePathOrUrl)));
}

//  LRU-Pflege (nur GUI-Thread -> keine Synchronisation noetig).
void ViewerController::touchCache(const QString& path) {
    m_cacheOrder.removeAll(path);
    m_cacheOrder.append(path);                 // juengster Eintrag ans Ende
}

void ViewerController::insertIntoCache(const QString& path, const QVariantList& anns) {
    m_annCache.insert(path, anns);
    touchCache(path);
    bool evicted = false;
    while (m_cacheOrder.size() > kMaxCachedPdfs) {
        const QString victim = m_cacheOrder.takeFirst();
        m_annCache.remove(victim);
        evicted = true;
    }
    // Nur bei TATSÄCHLICHER Eviction: Annotationslisten enthalten eingebettete
    // Medien-Payloads (QByteArray, potenziell MB) - Heap ans OS zurückgeben.
    if (evicted)
        mg::trimHeap();
}

//  Asynchrone Anforderung (aus QML). Blockiert nie den GUI-Thread.
void ViewerController::requestPdfAnnotations(const QString& filePathOrUrl) {
    const QString path = mg::toLocalPath(filePathOrUrl);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        // Defensiv: leeres Ergebnis (queued) -> QML kann Badges einheitlich leeren.
        QMetaObject::invokeMethod(this, [this, path]() {
            emit pdfAnnotationsReady(path, QVariantList{});
        }, Qt::QueuedConnection);
        return;
    }

    // Cache-Treffer -> sofort (queued, damit der Aufrufer immer asynchron reagiert).
    if (m_annCache.contains(path)) {
        touchCache(path);
        const QVariantList cached = m_annCache.value(path);
        QMetaObject::invokeMethod(this, [this, path, cached]() {
            emit pdfAnnotationsReady(path, cached);
        }, Qt::QueuedConnection);
        return;
    }

    // Doppelte Scans desselben Pfads vermeiden (z. B. schnelles Vor/Zurueck).
    if (m_inFlight.contains(path))
        return;
    m_inFlight.insert(path);

    QThreadPool::globalInstance()->start(new PdfScanTask(this, path));
}

//  Palette und Sprache werden im GUI-Faden geholt, nicht im Arbeitsfaden: der `EditorController` gehoert ihm.
static TextPdf::Stil textStil(const QString& src, const QColor& textColor, bool native) {
    TextPdf::Stil stil;
    stil.tinte = textColor.isValid() ? textColor : QColor(Qt::black);
    if (native) {
        const mg::editor::SyntaxPalette pal =
            mg::editor::activeController()
                ? mg::editor::activeController()->palette()
                : mg::editor::paletteForProfile(mg::editor::EditorProfile::Nightfall);
        stil.palette = pal;
        stil.tinte   = pal.text;
        stil.papier  = pal.background;
        stil.syntax  = true;
        stil.sprache = mg::editor::languageForPath(src).id;
    }
    return stil;
}

void ViewerController::startPdf(std::function<void()> arbeit) {
    class Task : public QRunnable {
    public:
        explicit Task(std::function<void()> a) : m_a(std::move(a)) { setAutoDelete(true); }
        void run() override { m_a(); }
    private:
        std::function<void()> m_a;
    };
    m_pdfPool.setMaxThreadCount(1);
    m_pdfPool.start(new Task(std::move(arbeit)));
}

// Der Text kommt aus dem EDITOR mit; die Quelldatei wird nur für den Zielnamen gebraucht und nicht angefasst.
// Paginieren und Zeichnen laufen im Worker, sonst hielte eine große Datei den UI-Thread an.
void ViewerController::exportTextToPdf(const QString& filePathOrUrl,
                                       const QString& content,
                                       const QColor& textColor,
                                       int tabWidth,
                                       bool native,
                                       bool landscape, int firstPage, int lastPage,
                                       const QList<int>& pages) {
    const QString src    = mg::toLocalPath(filePathOrUrl);
    const QString target = TextPdf::targetPathFor(src);
    if (target.isEmpty()) {
        // Defensiv: ohne Quelle gibt es keinen Zielnamen - Fehler queued melden,
        // damit QML immer denselben (asynchronen) Weg sieht.
        QMetaObject::invokeMethod(this, [this]() {
            emit textPdfExportFinished(false, QString(),
                                       QStringLiteral("Keine Datei geöffnet."));
        }, Qt::QueuedConnection);
        return;
    }
    const TextPdf::Stil stil = textStil(src, textColor, native);
    const TextPdf::Seiten seiten{landscape, firstPage, lastPage, pages};
    QPointer<ViewerController> owner(this);
    startPdf([owner, content, target, stil, tabWidth, seiten, abbruch = m_pdfAbbruch] {
        QString err;
        const bool ok = TextPdf::exportToPdf(content, target, stil, tabWidth, &err, seiten);
        if (abbruch->load()) return;
        QMetaObject::invokeMethod(owner, [owner, ok, target, err] {
            if (owner) emit owner->textPdfExportFinished(ok, target, err);
        }, Qt::QueuedConnection);
    });
}

void ViewerController::countTextPdfPages(const QString& filePathOrUrl, const QString& content,
                                         int tabWidth, bool native, bool landscape) {
    const TextPdf::Stil stil = textStil(mg::toLocalPath(filePathOrUrl), QColor(Qt::black), native);
    const TextPdf::Seiten seiten{landscape, 1, 0};
    const int gen = ++m_textZaehlGen;
    QPointer<ViewerController> owner(this);
    startPdf([owner, content, stil, tabWidth, seiten, gen, abbruch = m_pdfAbbruch] {
        int n = 0;
        TextPdf::exportToPdf(content, QString(), stil, tabWidth, nullptr, seiten, &n);
        if (abbruch->load()) return;
        QMetaObject::invokeMethod(owner, [owner, n, gen] {
            if (owner && gen == owner->m_textZaehlGen) emit owner->textPdfPagesCounted(n);
        }, Qt::QueuedConnection);
    });
}

void ViewerController::previewTextPdf(const QString& token, const QString& filePathOrUrl, const QString& content,
                                      int tabWidth, bool native, bool landscape) {
    auto& v = m_textVorschau[token];
    if (!v) v = std::make_shared<TextVorschau>();
    const QString ziel = v->datei.neu();
    if (ziel.isEmpty()) return;
    const int gen = ++v->gen;
    const TextPdf::Stil stil = textStil(mg::toLocalPath(filePathOrUrl), QColor(Qt::black), native);
    QPointer<ViewerController> owner(this);
    startPdf([owner, token, ziel, gen, content, stil, tabWidth, landscape, abbruch = m_pdfAbbruch] {
        int n = 0;
        const bool ok = TextPdf::exportToPdf(content, ziel, stil, tabWidth, nullptr,
                                             TextPdf::Seiten{landscape, 1, 0, {}}, &n);
        if (abbruch->load()) return;
        QMetaObject::invokeMethod(owner, [owner, token, ziel, gen, ok, n] {
            if (!owner) return;
            const auto it = owner->m_textVorschau.constFind(token);
            if (it == owner->m_textVorschau.cend()) { QFile::remove(ziel); return; }
            if (!ok || gen != (*it)->gen) { (*it)->datei.verwerfe(ziel); return; }
            (*it)->datei.uebernehme(ziel);
            emit owner->textPdfPreviewReady(token, ziel, n);
        }, Qt::QueuedConnection);
    });
}

//  Was noch schreibt, liefert danach an einen fehlenden Eintrag und wird dort geloescht.
void ViewerController::dropTextPdfPreview(const QString& token) { m_textVorschau.remove(token); }

bool ViewerController::canExportPdf(const QString& filePathOrUrl) const {
    const QString path = mg::toLocalPath(filePathOrUrl);
    if (MediaItem::detectType(path) != MediaType::Text) return false;
    return !isStorageFile(path) && !isDatevFile(path);
}

QStringList ViewerController::pdfCandidates(const QStringList& paths) const {
    QStringList out;
    for (const QString& p : paths)
        if (MediaItem::detectType(mg::toLocalPath(p)) == MediaType::Text) out.append(p);
    return out;
}

// Je Datei ein PDF daneben. Art und Stil werden HIER bestimmt: Palette, Tabulatorweite und die Tabellen-
// Einstellungen gehoeren dem GUI-Faden.
void ViewerController::exportFilesToPdf(const QStringList& paths, bool native, bool landscape,
                                        const QFont& tableFont) {
    enum class Art { Text, Markdown, Tabelle };
    struct Auftrag { QString quelle, ziel; Art art; TextPdf::Stil textStil; };
    QList<Auftrag> auftraege;
    QSet<QString> vergeben;
    for (const QString& p : paths) {
        const QString src = mg::toLocalPath(p);
        if (!canExportPdf(src)) continue;
        //  Zwei Dateien gleichen Namens mit anderer Endung bekaemen sonst dasselbe Ziel.
        QString ziel = TextPdf::targetPathFor(src);
        for (int n = 2; vergeben.contains(ziel); ++n)
            ziel = QFileInfo(src).absolutePath() + QLatin1Char('/') + QFileInfo(src).completeBaseName()
                   + QStringLiteral(" (%1).pdf").arg(n);
        vergeben.insert(ziel);
        const QString e = QFileInfo(src).suffix().toLower();
        const Art art = (e == u"md" || e == u"markdown") ? Art::Markdown
                      : isTableFile(src)                 ? Art::Tabelle : Art::Text;
        auftraege.append({src, ziel, art, textStil(src, QColor(Qt::black), native)});
    }
    const mg::editor::SyntaxPalette pal = mg::editor::activeController()
        ? mg::editor::activeController()->palette()
        : mg::editor::paletteForProfile(mg::editor::EditorProfile::Nightfall);
    mg::table::PdfOptionen tab;
    tab.druck = !native;
    tab.quer = landscape;
    tab.gitter = AppSettings::instance().tableGridLines();
    tab.schrift = tableFont;
    tab.grund = pal.background;
    tab.text = pal.text;
    tab.kopfGrund = pal.gutterBackground;
    tab.kopfText = pal.gutterText;
    const bool gruppiert = AppSettings::instance().tableGroupDigits();
    const int tabBreite = mg::editor::activeController() ? mg::editor::activeController()->tabWidth() : 4;
    QList<mg::editor::md::RenderStyle> mdStile;
    for (const Auftrag& a : std::as_const(auftraege))
        mdStile.append(a.art == Art::Markdown ? mg::editor::MarkdownView::styleFor(a.quelle, {})
                                              : mg::editor::md::RenderStyle());

    QPointer<ViewerController> owner(this);
    startPdf([owner, auftraege, mdStile, tab, gruppiert, tabBreite, native, landscape,
              abbruch = m_pdfAbbruch] {
        int gut = 0, schlecht = 0;
        QString letztes;
        for (int i = 0; i < auftraege.size(); ++i) {
            if (abbruch->load()) return;
            const Auftrag& a = auftraege[i];
            bool ok = false;
            if (a.art == Art::Tabelle) {
                ok = mg::table::dateiAlsPdf(a.quelle, tab, gruppiert, a.ziel, nullptr, abbruch.get());
            } else {
                QFile f(a.quelle);
                const QString text = f.open(QIODevice::ReadOnly)
                    ? mg::decodeUnknownText(f.read(kMaxTextBytes)) : QString();
                if (f.isOpen()) {
                    if (a.art == Art::Markdown) {
                        mg::editor::md::PdfOptions o;
                        o.print = !native;
                        o.landscape = landscape;
                        ok = mg::editor::md::writePdf(mg::editor::md::parse(text), mdStile[i], o, a.ziel,
                                                      nullptr, abbruch.get()) > 0;
                    } else {
                        ok = TextPdf::exportToPdf(text, a.ziel, a.textStil, tabBreite, nullptr,
                                                  TextPdf::Seiten{landscape, 1, 0});
                    }
                }
            }
            if (ok) { ++gut; letztes = a.ziel; } else { ++schlecht; }
        }
        QMetaObject::invokeMethod(owner, [owner, gut, schlecht, letztes] {
            if (owner) emit owner->filesPdfExportFinished(gut, schlecht, letztes);
        }, Qt::QueuedConnection);
    });
}

//  Ergebnis-Uebernahme auf dem GUI-Thread (vom Worker via QueuedConnection).
void ViewerController::applyScanResult(const QString& path, const QVariantList& anns,
                                       const QStringList& tempFiles) {
    m_inFlight.remove(path);
    for (const QString& t : tempFiles)
        if (!t.isEmpty())
            m_sessionTempFiles.append(t);
    insertIntoCache(path, anns);
    emit pdfAnnotationsReady(path, anns);
}

//  Synchrone Variante (Kompatibilitaet). Nutzt denselben Resultcache.
