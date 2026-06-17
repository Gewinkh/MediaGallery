#pragma once
#include <QAbstractListModel>
#include <QVector>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QFileInfo>
#include "MediaItem.h"

class JsonStorage;
class TagManager;
class ThumbnailLoader;
class QFileSystemWatcher;

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

    // QAbstractListModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int     count()  const { return m_items.size(); }
    QString folder() const { return m_folder; }

    // ── Ordner-Steuerung (von AppController-Signalen getrieben) ──────────────
    void loadFolder(const QString& folderPath);
    void reload();   // aktuellen Ordner neu einlesen (Drop/Refresh/Watcher)

    // ── QML-Invokables (per Dateipfad) ───────────────────────────────────────
    Q_INVOKABLE void ensureThumbnail(const QString& filePath);
    Q_INVOKABLE void cancelThumbnail(const QString& filePath);   // weggescrollte Kachel
    Q_INVOKABLE void renameItem(const QString& filePath, const QString& newBaseName);
    Q_INVOKABLE void toggleTag(const QString& filePath, const QString& tag);

signals:
    void countChanged();
    void folderChanged();
    void folderContentsChanged();   // externe Änderung (für Statusmeldung)

private slots:
    void onThumbnailReady(const QString& filePath, const QString& thumbUrl);
    void onThumbnailFailed(const QString& filePath);
    void onDirectoryChanged();

private:
    void rebuild(const QString& folderPath);   // startet inkrementelle Befüllung
    void feedChunk(bool firstChunk);           // eine Charge Zeilen einspeisen
    void finishFill();                         // Aufräumen nach letzter Charge
    void rebuildIndex();                       // Pfad→Row neu aufbauen (Mutationen)
    int  rowForPath(const QString& filePath) const;
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
    QFileInfoList m_pendingEntries;   // noch nicht eingespeiste Verzeichniseinträge
    int           m_pendingIndex = 0; // nächster Index in m_pendingEntries
    QString       m_pendingSidecar;   // "<Ordner>.json" → überspringen
    QTimer        m_fillTimer;        // 0-ms-Timer: speist Chargen, gibt dazwischen ab

    QString             m_folder;
    QFileSystemWatcher* m_watcher;
    QTimer              m_watchDebounce;
    int                 m_suppressWatch = 0;  // >0 → Watcher-Reload ignorieren
};
