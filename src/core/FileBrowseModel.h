#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  FileBrowseModel — Verzeichnis-Inhalt für den EIGENEN Datei-/Ordnerwähler
//  (`qml/common/FileChooser.qml`).
//
//  WARUM überhaupt: Qts `FileDialog`/`FolderDialog` öffnen ein eigenes Fenster,
//  dessen `ListView` in Qts eigener `FileDialog.qml` steckt — dort ist weder
//  `SmoothWheelArea` einhängbar noch das Aussehen vollständig bestimmbar. Der
//  Wähler wird deshalb selbst gebaut; dieses Modell liefert ihm die Daten.
//
//  Gelesen wird ASYNCHRON nach dem Hausmuster (`QRunnable` + eigener
//  `QThreadPool` + `std::atomic<bool>`-Cancel + `QueuedConnection`): auf einem
//  Netzlaufwerk blockiert schon das Auflisten eines Verzeichnisses spürbar.
//
//  Ein Typ je Wähler (`qmlRegisterType`), kein Singleton — zwei offene Wähler
//  stehen sonst im selben Verzeichnis.
// ─────────────────────────────────────────────────────────────────────────────

#include <QAbstractListModel>
#include <QDateTime>
#include <QStringList>
#include <QThreadPool>
#include <QUrl>

#include <atomic>
#include <functional>
#include <vector>
#include <memory>

class FileBrowseModel : public QAbstractListModel {
    Q_OBJECT
    //  Aktuelles Verzeichnis als LOKALER Pfad (nicht als URL — QML rechnet an
    //  den Rändern um, innen bleibt es ein Pfad).
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    //  Glob-Muster wie "*.png" (leer = alles). Ordner sind nie gefiltert.
    Q_PROPERTY(QStringList nameFilters READ nameFilters WRITE setNameFilters
                   NOTIFY nameFiltersChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden
                   NOTIFY showHiddenChanged)
    //  Nur Ordner zeigen (Ordner-Auswahl).
    Q_PROPERTY(bool dirsOnly READ dirsOnly WRITE setDirsOnly NOTIFY dirsOnlyChanged)
    Q_PROPERTY(int  count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY folderChanged)

public:
    explicit FileBrowseModel(QObject* parent = nullptr);
    ~FileBrowseModel() override;

    //  Eine Zeile — öffentlich, weil der Lese-Worker sie füllt.
    struct Row {
        QString   name;
        bool      isDir = false;
        qint64    size  = 0;
        QDateTime mtime;
    };

    enum Roles {
        NameRole = Qt::UserRole + 1,
        IsDirRole,
        PathRole,
        SizeTextRole,
        DateTextRole
    };

    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString     folder() const { return m_folder; }
    void        setFolder(const QString& path);
    QStringList nameFilters() const { return m_filters; }
    void        setNameFilters(const QStringList& f);
    bool        showHidden() const { return m_showHidden; }
    void        setShowHidden(bool v);
    bool        dirsOnly() const { return m_dirsOnly; }
    void        setDirsOnly(bool v);
    int         count() const { return int(m_rows.size()); }
    bool        loading() const { return m_loading; }
    bool        canGoUp() const;

    // ── Navigation ───────────────────────────────────────────────────────────
    Q_INVOKABLE void cdUp();
    Q_INVOKABLE void reload();
    Q_INVOKABLE QString entryPath(int row) const;
    Q_INVOKABLE bool    entryIsDir(int row) const;

    // ── Auskünfte für die Oberfläche ─────────────────────────────────────────
    //  Standard-Orte (Zuhause, Dokumente, Bilder …) als [{name, path}] — nur
    //  die, die es auf diesem System wirklich gibt.
    Q_INVOKABLE QVariantList places() const;
    //  Pfad in Segmente für die Brotkrumen-Zeile: [{name, path}], Wurzel zuerst.
    Q_INVOKABLE QVariantList crumbs() const;
    Q_INVOKABLE QString  join(const QString& dir, const QString& name) const;
    Q_INVOKABLE QString  dirOf(const QString& path) const;
    Q_INVOKABLE QString  baseName(const QString& path) const;
    Q_INVOKABLE bool     fileExists(const QString& path) const;
    Q_INVOKABLE bool     dirExists(const QString& path) const;
    //  Endung ergänzen, wenn der Name keine trägt (Speichern-Modus).
    Q_INVOKABLE QString  withSuffix(const QString& name, const QString& suffix) const;
    //  Pfad ⇄ URL — die Aufrufstellen reichen `file://`-URLs weiter.
    Q_INVOKABLE QUrl     toUrl(const QString& path) const;
    Q_INVOKABLE QString  fromUrl(const QUrl& url) const;
    //  Glob-Muster aus einem Filtertext der Form "Bilder (*.png *.jpg)".
    Q_INVOKABLE QStringList globsOf(const QString& filterText) const;

signals:
    void folderChanged();
    void nameFiltersChanged();
    void showHiddenChanged();
    void dirsOnlyChanged();
    void countChanged();
    void loadingChanged();

private:
    void startLoad();
    void applyRows(std::vector<Row> rows, quint64 gen);

    QString     m_folder;
    QStringList m_filters;
    bool        m_showHidden = false;
    bool        m_dirsOnly   = false;
    bool        m_loading    = false;
    std::vector<Row> m_rows;

    //  Ein Worker genügt — zwei Verzeichnisse gleichzeitig braucht niemand,
    //  und seriell bleibt die Reihenfolge der Ergebnisse eindeutig.
    QThreadPool m_pool;
    quint64     m_gen = 0;                       // verwirft veraltete Läufe
    std::shared_ptr<std::atomic<bool>> m_cancel; // kooperativer Abbruch
};
