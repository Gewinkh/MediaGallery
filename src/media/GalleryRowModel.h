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

// ─────────────────────────────────────────────────────────────────────────────
//  GalleryRowModel - die SICHTBAREN ZEILEN der Galerie.
//
//  Warum es das gibt: Ein `GridView` hat gleich hohe Zellen und kann eine Zeile
//  nicht umbrechen. Sobald ein Unterordner an Ort und Stelle aufklappt, braucht
//  die Galerie aber genau das - sein Inhalt beginnt auf einer neuen Zeile, und
//  hinter ihm liegt eine Flaeche, die exakt seinen Inhalt abdeckt.
//
//  Statt die Kacheln frei zu positionieren (das kostet das Delegate-Recycling
//  und damit die RAM-Obergrenze bei 10–50k Dateien) rechnet dieses Modell aus
//  der Proxy-Reihenfolge, der Spaltenzahl und dem Aufklapp-Zustand eine Liste
//  visueller ZEILEN. Die Galerie ist damit eine `ListView` darueber und
//  recycelt weiter - nur eben Zeilen statt Kacheln.
//
//  Eine Zeile enthaelt ausschliesslich Kacheln DESSELBEN Ordners. Ein Wechsel
//  des Bereichs bricht die Zeile um, auch wenn sie noch nicht voll ist: sonst
//  stuenden Dateien zweier Ordner nebeneinander unter einer Flaeche.
//
//  BAENDER (die helleren Flaechen): Jede Zeile weiss, wie tief sie liegt und
//  welche Baender bei ihr BEGINNEN bzw. ENDEN (`openMask`/`closeMask`, Bit k =
//  Ebene k). Die Ansicht malt daraus je Ebene ein Rechteck und rundet es oben
//  bzw. unten ab. Dadurch deckt die Flaeche immer genau den Inhalt ab - kommt
//  eine Datei dazu oder faellt weg, aendert sich einfach die Zeilenliste.
//
//  Die Kacheldaten kommen als `tiles` (Liste von Objekten) heraus. Das ist der
//  einzige Ort im Projekt, an dem je Kachel QVariants gebaut werden - bewusst:
//  es betrifft nur SICHTBARE Zeilen (die Ansicht liest die Rolle nur fuer ihre
//  Delegates), waehrend Filter und Sortierung weiterhin direkt auf den Structs
//  arbeiten.
// ─────────────────────────────────────────────────────────────────────────────
class GalleryRowModel : public QAbstractListModel {
    Q_OBJECT
    //  Das Quellmodell (`galleryModel`). Als QObject*, damit QML es direkt
    //  zuweisen kann; intern wird typisiert geprueft.
    Q_PROPERTY(QObject* source READ source WRITE setSource NOTIFY sourceChanged)
    //  Geometrie, aus der sich die Kachelzahl je Zeile ergibt. Sie haengt an
    //  der TIEFE: der Inhalt eines aufgeklappten Ordners rueckt je Ebene um
    //  `levelInset` ein (damit man sieht, dass er zu einem Unterordner
    //  gehoert), und genau diese Breite fehlt ihm dann fuer Kacheln.
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
        //  0 = Kachelzeile · 1 = KOPFZEILE eines aufgeklappten Ordners
        //  (Name + eigene Aktionen; s. `## Media` in Structure.md)
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
    //  Kacheln, die in eine Zeile dieser Tiefe passen (mindestens eine).
    Q_INVOKABLE int columnsForDepth(int depth) const;

    int  count() const { return m_rows.size(); }

    //  Welche visuelle Zeile zeigt eine bestimmte Proxy-Zeile? −1 = keine.
    Q_INVOKABLE int rowOfIndex(int proxyRow) const;
    //  Alles ueber eine Zeile in EINEM Zugriff - die Ansicht braucht das beim
    //  Ablegen, um aus einem Punkt den Zielordner zu bestimmen.
    //  Schluessel: kind · first · count · depth · ownerFolder
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
    //  Neue Zeilenliste als Diff einspielen (kein Reset - s. .cpp).
    void applyRows(const QVector<Row>& next);
    //  Kette der Bereiche von der Wurzel bis `scope` (Index 0 = Wurzel).
    bool isAncestorOrSame(int maybeAncestor, int scope) const;
    QVector<int> chainOf(int scope) const;
    //  Dieselbe Kette in einen VORHANDENEN Puffer - fuer den Neuaufbau, der sie
    //  je Zeile braucht (s. .cpp).
    void         chainOf(int scope, QVector<int>* out) const;
    void onSourceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

    QPointer<MediaProxyModel> m_proxy;
    QPointer<MediaModel>      m_src;
    QVector<Row>              m_rows;
    int                       m_contentWidth = 0;
    int                       m_cellWidth    = 1;
    int                       m_levelInset   = 0;
    //  Sammelt die vielen kleinen Einfuegungen der inkrementellen Befuellung zu
    //  EINEM Neuaufbau je Ereignisschleifen-Durchlauf. Ohne das liefe der
    //  Aufbau je Charge einmal ueber alle Zeilen - quadratisch beim Oeffnen
    //  eines grossen Ordners.
    QTimer m_rebuildTimer;
    //  Wann lief der letzte Aufbau? Begrenzt die Rate waehrend eines
    //  Grosseinlesens (s. `kMinRebuildGapMs` in der .cpp).
    QElapsedTimer m_lastRebuild;
    //  Hat die QUELLE ihren Inhalt komplett getauscht (Ordnerwechsel, Reload,
    //  neue Sortierung)? Dann reicht der Struktur-Diff NICHT: die Zeilen koennen
    //  zufaellig dieselbe Gestalt haben, tragen aber ganz andere Kacheln - die
    //  Ansicht erfuehre nichts und zeigte den alten Ordner weiter (vom Nutzer
    //  gemeldet: „Ordner wechseln klappt nicht mehr").
    bool m_sourceReset = false;
    //  Ab WELCHER Proxy-Zeile hat sich der Bestand geaendert (Einfuegen/
    //  Entfernen)? Alles ab dort traegt danach andere Kacheln - auch wenn die
    //  Zeilenliste gleich AUSSIEHT. `INT_MAX` = nichts offen.
    int  m_dirtyFrom = std::numeric_limits<int>::max();
};
