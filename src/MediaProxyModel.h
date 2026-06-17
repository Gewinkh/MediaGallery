#pragma once
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QSet>
#include <QDateTime>
#include <QPointer>

class TagManager;

// ─────────────────────────────────────────────────────────────────────────────
//  MediaProxyModel — QSortFilterProxyModel vor dem MediaModel.
//
//  Phase 3 erweitert die Filterung um den vollständigen Tag-Modus-Satz
//  (OR/AND/NUR/INKLUSIV) und den Kategorie-Filter. Die Semantik ist 1:1 aus dem
//  früheren Consumer (GalleryView::applyFilter) übernommen:
//    • OR/INKLUSIV : Item hat mindestens einen der (effektiven) Filter-Tags.
//    • AND         : Item-Tags nicht leer UND enthalten alle Filter-Tags.
//    • NUR         : Item-Tags nicht leer, ≥1 Treffer, UND ausschließlich Tags
//                    aus der Filtermenge (Teilmenge der Filter-Tags).
//    • Kategorie   : Direkte Datei↔Kategorie-Mitgliedschaft (categoriesForFile)
//                    lässt ein Item IMMER passieren; zusätzlich werden die Tags
//                    aktiver Kategorien in die effektive Filtermenge injiziert.
//
//  Filtern/Sortieren bleibt serverseitig; QML setzt nur die Properties/Slots.
//  Es werden keine Datenkopien nach QML geschoben.
// ─────────────────────────────────────────────────────────────────────────────
class MediaProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(int  count          READ count          NOTIFY countChanged)
    Q_PROPERTY(int  sortRole       READ sortFieldInt   WRITE setSortFieldInt   NOTIFY sortChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortChanged)
    Q_PROPERTY(bool showImages     READ showImages     WRITE setShowImages     NOTIFY filterChanged)
    Q_PROPERTY(bool showVideos     READ showVideos     WRITE setShowVideos     NOTIFY filterChanged)
    Q_PROPERTY(bool showAudio      READ showAudio      WRITE setShowAudio      NOTIFY filterChanged)
    Q_PROPERTY(bool showPdfs       READ showPdfs       WRITE setShowPdfs       NOTIFY filterChanged)
    Q_PROPERTY(bool showTexts      READ showTexts      WRITE setShowTexts      NOTIFY filterChanged)
    Q_PROPERTY(QStringList tagFilter      READ tagFilter      WRITE setTagFilter      NOTIFY filterChanged)
    Q_PROPERTY(int         tagFilterMode  READ tagFilterModeInt WRITE setTagFilterModeInt NOTIFY filterChanged)
    Q_PROPERTY(QStringList categoryFilter READ categoryFilter WRITE setCategoryFilter NOTIFY filterChanged)
    // Rückwärtskompatibel zu Phase 2: AND/OR-Umschalter (mappt auf den Modus).
    Q_PROPERTY(bool tagFilterAnd   READ tagFilterAnd   WRITE setTagFilterAnd   NOTIFY filterChanged)

public:
    // Korrespondiert mit ISettings::SortField (Date/Name/Tags/FileSize).
    enum class Field { Date = 0, Name = 1, Tags = 2, FileSize = 3 };
    // Korrespondiert mit TagFilterMode (FilterBar): OR/AND/NUR/INKLUSIV.
    enum class TagMode { Or = 0, And = 1, Nur = 2, Inklusiv = 3 };

    explicit MediaProxyModel(QObject* parent = nullptr);

    // TagManager wird in main() injiziert; nicht-besitzend.
    void setTagManager(TagManager* mgr);

    int  count() const { return rowCount(); }

    int  sortFieldInt() const { return static_cast<int>(m_field); }
    void setSortFieldInt(int f);
    bool sortDescending() const { return m_descending; }
    void setSortDescending(bool d);

    bool showImages() const { return m_showImages; }  void setShowImages(bool v);
    bool showVideos() const { return m_showVideos; }  void setShowVideos(bool v);
    bool showAudio()  const { return m_showAudio;  }  void setShowAudio(bool v);
    bool showPdfs()   const { return m_showPdfs;   }  void setShowPdfs(bool v);
    bool showTexts()  const { return m_showTexts;  }  void setShowTexts(bool v);

    QStringList tagFilter() const { return m_tagFilter; }
    void        setTagFilter(const QStringList& t);

    int  tagFilterModeInt() const { return static_cast<int>(m_mode); }
    void setTagFilterModeInt(int m);

    QStringList categoryFilter() const { return m_categoryFilter; }
    void        setCategoryFilter(const QStringList& ids);

    bool tagFilterAnd() const { return m_mode == TagMode::And; }
    void setTagFilterAnd(bool v);

    // ── Navigations-Accessoren (für FullscreenViewer; in Proxy-Reihenfolge) ──
    Q_INVOKABLE QString   filePathAt(int proxyRow) const;
    Q_INVOKABLE QString   fileNameAt(int proxyRow) const;
    Q_INVOKABLE QString   displayNameAt(int proxyRow) const;
    Q_INVOKABLE int       mediaTypeAt(int proxyRow) const;
    Q_INVOKABLE QString   typeLabelAt(int proxyRow) const;
    Q_INVOKABLE QStringList tagsAt(int proxyRow) const;
    Q_INVOKABLE QDateTime dateTimeAt(int proxyRow) const;
    Q_INVOKABLE qint64    fileSizeAt(int proxyRow) const;
    Q_INVOKABLE int       rowForPath(const QString& filePath) const;
    Q_INVOKABLE int       randomRow(int exceptRow = -1) const;

signals:
    void countChanged();
    void sortChanged();
    void filterChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    void reapplySort();
    void recomputeEffectiveTags();
    void collectTagsForCategory(const QString& id, QSet<QString>& out) const;
    QVariant roleAt(int proxyRow, int role) const;

    QPointer<TagManager> m_tagMgr;

    Field   m_field      = Field::Date;
    bool    m_descending = true;
    TagMode m_mode       = TagMode::Or;

    bool m_showImages = true;
    bool m_showVideos = true;
    bool m_showAudio  = true;
    bool m_showPdfs   = true;
    bool m_showTexts  = true;

    QStringList   m_tagFilter;        // manuell gewählte Tags
    QStringList   m_categoryFilter;   // aktive Kategorie-IDs
    QSet<QString> m_effectiveTags;    // manuell ∪ Tags aktiver Kategorien
    QSet<QString> m_activeCatIds;     // == m_categoryFilter als Set
};
