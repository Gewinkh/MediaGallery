//  WebEngineInit - nachladbar und als einzige mit Qt WebEngine gelinkt; sonst laedt das Programm 133 Bibliotheken schon beim Start.
#include <QtWebEngineQuick>

extern "C" Q_DECL_EXPORT void mg_webengine_initialize() {
    QtWebEngineQuick::initialize();
}
