// mg_logogen - rendert beim Bau das gezeichnete Logo (qml/common/AppLogo.qml) in die Icon-Groessen der
// Linux-Installation, das macOS-Iconset und als Fenstersymbol. Laeuft offscreen mit dem Software-Rasterer, braucht keinen Bildschirm.
// Aufruf: mg_logogen <zielordner> <icon-name>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <memory>

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    if (a.size() != 3) {
        qWarning("Aufruf: mg_logogen <zielordner> <icon-name>");
        return 2;
    }
    QQmlEngine engine;
    QQmlComponent comp(&engine, QUrl(QStringLiteral("qrc:/qml/common/AppLogo.qml")));
    if (comp.isError()) {
        qWarning("%s", qPrintable(comp.errorString()));
        return 1;
    }
    //  Jede Groesse eigens gezeichnet, nicht verkleinert: so bleiben die Kanten auch bei 16 px scharf.
    const int groessen[] = {16, 22, 24, 32, 48, 64, 128, 256, 512, 1024};
    //  Feste Dateinamen, die iconutil erwartet; @2x ist die doppelte Pixelzahl.
    const struct { int n; const char* name; } iconsetBilder[] = {
        {16, "icon_16x16"},          {32, "icon_16x16@2x"},   {32, "icon_32x32"},
        {64, "icon_32x32@2x"},       {128, "icon_128x128"},   {256, "icon_128x128@2x"},
        {256, "icon_256x256"},       {512, "icon_256x256@2x"}, {512, "icon_512x512"},
        {1024, "icon_512x512@2x"},
    };
    const QString iconset = a.at(1) + QLatin1Char('/') + a.at(2) + QStringLiteral(".iconset");
    if (!QDir().mkpath(iconset)) return 1;
    for (int n : groessen) {
        QQuickWindow fenster;
        fenster.setColor(Qt::transparent);
        fenster.resize(n, n);
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{QStringLiteral("size"), n}}));
        auto* logo = qobject_cast<QQuickItem*>(obj.get());
        if (!logo) return 1;
        logo->setParentItem(fenster.contentItem());
        fenster.show();
        QImage bild = fenster.grabWindow().convertToFormat(QImage::Format_ARGB32);
        bild.setDotsPerMeterX(2835);   // 72 dpi
        bild.setDotsPerMeterY(2835);
        //  hicolor kennt keinen Ordner 1024x1024; die Groesse dient nur dem Iconset.
        if (n <= 512) {
            const QString ordner = a.at(1) + QStringLiteral("/hicolor/%1x%1/apps").arg(n);
            if (!QDir().mkpath(ordner) || !bild.save(ordner + QLatin1Char('/') + a.at(2) + QStringLiteral(".png")))
                return 1;
        }
        for (const auto& b : iconsetBilder)
            if (b.n == n && !bild.save(iconset + QLatin1Char('/') + QLatin1String(b.name) + QStringLiteral(".png")))
                return 1;
        if (n == 256 && !bild.save(a.at(1) + QStringLiteral("/logo.png"))) return 1;
    }
    return 0;
}
