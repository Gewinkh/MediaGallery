#include "app/AppController.h"
#include "app/PaneController.h"
#include "app/PaneListModel.h"
#include "tags/TagController.h"
#include "core/PathUtils.h"

#include "core/SpellChecker.h"
#include "docx/DocxDocument.h"

#include "media/FolderService.h"
#include "core/JsonStorage.h"
#include "tags/TagManager.h"
#include "core/AppSettings.h"   // konkrete Settings-Signale für NOTIFY-Weiterleitung
#include "core/Strings.h"
#include "media/MediaItem.h"     // MediaItem::detectType für Drop-Behandlung
#include "core/RhiProber.h"
#include "core/ZCodec.h"        // docxAvailable: DOCX hängt an ZLIB

#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QPdfWriter>
#include <QPageSize>
#include <QPainter>
#include <QVariantMap>
#include <QSize>
#include <QPoint>
#include <QTimer>
#include <QWheelEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>

AppController::AppController(ISettings& settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    //  ── Wachhund über die Zwischenablage ────────────────────────────────────
    //  Kopiert ein ANDERES Programm, verliert unsere gemerkte Dateiliste ihre
    //  Gültigkeit - sonst fügte `Strg+V` später etwas ein, das gar nicht mehr
    //  in der Ablage steht. Die eigene Änderung wird über `m_clipSelfSet`
    //  durchgelassen (sie kommt als dasselbe Signal zurück).
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        connect(cb, &QClipboard::dataChanged, this, [this] {
            if (m_clipSelfSet) { m_clipSelfSet = false; return; }
            m_ownClipFiles.clear();
        });
    }

    // Settings (konkrete Instanz für Signale; Datenzugriff über ISettings&)
    AppSettings& as = AppSettings::instance();
    connect(&as, &AppSettings::colorSchemeChanged, this, &AppController::backgroundColorChanged);
    connect(&as, &AppSettings::colorSchemeChanged, this, &AppController::accentColorChanged);
    connect(&as, &AppSettings::colorSchemeChanged, this, &AppController::themeChanged);
    connect(&as, &AppSettings::themeChanged,       this, &AppController::themeChanged);
    connect(&as, &AppSettings::languageChanged,    this, &AppController::languageChanged);
    connect(&as, &AppSettings::tileSizeChanged,        this, &AppController::tileSizeChanged);
    connect(&as, &AppSettings::tileArrangementChanged, this, &AppController::tileArrangementChanged);
    connect(&as, &AppSettings::autoSaveSettingsChanged, this, &AppController::autoSaveChanged);

    //  Sicht auf `m_panes` fuer den `Repeater` der Shell (s. PaneListModel).
    m_panesModel = new PaneListModel(m_panes, this);
}

// ── Die fokussierte Hälfte ───────────────────────────────────────────────────
//  Diese Fassade hat keinen eigenen Ordner mehr; sie zeigt auf die Hälfte, in
//  der gerade gearbeitet wird, und reicht alles Ordnerbezogene dorthin weiter.
//  Beim Wechsel werden die Signale umgehängt und die abhängigen Anzeigen
//  aufgefrischt - sonst zeigte das Menü den Ordner der anderen Hälfte.
void AppController::setTagsFacade(TagController* facade) {
    m_tagsFacade = facade;
    if (m_tagsFacade && m_pane) m_tagsFacade->setTagManager(m_pane->tagManager());
}

void AppController::setFocusedPane(PaneController* pane) {
    if (m_pane == pane) return;
    if (m_pane) m_pane->disconnect(this);
    m_pane = pane;
    if (m_pane) {
        connect(m_pane, &PaneController::folderOpened,   this, &AppController::folderOpened);
        connect(m_pane, &PaneController::folderChanged,  this, &AppController::folderChanged);
        connect(m_pane, &PaneController::folderHistoryChanged,
                this, &AppController::folderHistoryChanged);
        connect(m_pane, &PaneController::folderContentsChanged,
                this, &AppController::folderContentsChanged);
        connect(m_pane, &PaneController::statusMessage,  this, &AppController::statusMessage);
        connect(m_pane, &PaneController::tagsChanged,       this, &AppController::tagsChanged);
        connect(m_pane, &PaneController::categoriesChanged, this, &AppController::categoriesChanged);
        connect(m_pane, &PaneController::optionsVisibleChanged,
                this, &AppController::optionsVisibleChanged);
    }
    //  Die appweite `Tags`-Fassade zeigt jetzt auf die Tags DIESER Hälfte
    //  (Einstellungen ▸ Tags/Kategorien/Konverter arbeiten damit dort, wo
    //  gerade gearbeitet wird).
    //  Solange die Einstellungen eine Hälfte FEST gewählt haben, bleibt die
    //  Fassade dort - sonst wechselte sie unter dem offenen Dialog weg.
    if (m_settingsPane < 0 && m_tagsFacade && m_pane)
        m_tagsFacade->setTagManager(m_pane->tagManager());
    emit folderChanged();
    emit folderHistoryChanged();
    emit tagsChanged();
    emit categoriesChanged();
    emit optionsVisibleChanged();       // der Modus gehört der Hälfte
}

// ── Die Hälften des Hauptfensters ────────────────────────────────────────────
QVariantList AppController::panes() const {
    QVariantList out;
    out.reserve(int(m_panes.size()));
    for (PaneController* p : m_panes)
        out.append(QVariant::fromValue(static_cast<QObject*>(p)));
    return out;
}

QAbstractItemModel* AppController::panesModel() const { return m_panesModel; }

int AppController::focusedPaneIndex() const {
    for (size_t i = 0; i < m_panes.size(); ++i)
        if (m_panes[i] == m_pane) return int(i);
    return -1;
}

int AppController::indexOfPane(QObject* pane) const {
    for (size_t i = 0; i < m_panes.size(); ++i)
        if (m_panes[i] == pane) return int(i);
    return -1;
}

QObject* AppController::addPane() {
    if (!m_loader || int(m_panes.size()) >= kMaxPanes) return nullptr;
    auto* pane = new PaneController(m_settings, *m_loader, this);
    //  Den Startordner schreibt NUR die erste Haelfte. Sonst ueberschreibt die
    //  zuletzt benutzte Haelfte den gemerkten Ordner der ersten, und beim
    //  naechsten Start stehen beide auf demselben (vom Nutzer gemeldet).
    pane->folderService().setPersistsLastFolder(m_panes.empty());
    //  Punktgenau einfuegen: die BESTEHENDE Haelfte darf dabei nicht neu
    //  gebaut werden, sonst ist ihre geoeffnete Datei weg (s. PaneListModel).
    m_panesModel->beginInsert(int(m_panes.size()));
    m_panes.push_back(pane);
    m_panesModel->endInsert();
    //  Die erste Hälfte bekommt sofort den Fokus; eine hinzugefügte auch - man
    //  hat sie gerade aufgemacht, also will man dort arbeiten.
    setFocusedPane(pane);
    emit panesChanged();
    return pane;
}

bool AppController::closePane(int index) {
    if (index < 0 || index >= int(m_panes.size())) return false;
    if (m_panes.size() <= 1) return false;          // die letzte bleibt
    PaneController* pane = m_panes[index];
    m_panesModel->beginRemove(index);
    m_panes.erase(m_panes.begin() + index);
    m_panesModel->endRemove();
    if (m_pane == pane)
        setFocusedPane(m_panes.front());
    //  Wer jetzt die erste ist, schreibt ab sofort den Startordner - sonst
    //  merkte sich nach dem Schliessen der ersten Haelfte niemand mehr einen.
    if (!m_panes.empty())
        m_panes.front()->folderService().setPersistsLastFolder(true);

    //  Der Player-Merker haengt am PLATZ der Haelfte (Bit je Platz). Faellt eine
    //  weg, muss ihr Bit verschwinden und die dahinter muessen aufruecken -
    //  sonst entsteht die geschlossene Haelfte beim naechsten Start wieder aus
    //  dem Merker (vom Nutzer gemeldet: „habe einen Screen geschlossen und beim
    //  Neustart sind wieder zwei da").
    {
        const int alt = m_settings.audioPlayerModeMask();
        int neu = 0;
        for (int i = 0, platz = 0; i < 4; ++i) {
            if (i == index) continue;                 // faellt weg
            if (alt & (1 << i)) neu |= (1 << platz);
            ++platz;
        }
        if (neu != alt) m_settings.setAudioPlayerModeMask(neu);
    }
    //  Den Fensterzustand SOFORT nachziehen, nicht erst beim Beenden: wer eine
    //  Haelfte schliesst und die App danach anders beendet (Absturz, Abmelden),
    //  bekam sie beim naechsten Start zurueck (vom Nutzer gemeldet).
    persistPaneFolders();
    //  Erst melden, dann löschen: QML gibt seine Hälfte im selben Zug frei.
    emit panesChanged();
    pane->deleteLater();
    return true;
}

//  Welche Ordner stehen wo? Der der ERSTEN Haelfte ist der Startordner, der der
//  zweiten gehoert zum Fensterzustand. An EINER Stelle geschrieben, damit die
//  beiden nicht auseinanderlaufen.
void AppController::persistPaneFolders() {
    if (!m_panes.empty()) {
        const QString first = m_panes.front()->currentFolder();
        if (!first.isEmpty()) m_settings.setLastFolder(first);
    }
    m_settings.setSecondFolder(m_panes.size() > 1 ? m_panes[1]->currentFolder()
                                                  : QString());
}

void AppController::focusPane(int index) {
    if (index < 0 || index >= int(m_panes.size())) return;
    if (m_panes[index] == m_pane) return;
    setFocusedPane(m_panes[index]);
    emit panesChanged();
}

QString AppController::secondFolder() const { return m_settings.secondFolder(); }

bool AppController::swapPanes() {
    if (m_panes.size() < 2) return false;
    //  VERSCHIEBEN, nicht neu bauen: so wandert jede Haelfte mit allem, was in
    //  ihr offen ist, auf den anderen Platz.
    m_panesModel->beginMove(1, 0);
    std::swap(m_panes[0], m_panes[1]);
    m_panesModel->endMove();
    //  Der Fokus hängt am OBJEKT, nicht am Platz - er wandert also mit.
    emit panesChanged();
    return true;
}

void AppController::setSettingsPaneIndex(int index) {
    const int v = (index >= 0 && index < int(m_panes.size())) ? index : -1;
    if (v == m_settingsPane) return;
    m_settingsPane = v;
    //  Die Fassade zeigt jetzt auf die gewählte Hälfte (oder wieder auf die
    //  fokussierte, wenn die Wahl aufgehoben wurde).
    PaneController* target = (v >= 0) ? m_panes[size_t(v)] : m_pane;
    if (m_tagsFacade && target) m_tagsFacade->setTagManager(target->tagManager());
    emit panesChanged();
}

qreal AppController::paneSplit() const { return m_settings.paneSplit(); }
void  AppController::setPaneSplit(qreal v) {
    if (qFuzzyCompare(m_settings.paneSplit(), qBound(0.15, v, 0.85))) return;
    m_settings.setPaneSplit(v);
    emit paneSplitChanged();
}

// ── Ordner - alles an die fokussierte Hälfte ─────────────────────────────────
QString AppController::currentFolder() const {
    return m_pane ? m_pane->currentFolder() : QString();
}
bool AppController::canNavigateBack() const {
    return m_pane && m_pane->canNavigateBack();
}
void AppController::restoreLastFolder()   { if (m_pane) m_pane->restoreLastFolder(); }
void AppController::openFolderUrl(const QUrl& url) { if (m_pane) m_pane->openFolderUrl(url); }
void AppController::openSubfolder(const QString& path) { if (m_pane) m_pane->openSubfolder(path); }
bool AppController::navigateBack()        { return m_pane && m_pane->navigateBack(); }
void AppController::refreshCurrentFolder(){ if (m_pane) m_pane->refreshCurrentFolder(); }

// ── Datei-Erstellung (FilterBar „Erstellen") ─────────────────────────────────
//  Erzeugt eine leere PDF/HTML/TXT im aktuellen Ordner. Alles atomar über
//  QSaveFile; die leere PDF ist eine EINZELNE weiße A4-Seite aus QPdfWriter
//  (72 dpi, Ränder 0 - dieselbe Punkt-Skala wie der PDF-Editor-Export, damit
//  neu erstellte PDFs sofort editierbar sind und WYSIWYG bleiben).
QString AppController::createEmptyFile(const QString& kind, const QString& baseName,
                                       const QString& targetFolder) {
    const QString open = currentFolder();
    if (open.isEmpty()) {
        emit statusMessage(Strings::get(StringKey::CreateFileFailed));
        return {};
    }
    //  Ein Zielordner ist nur INNERHALB des geoeffneten Ordners zulaessig.
    QString folder = open;
    if (!targetFolder.isEmpty() && targetFolder != open) {
        if (!targetFolder.startsWith(open + QLatin1Char('/'))
            || !QFileInfo(targetFolder).isDir()) {
            emit statusMessage(Strings::get(StringKey::CreateFileFailed));
            return {};
        }
        folder = targetFolder;
    }

    // Endung aus dem Typ ableiten (Whitelist).
    QString ext;
    if      (kind == QLatin1String("pdf"))  ext = QStringLiteral("pdf");
    else if (kind == QLatin1String("html")) ext = QStringLiteral("html");
    else if (kind == QLatin1String("txt"))  ext = QStringLiteral("txt");
    else if (kind == QLatin1String("docx")) ext = QStringLiteral("docx");
    else {
        emit statusMessage(Strings::get(StringKey::CreateFileFailed));
        return {};
    }

    // Namen säubern: Pfadtrenner raus, führende Punkte weg (keine versteckten
    // Dateien aus Versehen), Fallback auf einen generischen Namen.
    QString base = baseName.trimmed();
    base.remove(QLatin1Char('/'));
    base.remove(QLatin1Char('\\'));
    while (base.startsWith(QLatin1Char('.')))
        base.remove(0, 1);
    if (base.isEmpty())
        base = Strings::get(StringKey::CreateFileTitle);

    // Kollisionen per „ (n)"-Suffix auflösen (wie der Editor-Export).
    QString path = folder + QLatin1Char('/') + base + QLatin1Char('.') + ext;
    int n = 2;
    while (QFileInfo::exists(path)) {
        path = folder + QLatin1Char('/') + base
               + QStringLiteral(" (%1).").arg(n) + ext;
        ++n;
    }

    bool ok = false;
    QSaveFile out(path);
    if (out.open(QIODevice::WriteOnly)) {
        if (ext == QLatin1String("pdf")) {
            // Leere einseitige PDF: begin() legt die erste (weiße) Seite an -
            // es muss nichts gezeichnet werden.
            QPdfWriter writer(&out);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QPageSize::A4));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));
            writer.setCreator(QStringLiteral("MediaGallery"));
            writer.setTitle(base);
            QPainter p;
            if (p.begin(&writer)) {
                p.end();
                ok = out.commit();
            } else {
                out.cancelWriting();
            }
        } else {
            QByteArray bytes;
            if (ext == QLatin1String("html")) {
                // Minimal-Skelett (UTF-8): sofort im HTML-Vorschau-/Quelltext-
                // Editor der App nutzbar.
                bytes = QStringLiteral(
                            "<!DOCTYPE html>\n"
                            "<html lang=\"de\">\n"
                            "<head>\n"
                            "    <meta charset=\"utf-8\">\n"
                            "    <title>%1</title>\n"
                            "</head>\n"
                            "<body>\n\n"
                            "</body>\n"
                            "</html>\n").arg(base.toHtmlEscaped()).toUtf8();
            }
            else if (ext == QLatin1String("docx")) {
                // Leeres Word-Dokument (A4, Standardränder 2,5 cm) aus der
                // eigenen Container-Fabrik - sofort im DOCX-Editor nutzbar.
                bytes = Docx::Document::emptyDocxBytes(base);
            }
            // txt bleibt bewusst 0 Byte.
            if (bytes.isEmpty() || out.write(bytes) == bytes.size())
                ok = out.commit();
            else
                out.cancelWriting();
        }
    }

    if (!ok) {
        emit statusMessage(Strings::get(StringKey::CreateFileFailed));
        return {};
    }
    // Galerie sofort aktualisieren (deterministisch, nicht nur Watcher) - und
    // zwar die Hälfte, der der Ordner gehört.
    if (m_pane) m_pane->notifyContentsChanged(folder);
    emit statusMessage(Strings::get(StringKey::CreateFileDone)
                           .arg(QFileInfo(path).fileName()));
    return path;
}

// ── Drag & Drop ──────────────────────────────────────────────────────────────
void AppController::handleDroppedUrls(const QList<QUrl>& urls,
                                      const QString& targetFolder) {
    // 1) Verzeichnis im Drop -> als Galerie-Ordner öffnen (erstes gewinnt).
    for (const QUrl& url : urls) {
        const QString path = url.toLocalFile();
        if (path.isEmpty()) continue;
        if (QFileInfo(path).isDir()) {
            if (m_pane) m_pane->openFolder(path);   // leert den Rückweg selbst
            return;
        }
    }

    // 2) Mediendateien -> in den Zielordner kopieren (leer = der offene).
    const QString open = currentFolder();
    QString folder = open;
    if (!targetFolder.isEmpty() && targetFolder != open
        && targetFolder.startsWith(open + QLatin1Char('/'))
        && QFileInfo(targetFolder).isDir()) {
        folder = targetFolder;
    }
    const bool de = (m_settings.language() == Language::German);
    if (folder.isEmpty()) {
        emit statusMessage(de ? QStringLiteral("Kein Ordner geöffnet - Dateien können nicht hinzugefügt werden.")
                              : QStringLiteral("No folder open - files cannot be added."));
        return;
    }

    int copied = 0, skipped = 0;
    for (const QUrl& url : urls) {
        const QString src = url.toLocalFile();
        if (src.isEmpty()) continue;
        const QFileInfo fi(src);
        if (!fi.exists() || fi.isDir()) continue;
        if (MediaItem::detectType(src) == MediaType::Unknown) continue;

        const QString dest = QDir(folder).filePath(fi.fileName());
        if (QFileInfo::exists(dest)) { ++skipped; continue; }
        if (QFile::copy(src, dest)) ++copied;
    }

    if (copied > 0) {
        //  NUR fuer den offenen Ordner: `m_storage` IST dessen Sidecar - es auf
        //  einen Unterordner umzuschalten haenge Modell, Tag-Panel und Filter
        //  an die falsche Datei. Der Unterordner fuehrt sein eigenes Sidecar
        //  (s. MediaModel ▸ Sidecar-Sammlung) und liest beim Reload selbst neu.
        if (folder == open)
            if (m_pane) m_pane->storage().loadFolder(folder);   // Metadaten für neuen Bestand
        if (m_pane) m_pane->notifyContentsChanged(folder);   // Galerie lädt neu
    }

    if (copied > 0)
        emit statusMessage(de ? QStringLiteral("%1 Datei(en) hinzugefügt.").arg(copied)
                              : QStringLiteral("%1 file(s) added.").arg(copied));
    else if (skipped > 0)
        emit statusMessage(de ? QStringLiteral("%1 Datei(en) übersprungen (bereits vorhanden).").arg(skipped)
                              : QStringLiteral("%1 file(s) skipped (already present).").arg(skipped));
}

// ── Lesezeichen ──────────────────────────────────────────────────────────────
//  ── Mausrad waehrend eines Zuges ────────────────────────────────────────────
//  GEMESSEN auf Wayland (MG_DRAGLOG, echter Zug): waehrend eines Zuges erreichen
//  die Anwendung 892 `DragMove`, aber **kein einziges `Wheel`** und kein
//  `MouseMove`. Unter Wayland gehoert der Zeiger waehrend eines Zuges dem
//  Compositor (`wl_data_device`); Achsen-Ereignisse sind kein Teil des
//  Drag-Protokolls und werden gar nicht erst gesendet. Auf dieser Plattform ist
//  das Rad im Zug also NICHT zu haben - dort bleibt das Randscrollen der Galerie.
//
//  Der Filter bleibt trotzdem: auf X11 und Windows kann das Rad ankommen, und er
//  kostet nichts. Anwendungsfilter laufen in UMGEKEHRTER Installationsreihenfolge
//  - deshalb wird unserer nicht sofort gesetzt, sondern ueber einen 0-ms-Timer:
//  der feuert bereits INNERHALB der Ereignisschleife des Zuges, also nachdem Qt
//  seinen eigenen Filter gesetzt hat, und liegt damit davor.
//
//  Verbraucht wird nichts (`return false`) - der Filter schaut nur zu.
bool AppController::eventFilter(QObject* watched, QEvent* event) {
    if (m_tileDragActive) {
        //  DIAGNOSE (nur mit MG_DRAGLOG=1): welche Ereignisse erreichen die
        //  Anwendung waehrend eines Zuges ueberhaupt? Unter Wayland gehoert der
        //  Zeiger waehrend eines Zuges dem Compositor, und Achsen-Ereignisse
        //  sind kein Teil des Drag-Protokolls - dann kommt hier nie ein
        //  QEvent::Wheel an, und kein Filter kann daran etwas aendern.
        if (m_dragLog) {
            const int t = static_cast<int>(event->type());
            m_dragEventCounts[t] = m_dragEventCounts.value(t, 0) + 1;
        }
        if (event->type() == QEvent::Wheel) {
            const auto* we = static_cast<QWheelEvent*>(event);
            const int dy = we->angleDelta().y();
            if (m_dragLog)
                qInfo("[MG_DRAGLOG] Wheel waehrend des Zuges: angleDelta.y=%d", dy);
            if (dy != 0) emit dragWheel(dy);
        }
    }
    return QObject::eventFilter(watched, event);
}

void AppController::beginTileDrag() {
    if (m_tileDragActive) return;
    m_tileDragActive = true;
    m_dragLog = qEnvironmentVariableIsSet("MG_DRAGLOG");
    m_dragEventCounts.clear();
    if (m_dragLog) qInfo("[MG_DRAGLOG] Zug beginnt");
    QTimer::singleShot(0, this, [this]() {
        if (m_tileDragActive) qApp->installEventFilter(this);
    });
    emit tileDragActiveChanged();
}

void AppController::endTileDrag() {
    qApp->removeEventFilter(this);
    if (m_dragLog) {
        //  Nach Haeufigkeit sortiert waere schoener; die Zahl allein genuegt.
        QStringList seen;
        for (auto it = m_dragEventCounts.constBegin();
             it != m_dragEventCounts.constEnd(); ++it)
            seen << QStringLiteral("%1x Typ %2").arg(it.value()).arg(it.key());
        qInfo("[MG_DRAGLOG] Zug endet. Gesehene Ereignisse: %s",
              seen.isEmpty() ? "KEINE" : qPrintable(seen.join(QStringLiteral(", "))));
        qInfo("[MG_DRAGLOG]   (QEvent::Wheel = %d, MouseMove = %d, DragMove = %d)",
              int(QEvent::Wheel), int(QEvent::MouseMove), int(QEvent::DragMove));
    }
    if (!m_tileDragActive) return;
    m_tileDragActive = false;
    emit tileDragActiveChanged();
}

bool AppController::showHiddenFiles() const { return m_settings.showHiddenFiles(); }
void AppController::setShowHiddenFiles(bool v) {
    if (m_settings.showHiddenFiles() == v) return;
    m_settings.setShowHiddenFiles(v);
    emit showHiddenFilesChanged();
    //  ALLE Haelften neu einlesen, nicht nur die fokussierte: die Einstellung
    //  wird beim Umschalten meist aus den EINSTELLUNGEN heraus gesetzt, und dann
    //  zeigt `m_pane` gar nicht auf die Galerie, die man gerade vor sich hat -
    //  `refreshCurrentFolder()` lief dann ins Leere (vom Nutzer gemeldet: die
    //  Einstellung war im Programm, `.gitignore` erschien trotzdem nicht).
    for (PaneController* p : m_panes)
        if (p) p->refreshCurrentFolder();
}

bool AppController::fileDropMove() const { return m_settings.fileDropMove(); }

bool AppController::showAllFiles() const { return m_settings.showAllFiles(); }
bool AppController::deleteTagsInSubfolders() const { return m_settings.deleteTagsInSubfolders(); }
void AppController::setDeleteTagsInSubfolders(bool v) {
    if (m_settings.deleteTagsInSubfolders() == v) return;
    m_settings.setDeleteTagsInSubfolders(v);
    emit deleteTagsInSubfoldersChanged();
}

void AppController::setShowAllFiles(bool v) {
    if (m_settings.showAllFiles() == v) return;
    m_settings.setShowAllFiles(v);
    emit showAllFilesChanged();
}

bool AppController::galleryListLayout() const { return m_settings.galleryListLayout(); }

int AppController::listRowHeight() const { return m_settings.galleryListRowHeight(); }

void AppController::setScreenWidth(int w) {
    const int nw = qMax(0, w);
    if (nw == m_screenW) return;
    m_screenW = nw;
    emit screenWidthChanged();
}

void AppController::setListRowHeight(int px) {
    //  `ISettings` klemmt auf 28…160 - hier wird gegen den GESPEICHERTEN Wert
    //  verglichen, nicht gegen den übergebenen: am Anschlag feuert das Signal
    //  sonst bei jedem Radschritt weiter, und jede Kachelzeile rechnete neu.
    const int before = m_settings.galleryListRowHeight();
    m_settings.setGalleryListRowHeight(px);
    const int after = m_settings.galleryListRowHeight();
    if (after == before) return;
    m_settings.sync();
    emit listRowHeightChanged();
}

void AppController::zoomInList(int stepPx)  { setListRowHeight(listRowHeight() + stepPx); }
void AppController::zoomOutList(int stepPx) { setListRowHeight(listRowHeight() - stepPx); }

bool AppController::settingsGroupCollapsed(const QString& key) const {
    if (key.isEmpty()) return false;
    return m_settings.collapsedSettingsGroups().contains(key);
}

void AppController::setSettingsGroupCollapsed(const QString& key, bool collapsed) {
    if (key.isEmpty()) return;
    QStringList keys = m_settings.collapsedSettingsGroups();
    const bool had = keys.contains(key);
    if (had == collapsed) return;
    if (collapsed) keys.append(key);
    else           keys.removeAll(key);
    //  Sortiert ablegen: die Datei bleibt zwischen zwei Laeufen vergleichbar,
    //  und die Reihenfolge sagt ohnehin nichts aus.
    keys.sort();
    m_settings.setCollapsedSettingsGroups(keys);
    m_settings.sync();
}

void AppController::setGalleryListLayout(bool v) {
    if (m_settings.galleryListLayout() == v) return;
    m_settings.setGalleryListLayout(v);
    //  Kein Neu-Einlesen des Ordners: die Umschaltung ist reine Darstellung.
    //  `GalleryView` rechnet Zellengröße und Spaltenzahl aus `listMode` neu,
    //  das Modell bleibt unangetastet.
    m_settings.sync();
    emit galleryListLayoutChanged();
}

void AppController::setFileDropMove(bool v) {
    if (m_settings.fileDropMove() == v) return;
    m_settings.setFileDropMove(v);
    emit fileDropMoveChanged();
}

// ── Lesezeichen: Einträge, Gruppen, Anzeigereihenfolge ───────────────────────
//  Ein Eintrag steht als EINE Zeichenkette in den Einstellungen:
//      "Name\tPfad\tGruppenpfad"  (Gruppe optional, Altformat "Name\tPfad" bzw. "Pfad")
//  Die Gruppen selbst stehen getrennt davon in ihrer ANZEIGEreihenfolge:
//      "Gruppenpfad"  bzw.  "Gruppenpfad\t1"  (eingeklappt)
//
//  VERSCHACHTELUNG über den PFAD: die Identität einer Gruppe ist ihr voller
//  Pfad mit "/" als Trenner - "Persönlich", "Persönlich/Lernen". Der Elternteil
//  steht damit im Namen selbst; es braucht weder eine Kennung noch eine zweite
//  Liste. Zwei Folgen, die zusammengehören:
//    • Ein Gruppenname darf kein "/" enthalten (`isUsableGroupName`), sonst
//      hieße derselbe Text zwei verschiedene Bäume.
//    • Eine ALTE Konfiguration bleibt gültig, ohne umgeschrieben zu werden:
//      ihre Gruppennamen sind bereits Pfade ohne Trenner, also Gruppen der
//      obersten Ebene. Genau das war ihre Bedeutung vorher auch.
//  Ohne dritte Spalte gehört ein Eintrag zu „ohne Gruppe", und ohne Gruppenliste
//  gibt es genau diesen einen Abschnitt - also die Liste, die es vorher gab.
namespace {

constexpr QChar kGroupSep = QLatin1Char('/');

struct BmEntry {
    QString name;      // wie gespeichert; leer = Anzeige fällt auf den Ordnernamen zurück
    QString path;
    QString group;     // voller Gruppenpfad; leer = oberste Ebene
};

BmEntry parseBookmark(const QString& raw) {
    BmEntry e;
    const QStringList parts = raw.split(QLatin1Char('\t'));
    if (parts.size() >= 2) {
        e.name = parts.at(0).trimmed();
        e.path = parts.at(1).trimmed();
    } else {
        e.path = raw.trimmed();
    }
    if (parts.size() >= 3) e.group = parts.at(2).trimmed();
    return e;
}

QString packBookmark(const BmEntry& e) {
    if (e.group.isEmpty())
        return e.name.isEmpty() ? e.path : (e.name + QLatin1Char('\t') + e.path);
    return e.name + QLatin1Char('\t') + e.path + QLatin1Char('\t') + e.group;
}

QString displayName(const BmEntry& e) {
    return e.name.isEmpty() ? QFileInfo(e.path).fileName() : e.name;
}

// ── Gruppenpfade ────────────────────────────────────────────────────────────
//  Alles, was mit dem Trenner zu tun hat, steht HIER - nicht verstreut in den
//  Verwaltungsfunktionen.

//  Ein einzelnes Glied: nicht leer, kein Trenner, kein Tabulator (der trennt
//  die Spalten der gespeicherten Zeile).
bool usableGroupLeaf(const QString& name) {
    const QString t = name.trimmed();
    return !t.isEmpty() && !t.contains(kGroupSep) && !t.contains(QLatin1Char('\t'));
}

//  Beschneidet jedes Glied und wirft leere weg: "  A / / B " -> "A/B".
QString normalizeGroupPath(const QString& path) {
    QStringList out;
    const QStringList parts = path.split(kGroupSep);
    for (const QString& p : parts) {
        const QString t = p.trimmed();
        if (!t.isEmpty()) out.append(t);
    }
    return out.join(kGroupSep);
}

QString parentOfGroup(const QString& path) {
    const int cut = path.lastIndexOf(kGroupSep);
    return cut < 0 ? QString() : path.left(cut);
}

QString leafOfGroup(const QString& path) {
    const int cut = path.lastIndexOf(kGroupSep);
    return cut < 0 ? path : path.mid(cut + 1);
}

QString joinGroup(const QString& parent, const QString& leaf) {
    return parent.isEmpty() ? leaf : (parent + kGroupSep + leaf);
}

//  Liegt `path` IN `ancestor` (oder ist es selbst)? Der Test hinter dem
//  Ring-Schutz beim Verschieben und hinter „mitziehen" beim Umbenennen.
bool isSelfOrBelow(const QString& path, const QString& ancestor) {
    if (ancestor.isEmpty()) return true;                 // alles liegt unter der Wurzel
    if (path.compare(ancestor, Qt::CaseInsensitive) == 0) return true;
    const QString prefix = ancestor + kGroupSep;
    return path.startsWith(prefix, Qt::CaseInsensitive);
}

//  Ersetzt die Vorsilbe `from` durch `to` - für Umbenennen und Verschieben
//  eines ganzen Teilbaums. `path` muss vorher `isSelfOrBelow(path, from)` sein.
QString reparent(const QString& path, const QString& from, const QString& to) {
    return to + path.mid(from.size());
}

struct BmGroup {
    QString path;                  // voller Pfad
    bool    collapsed = false;
};

int groupPos(const QList<BmGroup>& groups, const QString& path) {
    for (int i = 0; i < groups.size(); ++i)
        if (groups.at(i).path.compare(path, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

QList<BmGroup> parseGroups(const QStringList& raw) {
    QList<BmGroup> out;
    out.reserve(raw.size());
    for (const QString& r : raw) {
        const QStringList parts = r.split(QLatin1Char('\t'));
        BmGroup g;
        g.path = normalizeGroupPath(parts.value(0));
        if (g.path.isEmpty()) continue;
        g.collapsed = parts.value(1) == QLatin1String("1");
        //  Pfade sind eindeutig (ohne Rücksicht auf Groß-/Kleinschreibung) -
        //  eine von Hand verdoppelte Zeile würde sonst zwei Abschnitte mit
        //  denselben Mitgliedern zeigen.
        if (groupPos(out, g.path) < 0) out.append(g);
    }
    //  FEHLENDE VORFAHREN ergänzen: steht "A/B" ohne "A" in der Liste (von Hand
    //  bearbeitet, oder "A" wurde gelöscht), hinge der ganze Ast an einem
    //  Elternteil, den die Tiefensuche nie besucht - er wäre unsichtbar. Die
    //  Vorsilbe wird deshalb angelegt, und zwar DIREKT VOR ihrem Kind, damit
    //  die Anzeigereihenfolge der Geschwister erhalten bleibt.
    for (int i = 0; i < out.size(); ++i) {
        const QString parent = parentOfGroup(out.at(i).path);
        if (parent.isEmpty() || groupPos(out, parent) >= 0) continue;
        out.insert(i, BmGroup{ parent, false });
        --i;                       // dieselbe Stelle erneut prüfen (Großeltern)
    }
    return out;
}

QStringList packGroups(const QList<BmGroup>& groups) {
    QStringList out;
    out.reserve(groups.size());
    for (const BmGroup& g : groups)
        out.append(g.collapsed ? (g.path + QLatin1String("\t1")) : g.path);
    return out;
}

} // namespace

bool AppController::isUsableGroupName(const QString& name) const {
    return usableGroupLeaf(name);
}

//  Der Abschnitt, in dem ein Eintrag landet: seine Gruppe, sofern es sie gibt -
//  sonst „ohne Gruppe". Ein von Hand verstellter Gruppenpfad lässt ein
//  Lesezeichen damit nie verschwinden.
QString AppController::bookmarkSection(const QString& group) const {
    const QString g = normalizeGroupPath(group);
    if (g.isEmpty()) return QString();
    const QList<BmGroup> groups = parseGroups(m_settings.bookmarkGroups());
    const int pos = groupPos(groups, g);
    return pos < 0 ? QString() : groups.at(pos).path;
}

QString AppController::ensureBookmarkGroup(const QString& fullPath) {
    const QString wanted = normalizeGroupPath(fullPath);
    if (wanted.isEmpty()) return QString();

    QList<BmGroup> groups = parseGroups(m_settings.bookmarkGroups());
    const QStringList parts = wanted.split(kGroupSep);
    QString sofar;                     // in der GESPEICHERTEN Schreibweise
    bool changed = false;
    for (const QString& leaf : parts) {
        if (!usableGroupLeaf(leaf)) return QString();
        const QString candidate = joinGroup(sofar, leaf);
        const int pos = groupPos(groups, candidate);
        if (pos >= 0) {
            sofar = groups.at(pos).path;          // bestehende Schreibweise behalten
        } else {
            groups.append(BmGroup{ candidate, false });
            sofar   = candidate;
            changed = true;
        }
    }
    if (changed) {
        m_settings.setBookmarkGroups(packGroups(groups));
        m_settings.sync();
    }
    return sofar;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Der ganze Baum als FLACHE Zeilenliste, in Anzeigereihenfolge.
//
//  Je Zeile:
//      kind        "group" | "bookmark"
//      name        angezeigter Text (Gruppe: letztes Glied; Eintrag: sein Name)
//      group       Gruppe: ihr VOLLER Pfad · Eintrag: der Pfad seiner Gruppe
//      parent      Pfad der Elterngruppe ("" = oberste Ebene)
//      depth       Einrücktiefe, 0 = oberste Ebene
//      hidden      true, wenn ein VORFAHR eingeklappt ist (Zeile nicht zeigen)
//      path        NUR Eintrag: der Ordner
//      index       NUR Eintrag: Platz in der gespeicherten Liste (Identität)
//      collapsed   NUR Gruppe: ist SIE eingeklappt?
//      count       NUR Gruppe: Zahl der DIREKTEN Kinder (Gruppen + Einträge)
//      pos         Platz unter den GLEICHARTIGEN Geschwistern - genau der Wert,
//                  den `moveBookmark`/`moveBookmarkGroup` als `pos` erwarten
//
//  Je Ebene erst die Lesezeichen, dann die Untergruppen - ein Ordner steht
//  damit immer über den Schubladen, die unter ihm liegen.
//
//  Tiefensuche mit EIGENEM STAPEL statt Rekursion: die Schachtelungstiefe kommt
//  aus einer Konfigurationsdatei und ist damit nach oben offen.
// ─────────────────────────────────────────────────────────────────────────────
QVariantList AppController::bookmarkTree() const {
    const QStringList raw = m_settings.savedFolders();
    const QList<BmGroup> groups = parseGroups(m_settings.bookmarkGroups());

    //  Einträge EINMAL lesen und ihrem Abschnitt zuordnen. `bookmarkSection`
    //  zerlegte die Gruppenliste je Eintrag neu - bei vielen Lesezeichen wäre
    //  das quadratisch, und die Liste steht hier bereits.
    struct Row { QString name, path, section; int index; };
    QList<Row> rows;
    rows.reserve(raw.size());
    for (int i = 0; i < raw.size(); ++i) {
        const BmEntry e = parseBookmark(raw.at(i));
        if (e.path.isEmpty()) continue;
        const QString g = normalizeGroupPath(e.group);
        const int gp = g.isEmpty() ? -1 : groupPos(groups, g);
        rows.append({ displayName(e), e.path, gp < 0 ? QString() : groups.at(gp).path, i });
    }

    QVariantList out;
    out.reserve(groups.size() + rows.size());

    //  Alle Lesezeichen EINER Ebene ausgeben.
    auto emitBookmarks = [&](const QString& parent, int depth, bool hidden) {
        int pos = 0;                       // Platz unter den GESCHWISTERN
        for (const Row& r : std::as_const(rows)) {
            if (r.section.compare(parent, Qt::CaseInsensitive) != 0) continue;
            QVariantMap m;
            m.insert(QStringLiteral("kind"),   QStringLiteral("bookmark"));
            m.insert(QStringLiteral("name"),   r.name);
            m.insert(QStringLiteral("path"),   r.path);
            m.insert(QStringLiteral("group"),  parent);
            m.insert(QStringLiteral("parent"), parent);
            m.insert(QStringLiteral("depth"),  depth);
            m.insert(QStringLiteral("hidden"), hidden);
            m.insert(QStringLiteral("index"),  r.index);
            m.insert(QStringLiteral("pos"),    pos++);
            out.append(m);
        }
    };

    //  Die Plätze der DIREKTEN Untergruppen einer Ebene, in Listenreihenfolge.
    auto childGroups = [&](const QString& parent) {
        QList<int> idx;
        for (int i = 0; i < groups.size(); ++i)
            if (parentOfGroup(groups.at(i).path).compare(parent, Qt::CaseInsensitive) == 0)
                idx.append(i);
        return idx;
    };

    auto directCount = [&](const QString& parent) {
        int n = childGroups(parent).size();
        for (const Row& r : std::as_const(rows))
            if (r.section.compare(parent, Qt::CaseInsensitive) == 0) ++n;
        return n;
    };

    //  Ein Stapeleintrag = eine Gruppe, deren KOPFZEILE noch aussteht.
    struct Frame { int group; int depth; bool hidden; int pos; };
    QList<Frame> stack;

    //  Oberste Ebene: erst ihre Lesezeichen, dann ihre Gruppen.
    emitBookmarks(QString(), 0, false);
    {
        const QList<int> roots = childGroups(QString());
        for (int k = roots.size() - 1; k >= 0; --k)
            stack.append(Frame{ roots.at(k), 0, false, k });
    }

    while (!stack.isEmpty()) {
        const Frame f = stack.takeLast();
        const BmGroup& g = groups.at(f.group);

        QVariantMap m;
        m.insert(QStringLiteral("kind"),      QStringLiteral("group"));
        m.insert(QStringLiteral("name"),      leafOfGroup(g.path));
        m.insert(QStringLiteral("path"),      QString());
        m.insert(QStringLiteral("group"),     g.path);
        m.insert(QStringLiteral("parent"),    parentOfGroup(g.path));
        m.insert(QStringLiteral("depth"),     f.depth);
        m.insert(QStringLiteral("hidden"),    f.hidden);
        m.insert(QStringLiteral("collapsed"), g.collapsed);
        m.insert(QStringLiteral("count"),     directCount(g.path));
        m.insert(QStringLiteral("pos"),       f.pos);
        out.append(m);

        //  Der Inhalt liegt eine Ebene tiefer und ist verborgen, sobald DIESE
        //  Gruppe zu ist - oder schon ein Vorfahr zu war.
        const bool inner = f.hidden || g.collapsed;
        emitBookmarks(g.path, f.depth + 1, inner);
        const QList<int> kids = childGroups(g.path);
        for (int k = kids.size() - 1; k >= 0; --k)
            stack.append(Frame{ kids.at(k), f.depth + 1, inner, k });
    }

    return out;
}

//  Flache Liste NUR der Lesezeichen, in derselben Anzeigereihenfolge - für
//  Ablegeleiste, Dateiwähler und alles, was Gruppen nicht darstellt.
//  Eingeklappte Gruppen zählen mit: „zugeklappt" ist eine Frage der Anzeige,
//  kein Ausschluss.
QVariantList AppController::savedFolders() const {
    QVariantList out;
    const QVariantList tree = bookmarkTree();
    for (const QVariant& v : tree) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("kind")).toString() == QLatin1String("bookmark"))
            out.append(m);
    }
    return out;
}

void AppController::openBookmark(const QString& path) {
    if (path.isEmpty()) return;
    if (m_pane) m_pane->openFolder(path);   // leert den Rückweg (s. openSubfolder)
}

// ── Lesezeichen-Verwaltung ───────────────────────────────────────────────────
void AppController::addBookmark(const QString& name, const QString& path,
                                const QString& group) {
    if (path.trimmed().isEmpty()) return;
    //  Eine noch unbekannte Gruppe wird angelegt statt verworfen - sonst
    //  landete der Eintrag stillschweigend woanders, als der Nutzer wählte.
    //  Mit Pfaden gilt das für den GANZEN Ast: "A/B/C" legt A, B und C an.
    const QString section = ensureBookmarkGroup(group);

    QStringList entries = m_settings.savedFolders();
    entries.append(packBookmark(BmEntry{ name.trimmed(), path.trimmed(), section }));
    m_settings.setSavedFolders(entries);
    m_settings.sync();
    emit savedFoldersChanged();
}

void AppController::updateBookmark(int index, const QString& name, const QString& path,
                                   const QString& group) {
    if (path.trimmed().isEmpty()) return;
    if (index < 0 || index >= m_settings.savedFolders().size()) return;
    const QString section = ensureBookmarkGroup(group);
    //  ERST jetzt lesen: `ensureBookmarkGroup` schreibt die Gruppenliste, nicht
    //  die Einträge - eine vorher gezogene Kopie wäre trotzdem unnötig alt.
    QStringList entries = m_settings.savedFolders();
    entries[index] = packBookmark(BmEntry{ name.trimmed(), path.trimmed(), section });
    m_settings.setSavedFolders(entries);
    m_settings.sync();
    emit savedFoldersChanged();
}

void AppController::removeBookmark(int index) {
    QStringList entries = m_settings.savedFolders();
    if (index < 0 || index >= entries.size()) return;
    entries.removeAt(index);
    m_settings.setSavedFolders(entries);
    m_settings.sync();
    emit savedFoldersChanged();
}

// ── Lesezeichen-Gruppen ──────────────────────────────────────────────────────
void AppController::addBookmarkGroup(const QString& name, const QString& parentPath) {
    if (!usableGroupLeaf(name)) return;
    //  Die Elterngruppe muss es geben - sonst hinge die neue Gruppe an einem
    //  Pfad, den niemand sieht. Fehlt sie, wird sie mit angelegt.
    const QString parent = parentPath.trimmed().isEmpty()
                               ? QString()
                               : ensureBookmarkGroup(parentPath);
    if (!parentPath.trimmed().isEmpty() && parent.isEmpty()) return;   // unbrauchbarer Pfad

    QList<BmGroup> groups = parseGroups(m_settings.bookmarkGroups());
    const QString full = joinGroup(parent, name.trimmed());
    if (groupPos(groups, full) >= 0) return;          // Name in dieser Ebene vergeben
    groups.append(BmGroup{ full, false });
    m_settings.setBookmarkGroups(packGroups(groups));
    m_settings.sync();
    emit savedFoldersChanged();
}

void AppController::renameBookmarkGroup(const QString& path, const QString& newName) {
    const QString from = normalizeGroupPath(path);
    if (from.isEmpty() || !usableGroupLeaf(newName)) return;

    QList<BmGroup> groups = parseGroups(m_settings.bookmarkGroups());
    const int pos = groupPos(groups, from);
    if (pos < 0) return;

    const QString to = joinGroup(parentOfGroup(from), newName.trimmed());
    //  Nur bei WIRKLICH gleichem Namen nichts tun. Eine reine Schreibweisen-
    //  Aenderung ("Studium" -> "studium") ist eine gueltige Umbenennung: die
    //  Kollisionspruefung unten findet dabei die Gruppe selbst und laesst sie durch.
    if (to == from) return;
    const int clash = groupPos(groups, to);
    if (clash >= 0 && clash != pos) return;           // Name in dieser Ebene vergeben

    //  Die Gruppe SELBST und jede Untergruppe tragen den alten Namen als
    //  Vorsilbe - alle ziehen mit, sonst risse der Ast ab.
    for (BmGroup& g : groups)
        if (isSelfOrBelow(g.path, from))
            g.path = reparent(g.path, from, to);
    m_settings.setBookmarkGroups(packGroups(groups));

    //  Die Mitglieder tragen ihren Gruppenpfad selbst - auch die der
    //  Untergruppen.
    QStringList entries = m_settings.savedFolders();
    for (QString& raw : entries) {
        BmEntry e = parseBookmark(raw);
        if (e.path.isEmpty()) continue;
        const QString g = normalizeGroupPath(e.group);
        if (g.isEmpty() || !isSelfOrBelow(g, from)) continue;
        e.group = reparent(g, from, to);
        raw = packBookmark(e);
    }
    m_settings.setSavedFolders(entries);
    m_settings.sync();
    emit savedFoldersChanged();
}

void AppController::removeBookmarkGroup(const QString& path) {
    const QString n = normalizeGroupPath(path);
    if (n.isEmpty()) return;
    QList<BmGroup> groups = parseGroups(m_settings.bookmarkGroups());
    if (groupPos(groups, n) < 0) return;

    //  Die Gruppe UND alles darunter verschwindet aus der Ordnung.
    for (int i = groups.size() - 1; i >= 0; --i)
        if (isSelfOrBelow(groups.at(i).path, n))
            groups.removeAt(i);
    m_settings.setBookmarkGroups(packGroups(groups));

    //  Die Lesezeichen des ganzen Astes bleiben - sie rücken nach „ohne
    //  Gruppe". Eine Gruppe zu schließen darf nie Pfade kosten.
    QStringList entries = m_settings.savedFolders();
    for (QString& raw : entries) {
        BmEntry e = parseBookmark(raw);
        if (e.path.isEmpty()) continue;
        const QString g = normalizeGroupPath(e.group);
        if (g.isEmpty() || !isSelfOrBelow(g, n)) continue;
        e.group.clear();
        raw = packBookmark(e);
    }
    m_settings.setSavedFolders(entries);
    m_settings.sync();
    emit savedFoldersChanged();
}

void AppController::setBookmarkGroupCollapsed(const QString& path, bool collapsed) {
    QList<BmGroup> groups = parseGroups(m_settings.bookmarkGroups());
    const int pos = groupPos(groups, normalizeGroupPath(path));
    if (pos < 0 || groups.at(pos).collapsed == collapsed) return;
    groups[pos].collapsed = collapsed;
    m_settings.setBookmarkGroups(packGroups(groups));
    m_settings.sync();
    emit savedFoldersChanged();
}

void AppController::moveBookmarkGroup(const QString& path, const QString& newParentPath,
                                      int pos) {
    const QString from = normalizeGroupPath(path);
    if (from.isEmpty()) return;

    QList<BmGroup> groups = parseGroups(m_settings.bookmarkGroups());
    const int at = groupPos(groups, from);
    if (at < 0) return;

    //  RING-SCHUTZ: eine Gruppe darf nicht unter sich selbst wandern. Ohne
    //  diesen Test entstünde ein Pfad wie "A/B/A/B", den die Tiefensuche nie
    //  erreicht - der Ast wäre weg.
    QString parent;
    if (!newParentPath.trimmed().isEmpty()) {
        parent = normalizeGroupPath(newParentPath);
        if (isSelfOrBelow(parent, from)) return;
        const int pp = groupPos(groups, parent);
        if (pp < 0) return;                       // Zielgruppe gibt es nicht
        parent = groups.at(pp).path;              // gespeicherte Schreibweise
    }

    const QString to = joinGroup(parent, leafOfGroup(groups.at(at).path));
    const int clash = groupPos(groups, to);
    if (clash >= 0 && clash != at) return;        // Name in der Zielebene vergeben

    if (to != groups.at(at).path) {
        for (BmGroup& g : groups)
            if (isSelfOrBelow(g.path, from))
                g.path = reparent(g.path, from, to);

        QStringList entries = m_settings.savedFolders();
        for (QString& raw : entries) {
            BmEntry e = parseBookmark(raw);
            if (e.path.isEmpty()) continue;
            const QString g = normalizeGroupPath(e.group);
            if (g.isEmpty() || !isSelfOrBelow(g, from)) continue;
            e.group = reparent(g, from, to);
            raw = packBookmark(e);
        }
        m_settings.setSavedFolders(entries);
    }

    //  Platz unter den GESCHWISTERN. Nur der Eintrag der Gruppe selbst zieht
    //  um; ihre Untergruppen behalten ihre Plätze in der Liste, denn deren
    //  Reihenfolge zählt nur untereinander (die Tiefensuche liest den Vater aus
    //  dem Pfad, nicht aus der Position).
    const int cur = groupPos(groups, to);
    const BmGroup moved = groups.takeAt(cur);
    QList<int> siblings;
    for (int i = 0; i < groups.size(); ++i)
        if (parentOfGroup(groups.at(i).path).compare(parent, Qt::CaseInsensitive) == 0)
            siblings.append(i);
    int dest;
    if (siblings.isEmpty())                     dest = groups.size();
    else if (pos < 0 || pos >= siblings.size()) dest = siblings.last() + 1;
    else                                        dest = siblings.at(pos);
    groups.insert(dest, moved);

    m_settings.setBookmarkGroups(packGroups(groups));
    m_settings.sync();
    emit savedFoldersChanged();
}

void AppController::moveBookmark(int index, const QString& targetGroup, int pos) {
    QStringList entries = m_settings.savedFolders();
    if (index < 0 || index >= entries.size()) return;
    BmEntry e = parseBookmark(entries.at(index));
    if (e.path.isEmpty()) return;

    const QString section = bookmarkSection(targetGroup);
    e.group = section;
    entries.removeAt(index);

    //  Die Reihenfolge INNERHALB einer Gruppe ist die Reihenfolge in der
    //  gespeicherten Liste - der neue Platz wird deshalb relativ zu den übrigen
    //  Mitgliedern derselben Gruppe gesucht.
    QList<int> members;
    for (int i = 0; i < entries.size(); ++i) {
        const BmEntry other = parseBookmark(entries.at(i));
        if (other.path.isEmpty()) continue;
        if (bookmarkSection(other.group).compare(section, Qt::CaseInsensitive) == 0)
            members.append(i);
    }
    int at;
    if (members.isEmpty())                        at = entries.size();
    else if (pos < 0 || pos >= members.size())    at = members.last() + 1;
    else                                          at = members.at(pos);
    entries.insert(at, packBookmark(e));

    m_settings.setSavedFolders(entries);
    m_settings.sync();
    emit savedFoldersChanged();
}

// ── Galerie-View-State ───────────────────────────────────────────────────────
int AppController::tileWidth()        const { return m_settings.tileWidth(); }
int AppController::tileHeight()       const { return m_settings.tileHeight(); }
int AppController::tileArrangement()  const { return static_cast<int>(m_settings.tileArrangement()); }
int AppController::manualAreaWidth()  const { return m_settings.manualAreaWidth(); }

void AppController::setTileSize(int w, int h) {
    // Obergrenze = darstellbare Galeriefläche (von der Shell gemeldet):
    // größer ließe sich eine Kachel in der App ohnehin nicht anzeigen.
    const int nw = qBound(40, w, m_maxTileW);
    const int nh = qBound(40, h, m_maxTileH);
    if (m_settings.tileWidth() == nw && m_settings.tileHeight() == nh) return;
    // AppSettings::setTileSize emittiert tileSizeChanged genau einmal (weitergeleitet).
    AppSettings::instance().setTileSize(nw, nh);
    m_settings.sync();
}

void AppController::setTileSizeLimit(int w, int h) {
    // Untergrenze 40 = minimale Kachelgröße (sonst wäre gar keine Größe gültig).
    const int nw = qMax(40, w);
    const int nh = qMax(40, h);
    if (nw == m_maxTileW && nh == m_maxTileH) return;
    m_maxTileW = nw;
    m_maxTileH = nh;
    emit tileSizeLimitChanged();
}

void AppController::zoomIn(int stepPx) {
    setTileSize(m_settings.tileWidth() + stepPx, m_settings.tileHeight() + stepPx);
}

void AppController::zoomOut(int stepPx) {
    setTileSize(m_settings.tileWidth() - stepPx, m_settings.tileHeight() - stepPx);
}

void AppController::setTileArrangement(int arrangement) {
    const auto a = static_cast<TileArrangement>(arrangement);
    if (m_settings.tileArrangement() == a) return;
    m_settings.setTileArrangement(a);   // emittiert tileArrangementChanged (weitergeleitet)
    m_settings.sync();
}

void AppController::setManualAreaWidth(int w) {
    const int nw = qMax(40, w);
    if (m_settings.manualAreaWidth() == nw) return;
    m_settings.setManualAreaWidth(nw);
    m_settings.sync();
    emit tileArrangementChanged();
}

// ── Einstellungen ────────────────────────────────────────────────────────────
QColor AppController::backgroundColor() const { return m_settings.backgroundColor(); }
QColor AppController::accentColor()     const { return m_settings.accentColor(); }

QString AppController::language() const {
    return m_settings.language() == Language::English
               ? QStringLiteral("en")
               : QStringLiteral("de");
}

QString AppController::videoPlayback() const {
    return m_settings.videoPlayback() == VideoPlayback::External
               ? QStringLiteral("external")
               : QStringLiteral("native");
}

QString AppController::pageTransition() const {
    return m_settings.pageTransition() == PageTransition::Fade
               ? QStringLiteral("fade")
               : QStringLiteral("slide");
}

QString AppController::extractSelectStyle() const {
    return m_settings.extractSelectStyle() == ExtractSelectStyle::Overlay
               ? QStringLiteral("overlay")
               : QStringLiteral("frame");
}

QString AppController::extractLayout() const {
    return m_settings.extractLayout() == ExtractLayout::Compact
               ? QStringLiteral("compact")
               : QStringLiteral("workbench");
}

bool AppController::audioAccentApple() const { return m_settings.audioAccentApple(); }

bool AppController::monoPlay() const { return m_settings.monoPlay(); }

int AppController::videoSeekStep() const { return m_settings.videoSeekStep(); }

bool AppController::spellCheck() const { return m_settings.spellCheckEnabled(); }
QString AppController::spellLanguage() const { return m_settings.spellLanguage(); }

void AppController::setSpellCheck(bool v) {
    if (m_settings.spellCheckEnabled() == v) return;
    m_settings.setSpellCheckEnabled(v);
    emit spellCheckChanged();
}
void AppController::setSpellLanguage(const QString& lang) {
    if (m_settings.spellLanguage() == lang) return;
    m_settings.setSpellLanguage(lang);
    emit spellCheckChanged();
}
QStringList AppController::spellLanguages() const {
    return mg::SpellChecker::availableLanguages();
}

//  Der Wert der FOKUSSIERTEN Hälfte; ohne Hälfte der gemerkte Stand.
bool AppController::optionsVisible() const {
    return m_pane ? m_pane->optionsVisible() : m_settings.optionsVisible();
}

void AppController::setBackgroundColor(const QColor& c) {
    if (m_settings.backgroundColor() == c) return;
    m_settings.setBackgroundColor(c);   // emittiert colorSchemeChanged -> weitergeleitet
}

void AppController::setAccentColor(const QColor& c) {
    if (m_settings.accentColor() == c) return;
    m_settings.setAccentColor(c);
}

void AppController::setLanguage(const QString& code) {
    const Language l = (code.compare(QLatin1String("en"), Qt::CaseInsensitive) == 0)
                           ? Language::English
                           : Language::German;
    if (m_settings.language() == l) return;
    m_settings.setLanguage(l);          // emittiert languageChanged -> weitergeleitet
}

void AppController::setVideoPlayback(const QString& mode) {
    const VideoPlayback v = (mode.compare(QLatin1String("external"), Qt::CaseInsensitive) == 0)
                                ? VideoPlayback::External
                                : VideoPlayback::Native;
    if (m_settings.videoPlayback() == v) return;
    m_settings.setVideoPlayback(v);
    m_settings.sync();
    emit videoPlaybackChanged();        // AppSettings sendet hierfür kein Signal
}

void AppController::setPageTransition(const QString& mode) {
    const PageTransition t = (mode.compare(QLatin1String("fade"), Qt::CaseInsensitive) == 0)
                                 ? PageTransition::Fade
                                 : PageTransition::Slide;
    if (m_settings.pageTransition() == t) return;
    m_settings.setPageTransition(t);
    m_settings.sync();
    emit pageTransitionChanged();
}

void AppController::setExtractSelectStyle(const QString& style) {
    const ExtractSelectStyle s =
        (style.compare(QLatin1String("overlay"), Qt::CaseInsensitive) == 0)
            ? ExtractSelectStyle::Overlay
            : ExtractSelectStyle::Frame;
    if (m_settings.extractSelectStyle() == s) return;
    m_settings.setExtractSelectStyle(s);
    m_settings.sync();
    emit extractSelectStyleChanged();
}

void AppController::setExtractLayout(const QString& layout) {
    const ExtractLayout l =
        (layout.compare(QLatin1String("compact"), Qt::CaseInsensitive) == 0)
            ? ExtractLayout::Compact
            : ExtractLayout::Workbench;
    if (m_settings.extractLayout() == l) return;
    m_settings.setExtractLayout(l);
    m_settings.sync();
    emit extractLayoutChanged();
}

void AppController::setAudioAccentApple(bool apple) {
    if (m_settings.audioAccentApple() == apple) return;
    m_settings.setAudioAccentApple(apple);
    m_settings.sync();
    emit audioAccentChanged();
}

void AppController::setMonoPlay(bool on) {
    if (m_settings.monoPlay() == on) return;
    m_settings.setMonoPlay(on);
    m_settings.sync();
    emit monoPlayChanged();
}

// Spulschritt der Pfeiltasten im Video-Vollbild. Vergleich gegen den bereits
// GEKLEMMTEN gespeicherten Wert (ISettings klemmt beim Schreiben) - ein
// Vergleich mit dem rohen Argument würde bei Werten außerhalb des Bereichs
// jedes Mal neu schreiben und ein Signal auslösen.
void AppController::setVideoSeekStep(int seconds) {
    const int before = m_settings.videoSeekStep();
    m_settings.setVideoSeekStep(seconds);
    if (m_settings.videoSeekStep() == before) return;
    m_settings.sync();
    emit videoSeekStepChanged();
}

// ─── Mono-Play: Wiedergabe-Koordination ───────────────────────────────────────
// Zentrale, zustandslose Vermittlung: der Start einer Wiedergabe wird als
// Broadcast an ALLE Wiedergabestellen gemeldet (inkl. der startenden - sie
// erkennt sich am eigenen Token und ignoriert die Meldung). Bewusst KEINE
// Registry/Pointer-Verwaltung: die Stellen leben in QML (Kacheln kommen und
// gehen), ein reiner Signal-Broadcast ist lebensdauer-sicher und O(1).
void AppController::announcePlayback(const QString& token) {
    if (!m_settings.monoPlay())
        return;                       // Option aus -> parallele Wiedergaben erlaubt
    emit playbackStarted(token);
}

// ─── RHI-Backend-Wechsel ──────────────────────────────────────────────────────
bool AppController::trySetRhiBackend(const QString& backend) {
    if (backend.compare(m_settings.rhiBackend(), Qt::CaseInsensitive) == 0)
        return true;

    // Einfach speichern - wirkt beim nächsten Start.
    // Kein Probe-Prozess, kein Test. Der Crash-Guard in RhiProber fängt
    // einen fehlerhaften Start beim nächsten Mal automatisch ab.
    RhiProber::setDesiredBackend(backend);
    return true;
}

void AppController::toggleOptions() {
    //  Der Modus gehört der HÄLFTE (s. PaneController::optionsVisible): das
    //  Menü schaltet die fokussierte um. Gemerkt wird der Stand trotzdem
    //  appweit - beim nächsten Start soll er wieder so sein.
    const bool v = !optionsVisible();
    if (m_pane) m_pane->setOptionsVisible(v);
    m_settings.setOptionsVisible(v);
    m_settings.sync();
    emit optionsVisibleChanged();       // AppSettings sendet hierfür kein Signal
}

// ── Fensterzustand ───────────────────────────────────────────────────────────
int  AppController::initialWindowWidth()  const { return m_settings.windowSize().width(); }
int  AppController::initialWindowHeight() const { return m_settings.windowSize().height(); }
int  AppController::initialWindowX()      const { return m_settings.windowPos().x(); }
int  AppController::initialWindowY()      const { return m_settings.windowPos().y(); }
bool AppController::startMaximized()      const { return m_settings.windowMaximized(); }

//  DOCX steht und fällt mit ZLIB: ZIP-Einträge sind rohes Deflate, das der
//  Qt-Fallback nicht entpacken kann - und auch das Speichern liest die Quelle
//  zuerst wieder ein (s. core/ZCodec.h).
bool AppController::docxAvailable()       const { return mg::zcodec::available(); }

void AppController::saveWindowState(int w, int h, int x, int y, bool maximized) {
    m_settings.setWindowMaximized(maximized);
    if (!maximized) {                   // im maximierten Zustand keine sinnvolle Normalgeometrie
        m_settings.setWindowSize(QSize(w, h));
        m_settings.setWindowPos(QPoint(x, y));
    }
    m_settings.sync();
    if (m_pane) m_pane->folderService().saveCurrentFolder();
    //  Beide Ordner ueber denselben Weg (s. `persistPaneFolders`).
    persistPaneFolders();
}

// ── Tags ─────────────────────────────────────────────────────────────────────
//  Tags gehören dem Ordner - also der fokussierten Hälfte.
QStringList AppController::allTags() const {
    return m_pane ? m_pane->tagManager().allTags() : QStringList();
}
QColor AppController::tagColor(const QString& tag) const {
    return m_pane ? m_pane->tagManager().tagColor(tag) : QColor();
}
//  ENTFALLEN: tagsForFile / addTagToFile / removeTagFromFile / setCustomDate /
//  clearCustomDate / fileTextPdfColor & Co. Sie nahmen den blanken DATEINAMEN
//  und trafen damit immer das Sidecar des GEOEFFNETEN Ordners - fuer eine Datei
//  aus einem aufgeklappten Unterordner also das falsche. Den Ordner einer Datei
//  kennt das Modell; die Wege liegen jetzt dort (`MediaModel::tagsOfFile`,
//  `addTag`/`removeTag`, `setCustomDate`/`clearCustomDate`,
//  `fileTextPdfColor` & Co.) und routen auf das richtige Sidecar.

//  ── Schriftfarbe des TXT->PDF-Exports: nur noch die GLOBALE Vorgabe ─────────
//  Die Ausnahme JE DATEI liegt im Sidecar des Ordners, dem die Datei gehoert -
//  und den kennt das Modell, nicht diese Fassade (s. `MediaModel::
//  fileTextPdfColor` & Co.).
QColor AppController::textPdfColor() const { return m_settings.textPdfColor(); }

void AppController::setTextPdfColor(const QColor& c) {
    if (m_settings.textPdfColor() == c) return;
    m_settings.setTextPdfColor(c);
    emit textPdfColorChanged();
}

// ── i18n ─────────────────────────────────────────────────────────────────────
QString AppController::text(int key) const {
    return Strings::get(static_cast<StringKey>(key));
}
QString AppController::text(int key, const QString& arg1) const {
    return Strings::get(static_cast<StringKey>(key), arg1);
}

QString AppController::uiText(const QString& lang, const QString& key) const {
    const Language l = (lang == QStringLiteral("en")) ? Language::English
                                                       : Language::German;
    return Strings::byName(key, l);
}

QString AppController::localPath(const QString& urlOrPath) const {
    return mg::toLocalPath(urlOrPath);
}

//  ── Dateien in die Zwischenablage ───────────────────────────────────────────
//  Drei Formate nebeneinander, weil die Dateimanager sich nicht einig sind:
//  `text/uri-list` versteht jeder (Dolphin, Thunar, PCManFM, und es ist auch
//  das Format des Ziehens), `x-special/gnome-copied-files` braucht die
//  GNOME-Linie (Nautilus/Nemo/Caja) mit vorangestelltem „copy", und
//  `text/plain` ist der Rueckfall fuer Terminal und Textfeld.
int AppController::copyFilesToClipboard(const QStringList& paths) const {
    QClipboard* cb = QGuiApplication::clipboard();
    if (!cb) return 0;

    QList<QUrl> urls;
    QStringList plain;
    urls.reserve(paths.size());
    plain.reserve(paths.size());
    for (const QString& p : paths) {
        const QString local = mg::toLocalPath(p);
        if (local.isEmpty() || !QFileInfo::exists(local)) continue;
        urls.append(QUrl::fromLocalFile(local));
        plain.append(local);
    }
    if (urls.isEmpty()) {
        //  Nichts abzulegen - die vorhandene Zwischenablage bleibt, wie sie ist.
        return 0;
    }

    auto* mime = new QMimeData;
    mime->setUrls(urls);
    mime->setText(plain.join(QLatin1Char('\n')));
    QByteArray gnome = QByteArrayLiteral("copy");
    for (const QUrl& u : std::as_const(urls))
        gnome += '\n' + u.toEncoded();
    mime->setData(QStringLiteral("x-special/gnome-copied-files"), gnome);
    //  Die Zwischenablage uebernimmt den Besitz.
    cb->setMimeData(mime);
    //  ZUSAETZLICH selbst merken (s. `clipboardFileUrls`). Der Merker wird VOR
    //  dem Schreiben gesetzt: `setMimeData` loest `dataChanged` aus, und der
    //  Wachhund darf die gerade gesetzte Liste nicht gleich wieder wegwerfen.
    auto* self = const_cast<AppController*>(this);
    self->m_clipSelfSet = true;
    self->m_ownClipFiles = plain;
    return urls.size();
}

QList<QUrl> AppController::clipboardFileUrls() const {
    QClipboard* cb = QGuiApplication::clipboard();
    if (!cb) return {};
    //  ── Haben WIR zuletzt kopiert? Dann gilt unsere eigene Liste ────────────
    //  GEMESSEN auf KDE/Wayland (`bench_shell g`): 29 Adressen abgelegt, die
    //  Systemablage liefert 3 zurueck - und zwar die eines FRUEHEREN Laufs, aus
    //  einem ganz anderen Ordner. Eine Zwischenablagen-Verwaltung (Klipper,
    //  erkennbar am Format `application/x-kde-onlyReplaceEmpty`) haelt dort
    //  einen gekuerzten Stand fest. Innerhalb der App gibt es keinen Grund,
    //  diesen Verlust hinzunehmen.
    //  Wann die eigene Liste WIEDER faellt, entscheidet `QClipboard::dataChanged`
    //  (s. Konstruktor): kopiert ein anderes Programm, ist sie weg.
    //  Weder `ownsClipboard()` noch ein Inhaltsvergleich taugen dafuer -
    //  Ersteres meldet unter Wayland falsch, Letzterer scheitert genau dann,
    //  wenn die Ablage einen ganz fremden (veralteten) Stand haelt.
    if (!m_ownClipFiles.isEmpty()) {
        QList<QUrl> mine;
        for (const QString& p : m_ownClipFiles) {
            if (!QFileInfo::exists(p) || QFileInfo(p).isDir()) continue;
            mine.append(QUrl::fromLocalFile(p));
        }
        if (!mine.isEmpty()) return mine;
    }
    //  Sonst gilt die Ablage des Systems (ein anderes Programm hat kopiert).
    const QMimeData* md = cb->mimeData();
    if (!md || !md->hasUrls()) return {};
    QList<QUrl> out;
    for (const QUrl& u : md->urls()) {
        const QString local = u.toLocalFile();
        if (local.isEmpty() || !QFileInfo::exists(local)) continue;
        if (QFileInfo(local).isDir()) continue;   // Ordner werden nicht kopiert
        out.append(u);
    }
    return out;
}

QString AppController::fileUrl(const QString& path) const {
    if (path.isEmpty())
        return QString();
    // Bereits eine URL? Unverändert zurückgeben - inkl. http(s), da z. B.
    // PdfMediaHandler::resolvedUri() bei verlinkten (nicht eingebetteten)
    // Medien eine externe URL statt eines lokalen Pfades liefern kann
    // (genutzt von VideoSurface für PDF-Video-/Link-Annotationen). Bewusst
    // keine generische Schema-Erkennung per Doppelpunkt, da Windows-
    // Laufwerksbuchstaben ("C:\…") sonst fälschlich als Schema gälten.
    if (path.startsWith(QStringLiteral("file:"))  ||
        path.startsWith(QStringLiteral("qrc:"))   ||
        path.startsWith(QStringLiteral("image://")) ||
        path.startsWith(QStringLiteral("http://")) ||
        path.startsWith(QStringLiteral("https://")))
        return path;
    return QUrl::fromLocalFile(path).toString();
}

QString AppController::menuFileText()           const { return Strings::get(StringKey::MenuFile); }
QString AppController::menuViewText()           const { return Strings::get(StringKey::MenuView); }
QString AppController::menuSettingsText()       const { return Strings::get(StringKey::MenuSettings); }
QString AppController::menuOpenFolderText()     const { return Strings::get(StringKey::MenuOpenFolder); }
QString AppController::menuRefreshText()        const { return Strings::get(StringKey::MenuRefresh); }
QString AppController::menuQuitText()           const { return Strings::get(StringKey::MenuQuit); }
QString AppController::menuToggleOptionsText()  const { return Strings::get(StringKey::MenuToggleOptions); }
QString AppController::menuLanguageText()       const { return Strings::get(StringKey::MenuLanguage); }
QString AppController::menuVideoPlaybackText()  const { return Strings::get(StringKey::MenuVideoPlayback); }
QString AppController::menuVideoNativeText()    const { return Strings::get(StringKey::MenuVideoNative); }
QString AppController::menuVideoExternalText()  const { return Strings::get(StringKey::MenuVideoExternal); }
QString AppController::menuBookmarksText()      const { return Strings::get(StringKey::MenuBookmarks); }
QString AppController::menuBookmarksEmptyText() const { return Strings::get(StringKey::MenuBookmarksEmpty); }
QString AppController::bookmarkAddText()        const { return Strings::get(StringKey::BookmarkAdd); }


// ── Theme-Farben ─────────────────────────────────────────────────────────────
QColor AppController::themeBackground()  const { return m_settings.currentTheme().background; }
QColor AppController::themeCard()        const { return m_settings.currentTheme().card; }
QColor AppController::themeTextPrimary() const { return m_settings.currentTheme().textPrimary; }
QColor AppController::themeTextMuted()   const { return m_settings.currentTheme().textMuted; }
QColor AppController::themeBorder()      const { return m_settings.currentTheme().border; }
QColor AppController::themeAccent()      const { return m_settings.currentTheme().accent; }
QColor AppController::themeMenuBarBg()   const { return m_settings.currentTheme().menuBarBg; }
QColor AppController::themeToolbarBg()   const { return m_settings.currentTheme().toolbarBg; }
QColor AppController::themeFilterBarBg() const { return m_settings.currentTheme().filterBarBg; }
QColor AppController::themeStatusBarBg() const { return m_settings.currentTheme().statusBarBg; }
QColor AppController::themeSidebarBg()   const { return m_settings.currentTheme().sidebarBg; }
QColor AppController::themeEditorBgText() const { return m_settings.currentTheme().editorBgText; }
QColor AppController::themeEditorBgHtml() const { return m_settings.currentTheme().editorBgHtml; }

// ── Editor / Auto-Save (Phase 4) ─────────────────────────────────────────────
bool AppController::autoSaveEnabled()  const { return m_settings.autoSaveEnabled(); }
int  AppController::autoSaveInterval() const { return m_settings.autoSaveIntervalSeconds(); }

void AppController::setAutoSaveEnabled(bool v) {
    if (m_settings.autoSaveEnabled() == v) return;
    m_settings.setAutoSaveEnabled(v);   // emittiert autoSaveSettingsChanged -> weitergeleitet
    m_settings.sync();
}

void AppController::setAutoSaveInterval(int seconds) {
    if (m_settings.autoSaveIntervalSeconds() == seconds) return;
    m_settings.setAutoSaveIntervalSeconds(seconds);
    m_settings.sync();
}

// ── Design / Theme (Phase 4) ─────────────────────────────────────────────────
int AppController::designProfile() const {
    return static_cast<int>(m_settings.designProfile());
}

void AppController::setDesignProfile(int profile) {
    const int clamped = qBound(0, profile, static_cast<int>(DesignProfile::Custom));
    const auto p = static_cast<DesignProfile>(clamped);
    if (m_settings.designProfile() == p) return;
    m_settings.setDesignProfile(p);     // emittiert themeChanged + colorSchemeChanged -> weitergeleitet
    m_settings.sync();
}

QVariantList AppController::designProfiles() const {
    struct Entry { DesignProfile p; const char* icon; const char* desc; };
    static const Entry entries[] = {
        { DesignProfile::Dark,         "\xF0\x9F\x8C\x99", "Klassisch dunkel, ruhiges Teal-Akzent" },
        { DesignProfile::DarkOLED,     "\xE2\x9A\xAB",     "Reines Schwarz mit Glow - ideal für OLED" },
        { DesignProfile::OceanDepth,   "\xF0\x9F\x8C\x8A", "Tiefes Blau mit Verlauf" },
        { DesignProfile::InfernoBlaze, "\xF0\x9F\x94\xA5", "Warmes Orange-Rot" },
        { DesignProfile::NeonPurple,   "\xE2\x9A\xA1",     "Leuchtendes Violett mit Glow" },
        { DesignProfile::MidnightRose, "\xF0\x9F\x8C\xB9", "Dunkles Rosé" },
        { DesignProfile::Elegant,      "\xE2\x9C\xA8",     "Sanftes Lavendel, elegant" },
        { DesignProfile::Simple,       "\xE2\x98\x80",     "Neutrales Graustufen-Theme" },
        { DesignProfile::Custom,       "\xF0\x9F\x8E\xA8", "Eigene Farben (unten anpassbar)" },
    };

    QVariantList out;
    for (const Entry& e : entries) {
        const ThemeColors th = (e.p == DesignProfile::Custom)
                                   ? m_settings.customTheme()
                                   : AppSettings::themeForProfile(e.p);
        QVariantMap m;
        m.insert("index",       static_cast<int>(e.p));
        m.insert("name",        th.name);
        m.insert("icon",        QString::fromUtf8(e.icon));
        m.insert("description", QString::fromUtf8(e.desc));
        m.insert("accent",      th.accent);
        m.insert("card",        th.card);
        m.insert("background",  th.background);
        out.append(m);
    }
    return out;
}

QVariantMap AppController::customThemeMap() const {
    const ThemeColors t = m_settings.customTheme();
    QVariantMap m;
    m.insert("name",            t.name);
    m.insert("background",      t.background);
    m.insert("card",            t.card);
    m.insert("textPrimary",     t.textPrimary);
    m.insert("textMuted",       t.textMuted);
    m.insert("border",          t.border);
    m.insert("accentType",      static_cast<int>(t.accentType));
    m.insert("accent",          t.accent);
    m.insert("accentGradEnd",   t.accentGradEnd);
    m.insert("glowRadius",      static_cast<double>(t.glowRadius));
    m.insert("glowIntensity",   static_cast<double>(t.glowIntensity));
    m.insert("bgIsGradient",    t.bgIsGradient);
    m.insert("bgGradStart",     t.bgGradStart);
    m.insert("bgGradEnd",       t.bgGradEnd);
    m.insert("bgGradAngle",     t.bgGradAngle);
    m.insert("tileBgType",      static_cast<int>(t.tileBgType));
    m.insert("tileBgColor",     t.tileBgColor);
    m.insert("tileBgGradEnd",   t.tileBgGradEnd);
    m.insert("tileBgGradAngle", t.tileBgGradAngle);
    m.insert("tileGlowOnHover", t.tileGlowOnHover);
    m.insert("tileGlowRadius",  static_cast<double>(t.tileGlowRadius));
    m.insert("pdfViewerBg",     t.pdfViewerBg);
    m.insert("pdfThumbBg",      t.pdfThumbBg);
    m.insert("pdfSidebarBg",    t.pdfSidebarBg);
    m.insert("pdfToolbarBg",    t.pdfToolbarBg);
    m.insert("pdfScrollbarBg",  t.pdfScrollbarBg);
    m.insert("sidebarBg",       t.sidebarBg);
    m.insert("editorBgText",    t.editorBgText);
    m.insert("editorBgHtml",    t.editorBgHtml);
    m.insert("menuBarBg",       t.menuBarBg);
    m.insert("toolbarBg",       t.toolbarBg);
    m.insert("filterBarBg",     t.filterBarBg);
    m.insert("statusBarBg",     t.statusBarBg);
    return m;
}

void AppController::setCustomThemeFromMap(const QVariantMap& m) {
    // Auf bestehendem Custom-Theme aufsetzen, damit fehlende Schlüssel erhalten bleiben.
    ThemeColors t = m_settings.customTheme();

    auto col = [&](const char* key, QColor fallback) -> QColor {
        const auto it = m.constFind(QLatin1String(key));
        if (it == m.constEnd()) return fallback;
        const QColor c = it->value<QColor>();
        return c.isValid() ? c : fallback;
    };
    auto ival = [&](const char* key, int fallback) -> int {
        const auto it = m.constFind(QLatin1String(key));
        return it == m.constEnd() ? fallback : it->toInt();
    };
    auto fval = [&](const char* key, float fallback) -> float {
        const auto it = m.constFind(QLatin1String(key));
        return it == m.constEnd() ? fallback : static_cast<float>(it->toDouble());
    };
    auto bval = [&](const char* key, bool fallback) -> bool {
        const auto it = m.constFind(QLatin1String(key));
        return it == m.constEnd() ? fallback : it->toBool();
    };

    if (m.contains("name")) t.name = m.value("name").toString();
    t.background      = col("background",      t.background);
    t.card            = col("card",            t.card);
    t.textPrimary     = col("textPrimary",     t.textPrimary);
    t.textMuted       = col("textMuted",       t.textMuted);
    t.border          = col("border",          t.border);
    t.accentType      = static_cast<AccentType>(qBound(0, ival("accentType", static_cast<int>(t.accentType)),
                                                       static_cast<int>(AccentType::Glow)));
    t.accent          = col("accent",          t.accent);
    t.accentGradEnd   = col("accentGradEnd",   t.accentGradEnd);
    t.glowRadius      = fval("glowRadius",     t.glowRadius);
    t.glowIntensity   = fval("glowIntensity",  t.glowIntensity);
    t.bgIsGradient    = bval("bgIsGradient",   t.bgIsGradient);
    t.bgGradStart     = col("bgGradStart",     t.bgGradStart);
    t.bgGradEnd       = col("bgGradEnd",       t.bgGradEnd);
    t.bgGradAngle     = ival("bgGradAngle",    t.bgGradAngle);
    t.tileBgType      = static_cast<TileBgType>(qBound(0, ival("tileBgType", static_cast<int>(t.tileBgType)),
                                                       static_cast<int>(TileBgType::Transparent)));
    t.tileBgColor     = col("tileBgColor",     t.tileBgColor);
    t.tileBgGradEnd   = col("tileBgGradEnd",   t.tileBgGradEnd);
    t.tileBgGradAngle = ival("tileBgGradAngle", t.tileBgGradAngle);
    t.tileGlowOnHover = bval("tileGlowOnHover", t.tileGlowOnHover);
    t.tileGlowRadius  = fval("tileGlowRadius",  t.tileGlowRadius);
    t.pdfViewerBg     = col("pdfViewerBg",     t.pdfViewerBg);
    t.pdfThumbBg      = col("pdfThumbBg",      t.pdfThumbBg);
    t.pdfSidebarBg    = col("pdfSidebarBg",    t.pdfSidebarBg);
    t.pdfToolbarBg    = col("pdfToolbarBg",    t.pdfToolbarBg);
    t.pdfScrollbarBg  = col("pdfScrollbarBg",  t.pdfScrollbarBg);
    t.sidebarBg       = col("sidebarBg",       t.sidebarBg);
    // Rückwärtskompatibel: ein alter „editorBg"-Wert seedet beide neuen Farben.
    const QColor legacyEditorBg = col("editorBg", t.editorBgText);
    t.editorBgText    = col("editorBgText",    legacyEditorBg);
    t.editorBgHtml    = col("editorBgHtml",    legacyEditorBg);
    t.menuBarBg       = col("menuBarBg",       t.menuBarBg);
    t.toolbarBg       = col("toolbarBg",       t.toolbarBg);
    t.filterBarBg     = col("filterBarBg",     t.filterBarBg);
    t.statusBarBg     = col("statusBarBg",     t.statusBarBg);

    m_settings.setCustomTheme(t);   // emittiert themeChanged + colorSchemeChanged -> Live-Vorschau
    m_settings.sync();
}

bool AppController::exportCustomTheme(const QUrl& fileUrl) {
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) return false;
    return m_settings.exportCustomTheme(path);
}

bool AppController::importCustomTheme(const QUrl& fileUrl) {
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) return false;
    const bool ok = m_settings.importCustomTheme(path);  // setzt customTheme + Profil=Custom
    if (ok) m_settings.sync();
    return ok;
}

QFont AppController::fallbackFont(const QString& family, qreal pixelSize,
                                 bool bold, bool italic, bool underline) const {
    QFont f;
    // Führende Familie (Latein/Wahl) + Naskh-/CJK-Rückfall je Glyphe. Nicht
    // installierte Familien überspringt Qt stillschweigend. Naskh VOR CJK, damit
    // arabischer Text nicht als System-Nastaliq landet.
    f.setFamilies({
        family,
        QStringLiteral("Amiri"),
        QStringLiteral("Noto Naskh Arabic"),
        QStringLiteral("Noto Sans Arabic"),
        QStringLiteral("Noto Sans CJK JP"),
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("Noto Sans CJK KR")
    });
    if (pixelSize >= 1.0)
        f.setPixelSize(qRound(pixelSize));
    f.setBold(bold);
    f.setItalic(italic);
    f.setUnderline(underline);
    return f;
}
