#include "app/DrawnIconItem.h"
#include "app/ShadowItem.h"

#include <QCoreApplication>
#include <QQmlEngine>

//  Liegt im Objektziel der QML-Ressource: `DrawnIcon.qml` und die Stil-Menues brauchen die Typen, und wer die
//  Ressource linkt, hat sie damit automatisch. Eine statische Bibliothek wuerfe diese Datei weg, weil niemand sie aufruft.
static void registerDrawnIconItem() {
    qmlRegisterType<DrawnIconItem>("MediaGallery", 1, 0, "DrawnIconItem");
    qmlRegisterType<ShadowItem>("MediaGallery", 1, 0, "ShadowItem");
}
Q_COREAPP_STARTUP_FUNCTION(registerDrawnIconItem)
