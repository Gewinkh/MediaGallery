#pragma once
#include <QAbstractListModel>
#include <QPointer>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <limits>
#include <QVariantMap>

class MediaModel;
class MediaProxyModel;

// Ein GridView kann keine Zeile umbrechen, ein aufgeklappter Unterordner braucht aber genau das. Dieses Modell
// rechnet visuelle ZEILEN aus; eine Zeile trägt nur Kacheln DESSELBEN Ordners, die Galerie bleibt eine ListView.
class GalleryRowModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QObject* source READ source WRITE setSource NOTIFY sourceChanged)
    // Die Kachelzahl je Zeile hängt an der TIEFE: der Inhalt eines aufgeklappten Ordners rückt je Ebene um
    // `levelInset` ein, und genau diese Breite fehlt ihm dann für Kacheln.
    Q_PROPERTY(int contentWidth READ contentWidth WRITE setContentWidth NOTIFY layoutChangedProp)
    Q_PROPERTY(int cellWidth    READ cellWidth    WRITE setCellWidth    NOTIFY layoutChangedProp)
    Q_PROPERTY(int levelInset   READ levelInset   WRITE setLevelInset   NOTIFY layoutChangedProp)
    Q_PROPERTY(int count   READ count   NOTIFY countChanged)

public:
    enum Roles {
        DepthRole = Qt::UserRole + 1,   // 0 = geoeffneter Ordner
        FirstIndexRole,                 // Proxy-Zeile der ersten Kachel
        TileCountRole,                  // Kacheln in dieser Zeile (1 … columns)
        OpenMaskRole,                   // Bit k: Band der Ebene k BEGINNT hier
        CloseMaskRole,                  // Bit k: Band der Ebene k ENDET hier
        OwnerFolderRole,                // Ordner, dem die Zeile gehoert
        TilesRole,                      // QVariantList der Kacheldaten
        //  0 = Kachelzeile · 1 = KOPFZEILE eines aufgeklappten Ordners (Name +
        //  eigene Aktionen; s. `## Media` in)
        KindRole,
        OwnerNameRole                   // nur Kopfzeilen: Name des Ordners
    };

    explicit GalleryRowModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QObject* source() const;
    void     setSource(QObject* src);

    int  contentWidth() const { return m_contentWidth; }
    void setContentWidth(int w);
    int  cellWidth() const { return m_cellWidth; }
    void setCellWidth(int w);
    int  levelInset() const { return m_levelInset; }
    void setLevelInset(int px);
    Q_INVOKABLE int columnsForDepth(int depth) const;

    int  count() const { return m_rows.size(); }

    Q_INVOKABLE int rowOfIndex(int proxyRow) const;
    // Alles über eine Zeile in EINEM Zugriff - die Ansicht braucht das beim Ablegen, um aus einem Punkt den
    // Zielordner zu bestimmen. Schlüssel: kind, first, count, depth, openMask, closeMask, ownerFolder.
    Q_INVOKABLE QVariantMap rowInfo(int row) const;

signals:
    void sourceChanged();
    void layoutChangedProp();
    void countChanged();

private:
    struct Row {
        int   kind  = 0;    // 0 = Kacheln, 1 = Kopfzeile
        int   first = 0;    // Proxy-Zeile der ersten Kachel (0 bei Kopfzeilen)
        int   count = 0;
        int   scope = 0;
        int   depth = 0;
        quint32 openMask  = 0;
        quint32 closeMask = 0;
    };

    void scheduleRebuild();
    void rebuildNow();
    void applyRows(const QVector<Row>& next);
    bool isAncestorOrSame(int maybeAncestor, int scope) const;
    QVector<int> chainOf(int scope) const;
    void         chainOf(int scope, QVector<int>* out) const;
    void onSourceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight,
                             const QList<int>& roles);

    QPointer<MediaProxyModel> m_proxy;
    QPointer<MediaModel>      m_src;
    QVector<Row>              m_rows;
    int                       m_contentWidth = 0;
    int                       m_cellWidth    = 1;
    int                       m_levelInset   = 0;
    // Sammelt die vielen kleinen Einfügungen der inkrementellen Befüllung zu EINEM Neuaufbau je Ereignisdurchlauf.
    // Ohne das liefe der Aufbau je Charge einmal über alle Zeilen - quadratisch bei einem großen Ordner.
    QTimer m_rebuildTimer;
    QElapsedTimer m_lastRebuild;
    // Hat die QUELLE ihren Inhalt komplett getauscht (Ordnerwechsel, Reload, neue Sortierung)? Dann reicht der
    // Struktur-Diff nicht: die Zeilen können dieselbe Gestalt haben, tragen aber ganz andere Kacheln.
    bool m_sourceReset = false;
    //  Ab WELCHER Proxy-Zeile hat sich der Bestand geaendert (Einfuegen/
    //  Entfernen)? Alles ab dort traegt danach andere Kacheln - auch wenn die
    //  Zeilenliste gleich AUSSIEHT. `INT_MAX` = nichts offen.
    int  m_dirtyFrom = std::numeric_limits<int>::max();
};
