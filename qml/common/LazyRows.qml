import QtQuick

// Lange Listen (Tags, Kategorien in Menues und Einstellungen): zuerst nur so viele Zeilen, wie in eine Fensterhoehe
// passen, der Rest folgt in Haeppchen auf den naechsten Bildern. 403 Tags am Stueck hielten ein Menue rund 0,4 s auf.
QtObject {
    id: lazy

    property real viewHeight: 1080
    property real rowHeight: 28         // Vorgabe: Mindesthoehe einer MenuItem-Zeile im App-Stil
    property int  chunk: 15
    readonly property ListModel model: ListModel {}

    property var    _src: []
    property var    _map: null
    property string _key: ""
    property Timer  _timer: Timer { interval: 16; onTriggered: lazy._append(lazy.chunk) }

    // Gleicher Schluessel = Zeilen bleiben stehen; liefert true, wenn neu gebaut wurde.
    function fill(list, key, map) {
        if (key === _key)
            return false
        _timer.stop()
        _key = key
        _src = list
        _map = map
        model.clear()
        _append(Math.ceil(Math.max(viewHeight, 600) / rowHeight) + 1)
        return true
    }

    function _append(n) {
        const end = Math.min(_src.length, model.count + n)
        const neu = []
        for (let i = model.count; i < end; i++)
            neu.push(_map(_src[i]))
        if (neu.length > 0)
            model.append(neu)
        if (model.count < _src.length)
            _timer.start()
    }
}
