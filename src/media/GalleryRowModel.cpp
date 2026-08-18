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
        //  Alles, was die REIHENFOLGE oder den Bestand aendert, loest einen
        //  Neuaufbau aus; `dataChanged` dagegen nur ein Auffrischen der
        //  betroffenen Zeile — ein Thumbnail verschiebt keine Kachel.
        //  Einfuegen/Entfernen aendert nur die STELLE — der Diff genuegt.
        connect(m_proxy, &QAbstractItemModel::rowsInserted,  this, &GalleryRowModel::scheduleRebuild);
        connect(m_proxy, &QAbstractItemModel::rowsRemoved,   this, &GalleryRowModel::scheduleRebuild);
        //  Diese drei TAUSCHEN den Inhalt: Ordnerwechsel/Reload (`modelReset`),
        //  neue Sortierung (`layoutChanged`), Umordnung (`rowsMoved`). Dabei
        //  koennen die Zeilen dieselbe GESTALT behalten und trotzdem voellig
        //  andere Kacheln tragen — die Ansicht braucht dann ein Auffrischen
        //  ueber ALLE Zeilen, nicht nur ueber die verschobenen.
        const auto markReset = [this]() { m_sourceReset = true; scheduleRebuild(); };
        connect(m_proxy, &QAbstractItemModel::rowsMoved,     this, markReset);
        connect(m_proxy, &QAbstractItemModel::modelReset,    this, markReset);
        connect(m_proxy, &QAbstractItemModel::layoutChanged, this, markReset);
        connect(m_proxy, &QAbstractItemModel::dataChanged,   this,
                [this](const QModelIndex& tl, const QModelIndex& br, const QList<int>&) {
                    onSourceDataChanged(tl, br);
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

void GalleryRowModel::scheduleRebuild() {
    if (!m_rebuildTimer.isActive())
        m_rebuildTimer.start();
}

//  Ist `maybeAncestor` derselbe Bereich wie `scope` oder einer darueber? Damit
//  entscheidet sich, ob eine Zeile ein NEUES Band beginnt (dann kommt eine
//  Kopfzeile davor) oder nur in ein laufendes zurueckkehrt — der Inhalt eines
//  Ordners nach einem eingeschobenen Unterordner braucht keine zweite Kopfzeile.
bool GalleryRowModel::isAncestorOrSame(int maybeAncestor, int scope) const {
    if (!m_src) return maybeAncestor == scope;
    for (int s = scope; s >= 0; s = m_src->scopeParentOf(s)) {
        if (s == maybeAncestor) return true;
        if (s == 0) break;
    }
    return false;
}


QVector<int> GalleryRowModel::chainOf(int scope) const {
    QVector<int> chain;
    if (!m_src) { chain.append(0); return chain; }
    for (int s = scope; s >= 0; s = m_src->scopeParentOf(s)) {
        chain.prepend(s);
        if (s == 0) break;
    }
    if (chain.isEmpty()) chain.append(0);
    return chain;
}

void GalleryRowModel::rebuildNow() {
    m_rebuildTimer.stop();

    QVector<Row> rows;
    const int n = m_proxy ? m_proxy->rowCount() : 0;
    rows.reserve(n / qMax(1, columnsForDepth(0)) + 8);

    //  ── Zeilen bilden ──────────────────────────────────────────────────────
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
        //  KOPFZEILE: vor der ERSTEN Zeile eines aufgeklappten Ordners. Sie
        //  traegt seinen Namen und seine eigenen Aktionen — man soll sehen und
        //  treffen koennen, in welchem Ordner man gerade etwas anlegt.
        //  Ein Band BEGINNT, wenn die Zeile davor nicht schon darin lag. Der
        //  Vergleich laeuft von der vorherigen Zeile AUFWAERTS: kehrt der
        //  Inhalt nach einem eingeschobenen Unterordner in seinen Ordner
        //  zurueck, ist das dasselbe Band und braucht keine zweite Kopfzeile.
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

    //  ── Baender: wo beginnt/endet die Flaeche einer Ebene? ─────────────────
    //  Die Ordnung ist eine Tiefensuche, ein Band also zusammenhaengend. Es
    //  genuegt der Vergleich mit dem direkten Nachbarn: hat die Zeile davor auf
    //  Ebene k einen ANDEREN Bereich (oder liegt sie flacher), beginnt hier das
    //  Band; dasselbe nach unten fuer das Ende.
    for (int r = 0; r < rows.size(); ++r) {
        const QVector<int> chain = chainOf(rows[r].scope);
        const QVector<int> prev  = (r > 0) ? chainOf(rows[r - 1].scope) : QVector<int>{};
        const QVector<int> next  = (r + 1 < rows.size()) ? chainOf(rows[r + 1].scope)
                                                         : QVector<int>{};
        for (int k = 1; k < chain.size(); ++k) {
            const int here = chain.at(k);
            if (k >= prev.size() || prev.at(k) != here) rows[r].openMask  |= (1u << k);
            if (k >= next.size() || next.at(k) != here) rows[r].closeMask |= (1u << k);
        }
    }

    applyRows(rows);
}

//  ── Die neue Zeilenliste EINSPIELEN, ohne die Ansicht zurueckzusetzen ───────
//  Ein `beginResetModel` wirft in der ListView ALLE Delegates weg (samt der
//  MediaTiles darin, samt ihrer Thumbnail-Anforderungen) und setzt `contentY`
//  auf 0 — beim Auf- und Zuklappen eines Ordners war genau das der spuerbare
//  Ruckler und der Sprung an den Anfang.
//
//  Stattdessen ein Diff: gleicher Anfang, gleiches Ende, dazwischen EIN
//  Entfernen und EIN Einfuegen. Das Ende wird dabei ohne `first` verglichen —
//  werden Zeilen eingefuegt, verschieben sich die Proxy-Indizes aller
//  folgenden Zeilen, ihre GESTALT bleibt aber gleich. Sie werden deshalb
//  behalten und nur aufgefrischt.
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

    //  Das behaltene Ende bekommt seine (verschobenen) Proxy-Indizes.
    bool tailMoved = false;
    for (int k = 0; k < suffix; ++k) {
        const int r = m_rows.size() - 1 - k;
        if (m_rows.at(r).first != next.at(r).first) tailMoved = true;
        m_rows[r] = next.at(r);
    }
    if (tailMoved && suffix > 0)
        emit dataChanged(index(m_rows.size() - suffix), index(m_rows.size() - 1));
    //  Hat die Quelle ihren Inhalt getauscht, ist AUCH der behaltene Anfang
    //  inhaltlich neu — er sieht nur gleich aus. Einmal ueber alles auffrischen
    //  ist trotzdem billiger als ein Reset: die Ansicht behaelt ihre Delegates
    //  und ihre Position, liest aber jede Zeile neu.
    if (m_sourceReset) {
        m_sourceReset = false;
        if (!m_rows.isEmpty())
            emit dataChanged(index(0), index(m_rows.size() - 1));
    }
    //  Sonst kann sich nur der behaltene Anfang in den Kacheldaten geaendert
    //  haben (Thumbnail, Tags) — das meldet `onSourceDataChanged` gezielt.

    if (oldMid != newMid)
        emit countChanged();
}

void GalleryRowModel::onSourceDataChanged(const QModelIndex& topLeft,
                                          const QModelIndex& bottomRight) {
    if (m_rows.isEmpty()) return;
    const int from = topLeft.row();
    const int to   = bottomRight.row();
    //  Die betroffenen VISUELLEN Zeilen sind zusammenhaengend (die Zeilenliste
    //  ist nach Proxy-Zeilen geordnet) — ein Bereich genuegt.
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
