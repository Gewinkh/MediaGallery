#pragma once
#include <QAbstractListModel>
#include <QVector>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTimer>
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

signals:
    void countChanged();
    void folderChanged();
    void folderContentsChanged();   // externe Änderung (für Statusmeldung)
    void thumbnailsInvalidated();   // Zielgröße gewechselt → Delegates fordern neu an

private slots:
    void onThumbnailReady(const QString& filePath, const QString& thumbUrl);
    void onThumbnailFailed(const QString& filePath);
    void onDirectoryChanged();

private:
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
};
