// mg_logogen - rendert beim Bau das gezeichnete Logo (qml/common/AppLogo.qml) in die Icon-Groessen der
// Linux-Installation und als Fenstersymbol. Laeuft offscreen mit dem Software-Rasterer, braucht keinen Bildschirm.
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
    const int groessen[] = {16, 22, 24, 32, 48, 64, 128, 256, 512};
    for (int n : groessen) {
        QQuickWindow fenster;
        fenster.setColor(Qt::transparent);
        fenster.resize(n, n);
        std::unique_ptr<QObject> obj(comp.createWithInitialProperties({{QStringLiteral("size"), n}}));
        auto* logo = qobject_cast<QQuickItem*>(obj.get());
        if (!logo) return 1;
        logo->setParentItem(fenster.contentItem());
        fenster.show();
        const QImage bild = fenster.grabWindow().convertToFormat(QImage::Format_ARGB32);
        const QString ordner = a.at(1) + QStringLiteral("/hicolor/%1x%1/apps").arg(n);
        if (!QDir().mkpath(ordner) || !bild.save(ordner + QLatin1Char('/') + a.at(2) + QStringLiteral(".png")))
            return 1;
        if (n == 256 && !bild.save(a.at(1) + QStringLiteral("/logo.png"))) return 1;
    }
    return 0;
}
