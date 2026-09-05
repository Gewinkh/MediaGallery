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
    // Wachhund über die Zwischenablage: kopiert ein anderes Programm, verliert unsere gemerkte Dateiliste ihre
    // Gültigkeit. Die eigene Änderung wird über `m_clipSelfSet` durchgelassen - sie kommt als dasselbe Signal zurück.
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        connect(cb, &QClipboard::dataChanged, this, [this] {
            if (m_clipSelfSet) { m_clipSelfSet = false; return; }
            m_ownClipFiles.clear();
        });
    }

    AppSettings& as = AppSettings::instance();
    connect(&as, &AppSettings::colorSchemeChanged, this, &AppController::backgroundColorChanged);
    connect(&as, &AppSettings::colorSchemeChanged, this, &AppController::accentColorChanged);
    connect(&as, &AppSettings::colorSchemeChanged, this, &AppController::themeChanged);
    connect(&as, &AppSettings::themeChanged,       this, &AppController::themeChanged);
    connect(&as, &AppSettings::languageChanged,    this, &AppController::languageChanged);
    connect(&as, &AppSettings::tileSizeChanged,        this, &AppController::tileSizeChanged);
    connect(&as, &AppSettings::tileArrangementChanged, this, &AppController::tileArrangementChanged);
    connect(&as, &AppSettings::autoSaveSettingsChanged, this, &AppController::autoSaveChanged);

    m_panesModel = new PaneListModel(m_panes, this);
}

// Diese Fassade hat keinen eigenen Ordner; sie zeigt auf die Hälfte, in der gerade gearbeitet wird. Beim
// Wechsel werden die Signale umgehängt - sonst zeigte das Menü den Ordner der anderen Hälfte.
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
    // Solange die Einstellungen eine Hälfte FEST gewählt haben, bleibt die Fassade dort - sonst wechselte sie
    // unter dem offenen Dialog weg.
    if (m_settingsPane < 0 && m_tagsFacade && m_pane)
        m_tagsFacade->setTagManager(m_pane->tagManager());
    emit folderChanged();
    emit folderHistoryChanged();
    emit tagsChanged();
    emit categoriesChanged();
    emit optionsVisibleChanged();       // der Modus gehört der Hälfte
}

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
    pane->folderService().setPersistsLastFolder(m_panes.empty());
    m_panesModel->beginInsert(int(m_panes.size()));
    m_panes.push_back(pane);
    m_panesModel->endInsert();
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
    if (!m_panes.empty())
        m_panes.front()->folderService().setPersistsLastFolder(true);

    // Der Player-Merker hängt am PLATZ der Hälfte (Bit je Platz). Fällt eine weg, muss ihr Bit verschwinden und
    // die dahinter aufrücken - sonst entsteht die geschlossene Hälfte beim nächsten Start wieder aus dem Merker.
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
    persistPaneFolders();
    emit panesChanged();
    pane->deleteLater();
    return true;
}

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
    m_panesModel->beginMove(1, 0);
    std::swap(m_panes[0], m_panes[1]);
    m_panesModel->endMove();
    emit panesChanged();
    return true;
}

void AppController::setSettingsPaneIndex(int index) {
    const int v = (index >= 0 && index < int(m_panes.size())) ? index : -1;
    if (v == m_settingsPane) return;
    m_settingsPane = v;
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

QString AppController::createEmptyFile(const QString& kind, const QString& baseName,
                                       const QString& targetFolder) {
    const QString open = currentFolder();
    if (open.isEmpty()) {
        emit statusMessage(Strings::get(StringKey::CreateFileFailed));
        return {};
    }
    QString folder = open;
    if (!targetFolder.isEmpty() && targetFolder != open) {
        if (!targetFolder.startsWith(open + QLatin1Char('/'))
            || !QFileInfo(targetFolder).isDir()) {
            emit statusMessage(Strings::get(StringKey::CreateFileFailed));
            return {};
        }
        folder = targetFolder;
    }

    const bool freeName = (kind == QLatin1String("free"));

    QString ext;
    if      (freeName)                      ext.clear();   // steckt im Namen
    else if (kind == QLatin1String("pdf"))  ext = QStringLiteral("pdf");
    else if (kind == QLatin1String("html")) ext = QStringLiteral("html");
    else if (kind == QLatin1String("txt"))  ext = QStringLiteral("txt");
    else if (kind == QLatin1String("docx")) ext = QStringLiteral("docx");
    else {
        emit statusMessage(Strings::get(StringKey::CreateFileFailed));
        return {};
    }

    QString base = baseName.trimmed();
    base.remove(QLatin1Char('/'));
    base.remove(QLatin1Char('\\'));
    while (base.startsWith(QLatin1Char('.')))
        base.remove(0, 1);
    if (freeName) {
        // Punkte und Leerzeichen am ENDE weg: "notiz." legt unter Windows eine Datei an, die dort niemand mehr öffnen
        // kann, und `..` wäre gar kein Dateiname.
        while (base.endsWith(QLatin1Char('.')) || base.endsWith(QLatin1Char(' ')))
            base.chop(1);
    }
    if (base.isEmpty())
        base = Strings::get(StringKey::CreateFileTitle);

    if (freeName) {
        const int dot = base.lastIndexOf(QLatin1Char('.'));
        if (dot > 0) {
            ext  = base.mid(dot + 1);
            base = base.left(dot);
        }
        if (base.isEmpty())
            base = Strings::get(StringKey::CreateFileTitle);
    }

    const QString dotExt = ext.isEmpty() ? QString()
                                         : QLatin1Char('.') + ext;
    QString path = folder + QLatin1Char('/') + base + dotExt;
    int n = 2;
    while (QFileInfo::exists(path)) {
        path = folder + QLatin1Char('/') + base
               + QStringLiteral(" (%1)").arg(n) + dotExt;
        ++n;
    }

    bool ok = false;
    QSaveFile out(path);
    if (out.open(QIODevice::WriteOnly)) {
        if (ext == QLatin1String("pdf")) {
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
                bytes = Docx::Document::emptyDocxBytes(base);
            }
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
    if (m_pane) m_pane->notifyContentsChanged(folder);
    // Eine Endung, die die Galerie nicht kennt, ist ohne "Alle Dateien anzeigen" unsichtbar - die Datei liegt im
    // Ordner, die Kachel fehlt. Gesagt, nicht heimlich behoben: die Einstellung gehört dem Nutzer.
    const bool invisible = MediaItem::detectType(path) == MediaType::Unknown
                           && !m_settings.showAllFiles();
    emit statusMessage(Strings::get(invisible ? StringKey::CreateFileHiddenHint
                                              : StringKey::CreateFileDone,
                                    QFileInfo(path).fileName()));
    return path;
}

void AppController::handleDroppedUrls(const QList<QUrl>& urls,
                                      const QString& targetFolder) {
    for (const QUrl& url : urls) {
        const QString path = url.toLocalFile();
        if (path.isEmpty()) continue;
        if (QFileInfo(path).isDir()) {
            if (m_pane) m_pane->openFolder(path);   // leert den Rückweg selbst
            return;
        }
    }

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
        // NUR für den offenen Ordner: `m_storage` IST dessen Sidecar - es auf einen Unterordner umzuschalten hängte
        // Modell, Tag-Panel und Filter an die falsche Datei. Der Unterordner liest beim Reload selbst neu.
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

// Mausrad waehrend eines Zuges: unter Wayland gehoert der Zeiger dem Compositor
// (gemessen 892 DragMove, 0 Wheel). Der Filter bleibt fuer X11/Windows und wird
// ueber einen 0-ms-Timer gesetzt, damit er VOR Qts eigenem liegt.
bool AppController::eventFilter(QObject* watched, QEvent* event) {
    if (m_tileDragActive) {
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
    // ALLE Hälften neu einlesen, nicht nur die fokussierte: umgeschaltet wird meist aus den EINSTELLUNGEN heraus,
    // und dann zeigt `m_pane` nicht auf die Galerie, die man vor sich hat - `refreshCurrentFolder` lief ins Leere.
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
    keys.sort();
    m_settings.setCollapsedSettingsGroups(keys);
    m_settings.sync();
}

bool AppController::textPreviewContent() const {
    return m_settings.textPreviewContent();
}

void AppController::setTextPreviewContent(bool v) {
    if (m_settings.textPreviewContent() == v) return;
    m_settings.setTextPreviewContent(v);
    m_settings.sync();
    emit textPreviewContentChanged();
}

void AppController::setGalleryListLayout(bool v) {
    if (m_settings.galleryListLayout() == v) return;
    m_settings.setGalleryListLayout(v);
    m_settings.sync();
    emit galleryListLayoutChanged();
}

void AppController::setFileDropMove(bool v) {
    if (m_settings.fileDropMove() == v) return;
    m_settings.setFileDropMove(v);
    emit fileDropMoveChanged();
}

// Ein Eintrag ist eine Zeichenkette "Name\tPfad\tGruppenpfad"; Gruppen stehen
// getrennt als "Gruppenpfad" bzw. "Gruppenpfad\t1" (eingeklappt). Die Identitaet
// einer Gruppe ist ihr voller Pfad mit "/" - deshalb darf ein Name kein "/" tragen.
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


bool usableGroupLeaf(const QString& name) {
    const QString t = name.trimmed();
    return !t.isEmpty() && !t.contains(kGroupSep) && !t.contains(QLatin1Char('\t'));
}

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

bool isSelfOrBelow(const QString& path, const QString& ancestor) {
    if (ancestor.isEmpty()) return true;                 // alles liegt unter der Wurzel
    if (path.compare(ancestor, Qt::CaseInsensitive) == 0) return true;
    const QString prefix = ancestor + kGroupSep;
    return path.startsWith(prefix, Qt::CaseInsensitive);
}

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
        if (groupPos(out, g.path) < 0) out.append(g);
    }
    // FEHLENDE VORFAHREN ergänzen: steht "A/B" ohne "A" in der Liste, hinge der Ast an einem Elternteil, den die
    // Tiefensuche nie besucht. Angelegt wird DIREKT VOR dem Kind, damit die Reihenfolge der Geschwister bleibt.
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

// Flache Zeilenliste in Anzeigereihenfolge; je Zeile kind/name/group/parent/depth/
// hidden, dazu path+index (Eintrag) bzw. collapsed+count (Gruppe) und pos.
// Eigener Stapel statt Rekursion: die Schachtelungstiefe kommt aus einer Datei.
QVariantList AppController::bookmarkTree() const {
    const QStringList raw = m_settings.savedFolders();
    const QList<BmGroup> groups = parseGroups(m_settings.bookmarkGroups());

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

    struct Frame { int group; int depth; bool hidden; int pos; };
    QList<Frame> stack;

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

        const bool inner = f.hidden || g.collapsed;
        emitBookmarks(g.path, f.depth + 1, inner);
        const QList<int> kids = childGroups(g.path);
        for (int k = kids.size() - 1; k >= 0; --k)
            stack.append(Frame{ kids.at(k), f.depth + 1, inner, k });
    }

    return out;
}

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

void AppController::addBookmark(const QString& name, const QString& path,
                                const QString& group) {
    if (path.trimmed().isEmpty()) return;
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

void AppController::addBookmarkGroup(const QString& name, const QString& parentPath) {
    if (!usableGroupLeaf(name)) return;
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
    if (to == from) return;
    const int clash = groupPos(groups, to);
    if (clash >= 0 && clash != pos) return;           // Name in dieser Ebene vergeben

    for (BmGroup& g : groups)
        if (isSelfOrBelow(g.path, from))
            g.path = reparent(g.path, from, to);
    m_settings.setBookmarkGroups(packGroups(groups));

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

    for (int i = groups.size() - 1; i >= 0; --i)
        if (isSelfOrBelow(groups.at(i).path, n))
            groups.removeAt(i);
    m_settings.setBookmarkGroups(packGroups(groups));

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

    // Platz unter den GESCHWISTERN: nur der Eintrag der Gruppe selbst zieht um, ihre Untergruppen behalten ihre
    // Plätze - deren Reihenfolge zählt nur untereinander, und die Tiefensuche liest den Vater aus dem Pfad.
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

// Vergleich gegen den bereits GEKLEMMTEN gespeicherten Wert: mit dem rohen Argument würde ein Wert außerhalb
// des Bereichs jedes Mal neu geschrieben und löste ein Signal aus.
void AppController::setVideoSeekStep(int seconds) {
    const int before = m_settings.videoSeekStep();
    m_settings.setVideoSeekStep(seconds);
    if (m_settings.videoSeekStep() == before) return;
    m_settings.sync();
    emit videoSeekStepChanged();
}

// Mono-Play: zustandslose Vermittlung als Broadcast an ALLE Stellen (die startende erkennt ihr eigenes Token).
// Bewusst keine Registry - die Stellen leben in QML und kommen und gehen; ein Broadcast ist lebensdauer-sicher.
void AppController::announcePlayback(const QString& token) {
    if (!m_settings.monoPlay())
        return;                       // Option aus -> parallele Wiedergaben erlaubt
    emit playbackStarted(token);
}

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

// Tags
//  Tags gehören dem Ordner - also der fokussierten Hälfte.
QStringList AppController::allTags() const {
    return m_pane ? m_pane->tagManager().allTags() : QStringList();
}
QColor AppController::tagColor(const QString& tag) const {
    return m_pane ? m_pane->tagManager().tagColor(tag) : QColor();
}
// ENTFALLEN: `tagsForFile` & Co. nahmen den blanken DATEINAMEN und trafen damit immer das Sidecar des
// GEÖFFNETEN Ordners. Die Wege liegen jetzt im Modell, das den Ordner einer Datei kennt.

// Nur noch die GLOBALE Vorgabe: die Ausnahme je Datei liegt im Sidecar des Ordners, dem die Datei gehört -
// und den kennt das Modell, nicht diese Fassade.
QColor AppController::textPdfColor() const { return m_settings.textPdfColor(); }

void AppController::setTextPdfColor(const QColor& c) {
    if (m_settings.textPdfColor() == c) return;
    m_settings.setTextPdfColor(c);
    emit textPdfColorChanged();
}

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
    // Haben wir zuletzt kopiert, gilt unsere eigene Liste: Klipper gab von 29 abgelegten
    // Adressen 3 aus einem frueheren Lauf zurueck. ownsClipboard() meldet unter Wayland
    // falsch, ein Inhaltsvergleich scheitert genau bei veraltetem Stand.
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
    // Bereits eine URL? Unverändert zurückgeben, inkl. http(s) - `PdfMediaHandler::resolvedUri()` liefert bei
    // verlinkten Medien eine externe URL. Keine Schema-Erkennung per Doppelpunkt, sonst gälte "C:\..." als Schema.
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
