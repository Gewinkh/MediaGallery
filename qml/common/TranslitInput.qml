pragma ComponentBehavior: Bound
import QtQuick
import MediaGallery 1.0

//  Live-Transliteration fuer ein gewoehnliches Eingabefeld (Suchen, Filtern).
//  Das Feld ruft `pruefe()` in seinem `onTextChanged`, der Rest passiert hier.
QtObject {
    id: haken

    property Item feld: null

    //  Ohne Sperre schriebe die eigene Ersetzung sich selbst wieder um.
    property bool _laeuft: false


    function pruefe() {
        if (haken._laeuft || !haken.feld || !Translit.enabled) return
        const r = Translit.liveApply(haken.feld.text, haken.feld.cursorPosition)
        if (!r.changed) return
        haken._laeuft = true
        haken.feld.text = haken.feld.text.slice(0, r.start) + r.replacement
                          + haken.feld.text.slice(r.end)
        haken.feld.cursorPosition = r.cursor
        haken._laeuft = false
    }
}
