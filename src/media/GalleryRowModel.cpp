#include "media/GalleryRowModel.h"
#include "media/MediaModel.h"
#include "media/MediaProxyModel.h"

#include <QVariantMap>

GalleryRowModel::GalleryRowModel(QObject* parent)
    : QAbstractListModel(parent)
{
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(0);   // „sobald die Ereignisschleife atmet"
    connect(&m_rebuildTimer, &QTimer::timeout, this, &GalleryRowModel::rebuildNow);
}

QObject* GalleryRowModel::source() const {
    return m_proxy.data();
}

void GalleryRowModel::setSource(QObject* src) {
    auto* proxy = qobject_cast<MediaProxyModel*>(src);
    if (proxy == m_proxy) return;
    m_sourceReset = true;      // ein neues Quellmodell ist immer ein Tausch

    if (m_proxy) m_proxy->disconnect(this);
    m_proxy = proxy;
    m_src   = proxy ? qobject_cast<MediaModel*>(proxy->sourceModel()) : nullptr;

    if (m_proxy) {
        // Alles, was Reihenfolge oder Bestand ändert, löst einen Neuaufbau aus; `dataChanged` nur ein Auffrischen der
        // Zeile. Beim Einfügen/Entfernen muss die STELLE mit: ab ihr trägt jede Zeile andere Kacheln.
        const auto markDirty = [this](const QModelIndex&, int first, int) {
            m_dirtyFrom = qMin(m_dirtyFrom, first);
            scheduleRebuild();
        };
        connect(m_proxy, &QAbstractItemModel::rowsInserted,  this, markDirty);
        connect(m_proxy, &QAbstractItemModel::rowsRemoved,   this, markDirty);
        const auto markReset = [this]() { m_sourceReset = true; scheduleRebuild(); };
        connect(m_proxy, &QAbstractItemModel::rowsMoved,     this, markReset);
        connect(m_proxy, &QAbstractItemModel::modelReset,    this, markReset);
        connect(m_proxy, &QAbstractItemModel::layoutChanged, this, markReset);
        connect(m_proxy, &QAbstractItemModel::dataChanged,   this,
                [this](const QModelIndex& tl, const QModelIndex& br, const QList<int>& roles) {
                    onSourceDataChanged(tl, br, roles);
                });
    }
    emit sourceChanged();
    rebuildNow();
}

void GalleryRowModel::setContentWidth(int w) {
    const int v = qMax(0, w);
    if (v == m_contentWidth) return;
    m_contentWidth = v;
    emit layoutChangedProp();
    rebuildNow();       // sofort: die Ansicht wartet auf die neue Aufteilung
}

void GalleryRowModel::setCellWidth(int w) {
    const int v = qMax(1, w);
    if (v == m_cellWidth) return;
    m_cellWidth = v;
    emit layoutChangedProp();
    rebuildNow();
}

void GalleryRowModel::setLevelInset(int px) {
    const int v = qMax(0, px);
    if (v == m_levelInset) return;
    m_levelInset = v;
    emit layoutChangedProp();
    rebuildNow();
}

int GalleryRowModel::columnsForDepth(int depth) const {
    const int usable = m_contentWidth - 2 * qMax(0, depth) * m_levelInset;
    return qMax(1, usable / qMax(1, m_cellWidth));
}

// Der Aufbau geht ueber alle Zeilen; je Fuell-Charge waere der Aufwand quadratisch -
// an 77.958 Zeilen (152 Chargen) gemessene 1785 ms. Waehrend eines Grosseinlesens
// wird deshalb zusammengefasst, sonst bleibt es sofort (naechster Durchlauf).
static constexpr int kFillRebuildGapMs = 80;

void GalleryRowModel::scheduleRebuild() {
    if (m_rebuildTimer.isActive()) return;

    int wait = 0;
    if (m_src && m_src->isFilling() && m_lastRebuild.isValid()) {
        const qint64 since = m_lastRebuild.elapsed();
        if (since < kFillRebuildGapMs) wait = int(kFillRebuildGapMs - since);
    }
    m_rebuildTimer.start(wait);
}

bool GalleryRowModel::isAncestorOrSame(int maybeAncestor, int scope) const {
    if (!m_src) return maybeAncestor == scope;
    for (int s = scope; s >= 0; s = m_src->scopeParentOf(s)) {
        if (s == maybeAncestor) return true;
        if (s == 0) break;
    }
    return false;
}


// Der Aufrufer stellt den Puffer: die Kette wird je Zeile gebraucht, und ein frischer Vektor je Aufruf war die
// häufigste Allokation der Galerie (92.000 Blöcke bei 30.000 Zeilen). Gefüllt von hinten statt mit `prepend`.
void GalleryRowModel::chainOf(int scope, QVector<int>* out) const {
    out->resize(0);                       // behaelt die Kapazitaet
    if (!m_src) { out->append(0); return; }

    int depth = 0;
    for (int s = scope; s >= 0; s = m_src->scopeParentOf(s)) {
        ++depth;
        if (s == 0) break;
    }
    if (depth == 0) { out->append(0); return; }

    out->resize(depth);
    int k = depth - 1;
    for (int s = scope; s >= 0 && k >= 0; s = m_src->scopeParentOf(s)) {
        (*out)[k--] = s;
        if (s == 0) break;
    }
}

QVector<int> GalleryRowModel::chainOf(int scope) const {
    QVector<int> chain;
    chainOf(scope, &chain);
    return chain;
}

void GalleryRowModel::rebuildNow() {
    m_rebuildTimer.stop();
    m_lastRebuild.restart();

    QVector<Row> rows;
    const int n = m_proxy ? m_proxy->rowCount() : 0;
    rows.reserve(n / qMax(1, columnsForDepth(0)) + 8);

    //  Zeilen bilden
    //  Eine Zeile traegt nur Kacheln DESSELBEN Ordners: ein Bereichswechsel
    //  bricht um, auch wenn die Zeile noch nicht voll ist.
    int i = 0;
    while (i < n) {
        const int scope = m_proxy->scopeAt(i);
        const int depth = m_src ? m_src->scopeDepthOf(scope) : 0;
        const int cols  = columnsForDepth(depth);
        int j = i;
        while (j < n && (j - i) < cols && m_proxy->scopeAt(j) == scope)
            ++j;
        // KOPFZEILE vor der ersten Zeile eines aufgeklappten Ordners - man soll sehen, in welchem Ordner man etwas
        // anlegt. Der Vergleich läuft aufwärts, damit ein nach einem Unterordner zurückkehrender Inhalt keine zweite bekommt.
        if (depth > 0) {
            const bool freshBand = rows.isEmpty()
                                || !isAncestorOrSame(scope, rows.constLast().scope);
            if (freshBand) {
                Row head;
                head.kind  = 1;
                head.first = i;
                head.count = 0;
                head.scope = scope;
                head.depth = depth;
                rows.append(head);
            }
        }

        Row r;
        r.first = i;
        r.count = j - i;
        r.scope = scope;
        r.depth = depth;
        rows.append(r);
        i = j;
    }

    // Die Ordnung ist eine Tiefensuche, ein Band also zusammenhängend: es genügt der Vergleich mit dem direkten
    // Nachbarn. DREI Puffer für den ganzen Lauf, und die Kette der vorigen Zeile wird durchgereicht statt neu gerechnet.
    QVector<int> chain, prev, next;
    chain.reserve(16); prev.reserve(16); next.reserve(16);
    if (!rows.isEmpty()) chainOf(rows[0].scope, &chain);
    for (int r = 0; r < rows.size(); ++r) {
        if (r + 1 < rows.size()) chainOf(rows[r + 1].scope, &next);
        else                     next.resize(0);
        for (int k = 1; k < chain.size(); ++k) {
            const int here = chain.at(k);
            if (k >= prev.size() || prev.at(k) != here) rows[r].openMask  |= (1u << k);
            if (k >= next.size() || next.at(k) != here) rows[r].closeMask |= (1u << k);
        }
        prev.swap(chain);
        chain.swap(next);
    }

    applyRows(rows);
}

// Diff statt beginResetModel: das wirft alle Delegates weg und setzt contentY auf 0 -
// genau der Ruckler und der Sprung an den Anfang beim Auf- und Zuklappen.
// Das Ende wird ohne first verglichen: die Indizes verschieben sich, die Gestalt nicht.
void GalleryRowModel::applyRows(const QVector<Row>& next) {
    const auto sameShape = [](const Row& a, const Row& b) {
        return a.kind == b.kind && a.count == b.count && a.scope == b.scope
            && a.depth == b.depth
            && a.openMask == b.openMask && a.closeMask == b.closeMask;
    };
    const auto sameAll = [&](const Row& a, const Row& b) {
        return a.first == b.first && sameShape(a, b);
    };

    int p = 0;
    while (p < m_rows.size() && p < next.size() && sameAll(m_rows.at(p), next.at(p)))
        ++p;
    int suffix = 0;
    while (suffix < m_rows.size() - p && suffix < next.size() - p
           && sameShape(m_rows.at(m_rows.size() - 1 - suffix),
                        next.at(next.size() - 1 - suffix)))
        ++suffix;

    const int oldMid = m_rows.size() - p - suffix;
    const int newMid = next.size()   - p - suffix;

    if (oldMid > 0) {
        beginRemoveRows(QModelIndex(), p, p + oldMid - 1);
        m_rows.remove(p, oldMid);
        endRemoveRows();
    }
    if (newMid > 0) {
        beginInsertRows(QModelIndex(), p, p + newMid - 1);
        for (int k = 0; k < newMid; ++k)
            m_rows.insert(p + k, next.at(p + k));
        endInsertRows();
    }

    bool tailMoved = false;
    for (int k = 0; k < suffix; ++k) {
        const int r = m_rows.size() - 1 - k;
        if (m_rows.at(r).first != next.at(r).first) tailMoved = true;
        m_rows[r] = next.at(r);
    }
    if (tailMoved && suffix > 0)
        emit dataChanged(index(m_rows.size() - suffix), index(m_rows.size() - 1));
    // Hat die Quelle ihren Inhalt getauscht, ist AUCH der behaltene Anfang inhaltlich neu - er sieht nur gleich aus.
    // Einmal über alles auffrischen ist trotzdem billiger als ein Reset: die Ansicht behält Delegates und Position.
    if (m_sourceReset) {
        m_sourceReset = false;
        m_dirtyFrom = std::numeric_limits<int>::max();
        if (!m_rows.isEmpty())
            emit dataChanged(index(0), index(m_rows.size() - 1));
    } else if (m_dirtyFrom != std::numeric_limits<int>::max()) {
        // Eine Zeile, deren Proxy-Bereich ab der geänderten Stelle liegt, zeigt andere Dateien - auch wenn `first`,
        // Gestalt und Kachelzahl gleich blieben. Die Zeilen davor sind nachweislich unberührt.
        int from = -1;
        for (int r = 0; r < m_rows.size(); ++r) {
            if (m_rows.at(r).first + m_rows.at(r).count > m_dirtyFrom) { from = r; break; }
        }
        m_dirtyFrom = std::numeric_limits<int>::max();
        if (from >= 0)
            emit dataChanged(index(from), index(m_rows.size() - 1));
    }

    if (oldMid != newMid)
        emit countChanged();
}

void GalleryRowModel::onSourceDataChanged(const QModelIndex& topLeft,
                                          const QModelIndex& bottomRight,
                                          const QList<int>& roles) {
    // Die MEHRFACHAUSWAHL steht bewusst NICHT in den Kacheldaten: eine Kachel bindet sie direkt an
    // `selectionRevision`. Sonst kostete jede Mausbewegung des Auswahlrahmens den vollen Umbau - 1145 gegen 55 µs.
    if (roles.size() == 1 && roles.first() == MediaModel::SelectedRole) return;

    if (m_rows.isEmpty()) return;
    const int from = topLeft.row();
    const int to   = bottomRight.row();
    int first = -1, last = -1;
    for (int r = 0; r < m_rows.size(); ++r) {
        const Row& row = m_rows.at(r);
        if (row.first + row.count <= from) continue;
        if (row.first > to) break;
        if (first < 0) first = r;
        last = r;
    }
    if (first < 0) return;
    emit dataChanged(index(first), index(last), { TilesRole });
}

int GalleryRowModel::rowOfIndex(int proxyRow) const {
    for (int r = 0; r < m_rows.size(); ++r) {
        const Row& row = m_rows.at(r);
        if (proxyRow >= row.first && proxyRow < row.first + row.count) return r;
    }
    return -1;
}

QVariantMap GalleryRowModel::rowInfo(int row) const {
    QVariantMap m;
    if (row < 0 || row >= m_rows.size()) return m;
    const Row& r = m_rows.at(row);
    m.insert(QStringLiteral("kind"),  r.kind);
    m.insert(QStringLiteral("first"), r.first);
    m.insert(QStringLiteral("count"), r.count);
    m.insert(QStringLiteral("depth"), r.depth);
    m.insert(QStringLiteral("openMask"),  static_cast<int>(r.openMask));
    m.insert(QStringLiteral("closeMask"), static_cast<int>(r.closeMask));
    m.insert(QStringLiteral("ownerFolder"),
             m_src ? m_src->folderOfScope(r.scope) : QString());
    return m;
}

int GalleryRowModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant GalleryRowModel::data(const QModelIndex& index, int role) const {
    const int r = index.row();
    if (r < 0 || r >= m_rows.size()) return {};
    const Row& row = m_rows.at(r);

    switch (role) {
    case KindRole:       return row.kind;
    case OwnerNameRole: {
        const QString f = m_src ? m_src->folderOfScope(row.scope) : QString();
        const int cut = qMax(f.lastIndexOf(QLatin1Char('/')),
                             f.lastIndexOf(QLatin1Char('\\')));
        return f.mid(cut + 1);
    }
    case DepthRole:      return row.depth;
    case FirstIndexRole: return row.first;
    case TileCountRole:  return row.count;
    case OpenMaskRole:   return static_cast<int>(row.openMask);
    case CloseMaskRole:  return static_cast<int>(row.closeMask);
    case OwnerFolderRole:
        return m_src ? m_src->folderOfScope(row.scope) : QString();
    case TilesRole: {
        QVariantList out;
        if (!m_proxy) return out;
        out.reserve(row.count);
        for (int k = 0; k < row.count; ++k) {
            const QModelIndex idx = m_proxy->index(row.first + k, 0);
            if (!idx.isValid()) continue;
            QVariantMap m;
            m.insert(QStringLiteral("row"),         row.first + k);
            m.insert(QStringLiteral("filePath"),    idx.data(MediaModel::FilePathRole));
            m.insert(QStringLiteral("displayName"), idx.data(MediaModel::DisplayNameRole));
            m.insert(QStringLiteral("mediaType"),   idx.data(MediaModel::MediaTypeRole));
            m.insert(QStringLiteral("typeLabel"),   idx.data(MediaModel::TypeLabelRole));
            m.insert(QStringLiteral("tags"),        idx.data(MediaModel::TagsRole));
            m.insert(QStringLiteral("dateTime"),    idx.data(MediaModel::DateTimeRole));
            m.insert(QStringLiteral("thumbUrl"),    idx.data(MediaModel::ThumbUrlRole));
            m.insert(QStringLiteral("thumbState"),  idx.data(MediaModel::ThumbStateRole));
            m.insert(QStringLiteral("expanded"),    idx.data(MediaModel::ExpandedRole));
            m.insert(QStringLiteral("childCount"),  idx.data(MediaModel::ChildCountRole));
            out.append(m);
        }
        return out;
    }
    default: return {};
    }
}

QHash<int, QByteArray> GalleryRowModel::roleNames() const {
    return {
        { DepthRole,       "depth"       },
        { FirstIndexRole,  "firstIndex"  },
        { TileCountRole,   "tileCount"   },
        { OpenMaskRole,    "openMask"    },
        { CloseMaskRole,   "closeMask"   },
        { OwnerFolderRole, "ownerFolder" },
        { TilesRole,       "tiles"       },
        { KindRole,        "kind"        },
        { OwnerNameRole,   "ownerName"   },
    };
}
