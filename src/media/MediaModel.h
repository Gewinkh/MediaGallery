#pragma once
#include <QAbstractListModel>
#include <QDir>
#include <QVector>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <QTimer>
#include <QColor>
#include <QDateTime>
#include <QFileInfo>
#include <QSet>
#include <memory>
#include <atomic>
#include "media/MediaItem.h"
#include "media/MediaProxyModel.h"

class JsonStorage;
class TagManager;
class ThumbnailLoader;
class QFileSystemWatcher;
class QDirIterator;
class QThreadPool;

// Listenmodell der Galerie, hält nur MediaItem-Daten und keine Pixmaps. Befüllung inkrementell: erste Charge
// synchron, der Rest gechunkt über einen 0-ms-Timer, damit die ersten Kacheln auch bei 50k Dateien stehen.
class MediaModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int  count       READ count       NOTIFY countChanged)
    Q_PROPERTY(QString folder   READ folder      NOTIFY folderChanged)
    Q_PROPERTY(int  selectionCount READ selectionCount NOTIFY selectionChanged)
    // Zaehlt jede Auswahlaenderung hoch. selectionCount genuegt als Bindung nicht:
    // ein wandernder Auswahlrahmen kann gleich viele, aber andere Kacheln treffen.
    Q_PROPERTY(int  selectionRevision READ selectionRevision NOTIFY selectionChanged)

public:
    enum Roles {
        FilePathRole = Qt::UserRole + 1,
        FileNameRole,
        DisplayNameRole,
        MediaTypeRole,     // int (MediaType)
        TypeLabelRole,     // "MP4"/"MP3"/"PDF"/… - Badge-Text, sonst ""
        TagsRole,          // QStringList
        DateTimeRole,      // QDateTime (effektiv: custom > Dateidatum)
        FileSizeRole,      // qint64
        ThumbUrlRole,      // "file:///…" oder "" solange ausstehend
        ThumbStateRole,    // 0=pending/none, 1=ready, 2=failed
        OwnerFolderRole,   // Ordner, DEM die Zeile gehoert (nicht ihr Pfad)
        DepthRole,         // 0 = geoeffneter Ordner, 1 = erste Aufklapp-Ebene, …
        ExpandedRole,      // nur Ordnerzeilen: ist dieser Ordner aufgeklappt?
        ChildCountRole,    // nur Ordnerzeilen: Medien darin (−1 = noch ungezaehlt)
        SelectedRole       // Mehrfachauswahl der Galerie (bool)
    };

    // Geltungsbereich: der offene Ordner (Index 0) oder ein aufgeklappter Unterordner.
    // Bereiche werden nie entfernt, auch nicht beim Zuklappen - sonst verschoeben sich
    // alle Indizes und jede Zeile muesste umgeschrieben werden.
    struct FolderScope {
        QString path;            // absoluter Ordnerpfad
        QString sidecar;         // "<Ordnername>.json" - beim Einlesen uebergehen
        int     parent    = -1;  // Elternbereich; −1 nur fuer die Wurzel
        int     depth     = 0;   // 0 = geoeffneter Ordner
        int     folderRow = -1;  // Zeile der ORDNERKACHEL (−1 fuer die Wurzel)
        bool    active    = false;
    };

    explicit MediaModel(JsonStorage& storage,
                        TagManager& tagManager,
                        ThumbnailLoader& loader,
                        QObject* parent = nullptr);
    // Out-of-line: m_pendingIt ist ein unique_ptr auf den nur vorwaerts deklarierten
    // QDirIterator; der implizite Destruktor braeuchte hier den vollstaendigen Typ.
    ~MediaModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int     count()  const { return m_items.size(); }
    // Laeuft ein Grosseinlesen? GalleryRowModel fasst seine Neuaufbauten dann zusammen.
    bool    isFilling() const { return hasMoreToFill(); }
    QString folder() const { return m_folder; }

    // Direktzugriff fuer MediaProxyModel: Filter und Sortierung ueber data()/QVariant
    // wuerden je Zeile QStringList- und QDateTime-Kopien boxen.
    const MediaItem* itemAt(int row) const {
        return (row >= 0 && row < m_items.size()) ? &m_items.at(row) : nullptr;
    }

    int rowForPath(const QString& filePath) const;

    int scopeDepthOf(int scope) const;
    const MediaItem* folderItemOfScope(int scope) const;
    int     scopeParentOf(int scope) const;
    QString folderOfScope(int scope) const;
    bool isFileRow(int row) const;
    // Bereich 0: nur dort greifen m_tagManager und der Kategorienbaum der Seitenleiste.
    bool isRootFileRow(int row) const;
    void setTagOnRow(int row, const QString& tag, bool on);

    void loadFolder(const QString& rawFolderPath);
    void reload();   // aktuellen Ordner neu einlesen (Drop/Refresh/Watcher)
    // Sidecars der aufgeklappten Unterordner wurden auf der Platte geaendert: Kopien
    // wegwerfen und neu einlesen. Verwerfen ohne Speichern ist sicher - Unterordner
    // schreiben sofort.
    void dropScopeSidecars();

    void setShowAllFiles(bool v);
    bool showAllFiles() const { return m_showAllFiles; }

    // Aufgeklappter Inhalt kommt als weitere Zeilen desselben Modells (eigener Bereich).
    // m_expanded haelt Pfade statt Bereiche: der Zustand ueberlebt ein reload(), und
    // Zuklappen vergisst die Enkel nicht.
    Q_INVOKABLE bool expandFolder(const QString& folderPath);
    Q_INVOKABLE bool collapseFolder(const QString& folderPath);
    Q_INVOKABLE bool toggleFolder(const QString& folderPath);
    Q_INVOKABLE bool isFolderExpanded(const QString& folderPath) const {
        return m_expanded.contains(folderPath);
    }
    Q_INVOKABLE void collapseAll();

    // Zaehlt asynchron - ein Verzeichnis kann Zehntausende Eintraege haben.
    // Ergebnis kommt als ChildCountRole.
    Q_INVOKABLE void ensureFolderCount(const QString& folderPath);
    // -1 = noch nicht gezaehlt.
    Q_INVOKABLE int  folderCount(const QString& folderPath) const {
        return m_folderCounts.value(folderPath, -1);
    }

    // Alle drei lesen den Ordner danach neu ein: billiger als Bereichstabelle,
    // Aufklapp-Zustand und Beobachtung einzeln nachzuziehen.
    // Rueckgabe: 0 ok, 1 Name unbrauchbar, 2 existiert, 3 fehlgeschlagen.
    Q_INVOKABLE int  createFolder(const QString& parentFolder, const QString& name);
    Q_INVOKABLE int  renameFolder(const QString& folderPath, const QString& newName);
    Q_INVOKABLE bool deleteFolder(const QString& folderPath);

    // Die Filterleiste muss zeigen, was sichtbar IST: Tags des offenen Ordners und aller aufgeklappten vereinigt.
    // Geurteilt wird mit `MediaProxyModel::acceptsFile`, damit Suche und Anzeige dieselbe Regel benutzen.
    void applyDeepFilter(const MediaProxyModel::FilterCriteria& c,
                         const QStringList& categoryNames);
    // Nur vom Worker, ueber die Ereignisschleife.
    void noteDeepMatches(const QStringList& folders, int generation);
    bool deepFilterActive() const { return m_deepActive; }
    // Nur Ordner auf dem Weg zu einem Treffer duerfen bei aktiver Suche offen bleiben.
    bool isOnDeepChain(const QString& folderPath) const {
        return m_deepChain.contains(folderPath);
    }

    Q_INVOKABLE QStringList visibleTags() const;
    // Erst der offene Ordner, dann die aufgeklappten; ungueltig = unbekannt.
    Q_INVOKABLE QColor      visibleTagColor(const QString& tag) const;
    // Je aufgeklapptem Bereich die Dateinamen der gleichnamigen Kategorie.
    // Bereich 0 fuellt der Proxy selbst - er hat den TagManager.
    void fillCategoryFilesByScope(const QStringList& categoryNames,
                                  QHash<int, QSet<QString>>& out) const;
    // Nur vom Worker, ueber die Ereignisschleife.
    void noteFolderCount(const QString& folderPath, int count, int generation, int ticket);
    Q_INVOKABLE QStringList expandedFolders() const;
    Q_INVOKABLE void        setExpandedFolders(const QStringList& folderPaths);

    // Alle Thumbnails auf ausstehend zuruecksetzen; sichtbare Delegates fordern
    // ueber thumbnailsInvalidated neu an, der Rest bleibt lazy.
    void refreshThumbnails();

    // Abbestellen ist verzögert und läuft nur, wenn den Pfad im selben Ereignisdurchlauf niemand angefordert hat.
    // Sonst ginge die Miniatur verloren, wenn eine Datei beim Neuaufbau von Kachel A zu Kachel B wandert.
    Q_INVOKABLE void ensureThumbnail(const QString& filePath);
    Q_INVOKABLE void cancelThumbnail(const QString& filePath);   // weggescrollte Kachel
    Q_INVOKABLE void renameItem(const QString& filePath, const QString& newBaseName);
    // In den Papierkorb; raeumt Editor-Sidecar und persistierte Metadaten mit ab.
    Q_INVOKABLE bool deleteItem(const QString& filePath);

    // Begleitdateien einer Datei: Editor-Sidecar und DOCX-Sicherungskopie.
    // Bitmaske: 1 = Sidecar, 2 = Sicherungskopie.
    Q_INVOKABLE int  companionKinds(const QString& filePath) const;
    Q_INVOKABLE bool removeCompanion(const QString& filePath, int kind);
    // Routet auf das Sidecar des Ordners, dem die Datei GEHOERT - nicht auf den
    // offenen Ordner. Kennt das Modell die Datei nicht, passiert nichts.
    Q_INVOKABLE QStringList tagsOfFile(const QString& filePath) const;
    Q_INVOKABLE void        removeTag(const QString& filePath, const QString& tag);
    // Beantwortet aus der Datei: Aenderungs- weicht von Erstellungsdatum ab.
    Q_INVOKABLE bool        hasCustomDate(const QString& filePath) const;
    Q_INVOKABLE QDateTime   customDate(const QString& filePath) const;
    Q_INVOKABLE void        setCustomDate(const QString& filePath, const QDateTime& dt);
    Q_INVOKABLE void        clearCustomDate(const QString& filePath);
    // Ungueltig = keine eigene Wahl, Aufrufer nimmt App.textPdfColor.
    Q_INVOKABLE QColor      fileTextPdfColor(const QString& filePath) const;
    Q_INVOKABLE bool        hasFileTextPdfColor(const QString& filePath) const;
    Q_INVOKABLE void        setFileTextPdfColor(const QString& filePath, const QColor& c);
    Q_INVOKABLE void        clearFileTextPdfColor(const QString& filePath);

    Q_INVOKABLE void toggleTag(const QString& filePath, const QString& tag);
    // Nur hinzufuegen, nie entfernen: ein Zug auf einen Tag ist eine Zuweisung,
    // kein Umschalter.
    Q_INVOKABLE void addTag(const QString& filePath, const QString& tag);
    // isInLoadedFolder: Kategorien merken sich Dateinamen im Sidecar DIESES Ordners;
    // der Name einer fremden Datei bliebe dort als Waise liegen.
    // hiddenFlag: QDir::Hidden oder nichts, je nach Einstellung.
    static QDir::Filters hiddenFlag();

    Q_INVOKABLE bool ownsFile(const QString& filePath) const {
        return rowForPath(filePath) >= 0;
    }
    Q_INVOKABLE bool hasFile(const QString& filePath) const {
        return isRootFileRow(rowForPath(filePath));
    }

    // Auswahl liegt hier, nicht in QML: sie wird je sichtbarer Kachel gelesen und von mehreren Stellen geschrieben.
    // Als Parallelvektor - ein bool im Struct kostete wegen der Ausrichtung acht Byte je Zeile.
    int  selectionCount() const { return m_selCount; }
    int  selectionRevision() const { return m_selRevision; }
    Q_INVOKABLE bool isSelected(const QString& filePath) const;
    Q_INVOKABLE void setSelected(const QString& filePath, bool on);
    Q_INVOKABLE void toggleSelected(const QString& filePath);
    Q_INVOKABLE void clearSelection();
    // filesOnly laesst Ordner weg - die sind weder ziehbar noch kopierbar.
    Q_INVOKABLE QStringList selectedPaths(bool filesOnly = false) const;
    Q_INVOKABLE QStringList selectedFileNames() const;
    // Schnittmenge - daran haengt das Haekchen im Kontextmenue.
    Q_INVOKABLE QStringList tagsOfSelection() const;
    Q_INVOKABLE void setTagOnSelection(const QString& tag, bool on);
    // Meldet nur tatsaechliche Aenderungen: der Auswahlrahmen ruft das je Mausbewegung.
    void setSelectedRows(const QVector<int>& sortedRows);
    QVector<int> selectedRows() const;

    // Ein Schritt auf dem Undo-Stapel; Rueckgabe: geloeschte Dateien.
    Q_INVOKABLE int deleteSelected();

    // Gelöscht wird in den Papierkorb - das macht den Rückweg möglich; der Stapel lebt nur für die Sitzung.
    // `collision`: 0 fragen, 1 ersetzen, 2 umbenennen. Beim Verschieben wandern Tags, Kategorie und Datum mit.
    Q_INVOKABLE int     transferToFolder(const QString& filePath, const QString& destFolder,
                                         bool move, int collision = 0);
    Q_INVOKABLE QString transferTargetName(const QString& filePath,
                                           const QString& destFolder) const;

    Q_INVOKABLE bool    undoFileOp();
    Q_INVOKABLE bool    redoFileOp();
    Q_INVOKABLE QString undoFileOpName() const;
    Q_INVOKABLE QString redoFileOpName() const;
    // 1 bei Einzelvorgang, N bei geloeschter Mehrfachauswahl.
    Q_INVOKABLE int     undoFileOpCount() const;
    Q_INVOKABLE int     redoFileOpCount() const;

signals:
    void countChanged();
    void selectionChanged();
    void folderChanged();
    void folderContentsChanged();   // externe Änderung (für Statusmeldung)
    void thumbnailsInvalidated();   // Zielgröße gewechselt -> Delegates fordern neu an
    void fileHistoryChanged();      // Undo-/Redo-Stapel der Datei-Vorgänge
    void expansionChanged();        // ein Unterordner wurde auf-/zugeklappt
    // Datum liess sich nicht an die Datei schreiben, steht nur im Sidecar.
    void fileDateNotWritten(const QString& fileName);

private slots:
    void onThumbnailReady(const QString& filePath, const QString& thumbUrl);
    void onThumbnailFailed(const QString& filePath);
    void onDirectoryChanged();

private:
    // trashPath leer = kein Papierkorb vorhanden; so einer kommt nicht auf den Stapel.
    struct FileOp {
        // Companion = eine Begleitdatei allein, path ist dann die Begleitdatei.
        // Folder = ganzer Ordner; braucht keine Metadaten, die liegen in seinem Sidecar.
        enum class Kind { Delete, Move, Companion, Folder };
        Kind        kind = Kind::Delete;
        // 0 = Einzelvorgang; gleiche Nummer geht als ein Schritt zurueck.
        int         group = 0;
        QString     path;                 // ursprünglicher Pfad im Ordner
        QString     movedTo;              // nur Kind::Move: neuer Pfad
        QString     trashPath;            // Ablage im Papierkorb
        QString     sidecarPath;          // "<pfad>.mgedit.json" (falls vorhanden)
        QString     sidecarTrashPath;
        QString     bakPath;
        QString     bakTrashPath;
        QStringList tags;
        QStringList categoryIds;          // DIREKTE Mitgliedschaften (IDs, eigener Ordner)
        QStringList categoryNames;        // dieselben als NAMEN (für einen fremden Ordner)
        // Kein Datum: es haengt an der Datei und wandert mit ihr.
    };
    static constexpr int kMaxFileOps = 50;

    bool trashFile(const QString& filePath, FileOp* op);  // Datei + Metadaten weg
    bool restoreFile(const FileOp& op);                   // Papierkorb -> Ordner
    bool restoreFolder(const FileOp& op, bool reloadNow = true);  // Papierkorb -> Ordner (Verzeichnis)
    void collectMeta(const QString& fileName, FileOp* op) const;
    void dropMeta(const QString& fileName, const FileOp& op);
    void restoreMeta(const QString& fileName, const FileOp& op);
    // Eigene, kurzlebige JsonStorage-Instanz - die laufende gehoert dem offenen Ordner.
    static void writeMetaToFolder(const QString& folder, const QString& fileName,
                                  const FileOp& op, const QHash<QString, QColor>& tagColors);
    static void removeMetaFromFolder(const QString& folder, const QString& fileName);
    // Praefix-Ersetzung mitsamt Enkeln.
    void remapExpanded(const QString& oldPath, const QString& newPath);
    bool isVisibleFolder(const QString& folderPath) const;
    bool pushUndo(const FileOp& op);                      // Stapel + Signal
    bool undoMove(const FileOp& op);                      // Verschiebung zurücknehmen
    void appendRowFor(const QString& filePath);           // Zeile ans Ende (Proxy sortiert)
    bool dropRowFor(const QString& filePath);             // Zeile gezielt entfernen
    // O(n), aber nur bei Strukturaenderungen - die kosten ohnehin rebuildPathIndex.
    void recountSelection();
    // Immer hierueber, nie emit selectionChanged() von Hand: die Kacheln haengen an
    // der Fortschreibungszahl.
    void noteSelectionChanged();
    // reloadNow == false: deleteSelected liest nach mehreren Ordnern einmal neu ein.
    bool trashFolderAt(const QString& folderPath, bool reloadNow);
    void clearFileHistory();

    void rebuild(const QString& folderPath);   // startet inkrementelle Befüllung
    void feedChunk(bool firstChunk);           // eine Charge Zeilen einspeisen
    void finishFill();                         // Aufräumen nach letzter Charge
    void emitRow(int row, const QVector<int>& roles);
    void emitRows(int first, int last, const QVector<int>& roles);

    // Bereich besorgen (reaktivieren oder anlegen) und Inhalt vormerken.
    int  ensureScope(const QString& folderPath, int parentScope, int folderRow);
    void startScan(int scope);                 // Iterator fuer EINEN Bereich
    bool hasMoreToFill() const;
    // Greift auch nach reload() und beim Wiederaufklappen eines Unterbaums.
    void queueExpandedFolders(int firstRow);
    void collectDescendantScopes(int scope, QSet<int>& out) const;
    void removeRowsOfScopes(const QSet<int>& scopes);
    void rebuildPathIndex();                   // m_pathToRow + FolderScope::folderRow
    QString sidecarOfScope(int scope) const;

    static QString typeLabel(const MediaItem& item);

    JsonStorage&      m_storage;
    TagManager&       m_tagManager;
    ThumbnailLoader&  m_loader;

    QVector<MediaItem>     m_items;       // reine Daten, keine Pixmaps
    QVector<QString>       m_thumbUrls;   // parallel: Cache-URL je Zeile ("" = none)
    QVector<int>           m_thumbState;  // parallel: 0/1/2
    // quint8 statt bool im Struct - s. selectionCount().
    QVector<quint8>        m_selected;
    int                    m_selCount = 0;
    int                    m_selRevision = 0;
    int                    m_opGroup = 0;
    QHash<QString, int>    m_pathToRow;   // schnelle Adressierung für Updates

    // Streamend statt als Liste: der Iterator haelt immer nur einen Eintrag.
    // Es laeuft immer nur einer - sonst haette jeder Ordner einen eigenen
    // Verzeichnis-Deskriptor offen gehalten.
    std::unique_ptr<QDirIterator> m_pendingIt;   // nullptr = kein Iterator aktiv
    int           m_pendingScope = -1;      // Bereich des laufenden Iterators
    QList<int>    m_scanQueue;              // wartende Bereiche
    bool          m_showAllFiles = false;   // s. setShowAllFiles
    QTimer        m_fillTimer;        // 0-ms-Timer: speist Chargen, gibt dazwischen ab
    // Vorgemerkte Abbestellungen und der 0-ms-Timer, der sie ausfuehrt.
    bool          m_pendingInvalidate = false;
    QSet<QString> m_cancelPending;
    // Was in diesem Durchlauf angefordert wurde; wird mit den Vormerkungen geleert.
    QSet<QString> m_thumbWanted;
    QTimer        m_cancelTimer;

    // Der offene Ordner behaelt m_storage. Jeder aufgeklappte Unterordner bekommt eine
    // lazy erzeugte Instanz auf sein eigenes Sidecar; beim Zuklappen wird sie
    // abgeraeumt - ein Sidecar kann gross sein.
    QHash<int, JsonStorage*> m_scopeStorage;   // nur Bereiche > 0
    JsonStorage* storageForScope(int scope);
    // nullptr, wenn der Ordner nicht offen ist - dann s. writeMetaToFolder.
    JsonStorage* storageForFolder(const QString& folder);
    JsonStorage* storageOfFile(const QString& filePath, int* row = nullptr);
    // Bereich 0 laeuft ueber m_tagManager, damit die Seitenleiste es mitbekommt.
    void collectMetaAt(const QString& folder, const QString& fileName, FileOp* op) const;
    void dropMetaAt(const QString& folder, const QString& fileName, const FileOp& op);
    void restoreMetaAt(const QString& folder, const QString& fileName, const FileOp& op);
    // Ueber Namen, nicht IDs: die eines fremden Ordners sind andere.
    static QStringList categoryNamesOf(JsonStorage& st, const QString& fileName);
    static void        attachCategories(JsonStorage& st, const QString& fileName,
                                        const QStringList& names);
    static void        stripCategories(JsonStorage& st, const QString& fileName);

    QVector<FolderScope> m_scopes;        // [0] = geoeffneter Ordner
    QHash<QString, int>  m_scopeOfPath;   // Ordnerpfad -> Bereichs-Index
    QSet<QString>        m_expanded;      // aufgeklappte Ordner (Wahrheitsquelle)

    // Aufklapp-Gedaechtnis je Ordner, nur fuer die Sitzung. Nicht auf Platte: das
    // schriebe bei jedem Auf- und Zuklappen in einen fremden Ordner.
    static constexpr int kMaxFolderMemory = 32;
    // Die Generation verwirft Antworten zu einem frueheren Ordnerstand.
    QHash<QString, int> m_folderCounts;
    // Ueberlebt einen Neuaufbau - die Kachel fragt danach nicht erneut.
    QSet<QString>       m_countWanted;
    QSet<QString>       m_countPending;
    int                 m_countGeneration = 0;
    // Je Ordner eine Marke: steigt, sobald ein Stand ungueltig wird, und verwirft nur
    // dessen laufenden Auftrag statt aller (das taete die globale Generation).
    QHash<QString, int> m_countTicket;
    void invalidateFolderCount(const QString& folderPath);
    QThreadPool*        m_countPool = nullptr;

    bool         m_deepActive = false;     // laeuft gerade eine gefilterte Ansicht?
    QStringList  m_deepSnapshot;           // Aufklapp-Zustand VOR der Suche
    // Treffer-Ordner und ihre Kette - ohne die von Hand geoeffneten.
    QSet<QString> m_deepChain;
    int          m_deepGeneration = 0;
    QThreadPool* m_deepPool = nullptr;
    QTimer       m_deepTimer;              // entprellt das Tippen
    // Nur fuer MG_DEEPLOG=1; ungueltig, solange nicht gemessen wird.
    QElapsedTimer m_deepFillTimer;
    MediaProxyModel::FilterCriteria m_deepCriteria;
    QStringList  m_deepCategoryNames;
    std::shared_ptr<std::atomic<bool>> m_deepCancel;
    void startDeepScan();

    QHash<QString, QStringList> m_expandedMemory;
    QStringList                 m_memoryOrder;   // aeltester vorn (Deckel)
    void rememberExpansion(const QString& folderPath);

    QString             m_folder;
    QFileSystemWatcher* m_watcher;
    QTimer              m_watchDebounce;
    int                 m_suppressWatch = 0;  // >0 -> Watcher-Reload ignorieren

    QVector<FileOp>     m_undoOps;        // zuletzt gelöscht = hinten
    QVector<FileOp>     m_redoOps;        // zurückgeholt, kann erneut gelöscht werden
};
