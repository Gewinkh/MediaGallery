#pragma once
#include "core/SearchPattern.h"
#include "media/MediaItem.h"

#include <QSortFilterProxyModel>
#include <QtGlobal>
#include <QStringList>
#include <QSet>
#include <QHash>
#include <QDateTime>
#include <QPointer>

class TagManager;
class MediaModel;
struct MediaItem;

// Tag-Modi: OR mindestens einer, AND alle, NUR ausschließlich Filter-Tags; Kategorie-Mitgliedschaft lässt immer
// passieren und spielt ihre Tags in die Menge. Alle filterabhängigen Mengen werden einmal je Änderung
// vorberechnet - je Zeile bleiben O(1)-Lookups statt eines rekursiven Baum-Scans.
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
    Q_PROPERTY(bool showFolders    READ showFolders    WRITE setShowFolders    NOTIFY filterChanged)
    // Eine WEISSE Liste, keine schwarze - die Menge des Abspielbaren ist endlich und bekannt, die des Übrigen
    // nicht: `MediaType::Unknown` lief an jedem Typ-Häkchen vorbei, und ZIP oder XLSX standen im Player-Modus.
    Q_PROPERTY(bool onlyPlayable   READ onlyPlayable   WRITE setOnlyPlayable   NOTIFY filterChanged)
    Q_PROPERTY(QStringList tagFilter      READ tagFilter      WRITE setTagFilter      NOTIFY filterChanged)
    Q_PROPERTY(int         tagFilterMode  READ tagFilterModeInt WRITE setTagFilterModeInt NOTIFY filterChanged)
    Q_PROPERTY(QStringList categoryFilter READ categoryFilter WRITE setCategoryFilter NOTIFY filterChanged)
    Q_PROPERTY(QString     searchText     READ searchText     WRITE setSearchText     NOTIFY filterChanged)
    Q_PROPERTY(bool tagFilterAnd   READ tagFilterAnd   WRITE setTagFilterAnd   NOTIFY filterChanged)

public:
    // Das Filterurteil als ZUSTANDSLOSE Funktion über eine Momentaufnahme: die rekursive Suche muss im Worker
    // entscheiden, ob ein unsichtbarer Ordner einen Treffer enthält. Eine zweite, ungefähre Regel öffnete leere Ordner.
    struct FilterCriteria {
        QString       search;
        // Derselbe Begriff, EINMAL übersetzt: wörtlich und - bei Sonderzeichen - zusätzlich als Muster. Er wird je
        // Zeile ausgewertet, darf also nicht je Zeile neu gebaut werden.
        mg::search::Pattern pattern;
        QSet<QString> tags;          // effektive Tags (manuell ∪ Kategorien)
        int           mode = 0;      // TagMode
        bool          categoryActive = false;
        QSet<QString> catFiles;      // Dateinamen in aktiven Kategorien DIESES Ordners
        bool showImages = true, showVideos = true, showAudio = true;
        bool showPdfs   = true, showTexts  = true, showFolders = true;
        //  s. Q_PROPERTY onlyPlayable - im Player-Modus entscheidet allein die
        //  weisse Liste (`isPlayableType`), die Häkchen oben spielen dann nur
        //  noch für „Videos mitzeigen" eine Rolle.
        bool onlyPlayable = false;
        void setSearch(const QString& s) {
            search = s;
            pattern = mg::search::Pattern(s, false, false);
        }
        bool isEmpty() const {
            return search.isEmpty() && tags.isEmpty() && !categoryActive;
        }
    };
    static bool acceptsFile(int mediaType, const QString& displayName,
                            const QString& fileName, const QStringList& tags,
                            const FilterCriteria& c);
    // Die weiße Liste des Player-Modus: HIER erweitern, wenn ein neuer Typ abspielbar wird - an genau EINER
    // Stelle, nicht verstreut über Filter und QML. `withVideo` kommt aus "Videos mitzeigen".
    static bool isPlayableType(MediaType t, bool withVideo);

    FilterCriteria criteria() const;
    QStringList activeCategoryNames() const;

    enum class Field { Date = 0, Name = 1, Tags = 2, FileSize = 3 };
    enum class TagMode { Or = 0, And = 1, Nur = 2, Inklusiv = 3 };

    explicit MediaProxyModel(QObject* parent = nullptr);

    // Merkt sich das Quellmodell zusätzlich typisiert (`m_src`): Filter und Sortierung greifen darüber direkt auf
    // die MediaItem-Structs zu, statt je Zeile QVariants zu bauen.
    void setSourceModel(QAbstractItemModel* source) override;

    void setTagManager(TagManager* mgr);

    int  count() const { return rowCount(); }

    int  sortFieldInt() const { return static_cast<int>(m_field); }
    void setSortFieldInt(int f);
    bool sortDescending() const { return m_descending; }
    void setSortDescending(bool d);

    bool showImages() const { return m_showImages; }  void setShowImages(bool v);
    bool showVideos() const { return m_showVideos; }  void setShowVideos(bool v);
    bool showAudio()  const { return m_showAudio;  }  void setShowAudio(bool v);
    bool onlyPlayable() const { return m_onlyPlayable; } void setOnlyPlayable(bool v);
    bool showPdfs()   const { return m_showPdfs;   }  void setShowPdfs(bool v);
    bool showTexts()  const { return m_showTexts;  }  void setShowTexts(bool v);
    bool showFolders() const { return m_showFolders; } void setShowFolders(bool v);

    QStringList tagFilter() const { return m_tagFilter; }
    void        setTagFilter(const QStringList& t);

    int  tagFilterModeInt() const { return static_cast<int>(m_mode); }
    void setTagFilterModeInt(int m);

    QStringList categoryFilter() const { return m_categoryFilter; }
    void        setCategoryFilter(const QStringList& ids);

    bool tagFilterAnd() const { return m_mode == TagMode::And; }
    void setTagFilterAnd(bool v);

    QString searchText() const { return m_search; }
    void    setSearchText(const QString& t);

    Q_INVOKABLE QString   filePathAt(int proxyRow) const;
    Q_INVOKABLE QString   displayNameAt(int proxyRow) const;
    Q_INVOKABLE int       mediaTypeAt(int proxyRow) const;
    Q_INVOKABLE QStringList tagsAt(int proxyRow) const;
    Q_INVOKABLE QDateTime dateTimeAt(int proxyRow) const;
    Q_INVOKABLE int       rowForPath(const QString& filePath) const;
    Q_INVOKABLE int       randomRow(int exceptRow = -1) const;
    // Nachbar-Zeile im Vollbild: bleibt IM ORDNER der Ausgangszeile und überspringt Ordnerkacheln - die sind keine
    // Datei. Läuft innerhalb dieses Ordners um; -1 = nichts anzusteuern.
    Q_INVOKABLE int       stepRow(int proxyRow, int delta) const;
    Q_INVOKABLE int       scopeAt(int proxyRow) const { return scopeOfProxyRow(proxyRow); }
    Q_INVOKABLE int       depthAt(int proxyRow) const;

    // Gehalten wird die Auswahl im `MediaModel`; hier liegen nur die Wege, die die SICHTBARE Ordnung brauchen -
    // ein Umschalt-Bereich und ein Auswahlrahmen meinen das, was man sieht, nicht die Modellzeilen.
    Q_INVOKABLE void selectRange(int fromProxyRow, int toProxyRow, bool additive);
    Q_INVOKABLE void selectAllVisible();
    Q_INVOKABLE QStringList selectedPaths(bool filesOnly = false) const;

    // `beginBand` merkt sich den Ausgangsstand, `updateBand` bekommt je Mausbewegung die überdeckten PROXY-Bereiche
    // als flache Liste und setzt die Auswahl auf Ausgangsstand plus Rahmen. Gemeldet wird nur der Unterschied.
    Q_INVOKABLE void beginBand(bool additive);
    Q_INVOKABLE void updateBand(const QVariantList& proxyRanges);
    Q_INVOKABLE void endBand();

signals:
    void countChanged();
    void sortChanged();
    void filterChanged();

protected:
    // `invalidateRowsFilter()` ist ab Qt 6.9 veraltet, das Projekt baut aber ab Qt 6.4 - deshalb EINE Stelle mit
    // Weiche statt neun Aufrufe mit `#if` drumherum.
    void refilterRows() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        beginFilterChange();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateRowsFilter();
#endif
    }

    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    void reapplySort();
    bool sameScopeLess(const MediaItem* a, const MediaItem* b) const;
    bool fieldLess(const MediaItem* a, const MediaItem* b) const;
    bool flatLessThan(const QModelIndex& left, const QModelIndex& right) const;
    int  scopeOfProxyRow(int proxyRow) const;
    bool isStepTarget(int proxyRow, int scope) const;
    void recomputeFilterCaches();        // effektive Tags + aktive Kategorie-Dateien
    void collectTagsForCategory(const QString& id, QSet<QString>& out) const;
    QVariant roleAt(int proxyRow, int role) const;

    QPointer<TagManager>  m_tagMgr;
    QPointer<MediaModel>  m_src;   // typisiertes Quellmodell (s. setSourceModel)

    Field   m_field      = Field::Date;
    bool    m_descending = true;
    TagMode m_mode       = TagMode::Or;

    bool m_showImages = true;
    bool m_showVideos = true;
    bool m_showAudio  = true;
    bool m_onlyPlayable = false;      // s. onlyPlayable()
    bool m_showPdfs   = true;
    bool m_showTexts  = true;
    bool m_showFolders = true;

    QString       m_search;           // Suchtext, bereits getrimmt
    mg::search::Pattern m_searchPattern;   // derselbe Text, uebersetzt
    QStringList   m_tagFilter;        // manuell gewählte Tags
    QStringList   m_categoryFilter;   // aktive Kategorie-IDs
    QSet<QString> m_effectiveTags;    // manuell ∪ Tags aktiver Kategorien (Cache)
    QSet<QString> m_activeCatIds;     // == m_categoryFilter als Set
    QSet<QString> m_activeCatFiles;   // Dateien direkt in aktiven Kategorien (Cache)
    //  Dieselbe Menge JE BEREICH: die Kategorien eines aufgeklappten
    //  Unterordners liegen in SEINEM Sidecar, unter gleichnamigen Knoten.
    QHash<int, QSet<QString>> m_catFilesByScope;

    QVector<int> m_bandBase;
};
