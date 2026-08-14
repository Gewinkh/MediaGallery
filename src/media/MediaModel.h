#pragma once
#include <QAbstractListModel>
#include <QVector>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QColor>
#include <QDateTime>
#include <QFileInfo>
#include <memory>
#include "media/MediaItem.h"

class JsonStorage;
class TagManager;
class ThumbnailLoader;
class QFileSystemWatcher;
class QDirIterator;

// ─────────────────────────────────────────────────────────────────────────────
//  MediaModel — QAbstractListModel (Phase 2/3, RAM-kritisch).
//
//  Hält reine MediaItem-DATEN (KEINE QPixmaps, KEINE Widgets, 1 Struct/Datei).
//  QML liest über Rollen; es werden keine Datenkopien nach QML geschoben.
//  Thumbnails kommen als "file:///...".URL aus dem Disk-Cache des ThumbnailLoader
//  und werden pro Zeile lazy (sichtbarkeitsgesteuert via ensureThumbnail) gefüllt;
//  Updates laufen über dataChanged(ThumbUrlRole).
//
//  Performance (Ordner öffnen):
//   Statt eines einzigen beginResetModel/endResetModel über den GESAMTEN Ordner
//   wird INKREMENTELL befüllt: ein leeres Modell wird sofort publiziert, danach
//   werden Zeilen in Chargen (beginInsertRows) eingespeist — die erste Charge
//   synchron (Viewport sofort sichtbar), der Rest gechunkt über einen 0-ms-Timer,
//   der zwischen den Chargen an die Event-Loop zurückgibt. Dadurch erscheinen die
//   ersten Kacheln nahezu sofort, auch bei 10–50k Dateien, statt erst nach der
//   kompletten Enumeration.
//
//  Mutationen werden per Dateipfad adressiert (robust gegen Proxy-Sortierung/
//  Filterung): renameItem / toggleTag suchen die Zeile über einen Pfad→Row-Hash.
//
//  Ein QFileSystemWatcher beobachtet den Ordner und löst (entprellt) ein Reload
//  aus; interne Mutationen unterdrücken diesen Reload kurzzeitig.
// ─────────────────────────────────────────────────────────────────────────────
class MediaModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int  count       READ count       NOTIFY countChanged)
    Q_PROPERTY(QString folder   READ folder      NOTIFY folderChanged)

public:
    enum Roles {
        FilePathRole = Qt::UserRole + 1,
        FileNameRole,
        DisplayNameRole,
        MediaTypeRole,     // int (MediaType)
        TypeLabelRole,     // "MP4"/"MP3"/"PDF"/… — Badge-Text, sonst ""
        TagsRole,          // QStringList
        DateTimeRole,      // QDateTime (effektiv: custom > Dateidatum)
        FileSizeRole,      // qint64
        ThumbUrlRole,      // "file:///…" oder "" solange ausstehend
        ThumbStateRole     // 0=pending/none, 1=ready, 2=failed
    };

    explicit MediaModel(JsonStorage& storage,
                        TagManager& tagManager,
                        ThumbnailLoader& loader,
                        QObject* parent = nullptr);
    //  Out-of-line: m_pendingIt ist ein unique_ptr auf den nur VORWÄRTS
    //  deklarierten QDirIterator — der implizite Destruktor bräuchte hier den
    //  vollständigen Typ und würde <QDirIterator> in jede einbindende
    //  Übersetzungseinheit ziehen.
    ~MediaModel() override;

    // QAbstractListModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int     count()  const { return m_items.size(); }
    QString folder() const { return m_folder; }

    // Direktzugriff auf die Quelldaten einer Zeile (nullptr bei ungültigem Index).
    // Ausschliesslich fuer MediaProxyModel: Filter und Sortierung laufen ueber
    // ALLE Zeilen und wuerden ueber data()/QVariant je Zeile QStringList- und
    // QDateTime-Kopien in QVariants boxen. Ueber die Struct-Referenz entfaellt
    // das vollstaendig (gleiche Semantik, da beide im GUI-Thread laufen).
    const MediaItem* itemAt(int row) const {
        return (row >= 0 && row < m_items.size()) ? &m_items.at(row) : nullptr;
    }

    // Quellzeile zu einem Dateipfad (O(1) ueber den Pfad→Zeile-Hash), −1 wenn
    // nicht vorhanden. Auch von MediaProxyModel::rowForPath genutzt.
    int rowForPath(const QString& filePath) const;

    // ── Ordner-Steuerung (von AppController-Signalen getrieben) ──────────────
    void loadFolder(const QString& folderPath);
    void reload();   // aktuellen Ordner neu einlesen (Drop/Refresh/Watcher)

    // Alle Thumbnails auf „ausstehend" zurücksetzen (z. B. nach einem Wechsel
    // der Thumbnail-Zielgröße): sichtbare Delegates fordern via
    // thumbnailsInvalidated → ensureThumbnail neu an; der Rest bleibt lazy.
    void refreshThumbnails();

    // ── QML-Invokables (per Dateipfad) ───────────────────────────────────────
    Q_INVOKABLE void ensureThumbnail(const QString& filePath);
    Q_INVOKABLE void cancelThumbnail(const QString& filePath);   // weggescrollte Kachel
    Q_INVOKABLE void renameItem(const QString& filePath, const QString& newBaseName);
    //  Datei löschen (Kontextmenü): verschiebt in den PAPIERKORB (reversibel;
    //  Fallback: endgültig, falls das System keinen Papierkorb bietet), räumt
    //  ein evtl. PDF-Editor-Sidecar (<pfad>.mgedit.json) und die persistierten
    //  Metadaten (Tags/Datum) mit ab und entfernt die Zeile aus dem Modell.
    Q_INVOKABLE bool deleteItem(const QString& filePath);
    Q_INVOKABLE void toggleTag(const QString& filePath, const QString& tag);
    //  Tag NUR hinzufügen (nie entfernen) — für das Ablegen einer Kachel auf
    //  einem Tag: ein Zug ist eine Zuweisung, kein Umschalter. Liegt der Tag
    //  schon an, passiert nichts.
    Q_INVOKABLE void addTag(const QString& filePath, const QString& tag);
    //  Gehört die Datei zum aktuell geladenen Ordner? Die Seitenleiste fragt das,
    //  bevor sie eine gezogene Datei einer Kategorie zuordnet: Kategorien merken
    //  sich DATEINAMEN im Sidecar DIESES Ordners — der Name einer fremden Datei
    //  bliebe dort als Waise liegen. `addTag` prüft das intern selbst.
    Q_INVOKABLE bool hasFile(const QString& filePath) const { return rowForPath(filePath) >= 0; }

    // ── Rückholbare Datei-Vorgänge (Galerie-Undo) ────────────────────────────
    //  Für das Dateisystem gab es bisher kein Undo: ein Fehlgriff war endgültig.
    //  Gelöscht wird in den PAPIERKORB — genau das macht den Rückweg möglich.
    //  Der Stapel lebt nur für die SITZUNG und nur für den offenen Ordner (ein
    //  Ordnerwechsel leert ihn): eine Rücknahme in einen Ordner, den man gerade
    //  nicht sieht, wäre nicht nachvollziehbar. Mitgesichert werden Tags,
    //  Kategorien-Mitgliedschaften und ein eigenes Datum — sie verschwinden beim
    //  Löschen mit und müssen beim Zurückholen wieder da sein.
    // ── Kachel auf ein LESEZEICHEN gezogen: verschieben oder kopieren ────────
    //  `collision`: 0 = fragen (Rückgabe 1, wenn der Name schon vergeben ist),
    //  1 = ersetzen, 2 = umbenennen („Name (2)").
    //  Rückgabe: 0 = erledigt · 1 = Namenskollision (Aufrufer fragt nach) ·
    //  2 = nicht möglich (fremder Pfad, Zielordner fehlt, gleicher Ordner, I/O).
    //  Beim VERSCHIEBEN wandern Tags, Kategorie-Mitgliedschaft und eigenes Datum
    //  mit in den Zielordner (die Zuordnungen liegen JE ORDNER im Sidecar);
    //  beim KOPIEREN nicht — dort entsteht drüben eine unverschlagwortete Kopie
    //  (Festlegung des Nutzers).
    Q_INVOKABLE int     transferToFolder(const QString& filePath, const QString& destFolder,
                                         bool move, int collision = 0);
    //  Wie hieße die Datei drüben (mit „ (2)" bei Kollision)? Für die Rückfrage.
    Q_INVOKABLE QString transferTargetName(const QString& filePath,
                                           const QString& destFolder) const;

    Q_INVOKABLE bool    undoFileOp();
    Q_INVOKABLE bool    redoFileOp();
    //  Dateiname des jeweils nächsten Schrittes ("" = nichts vorhanden) — die
    //  Oberfläche baut daraus ihre Meldung.
    Q_INVOKABLE QString undoFileOpName() const;
    Q_INVOKABLE QString redoFileOpName() const;

signals:
    void countChanged();
    void folderChanged();
    void folderContentsChanged();   // externe Änderung (für Statusmeldung)
    void thumbnailsInvalidated();   // Zielgröße gewechselt → Delegates fordern neu an
    void fileHistoryChanged();      // Undo-/Redo-Stapel der Datei-Vorgänge

private slots:
    void onThumbnailReady(const QString& filePath, const QString& thumbUrl);
    void onThumbnailFailed(const QString& filePath);
    void onDirectoryChanged();

private:
    //  Ein rückholbarer Datei-Vorgang (heute: Löschen). `trashPath` leer heißt:
    //  das System hat keinen Papierkorb geboten, die Datei ist endgültig weg —
    //  ein solcher Vorgang kommt gar nicht erst auf den Stapel.
    struct FileOp {
        //  Löschen (Papierkorb) ODER Verschieben in einen anderen Ordner —
        //  beide sind rückholbar und teilen sich denselben Stapel.
        enum class Kind { Delete, Move };
        Kind        kind = Kind::Delete;
        QString     path;                 // ursprünglicher Pfad im Ordner
        QString     movedTo;              // nur Kind::Move: neuer Pfad
        QString     trashPath;            // Ablage im Papierkorb
        QString     sidecarPath;          // "<pfad>.mgedit.json" (falls vorhanden)
        QString     sidecarTrashPath;
        QStringList tags;
        QStringList categoryIds;          // DIREKTE Mitgliedschaften (IDs, eigener Ordner)
        QStringList categoryNames;        // dieselben als NAMEN (für einen fremden Ordner)
        QDateTime   customDate;
        bool        hasCustomDate = false;
    };
    //  Deckel gegen unbegrenztes Wachstum (RAM = Priorität 1): der Stapel hält
    //  nur Pfade und Metadaten, aber er soll auch bei Massenlöschungen nicht
    //  ungebremst wachsen.
    static constexpr int kMaxFileOps = 50;

    bool trashFile(const QString& filePath, FileOp* op);  // Datei + Metadaten weg
    bool restoreFile(const FileOp& op);                   // Papierkorb → Ordner
    //  Metadaten einer Datei aus dem OFFENEN Ordner einsammeln bzw. entfernen —
    //  gemeinsame Grundlage von Löschen und Verschieben.
    void collectMeta(const QString& fileName, FileOp* op) const;
    void dropMeta(const QString& fileName, const FileOp& op);
    void restoreMeta(const QString& fileName, const FileOp& op);
    //  Metadaten in den Sidecar eines FREMDEN Ordners schreiben bzw. daraus
    //  entfernen (eigene, kurzlebige JsonStorage-Instanz — die laufende gehört
    //  dem offenen Ordner und darf dabei nicht umgeschaltet werden).
    static void writeMetaToFolder(const QString& folder, const QString& fileName,
                                  const FileOp& op, const QHash<QString, QColor>& tagColors);
    static void removeMetaFromFolder(const QString& folder, const QString& fileName);
    bool pushUndo(const FileOp& op);                      // Stapel + Signal
    bool undoMove(const FileOp& op);                      // Verschiebung zurücknehmen
    void appendRowFor(const QString& filePath);           // Zeile ans Ende (Proxy sortiert)
    bool dropRowFor(const QString& filePath);             // Zeile gezielt entfernen
    void clearFileHistory();

    void rebuild(const QString& folderPath);   // startet inkrementelle Befüllung
    void feedChunk(bool firstChunk);           // eine Charge Zeilen einspeisen
    void finishFill();                         // Aufräumen nach letzter Charge
    void emitRow(int row, const QVector<int>& roles);

    static QString typeLabel(const MediaItem& item);

    JsonStorage&      m_storage;
    TagManager&       m_tagManager;
    ThumbnailLoader&  m_loader;

    QVector<MediaItem>     m_items;       // reine Daten, keine Pixmaps
    QVector<QString>       m_thumbUrls;   // parallel: Cache-URL je Zeile ("" = none)
    QVector<int>           m_thumbState;  // parallel: 0/1/2
    QHash<QString, int>    m_pathToRow;   // schnelle Adressierung für Updates

    // ── Inkrementelle Befüllung ──────────────────────────────────────────────
    //  STREAMEND statt als Liste: der Iterator hält immer nur EINEN Eintrag,
    //  jede Charge liest genau so viele, wie sie einspeist (s. rebuild()).
    std::unique_ptr<QDirIterator> m_pendingIt;   // nullptr = keine Befüllung aktiv
    QString       m_pendingSidecar;   // "<Ordner>.json" → überspringen
    QTimer        m_fillTimer;        // 0-ms-Timer: speist Chargen, gibt dazwischen ab

    QString             m_folder;
    QFileSystemWatcher* m_watcher;
    QTimer              m_watchDebounce;
    int                 m_suppressWatch = 0;  // >0 → Watcher-Reload ignorieren

    QVector<FileOp>     m_undoOps;        // zuletzt gelöscht = hinten
    QVector<FileOp>     m_redoOps;        // zurückgeholt, kann erneut gelöscht werden
};
