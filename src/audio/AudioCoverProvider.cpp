#include "audio/AudioCoverProvider.h"

#include "audio/AudioTags.h"

#include <QImage>
#include <QUrl>

AudioCoverProvider::AudioCoverProvider()
    : QQuickImageProvider(QQuickImageProvider::Image,
                          QQmlImageProviderBase::ForceAsynchronousImageLoading) {}

QString AudioCoverProvider::sourceFor(const QString& path, int rev) {
    if (path.isEmpty()) return {};
    return QStringLiteral("image://audiocover/%1?rev=%2")
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(path)))
        .arg(rev);
}

QImage AudioCoverProvider::requestImage(const QString& id, QSize* size,
                                        const QSize& requestedSize) {
    //  Den Cache-Brecher abschneiden - er gehört nicht zum Pfad.
    QString enc = id;
    const int q = enc.lastIndexOf(QLatin1Char('?'));
    if (q >= 0) enc.truncate(q);
    const QString path = QUrl::fromPercentEncoding(enc.toUtf8());
    if (path.isEmpty()) return {};

    const QByteArray data = AudioTags::readCover(path);
    if (data.isEmpty()) return {};

    QImage img;
    if (!img.loadFromData(data)) return {};

    //  Verkleinern, BEVOR das Bild in die Szene geht. Ein Titelbild ist gern
    //  1500×1500 (9 MB als ARGB32), die Fläche im Player ein Bruchteil davon.
    if (requestedSize.isValid()
        && (requestedSize.width() > 0 || requestedSize.height() > 0)) {
        img = img.scaled(requestedSize.width()  > 0 ? requestedSize.width()  : img.width(),
                         requestedSize.height() > 0 ? requestedSize.height() : img.height(),
                         Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (size) *size = img.size();
    return img;
}
