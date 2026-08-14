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
    sort(0, m_descending ? Qt::DescendingOrder : Qt::AscendingOrder);

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

// Typ-Umschalter berühren nur den Zeilenfilter → refilterRows() statt
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

    // ── Medientyp-Filter ────────────────────────────────────────────────────
    const auto type = item ? item->type
                           : static_cast<MediaType>(idx.data(MediaModel::MediaTypeRole).toInt());
    switch (type) {
    case MediaType::Image: if (!m_showImages) return false; break;
    case MediaType::Video: if (!m_showVideos) return false; break;
    case MediaType::Audio: if (!m_showAudio)  return false; break;
    case MediaType::Pdf:   if (!m_showPdfs)   return false; break;
    case MediaType::Text:  if (!m_showTexts)  return false; break;
    // DOCX läuft bewusst unter dem Text-Filter (Dokumente).
    case MediaType::Docx:  if (!m_showTexts)  return false; break;
    default:               return false;
    }

    // ── Freitextsuche (UND-verknüpft, deshalb VOR jedem „return true") ───────
    //  Anzeigename, Dateiname und Tags. Der Dateiname kommt dazu, weil der
    //  Anzeigename ein anderer sein kann — wer die Endung tippt, sucht die Datei.
    if (!m_search.isEmpty()) {
        const QString name = item ? item->displayName
                                  : idx.data(MediaModel::DisplayNameRole).toString();
        bool hit = name.contains(m_search, Qt::CaseInsensitive);
        if (!hit) {
            const QString file = item ? item->fileName()
                                      : idx.data(MediaModel::FileNameRole).toString();
            hit = file.contains(m_search, Qt::CaseInsensitive);
        }
        if (!hit) {
            const QStringList tags = item ? item->tags
                                          : idx.data(MediaModel::TagsRole).toStringList();
            for (const QString& tag : tags)
                if (tag.contains(m_search, Qt::CaseInsensitive)) { hit = true; break; }
        }
        if (!hit) return false;
    }

    const bool hasTagFilter      = !m_effectiveTags.isEmpty();
    const bool hasCategoryFilter = !m_activeCatIds.isEmpty();
    if (!hasTagFilter && !hasCategoryFilter)
        return true;

    // ── Direkte Datei↔Kategorie-Mitgliedschaft: passiert immer ───────────────
    //  O(1)-Lookup im vorberechneten Cache statt rekursivem Baum-Scan pro Zeile.
    if (hasCategoryFilter && !m_activeCatFiles.isEmpty()) {
        const QString fileName = item ? item->fileName()
                                      : idx.data(MediaModel::FileNameRole).toString();
        if (m_activeCatFiles.contains(fileName))
            return true;
    }

    // Kategorie aktiv, aber keine (effektiven) Tags → kein weiterer Pfad.
    if (!hasTagFilter)
        return false;

    // ── Tag-Modus-Auswertung gegen die effektive Filtermenge ─────────────────
    //  Beim schnellen Pfad ohne Kopie (Referenz auf die Item-Tags), sonst aus
    //  dem QVariant. `owned` haelt die Kopie nur im Fallback am Leben.
    const QStringList owned = item ? QStringList()
                                   : idx.data(MediaModel::TagsRole).toStringList();
    const QStringList& itemTags = item ? item->tags : owned;
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
    // Schneller Pfad wie in filterAcceptsRow: Sortieren laeuft O(n log n) mal
    // durch diese Funktion — je Vergleich zwei QVariants (inkl. QDateTime-/
    // QStringList-Kopien) waren der Loewenanteil der Sortierkosten.
    if (m_src) {
        const MediaItem* a = m_src->itemAt(left.row());
        const MediaItem* b = m_src->itemAt(right.row());
        if (a && b) {
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
    }

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
QString MediaProxyModel::displayNameAt(int r) const { return roleAt(r, MediaModel::DisplayNameRole).toString(); }
int     MediaProxyModel::mediaTypeAt(int r)   const { return roleAt(r, MediaModel::MediaTypeRole).toInt(); }
QStringList MediaProxyModel::tagsAt(int r)    const { return roleAt(r, MediaModel::TagsRole).toStringList(); }
QDateTime MediaProxyModel::dateTimeAt(int r)  const { return roleAt(r, MediaModel::DateTimeRole).toDateTime(); }

int MediaProxyModel::rowForPath(const QString& filePath) const {
    // O(1) ueber den Pfad→Zeile-Hash des Quellmodells + mapFromSource statt
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

int MediaProxyModel::randomRow(int exceptRow) const {
    const int n = rowCount();
    if (n <= 0) return -1;
    if (n == 1) return 0;
    int r = exceptRow;
    while (r == exceptRow)
        r = QRandomGenerator::global()->bounded(n);
    return r;
}
