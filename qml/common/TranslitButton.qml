import QtQuick
import QtQuick.Controls
import MediaGallery 1.0

// ─────────────────────────────────────────────────────────────────────────────
//  TranslitButton.qml - Umschalter der Live-Transliteration (oben rechts in
//  TextSurface und in der PDF-Editor-Toolbar).
//
//  • Klick öffnet ein kleines Popup: „Aus" + die drei Schemata (Arabisch mit
//    Harakat, Japanisch–Hiragana, Japanisch–Katakana). Die Auswahl setzt
//    Translit.scheme UND aktiviert; „Aus" deaktiviert nur (Schema bleibt).
//  • Der Glyph zeigt das aktive Schema (ع / あ / ア); Akzent-Rahmen = aktiv.
//  • Zustand + Schema sind global (Translit-Singleton, persistiert) - der
//    Button ist überall nur eine Sicht darauf.
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: root

    width: 30; height: 26; radius: 6
    color: Translit.enabled
           ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.30)
           : (hover.hovered
              ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.16)
              : Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.07))
    border.color: Translit.enabled ? App.themeAccent : App.themeBorder
    border.width: 1

    readonly property string glyph: Translit.scheme === "ja-hira" ? "\u3042"      // あ
                                  : Translit.scheme === "ja-kata" ? "\u30A2"      // ア
                                                                  : "\u0639"      // ع

    Text {
        anchors.centerIn: parent
        text: root.glyph
        color: App.themeTextPrimary
        font.pixelSize: 13
        opacity: Translit.enabled ? 1.0 : 0.55
    }

    HoverHandler { id: hover }
    TapHandler { onTapped: menu.open() }
    ToolTip.text: App.uiText(App.language, "TranslitTip")
    ToolTip.visible: hover.hovered && !menu.opened

    // ── Schema-Auswahl ────────────────────────────────────────────────────────
    Popup {
        id: menu
        x: root.width - width
        y: root.height + 4
        padding: 6
        background: Rectangle {
            color: App.themeToolbarBg
            border.color: App.themeBorder; border.width: 1
            radius: 8
        }
        contentItem: Column {
            spacing: 2

            component SchemeItem: Rectangle {
                id: item
                property string label: ""
                property string schemeId: ""      // "" = Aus
                readonly property bool current: schemeId.length === 0
                                                ? !Translit.enabled
                                                : (Translit.enabled && Translit.scheme === schemeId)
                width: 210; height: 28; radius: 6
                color: itemHover.hovered
                       ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g, App.themeTextPrimary.b, 0.14)
                       : "transparent"
                Text {
                    anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                    text: item.label
                    color: App.themeTextPrimary; font.pixelSize: 12
                }
                Text {
                    anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                    visible: item.current
                    text: "\u2713"
                    color: App.themeAccent; font.pixelSize: 12
                }
                HoverHandler { id: itemHover }
                TapHandler {
                    onTapped: {
                        if (item.schemeId.length === 0) {
                            Translit.enabled = false
                        } else {
                            Translit.scheme  = item.schemeId
                            Translit.enabled = true
                        }
                        menu.close()
                    }
                }
            }

            SchemeItem { label: App.uiText(App.language, "TranslitOff");      schemeId: "" }
            Rectangle { width: 210; height: 1; color: App.themeBorder }
            SchemeItem { label: App.uiText(App.language, "TranslitArabic");   schemeId: "ar" }
            SchemeItem { label: App.uiText(App.language, "TranslitHiragana"); schemeId: "ja-hira" }
            SchemeItem { label: App.uiText(App.language, "TranslitKatakana"); schemeId: "ja-kata" }
        }
    }
}
