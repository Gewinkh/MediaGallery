#include "app/ViewerController.h"
#include "core/EditSidecar.h"
#include "core/MediaLogs.h"
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
#include "table/DelimitedText.h"

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
    const QString path = mg::toLocalPath(filePathOrUrl);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
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

// Der Text kommt aus dem EDITOR mit; die Quelldatei wird nur für den Zielnamen gebraucht und nicht angefasst.
// Paginieren und Zeichnen laufen im Worker, sonst hielte eine große Datei den UI-Thread an.
void ViewerController::exportTextToPdf(const QString& filePathOrUrl,
                                       const QString& content,
                                       const QColor& textColor,
                                       int tabWidth,
                                       bool native) {
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

    //  Palette und Sprache werden HIER geholt, nicht im Faden: der
    //  `EditorController` gehoert dem GUI-Faden.
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

    class TextPdfTask : public QRunnable {
    public:
        TextPdfTask(ViewerController* owner, QString text, QString target,
                    TextPdf::Stil stil, int tabWidth)
            : m_owner(owner), m_text(std::move(text)), m_target(std::move(target)),
              m_stil(std::move(stil)), m_tabWidth(tabWidth)
        { setAutoDelete(true); }

        void run() override {
            QString err;
            const bool ok = TextPdf::exportToPdf(m_text, m_target, m_stil, m_tabWidth, &err);
            // Owner als QPointer: er kann waehrend des Exports (App-Ende)
            // verschwinden - wie bei PdfScanTask.
            QPointer<ViewerController> owner = m_owner;
            if (!owner) return;
            const QString tgt = m_target;
            QMetaObject::invokeMethod(owner, [owner, ok, tgt, err]() {
                if (owner)
                    emit owner->textPdfExportFinished(ok, tgt, err);
            }, Qt::QueuedConnection);
        }
    private:
        QPointer<ViewerController> m_owner;
        QString                    m_text;
        QString                    m_target;
        TextPdf::Stil              m_stil;
        int                        m_tabWidth = 4;
    };

    QThreadPool::globalInstance()->start(
        new TextPdfTask(this, content, target, stil, tabWidth));
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
