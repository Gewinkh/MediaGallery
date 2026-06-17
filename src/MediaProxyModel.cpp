#include "MediaProxyModel.h"
#include "MediaModel.h"
#include "MediaItem.h"
#include "TagManager.h"
#include "TagCategory.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <functional>

MediaProxyModel::MediaProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    sort(0, m_descending ? Qt::DescendingOrder : Qt::AscendingOrder);

    // count-Property reaktiv halten.
    connect(this, &QAbstractItemModel::rowsInserted,   this, &MediaProxyModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved,    this, &MediaProxyModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset,     this, &MediaProxyModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged,  this, &MediaProxyModel::countChanged);
}

void MediaProxyModel::setTagManager(TagManager* mgr) {
    if (m_tagMgr == mgr) return;
    if (m_tagMgr) m_tagMgr->disconnect(this);
    m_tagMgr = mgr;
    if (m_tagMgr) {
        connect(m_tagMgr, &TagManager::categoriesChanged, this, [this] {
            recomputeFilterCaches();
            invalidateRowsFilter();
            emit filterChanged();
        });
        connect(m_tagMgr, &TagManager::tagsChanged, this, [this] {
            recomputeFilterCaches();
            invalidateRowsFilter();
        });
    }
    recomputeFilterCaches();
    invalidateRowsFilter();
}

void MediaProxyModel::reapplySort() {
    sort(0, m_descending ? Qt::DescendingOrder : Qt::AscendingOrder);
}

void MediaProxyModel::setSortFieldInt(int f) {
    const Field nf = static_cast<Field>(f);
    if (nf == m_field) return;
    m_field = nf;
    // Nur neu sortieren — der Zeilenfilter bleibt unberührt (kein Full-Reset).
    invalidate();
    reapplySort();
    emit sortChanged();
}

void MediaProxyModel::setSortDescending(bool d) {
    if (d == m_descending) return;
    m_descending = d;
    reapplySort();
    emit sortChanged();
}

// Typ-Umschalter berühren nur den Zeilenfilter → invalidateRowsFilter() statt
// invalidate() (kein Re-Sort, keine Spalten-Neubewertung).
#define PROXY_BOOL_SETTER(Setter, Member)            \
    void MediaProxyModel::Setter(bool v) {           \
        if (v == Member) return;                     \
        Member = v;                                  \
        invalidateRowsFilter();                      \
        emit filterChanged();                        \
    }
PROXY_BOOL_SETTER(setShowImages, m_showImages)
PROXY_BOOL_SETTER(setShowVideos, m_showVideos)
PROXY_BOOL_SETTER(setShowAudio,  m_showAudio)
PROXY_BOOL_SETTER(setShowPdfs,   m_showPdfs)
PROXY_BOOL_SETTER(setShowTexts,  m_showTexts)
#undef PROXY_BOOL_SETTER

void MediaProxyModel::setTagFilter(const QStringList& t) {
    if (t == m_tagFilter) return;
    m_tagFilter = t;
    recomputeFilterCaches();
    invalidateRowsFilter();
    emit filterChanged();
}

void MediaProxyModel::setTagFilterModeInt(int m) {
    const TagMode nm = static_cast<TagMode>(m);
    if (nm == m_mode) return;
    m_mode = nm;
    invalidateRowsFilter();
    emit filterChanged();
}

void MediaProxyModel::setCategoryFilter(const QStringList& ids) {
    if (ids == m_categoryFilter) return;
    m_categoryFilter = ids;
    m_activeCatIds = QSet<QString>(ids.begin(), ids.end());
    recomputeFilterCaches();
    invalidateRowsFilter();
    emit filterChanged();
}

void MediaProxyModel::setTagFilterAnd(bool v) {
    const TagMode nm = v ? TagMode::And : TagMode::Or;
    if (nm == m_mode) return;
    m_mode = nm;
    invalidateRowsFilter();
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

bool MediaProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!idx.isValid()) return false;

    // ── Medientyp-Filter ────────────────────────────────────────────────────
    const auto type = static_cast<MediaType>(idx.data(MediaModel::MediaTypeRole).toInt());
    switch (type) {
    case MediaType::Image: if (!m_showImages) return false; break;
    case MediaType::Video: if (!m_showVideos) return false; break;
    case MediaType::Audio: if (!m_showAudio)  return false; break;
    case MediaType::Pdf:   if (!m_showPdfs)   return false; break;
    case MediaType::Text:  if (!m_showTexts)  return false; break;
    default:               return false;
    }

    const bool hasTagFilter      = !m_effectiveTags.isEmpty();
    const bool hasCategoryFilter = !m_activeCatIds.isEmpty();
    if (!hasTagFilter && !hasCategoryFilter)
        return true;

    // ── Direkte Datei↔Kategorie-Mitgliedschaft: passiert immer ───────────────
    //  O(1)-Lookup im vorberechneten Cache statt rekursivem Baum-Scan pro Zeile.
    if (hasCategoryFilter && !m_activeCatFiles.isEmpty()) {
        const QString fileName = idx.data(MediaModel::FileNameRole).toString();
        if (m_activeCatFiles.contains(fileName))
            return true;
    }

    // Kategorie aktiv, aber keine (effektiven) Tags → kein weiterer Pfad.
    if (!hasTagFilter)
        return false;

    // ── Tag-Modus-Auswertung gegen die effektive Filtermenge ─────────────────
    const QStringList itemTags = idx.data(MediaModel::TagsRole).toStringList();
    switch (m_mode) {
    case TagMode::Or:
    case TagMode::Inklusiv:
        for (const QString& t : itemTags)
            if (m_effectiveTags.contains(t)) return true;
        return false;

    case TagMode::And:
        if (itemTags.isEmpty()) return false;
        for (const QString& t : std::as_const(m_effectiveTags))
            if (!itemTags.contains(t)) return false;
        return true;

    case TagMode::Nur: {
        if (itemTags.isEmpty()) return false;
        bool hasOne = false;
        for (const QString& t : itemTags)
            if (m_effectiveTags.contains(t)) { hasOne = true; break; }
        if (!hasOne) return false;
        for (const QString& t : itemTags)
            if (!m_effectiveTags.contains(t)) return false;
        return true;
    }
    }
    return false;
}

bool MediaProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
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

// ── Navigations-Accessoren ───────────────────────────────────────────────────
QVariant MediaProxyModel::roleAt(int proxyRow, int role) const {
    if (proxyRow < 0 || proxyRow >= rowCount()) return {};
    return index(proxyRow, 0).data(role);
}

QString MediaProxyModel::filePathAt(int r)    const { return roleAt(r, MediaModel::FilePathRole).toString(); }
QString MediaProxyModel::fileNameAt(int r)    const { return roleAt(r, MediaModel::FileNameRole).toString(); }
QString MediaProxyModel::displayNameAt(int r) const { return roleAt(r, MediaModel::DisplayNameRole).toString(); }
int     MediaProxyModel::mediaTypeAt(int r)   const { return roleAt(r, MediaModel::MediaTypeRole).toInt(); }
QString MediaProxyModel::typeLabelAt(int r)   const { return roleAt(r, MediaModel::TypeLabelRole).toString(); }
QStringList MediaProxyModel::tagsAt(int r)    const { return roleAt(r, MediaModel::TagsRole).toStringList(); }
QDateTime MediaProxyModel::dateTimeAt(int r)  const { return roleAt(r, MediaModel::DateTimeRole).toDateTime(); }
qint64  MediaProxyModel::fileSizeAt(int r)    const { return roleAt(r, MediaModel::FileSizeRole).toLongLong(); }

int MediaProxyModel::rowForPath(const QString& filePath) const {
    const int n = rowCount();
    for (int i = 0; i < n; ++i)
        if (index(i, 0).data(MediaModel::FilePathRole).toString() == filePath)
            return i;
    return -1;
}

int MediaProxyModel::randomRow(int exceptRow) const {
    const int n = rowCount();
    if (n <= 0) return -1;
    if (n == 1) return 0;
    int r = exceptRow;
    while (r == exceptRow)
        r = QRandomGenerator::global()->bounded(n);
    return r;
}
