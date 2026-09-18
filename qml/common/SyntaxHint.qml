pragma ComponentBehavior: Bound
import QtQuick
import MediaGallery 1.0

//  Ein Hinweistext, in dem `so markierte` Stuecke als kleine Box erscheinen -
//  die Schreibweise selbst soll sich vom Fliesstext abheben. Das Satzzeichen
//  direkt hinter einer Box haengt an ihr, damit der Umbruch kein Komma allein
//  an den Zeilenanfang stellt.
Flow {
    id: root

    //  Der fertige Text; Boxen werden mit Backticks markiert.
    property string quelle: ""

    spacing: 4

    readonly property var _stuecke: {
        const aus = [];
        const teile = root.quelle.split("`");
        for (let i = 0; i < teile.length; ++i) {
            if (i % 2 === 1) { aus.push({ box: teile[i], nach: "" }); continue; }
            let rest = teile[i];
            if (i > 0) {
                const haken = rest.match(/^\S*/)[0];
                aus[aus.length - 1].nach = haken;
                rest = rest.slice(haken.length);
            }
            for (const wort of rest.split(" "))
                if (wort.length > 0) aus.push({ box: "", nach: wort });
        }
        return aus;
    }

    Repeater {
        model: root._stuecke
        delegate: Row {
            required property var modelData
            Rectangle {
                visible: modelData.box.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: boxText.implicitWidth + 8
                height: boxText.implicitHeight + 2
                radius: 4
                color: App.themeBackground
                border.color: App.themeBorder
                border.width: 1
                Text {
                    id: boxText
                    anchors.centerIn: parent
                    text: modelData.box
                    color: App.themeAccent
                    font.family: "monospace"
                    font.pixelSize: 11
                    font.bold: true
                }
            }
            Text {
                visible: modelData.nach.length > 0
                anchors.verticalCenter: parent.verticalCenter
                text: modelData.nach
                color: App.themeTextMuted
                font.pixelSize: 11
            }
        }
    }
}
