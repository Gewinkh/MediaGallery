#include "app/DrawnIconItem.h"

#include <QCoreApplication>
#include <QQmlEngine>

//  Liegt im Objektziel der QML-Ressource: `DrawnIcon.qml` braucht den Typ, und wer die Ressource linkt, hat ihn damit
//  automatisch. Eine statische Bibliothek wuerfe diese Datei weg, weil niemand sie aufruft.
static void registerDrawnIconItem() {
    qmlRegisterType<DrawnIconItem>("MediaGallery", 1, 0, "DrawnIconItem");
}
Q_COREAPP_STARTUP_FUNCTION(registerDrawnIconItem)
