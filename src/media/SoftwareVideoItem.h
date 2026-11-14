#pragma once
//  Videobild fuer das Software-Backend: VideoOutput zeichnet in Qt 6.12 nur ueber RHI, dort bliebe die Flaeche schwarz.
//  Die Bilder werden im Arbeitsfaden auf Geraetepixel gewandelt; ist er noch beschaeftigt, gilt nur das neueste.
#include <QImage>
#include <QQuickItem>

#include <memory>

class QVideoSink;

namespace mg::media {

class SoftwareVideoItem : public QQuickItem {
    Q_OBJECT
public:
    explicit SoftwareVideoItem(QQuickItem* parent = nullptr);
    ~SoftwareVideoItem() override;

    //  MediaPlayer.videoOutput ruft diese Methode an jedem Objekt, das kein QVideoSink ist.
    Q_INVOKABLE QVideoSink* videoSink() const { return m_sink; }

    struct Stand;

protected:
    QSGNode* updatePaintNode(QSGNode* alt, UpdatePaintNodeData*) override;
    void geometryChange(const QRectF& neu, const QRectF& alt) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    void merkeZiel();

    QVideoSink* m_sink = nullptr;
    std::shared_ptr<Stand> m_stand;
    QImage m_bild;
    QSize m_groesse;
    bool m_neu = false;

    friend struct Stand;
};

}  // namespace mg::media
