#pragma once
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

// ─────────────────────────────────────────────────────────────────────────────
//  MediaProxyModel - QSortFilterProxyModel vor dem MediaModel.
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
//  Performance (Filter/Tab wechseln):
//   Die Filter-Hotpath-Funktion filterAcceptsRow() läuft pro Zeile. Sämtliche
//   filterabhängigen Mengen werden daher EINMAL pro Filteränderung in Caches
//   vorberechnet (recomputeFilterCaches):
//     • m_effectiveTags  : manuelle Tags ∪ Tags aller aktiven Kategorien
//     • m_activeCatFiles : Dateinamen, die direkt einer aktiven Kategorie
//                          angehören.
//   Damit entfällt der frühere, pro Zeile ausgeführte rekursive Kategoriebaum-
//   Scan (TagManager::categoriesForFile) inkl. QStringList-Allokation; je Zeile
//   bleiben nur noch O(1)-Set-Lookups.
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
    //  Ordnerkacheln der Unterordner - eigener Eintrag in der Medien-Rubrik.
    Q_PROPERTY(bool showFolders    READ showFolders    WRITE setShowFolders    NOTIFY filterChanged)
    Q_PROPERTY(QStringList tagFilter      READ tagFilter      WRITE setTagFilter      NOTIFY filterChanged)
    Q_PROPERTY(int         tagFilterMode  READ tagFilterModeInt WRITE setTagFilterModeInt NOTIFY filterChanged)
    Q_PROPERTY(QStringList categoryFilter READ categoryFilter WRITE setCategoryFilter NOTIFY filterChanged)
    //  Freitextsuche der Filterleiste - UND-verknüpft mit allen anderen Filtern.
    Q_PROPERTY(QString     searchText     READ searchText     WRITE setSearchText     NOTIFY filterChanged)
    // Rückwärtskompatibel zu Phase 2: AND/OR-Umschalter (mappt auf den Modus).
    Q_PROPERTY(bool tagFilterAnd   READ tagFilterAnd   WRITE setTagFilterAnd   NOTIFY filterChanged)

public:
    // ─────────────────────────────────────────────────────────────────────────
    //  Das Filterurteil als ZUSTANDSLOSE Funktion über eine Momentaufnahme.
    //
    //  Grund: die rekursive Suche muss in einem Worker entscheiden, ob ein
    //  Ordner, den man gar nicht sieht, einen Treffer enthält. Täte sie das mit
    //  einer eigenen, „ungefähren" Regel, öffneten sich Ordner ohne sichtbaren
    //  Inhalt (leeres Band) oder Treffer blieben verborgen. Beide Seiten rufen
    //  deshalb dieselbe Funktion; sie liest NUR ihre Parameter und ist damit
    //  auch über Threadgrenzen unbedenklich.
    // ─────────────────────────────────────────────────────────────────────────
    struct FilterCriteria {
        QString       search;
        QSet<QString> tags;          // effektive Tags (manuell ∪ Kategorien)
        int           mode = 0;      // TagMode
        bool          categoryActive = false;
        QSet<QString> catFiles;      // Dateinamen in aktiven Kategorien DIESES Ordners
        bool showImages = true, showVideos = true, showAudio = true;
        bool showPdfs   = true, showTexts  = true, showFolders = true;
        //  Kein Filter aktiv ⇒ es gibt nichts zu suchen.
        bool isEmpty() const {
            return search.isEmpty() && tags.isEmpty() && !categoryActive;
        }
    };
    static bool acceptsFile(int mediaType, const QString& displayName,
                            const QString& fileName, const QStringList& tags,
                            const FilterCriteria& c);

    //  Momentaufnahme des aktuellen Filters (fuer die rekursive Suche).
    FilterCriteria criteria() const;
    //  NAMEN der aktiven Kategorien - die IDs eines fremden Ordners sind andere.
    QStringList activeCategoryNames() const;

    // Korrespondiert mit ISettings::SortField (Date/Name/Tags/FileSize).
    enum class Field { Date = 0, Name = 1, Tags = 2, FileSize = 3 };
    // Korrespondiert mit TagFilterMode (FilterBar): OR/AND/NUR/INKLUSIV.
    enum class TagMode { Or = 0, And = 1, Nur = 2, Inklusiv = 3 };

    explicit MediaProxyModel(QObject* parent = nullptr);

    // Merkt sich das Quellmodell zusaetzlich typisiert (m_src) - Filter und
    // Sortierung greifen darueber direkt auf die MediaItem-Structs zu, statt je
    // Zeile QVariants zu bauen. Faellt auf den QVariant-Pfad zurueck, falls je
    // ein anderes Quellmodell gesetzt wird.
    void setSourceModel(QAbstractItemModel* source) override;

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
    bool showFolders() const { return m_showFolders; } void setShowFolders(bool v);

    QStringList tagFilter() const { return m_tagFilter; }
    void        setTagFilter(const QStringList& t);

    int  tagFilterModeInt() const { return static_cast<int>(m_mode); }
    void setTagFilterModeInt(int m);

    QStringList categoryFilter() const { return m_categoryFilter; }
    void        setCategoryFilter(const QStringList& ids);

    bool tagFilterAnd() const { return m_mode == TagMode::And; }
    void setTagFilterAnd(bool v);

    //  Gesucht wird in ANZEIGENAME, DATEINAME und TAGS (Teilstring, ohne Rücksicht
    //  auf Groß-/Kleinschreibung) - nur im offenen Ordner, kein Dateiinhalt.
    QString searchText() const { return m_search; }
    void    setSearchText(const QString& t);

    // ── Navigations-Accessoren (für FullscreenViewer; in Proxy-Reihenfolge) ──
    Q_INVOKABLE QString   filePathAt(int proxyRow) const;
    Q_INVOKABLE QString   displayNameAt(int proxyRow) const;
    Q_INVOKABLE int       mediaTypeAt(int proxyRow) const;
    Q_INVOKABLE QStringList tagsAt(int proxyRow) const;
    Q_INVOKABLE QDateTime dateTimeAt(int proxyRow) const;
    Q_INVOKABLE int       rowForPath(const QString& filePath) const;
    Q_INVOKABLE int       randomRow(int exceptRow = -1) const;
    //  Nachbar-Zeile im Vollbild: `delta` = +1/−1. Bleibt IM ORDNER der
    //  Ausgangszeile (Unterordner-Inhalt blaettert nicht in den Elternordner
    //  hinueber) und ueberspringt Ordnerkacheln - die sind keine Datei.
    //  Laeuft innerhalb dieses Ordners um; −1 = nichts anzusteuern.
    Q_INVOKABLE int       stepRow(int proxyRow, int delta) const;
    //  Ordner-Bereich einer Proxy-Zeile (MediaItem::scope); −1 wenn unbekannt.
    //  Das Zeilenmodell bricht daran seine Zeilen um.
    Q_INVOKABLE int       scopeAt(int proxyRow) const { return scopeOfProxyRow(proxyRow); }
    //  Tiefe einer Proxy-Zeile (0 = geoeffneter Ordner) - die Ablegeleiste
    //  rueckt Unterordner danach ein.
    Q_INVOKABLE int       depthAt(int proxyRow) const;

signals:
    void countChanged();
    void sortChanged();
    void filterChanged();

protected:
    //  Zeilenfilter neu auswerten. `invalidateRowsFilter()` ist ab Qt 6.9
    //  veraltet (Ersatz: `beginFilterChange()` + `endFilterChange(Rows)`), das
    //  Projekt baut aber ab Qt 6.4 - deshalb EINE Stelle mit Weiche statt
    //  neun Aufrufe mit `#if` drumherum.
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
    //  Ordnung ZWISCHEN zwei Zeilen desselben Bereichs: Ordner zuerst, dann das
    //  gewaehlte Sortierfeld. Die Richtung (auf-/absteigend) wird HIER gedreht -
    //  s. lessThan().
    bool sameScopeLess(const MediaItem* a, const MediaItem* b) const;
    //  Vergleich nach dem Sortierfeld, immer aufsteigend.
    bool fieldLess(const MediaItem* a, const MediaItem* b) const;
    //  Fallback ohne MediaModel (fremdes Quellmodell): flach ueber QVariants.
    bool flatLessThan(const QModelIndex& left, const QModelIndex& right) const;
    //  Bereich (MediaItem::scope) einer PROXY-Zeile; −1 wenn unbekannt.
    int  scopeOfProxyRow(int proxyRow) const;
    //  Zaehlt eine Proxy-Zeile als Blaetter-Ziel? (Datei, gleicher Bereich)
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
    bool m_showPdfs   = true;
    bool m_showTexts  = true;
    bool m_showFolders = true;

    QString       m_search;           // Suchtext, bereits getrimmt
    QStringList   m_tagFilter;        // manuell gewählte Tags
    QStringList   m_categoryFilter;   // aktive Kategorie-IDs
    QSet<QString> m_effectiveTags;    // manuell ∪ Tags aktiver Kategorien (Cache)
    QSet<QString> m_activeCatIds;     // == m_categoryFilter als Set
    QSet<QString> m_activeCatFiles;   // Dateien direkt in aktiven Kategorien (Cache)
    //  Dieselbe Menge JE BEREICH: die Kategorien eines aufgeklappten
    //  Unterordners liegen in SEINEM Sidecar, unter gleichnamigen Knoten.
    QHash<int, QSet<QString>> m_catFilesByScope;
};
