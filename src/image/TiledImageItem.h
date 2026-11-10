#pragma once
// Bildanzeige in Kacheln fuer den Bildbetrachter: dekodiert die verlangte Stufe im Arbeitsfaden und zeigt sie je nach
// Backend auf zwei Wegen. GPU: Stufen-Kacheln als Texturen, wenige je Bild hochgeladen, die CPU-Kopie danach frei.
// Software: die Stufe bleibt im RAM, gezeigt werden einmal gerechnete Kacheln in Geraetepixeln (LRU-Zwischenspeicher).
// Kein Ersatz fuer Image allgemein: keine Animation, kein Netz, nur lokale Dateien.

#include <QImage>
#include <QQuickItem>
#include <QTimer>

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mg::image {

// `outer` = `inner` plus `gutter` Pixel zum Nachbarn hin (nicht am Bildrand): die bilineare Filterung liest an der
// Kachelkante echte Nachbarpixel, sonst saehe man bei starkem Zoom eine Naht.
struct TileRect {
    QRect inner;
    QRect outer;
};
std::vector<TileRect> tileGrid(QSize image, int tile, int gutter);

// Wie Image.sourceSize mit einer Kante: die laengere Seite wird `longSide`, nie groesser als das Original.
QSize stageSize(QSize full, int longSide);

// Ausschnitt `area` (Geraetepixel) der auf `device` gestreckten Stufe, bilinear wie QPainter es je Bild taete.
QImage renderDisplayTile(const QImage& stage, QSize device, QRect area);

class TiledImageItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    // Laengere Kante der Stufe in logischen Pixeln wie Image.sourceSize, gerechnet wird in Geraetepixeln; nicht ganzzahlig,
    // damit eine eingepasste Stufe genau so gross wird wie die Flaeche auf dem Schirm.
    Q_PROPERTY(qreal decodeSize READ decodeSize WRITE setDecodeSize NOTIFY decodeSizeChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool mipmap READ mipmap WRITE setMipmap NOTIFY mipmapChanged)
    Q_PROPERTY(QRectF viewRect READ viewRect WRITE setViewRect NOTIFY viewRectChanged)
    Q_PROPERTY(int tilesReady READ tilesReady NOTIFY tilesReadyChanged)
    // Das gezeigte Bild gehoert zu `source`. Nach einem Quellwechsel steht bis dahin das alte; QML haelt solange
    // Mass und Lage der Flaeche, sonst rueckte das alte Bild vor dem Tausch.
    Q_PROPERTY(bool current READ current NOTIFY currentChanged)

public:
    enum Status { Null, Ready, Loading, Error };
    Q_ENUM(Status)

    explicit TiledImageItem(QQuickItem* parent = nullptr);
    ~TiledImageItem() override;

    QString source() const { return m_source; }
    void setSource(const QString& s);
    qreal decodeSize() const { return m_decodeSize; }
    void setDecodeSize(qreal d);
    Status status() const { return m_status; }
    bool mipmap() const { return m_mipmap; }
    void setMipmap(bool m);
    QRectF viewRect() const { return m_viewRect; }
    void setViewRect(const QRectF& r);
    int tilesReady() const { return int(m_cache.size()); }
    bool current() const { return m_current; }

    static constexpr int kGpuTile = 1024;
    static constexpr int kCpuTile = 512;

signals:
    void sourceChanged();
    void decodeSizeChanged();
    void statusChanged();
    void mipmapChanged();
    void viewRectChanged();
    void tilesReadyChanged();
    void currentChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
    void geometryChange(const QRectF& now, const QRectF& before) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    friend class DecodeTask;
    friend class TileTask;
    friend class LevelTask;
    struct Shared;
    struct Decoded;
    struct CpuTile {
        QImage image;
        quint64 lastUse = 0;
    };

    bool software() const;
    void setStatus(Status s);
    void setCurrent(bool c);
    void requestDecode();
    void startDecode();
    bool reuseRest(quint64 gen, int longSide);
    void adoptDecoded(const std::shared_ptr<Decoded>& d);
    void adoptTile(quint64 epoch, quint64 key, QImage img);
    void adoptLevel(quint64 gen, int shift, QImage img);
    void updateLevel();
    void dropLevel();
    int wantShift() const;
    bool foreign() const { return m_stageSrcGen != m_srcGen; }
    bool restActive() const;
    void syncRest();
    const QImage& cpuSource() const { return restActive() ? m_rest : m_cpuLevel.isNull() ? m_cpuStage : m_cpuLevel; }
    void resetCpuTiles();
    void planCpuTiles();
    QSize deviceSize() const;
    QRect deviceView() const;

    QString m_source;
    // Quelle der gezeigten Stufe: weicht sie ab, laedt gerade ein anderes Bild, und das alte wird unverzerrt
    // eingepasst statt auf das Seitenverhaeltnis des neuen gestreckt.
    quint64 m_srcGen = 0;
    quint64 m_stageSrcGen = 0;
    bool m_current = true;
    qreal m_decodeSize = 0;
    Status m_status = Null;
    bool m_mipmap = false;
    QRectF m_viewRect;

    std::shared_ptr<Shared> m_shared;
    bool m_decodeQueued = false;
    bool m_isSoftware = false;

    // GPU: Kacheln der neuen Stufe, bis updatePaintNode sie als Textur uebernimmt (dann leer = CPU-Kopie frei).
    quint64 m_stageGen = 0;
    QSize m_stage;
    bool m_stageReduced = false;                     // kleiner als die Datei: Kandidat fuer m_rest / Root::rest
    std::vector<TileRect> m_gpuRects;
    std::vector<QImage> m_gpuTiles;
    // Dateigroesse und Mass von Root::rest (aus updatePaintNode): gleich grosse Stufe wird uebernommen, nicht gelesen.
    QSize m_full;
    quint64 m_fullSrcGen = 0;
    QSize m_gpuRest;
    quint64 m_gpuRestSrcGen = 0;
    quint64 m_promoteGen = 0;
    bool m_noReuse = false;

    // Software: Stufe plus Kacheln in Geraetepixeln fuer genau einen Massstab (`m_cpuDevice`).
    QImage m_cpuStage;
    // Ab halber Groesse liest bilinear nur noch jedes zweite Pixel (Treppen, "frittiert"): dann gilt eine per
    // Flaechenmittel um 2^shift verkleinerte Stufe, wie die Mipmap der GPU.
    QImage m_cpuLevel;
    // Die letzte genau in Flaechengroesse dekodierte Stufe desselben Bildes (hoechstens Ansichtsgroesse): nach dem
    // Zurueckzoomen auf Einpassen sofort wieder das Bild, ohne weiche Zwischenstufe bis zur naechsten Dekodierung.
    QImage m_rest;
    quint64 m_restSrcGen = 0;
    bool m_restShown = false;
    int m_levelShift = 0;
    int m_levelWant = 0;
    quint64 m_baseGen = 0;
    QSize m_cpuDevice;
    quint64 m_cpuEpoch = 0;
    std::unordered_map<quint64, CpuTile> m_cache;
    std::unordered_set<quint64> m_pending;
    std::vector<quint64> m_dropped;
    quint64 m_useClock = 0;
    QTimer m_settle;
};

} // namespace mg::image
