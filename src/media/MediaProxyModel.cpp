#include "media/MediaProxyModel.h"
#include "media/MediaModel.h"
#include "media/MediaItem.h"
#include "tags/TagManager.h"
#include "tags/TagCategory.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <functional>

MediaProxyModel::MediaProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    //  IMMER aufsteigend sortieren lassen. Die Richtung dreht `fieldLess`
    //  selbst: `Qt::DescendingOrder` kehrt JEDEN Vergleich um - damit stuenden
    //  Ordner hinten und, schlimmer, der Inhalt eines aufgeklappten Ordners VOR
    //  seiner Kachel. Die Baumordnung darf die Richtung nicht sehen.
    sort(0, Qt::AscendingOrder);

    // count-Property reaktiv halten.
    connect(this, &QAbstractItemModel::rowsInserted,   this, &MediaProxyModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved,    this, &MediaProxyModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset,     this, &MediaProxyModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged,  this, &MediaProxyModel::countChanged);
}

void MediaProxyModel::setSourceModel(QAbstractItemModel* source) {
    m_src = qobject_cast<MediaModel*>(source);
    QSortFilterProxyModel::setSourceModel(source);
}

void MediaProxyModel::setTagManager(TagManager* mgr) {
    if (m_tagMgr == mgr) return;
    if (m_tagMgr) m_tagMgr->disconnect(this);
    m_tagMgr = mgr;
    if (m_tagMgr) {
        connect(m_tagMgr, &TagManager::categoriesChanged, this, [this] {
            recomputeFilterCaches();
            refilterRows();
            emit filterChanged();
        });
        connect(m_tagMgr, &TagManager::tagsChanged, this, [this] {
            recomputeFilterCaches();
            refilterRows();
        });
    }
    recomputeFilterCaches();
    refilterRows();
}

void MediaProxyModel::reapplySort() {
    //  Die Sortierordnung des Proxys bleibt aufsteigend (s. Konstruktor); ein
    //  Wechsel des Feldes oder der Richtung erzwingt den neuen Lauf ueber
    //  invalidate() des Aufrufers.
    sort(0, Qt::AscendingOrder);
}

void MediaProxyModel::setSortFieldInt(int f) {
    const Field nf = static_cast<Field>(f);
    if (nf == m_field) return;
    m_field = nf;
    // Nur neu sortieren - der Zeilenfilter bleibt unberührt (kein Full-Reset).
    invalidate();
    reapplySort();
    emit sortChanged();
}

void MediaProxyModel::setSortDescending(bool d) {
    if (d == m_descending) return;
    m_descending = d;
    //  Die Sortierordnung des Proxys aendert sich NICHT mehr (sie bleibt
    //  aufsteigend), deshalb genuegt `reapplySort()` allein nicht - ohne
    //  invalidate() bemerkte der Proxy gar keinen Anlass, neu zu ordnen.
    invalidate();
    reapplySort();
    emit sortChanged();
}

// Typ-Umschalter berühren nur den Zeilenfilter -> refilterRows() statt
// invalidate() (kein Re-Sort, keine Spalten-Neubewertung).
#define PROXY_BOOL_SETTER(Setter, Member)            \
    void MediaProxyModel::Setter(bool v) {           \
        if (v == Member) return;                     \
        Member = v;                                  \
        refilterRows();                      \
        emit filterChanged();                        \
    }
PROXY_BOOL_SETTER(setShowImages, m_showImages)
PROXY_BOOL_SETTER(setShowVideos, m_showVideos)
PROXY_BOOL_SETTER(setShowAudio,  m_showAudio)
PROXY_BOOL_SETTER(setShowPdfs,   m_showPdfs)
PROXY_BOOL_SETTER(setShowTexts,  m_showTexts)
PROXY_BOOL_SETTER(setShowFolders, m_showFolders)
#undef PROXY_BOOL_SETTER

void MediaProxyModel::setTagFilter(const QStringList& t) {
    if (t == m_tagFilter) return;
    m_tagFilter = t;
    recomputeFilterCaches();
    refilterRows();
    emit filterChanged();
}

void MediaProxyModel::setSearchText(const QString& t) {
    //  Getrimmt gespeichert: ein versehentliches Leerzeichen darf die Galerie
    //  nicht leeren, und „a " soll dasselbe finden wie „a".
    const QString trimmed = t.trimmed();
    if (trimmed == m_search) return;
    m_search = trimmed;
    refilterRows();
    emit filterChanged();
}

void MediaProxyModel::setTagFilterModeInt(int m) {
    const TagMode nm = static_cast<TagMode>(m);
    if (nm == m_mode) return;
    m_mode = nm;
    refilterRows();
    emit filterChanged();
}

void MediaProxyModel::setCategoryFilter(const QStringList& ids) {
    if (ids == m_categoryFilter) return;
    m_categoryFilter = ids;
    m_activeCatIds = QSet<QString>(ids.begin(), ids.end());
    recomputeFilterCaches();
    refilterRows();
    emit filterChanged();
}

void MediaProxyModel::setTagFilterAnd(bool v) {
    const TagMode nm = v ? TagMode::And : TagMode::Or;
    if (nm == m_mode) return;
    m_mode = nm;
    refilterRows();
    emit filterChanged();
}

// ── Filter-Caches einmalig pro Änderung vorberechnen ─────────────────────────
//  m_effectiveTags  : manuelle Tags ∪ Tags aller aktiven Kategorien (rekursiv)
//  m_activeCatFiles : Dateinamen, die DIREKT einer aktiven Kategorie angehören
//                     (entspricht dem alten categoriesForFile ∩ activeCatIds,
//                      jedoch ohne Pro-Zeile-Baumdurchlauf).
void MediaProxyModel::recomputeFilterCaches() {
    // Effektive Tags
    m_effectiveTags = QSet<QString>(m_tagFilter.begin(), m_tagFilter.end());
    if (m_tagMgr) {
        for (const QString& id : std::as_const(m_categoryFilter))
            collectTagsForCategory(id, m_effectiveTags);
    }

    // Direkt-Dateien aktiver Kategorien
    m_activeCatFiles.clear();
    if (m_tagMgr) {
        for (const QString& id : std::as_const(m_categoryFilter)) {
            const TagCategory* cat = m_tagMgr->categoryById(id);
            if (!cat) continue;
            for (const QString& f : std::as_const(cat->files))
                m_activeCatFiles.insert(f);
        }
    }

    //  Dieselbe Menge fuer die aufgeklappten Unterordner. Sie fuehren ihre
    //  Kategorien im eigenen Sidecar; verbunden werden sie ueber den NAMEN,
    //  weil die IDs eines anderen Ordners andere sind.
    m_catFilesByScope.clear();
    m_catFilesByScope.insert(0, m_activeCatFiles);
    if (m_src && !m_categoryFilter.isEmpty())
        m_src->fillCategoryFilesByScope(activeCategoryNames(), m_catFilesByScope);
}

void MediaProxyModel::collectTagsForCategory(const QString& id, QSet<QString>& out) const {
    if (!m_tagMgr) return;
    const TagCategory* cat = m_tagMgr->categoryById(id);
    if (!cat) return;
    for (const QString& t : cat->tags) out.insert(t);
    // Rekursiv über Unterkategorien (collectTagsForId-Äquivalent).
    std::function<void(const QList<TagCategory>&)> rec =
        [&](const QList<TagCategory>& children) {
            for (const TagCategory& ch : children) {
                for (const QString& t : ch.tags) out.insert(t);
                rec(ch.children);
            }
        };
    rec(cat->children);
}

// ── Das Filterurteil, zustandslos (s. Header) ───────────────────────────────
bool MediaProxyModel::acceptsFile(int mediaType, const QString& displayName,
                                  const QString& fileName, const QStringList& tags,
                                  const FilterCriteria& c) {
    switch (static_cast<MediaType>(mediaType)) {
    case MediaType::Image: if (!c.showImages) return false; break;
    case MediaType::Video: if (!c.showVideos) return false; break;
    case MediaType::Audio: if (!c.showAudio)  return false; break;
    case MediaType::Pdf:   if (!c.showPdfs)   return false; break;
    case MediaType::Text:  if (!c.showTexts)  return false; break;
    case MediaType::Docx:  if (!c.showTexts)  return false; break;   // Dokumente
    case MediaType::Unknown: break;   // nur bei „Alle Dateien anzeigen" im Modell
    default:               return false;   // Ordner haben eigene Regeln
    }

    //  Freitextsuche - UND-verknüpft, deshalb VOR jedem frühen `return true`.
    if (!c.search.isEmpty()) {
        bool hit = displayName.contains(c.search, Qt::CaseInsensitive)
                || fileName.contains(c.search, Qt::CaseInsensitive);
        if (!hit)
            for (const QString& t : tags)
                if (t.contains(c.search, Qt::CaseInsensitive)) { hit = true; break; }
        if (!hit) return false;
    }

    const bool hasTagFilter = !c.tags.isEmpty();
    if (!hasTagFilter && !c.categoryActive)
        return true;

    //  Direkte Datei↔Kategorie-Mitgliedschaft lässt immer passieren.
    if (c.categoryActive && c.catFiles.contains(fileName))
        return true;
    if (!hasTagFilter)
        return false;

    switch (static_cast<TagMode>(c.mode)) {
    case TagMode::Or:
    case TagMode::Inklusiv:
        for (const QString& t : tags)
            if (c.tags.contains(t)) return true;
        return false;

    case TagMode::And:
        if (tags.isEmpty()) return false;
        for (const QString& t : c.tags)
            if (!tags.contains(t)) return false;
        return true;

    case TagMode::Nur: {
        if (tags.isEmpty()) return false;
        bool hasOne = false;
        for (const QString& t : tags)
            if (c.tags.contains(t)) { hasOne = true; break; }
        if (!hasOne) return false;
        for (const QString& t : tags)
            if (!c.tags.contains(t)) return false;
        return true;
    }
    }
    return false;
}

MediaProxyModel::FilterCriteria MediaProxyModel::criteria() const {
    FilterCriteria c;
    c.search         = m_search;
    c.tags           = m_effectiveTags;
    c.mode           = static_cast<int>(m_mode);
    c.categoryActive = !m_activeCatIds.isEmpty();
    c.catFiles       = m_activeCatFiles;
    c.showImages     = m_showImages;
    c.showVideos     = m_showVideos;
    c.showAudio      = m_showAudio;
    c.showPdfs       = m_showPdfs;
    c.showTexts      = m_showTexts;
    c.showFolders    = m_showFolders;
    return c;
}

QStringList MediaProxyModel::activeCategoryNames() const {
    QStringList out;
    if (!m_tagMgr) return out;
    for (const QString& id : m_categoryFilter) {
        const TagCategory* cat = m_tagMgr->categoryById(id);
        if (cat && !out.contains(cat->name)) out.append(cat->name);
    }
    return out;
}

bool MediaProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    // Schneller Pfad: direkt auf das MediaItem-Struct (kein QVariant je Zeile).
    // Nur fuer Zeilen der Wurzelebene des bekannten Quellmodells; sonst (fremdes
    // Modell, hypothetische Baumzeilen) bleibt der QVariant-Weg.
    const MediaItem* item = (m_src && !sourceParent.isValid())
                                ? m_src->itemAt(sourceRow) : nullptr;

    QModelIndex idx;
    if (!item) {
        idx = sourceModel()->index(sourceRow, 0, sourceParent);
        if (!idx.isValid()) return false;
    }

    //  „Ordner" ausgeschaltet blendet nicht nur die Kacheln aus, sondern auch
    //  den Inhalt aufgeklappter Unterordner: sonst staende dieser Inhalt ohne
    //  den Kopf da, zu dem er gehoert.
    if (!m_showFolders) {
        const bool nested = item ? (item->scope != 0)
                                 : (idx.data(MediaModel::DepthRole).toInt() > 0);
        if (nested) return false;
    }

    const auto type = item ? item->type
                           : static_cast<MediaType>(idx.data(MediaModel::MediaTypeRole).toInt());

    // ── Ordnerkacheln: eigene Regeln fuer Suche und Tag-/Kategorie-Filter ────
    //  Ein Ordner traegt keine Tags. Waere er trotzdem an jeden Filter
    //  gebunden, verschwaende der Weg nach unten, sobald man filtert - und ein
    //  AUFGEKLAPPTER Ordner haette ploetzlich Inhalt ohne Kachel darueber.
    if (type == MediaType::Folder) {
        if (!m_showFolders) return false;

        const bool filtering = !m_search.isEmpty() || !m_effectiveTags.isEmpty()
                            || !m_activeCatIds.isEmpty();
        if (!filtering) return true;          // ohne Filter steht jeder Ordner da

        //  Der eigene NAME zaehlt immer - so findet man einen Ordner auch dann,
        //  wenn nichts darin passt.
        const QString name = item ? item->displayName
                                  : idx.data(MediaModel::DisplayNameRole).toString();
        if (!m_search.isEmpty() && name.contains(m_search, Qt::CaseInsensitive))
            return true;

        const QString path = item ? item->filePath
                                  : idx.data(MediaModel::FilePathRole).toString();
        //  Mit Tiefensuche steht nur der WEG zu einem Treffer im Ergebnis. Ein
        //  von Hand geoeffneter Ordner ohne Treffer bleibt zwar aufgeklappt,
        //  gehoert aber nicht dazu - sonst haenge er leer im Suchergebnis
        //  (vom Nutzer gemeldet).
        if (m_src && m_src->deepFilterActive())
            return m_src->isOnDeepChain(path);

        //  Ohne Tiefensuche (kein Verdrahten, Testtreiber): ein aufgeklappter
        //  Ordner bleibt sichtbar, sonst verschwaende der Weg zu seinem
        //  gefilterten Inhalt.
        return m_src && m_src->isFolderExpanded(path);
    }

    // ── Dateien: das gemeinsame Urteil (dieselbe Funktion wie die Tiefensuche)
    FilterCriteria c = criteria();
    //  Die Kategorien eines aufgeklappten Unterordners liegen in SEINEM
    //  Sidecar - die Dateinamen des offenen Ordners gelten dort nicht.
    const int scope = item ? item->scope : 0;
    if (c.categoryActive)
        c.catFiles = m_catFilesByScope.value(scope, scope == 0 ? m_activeCatFiles
                                                               : QSet<QString>());

    const QString displayName = item ? item->displayName
                                     : idx.data(MediaModel::DisplayNameRole).toString();
    const QString fileName    = item ? item->fileName()
                                     : idx.data(MediaModel::FileNameRole).toString();
    const QStringList owned   = item ? QStringList()
                                     : idx.data(MediaModel::TagsRole).toStringList();
    const QStringList& tags   = item ? item->tags : owned;

    return acceptsFile(static_cast<int>(type), displayName, fileName, tags, c);
}

// ── Ordnung ──────────────────────────────────────────────────────────────────
//  Vergleich nach dem gewaehlten Sortierfeld, IMMER aufsteigend. Gleichstand
//  bricht ueber Datum und Anzeigename - deterministisch, damit dieselbe Liste
//  nicht bei jedem Lauf anders steht.
bool MediaProxyModel::fieldLess(const MediaItem* a, const MediaItem* b) const {
    switch (m_field) {
    case Field::Name: {
        const int cmp = a->displayName.compare(b->displayName, Qt::CaseInsensitive);
        if (cmp != 0) return cmp < 0;
        break;
    }
    case Field::FileSize:
        if (a->fileSize != b->fileSize) return a->fileSize < b->fileSize;
        break;
    case Field::Tags: {
        const int cmp = a->tags.join(QChar(',')).compare(
                            b->tags.join(QChar(',')), Qt::CaseInsensitive);
        if (cmp != 0) return cmp < 0;
        break;
    }
    case Field::Date:
    default:
        break;
    }
    if (a->dateTime != b->dateTime) return a->dateTime < b->dateTime;
    return a->displayName.compare(b->displayName, Qt::CaseInsensitive) < 0;
}

//  Zwei Zeilen DESSELBEN Ordners.
bool MediaProxyModel::sameScopeLess(const MediaItem* a, const MediaItem* b) const {
    //  Ordner stehen immer vorn - in BEIDEN Sortierrichtungen. Sie sind der Weg
    //  nach unten, nicht ein Medium unter anderen.
    if (a->isFolder() != b->isFolder()) return a->isFolder();
    return m_descending ? fieldLess(b, a) : fieldLess(a, b);
}

bool MediaProxyModel::flatLessThan(const QModelIndex& left, const QModelIndex& right) const {
    switch (m_field) {
    case Field::Name: {
        const QString a = left.data(MediaModel::DisplayNameRole).toString();
        const QString b = right.data(MediaModel::DisplayNameRole).toString();
        const int cmp = a.compare(b, Qt::CaseInsensitive);
        if (cmp != 0) return cmp < 0;
        break;
    }
    case Field::FileSize: {
        const qint64 a = left.data(MediaModel::FileSizeRole).toLongLong();
        const qint64 b = right.data(MediaModel::FileSizeRole).toLongLong();
        if (a != b) return a < b;
        break;
    }
    case Field::Tags: {
        const QString a = left.data(MediaModel::TagsRole).toStringList().join(QChar(','));
        const QString b = right.data(MediaModel::TagsRole).toStringList().join(QChar(','));
        const int cmp = a.compare(b, Qt::CaseInsensitive);
        if (cmp != 0) return cmp < 0;
        break;
    }
    case Field::Date:
    default:
        break;
    }
    const QDateTime da = left.data(MediaModel::DateTimeRole).toDateTime();
    const QDateTime db = right.data(MediaModel::DateTimeRole).toDateTime();
    if (da != db) return da < db;
    return left.data(MediaModel::DisplayNameRole).toString()
               .compare(right.data(MediaModel::DisplayNameRole).toString(),
                        Qt::CaseInsensitive) < 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Sortierung ist HIERARCHISCH, seit aufgeklappte Unterordner ihre Dateien in
//  dasselbe Modell einspeisen. Die Ordnung ist die eines Pfades: Zeilen werden
//  an der Stelle verglichen, an der sich ihre Ordnerketten TRENNEN - dadurch
//  bleibt der Inhalt eines Ordners geschlossen hinter seiner Kachel stehen,
//  egal nach welchem Feld sortiert wird.
//
//  Die Kette wird ueber Bereichs-Indizes geklettert (MediaModel::
//  folderItemOfScope), nicht ueber Pfad-Zerlegung: das ist O(Tiefe) mit
//  Ganzzahlen statt String-Arbeit je Vergleich. Liegen beide Zeilen im selben
//  Ordner - der Normalfall, solange nichts aufgeklappt ist - kostet der
//  Baumanteil genau einen int-Vergleich.
// ─────────────────────────────────────────────────────────────────────────────
bool MediaProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    // Schneller Pfad wie in filterAcceptsRow: Sortieren laeuft O(n log n) mal
    // durch diese Funktion - je Vergleich zwei QVariants (inkl. QDateTime-/
    // QStringList-Kopien) waren der Loewenanteil der Sortierkosten.
    if (m_src) {
        const MediaItem* a = m_src->itemAt(left.row());
        const MediaItem* b = m_src->itemAt(right.row());
        if (a && b) {
            if (a->scope == b->scope)
                return sameScopeLess(a, b);

            const int da0 = m_src->scopeDepthOf(a->scope);
            const int db0 = m_src->scopeDepthOf(b->scope);

            const MediaItem* ca = a;
            const MediaItem* cb = b;
            int da = da0, db = db0;
            //  Erst auf gleiche Tiefe bringen …
            while (da > db) {
                ca = m_src->folderItemOfScope(ca->scope);
                if (!ca) return false;      // Kette gerissen: Reihenfolge egal
                --da;
            }
            while (db > da) {
                cb = m_src->folderItemOfScope(cb->scope);
                if (!cb) return true;
                --db;
            }
            //  … dieselbe Kachel heisst: einer liegt IM Ordner des anderen.
            //  Der Ordner steht vorn, sein Inhalt dahinter.
            if (ca == cb) return da0 < db0;
            //  … sonst gemeinsam hoch, bis beide im selben Ordner stehen.
            while (ca->scope != cb->scope) {
                const MediaItem* na = m_src->folderItemOfScope(ca->scope);
                const MediaItem* nb = m_src->folderItemOfScope(cb->scope);
                if (!na || !nb) break;
                ca = na;
                cb = nb;
                if (ca == cb) return da0 < db0;
            }
            return sameScopeLess(ca, cb);
        }
    }

    //  Fallback (fremdes Quellmodell): flach. Die Richtung wird hier durch
    //  Vertauschen der Argumente gedreht, weil der Proxy selbst aufsteigend
    //  sortiert (s. Konstruktor).
    return m_descending ? flatLessThan(right, left) : flatLessThan(left, right);
}

// ── Navigations-Accessoren ───────────────────────────────────────────────────
QVariant MediaProxyModel::roleAt(int proxyRow, int role) const {
    if (proxyRow < 0 || proxyRow >= rowCount()) return {};
    return index(proxyRow, 0).data(role);
}

QString MediaProxyModel::filePathAt(int r)    const { return roleAt(r, MediaModel::FilePathRole).toString(); }
QString MediaProxyModel::displayNameAt(int r) const { return roleAt(r, MediaModel::DisplayNameRole).toString(); }
int     MediaProxyModel::mediaTypeAt(int r)   const { return roleAt(r, MediaModel::MediaTypeRole).toInt(); }
QStringList MediaProxyModel::tagsAt(int r)    const { return roleAt(r, MediaModel::TagsRole).toStringList(); }
QDateTime MediaProxyModel::dateTimeAt(int r)  const { return roleAt(r, MediaModel::DateTimeRole).toDateTime(); }

int MediaProxyModel::depthAt(int proxyRow) const {
    return roleAt(proxyRow, MediaModel::DepthRole).toInt();
}

int MediaProxyModel::rowForPath(const QString& filePath) const {
    // O(1) ueber den Pfad->Zeile-Hash des Quellmodells + mapFromSource statt
    // eines linearen Scans mit QVariant-Konvertierung je Zeile (die Funktion
    // laeuft bei JEDEM Oeffnen/Weiterblaettern im Vollbild).
    if (m_src) {
        const int srcRow = m_src->rowForPath(filePath);
        if (srcRow < 0) return -1;
        const QModelIndex proxy = mapFromSource(m_src->index(srcRow));
        return proxy.isValid() ? proxy.row() : -1;   // −1 = durch Filter ausgeblendet
    }
    const int n = rowCount();
    for (int i = 0; i < n; ++i)
        if (index(i, 0).data(MediaModel::FilePathRole).toString() == filePath)
            return i;
    return -1;
}

// ── Blaettern im Vollbild: ordnerlokal ───────────────────────────────────────
//  Seit aufgeklappte Unterordner in derselben Liste stehen, waere ein flaches
//  „naechste Zeile" ein Sprung ueber die Ordnergrenze: aus der letzten Datei
//  eines Unterordners rutschte man in den Elternordner. Gemessen wird deshalb
//  am BEREICH - ein int-Vergleich, keine Pfadarbeit.
int MediaProxyModel::scopeOfProxyRow(int proxyRow) const {
    if (!m_src || proxyRow < 0 || proxyRow >= rowCount()) return -1;
    const QModelIndex src = mapToSource(index(proxyRow, 0));
    const MediaItem* it = src.isValid() ? m_src->itemAt(src.row()) : nullptr;
    return it ? it->scope : -1;
}

bool MediaProxyModel::isStepTarget(int proxyRow, int scope) const {
    if (!m_src) return true;                 // fremdes Quellmodell: flach wie frueher
    const QModelIndex src = mapToSource(index(proxyRow, 0));
    const MediaItem* it = src.isValid() ? m_src->itemAt(src.row()) : nullptr;
    if (!it || it->isFolder()) return false; // eine Ordnerkachel ist keine Datei
    return scope < 0 || it->scope == scope;
}

int MediaProxyModel::stepRow(int proxyRow, int delta) const {
    const int n = rowCount();
    if (n <= 0 || delta == 0) return -1;
    if (proxyRow < 0 || proxyRow >= n) return -1;
    const int scope = scopeOfProxyRow(proxyRow);
    const int step  = (delta > 0) ? 1 : -1;
    //  Bis zu n Schritte: der letzte landet wieder auf der Ausgangszeile, damit
    //  eine EINZELNE Datei im Ordner sich wie bisher selbst „weiterblaettert"
    //  statt die Ansicht zu schliessen.
    for (int k = 1; k <= n; ++k) {
        const int r = ((proxyRow + k * step) % n + n) % n;
        if (isStepTarget(r, scope)) return r;
    }
    return -1;
}

int MediaProxyModel::randomRow(int exceptRow) const {
    const int n = rowCount();
    if (n <= 0) return -1;
    //  Der Zufall bleibt im selben Ordner und trifft nie eine Ordnerkachel -
    //  sonst spraenge „Zufall" aus dem Unterordner heraus oder in eine Zeile,
    //  die der Betrachter gar nicht anzeigen kann.
    const int scope = scopeOfProxyRow(exceptRow);
    QVector<int> candidates;
    candidates.reserve(n);
    for (int r = 0; r < n; ++r)
        if (r != exceptRow && isStepTarget(r, scope)) candidates.append(r);
    if (candidates.isEmpty())
        return (exceptRow >= 0 && exceptRow < n) ? exceptRow : -1;
    return candidates.at(QRandomGenerator::global()->bounded(candidates.size()));
}
