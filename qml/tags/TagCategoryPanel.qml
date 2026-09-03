pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import MediaGallery 1.0
import "../common"

// ─────────────────────────────────────────────────────────────────────────────
//  TagCategoryPanel.qml - einheitliches Panel-System für Tags UND Kategorien
//  (rechte Seitenleiste; ersetzt TagCategoryPanel(QWidget)).
//
//  Aufbau (ein Panel, zwei strukturell gleichwertige Abschnitte, gemeinsamer
//  SectionHeader - keine UI-Duplikation):
//    • Abschnitt „Tags":       ALLE Tags als Chips mit klarem Aktiv-/Inaktiv-
//                              Zustand (Toggle gegen galleryModel.tagFilter).
//                              „+" im Kopf erstellt einen neuen Tag.
//    • Abschnitt „Kategorien": bestehender Baum aus panel.tagsCtl.categoriesTree() über
//                              rekursive CategoryNode-Knoten. „+" im Kopf
//                              erstellt eine neue Wurzelkategorie.
//
//  Filter-Konsistenz (referenzbasiert):
//    Referenzquelle für den Tag-Filter ist AUSSCHLIESSLICH der Proxy
//    (galleryModel.tagFilter); activeTagFilter ist nur ein reaktiver Spiegel.
//    Beim ABWÄHLEN einer Kategorie werden abhängige aktive Unterkategorien und
//    deren Tags mit deaktiviert - außer sie werden von einem anderen weiterhin
//    aktiven Filter referenziert (siehe toggleCategory).
// ─────────────────────────────────────────────────────────────────────────────
Rectangle {
    id: panel
    color: App.themeSidebarBg
    border.color: App.themeBorder
    border.width: 1

    signal enterAddToTagMode(string tag)
    signal enterGroupMode(string tag)

    // Individuelle Abschnitts-Sichtbarkeit (gesteuert über Filter ▸ Tags &
    // Kategorien): das Panel selbst ist sichtbar, solange mindestens ein
    // Abschnitt aktiv ist (Bindung in ApplicationShell).
    property bool showTagsSection: true
    property bool showCategoriesSection: true

    //  WELCHE Tags/Kategorien? Die des EIGENEN Ordners - nicht die der gerade
    //  fokussierten Hälfte. `Tags` ist appweit und folgt dem Fokus; da der Fokus
    //  dem Mauszeiger folgt, wechselte der Inhalt des Panels, sobald die Maus
    //  über die andere Hälfte fuhr (Nutzerbefund). Die Galerie reicht deshalb
    //  ihren eigenen `PaneCtl.tags` herein; in den Einstellungen bleibt die
    //  Vorgabe stehen, dort IST die Fassade gemeint.
    property var tagsCtl: Tags
    //  Ebenso der Ordnerwechsel: der EIGENE zählt.
    property var folderSource: App

    //  Rueckgaengig-Leiste: EINE, am Fuss des Panels, und deutlich sichtbar.
    //  Zwei getrennte Leisten (Tags/Kategorien) waren ueberlegt und wieder
    //  verworfen - viele Vorgaenge fassen beides an, die Trennung haette mehr
    //  Fragen aufgeworfen als beantwortet (Festlegung des Nutzers 2026-09-03).
    readonly property bool hasUndo: (panel.tagsCtl && (panel.tagsCtl.canUndo
                                                       || panel.tagsCtl.canRedo)) ? true : false
    readonly property int  undoBarHeight: panel.hasUndo ? 44 : 0

    property var tree: []
    property var allTagsModel: []
    property var activeCategories: []
    // Reaktiver Spiegel von galleryModel.tagFilter (NIE direkt mutieren -
    // Mutationen laufen immer über den Proxy, der Spiegel folgt via Connections).
    property var activeTagFilter: []

    //  Zähler, der bei jeder Tag-/Kategorie-Änderung hochgeht. **Er ist die
    //  Abhängigkeit, die den Farb-Bindungen fehlte:** `tagColor(...)` ist ein
    //  FUNKTIONSaufruf, und darauf erzeugt QML keine Bindung - eine geänderte
    //  Tagfarbe kam deshalb erst an, wenn die Delegates neu gebaut wurden
    //  (beide Panels aus und wieder ein; Nutzerbefund 2026-09-03). `allTags()`
    //  liefert bei einer reinen Farbänderung dieselbe Liste, der Repeater baut
    //  also von sich aus nichts neu. Wer `tagColorOf` ruft, hängt daran mit.
    property int tagRev: 0
    function tagColorOf(tag) {
        void panel.tagRev                 // s. oben - die Bindung braucht sie
        return panel.tagsCtl.tagColor(tag)
    }

    function refresh() {
        tree = panel.tagsCtl.categoriesTree()
        allTagsModel = panel.tagsCtl.allTags()
        panel.tagRev++
    }
    Component.onCompleted: {
        refresh()
        activeTagFilter = galleryModel.tagFilter
    }
    Connections {
        target: panel.tagsCtl
        function onCategoriesChanged() { panel.refresh() }
        function onTagsChanged()       { panel.refresh() }
    }
    // Beim Ordnerwechsel/-start neu ziehen: JsonStorage lädt die Tags/Kategorien
    // eines Ordners OHNE tagsChanged/categoriesChanged zu emittieren - ohne
    // diesen Hook bliebe das Panel bis zur ersten Mutation leer.
    Connections {
        target: panel.folderSource
        function onFolderOpened(path) { panel.refresh() }
    }
    Connections {
        target: galleryModel
        function onFilterChanged() { panel.activeTagFilter = galleryModel.tagFilter }
    }

    // ── Zustands-Callbacks (auch für CategoryNode) ────────────────────────────
    function isCategoryActive(id) { return activeCategories.indexOf(id) >= 0 }
    function isTagActive(tag)     { return activeTagFilter.indexOf(tag) >= 0 }

    //  Namensliste nach Suchbegriff filtern (Groß-/Kleinschreibung egal).
    function filterList(names, needle) {
        const n = (needle || "").trim().toLowerCase()
        if (n.length === 0) return names
        var out = []
        for (var i = 0; i < names.length; ++i)
            if (String(names[i]).toLowerCase().indexOf(n) >= 0) out.push(names[i])
        return out
    }
    //  Dasselbe für die flache Kategorienliste (Einträge mit `name`/`id`).
    function filterCats(needle) {
        const all = panel.tagsCtl.categoriesFlat()
        const n = (needle || "").trim().toLowerCase()
        var out = []
        for (var i = 0; i < all.length; ++i)
            if (n.length === 0 || String(all[i].name).toLowerCase().indexOf(n) >= 0)
                out.push(all[i])
        return out
    }

    // ── Referenz-Helfer für die Kaskadenlogik ─────────────────────────────────
    //  Alle Prüfungen laufen über den aktuellen Baum (tree) - per ID, nicht per
    //  Name (Referenzbasis: TagCategory.id).
    function _findNode(nodes, id) {
        for (var i = 0; i < nodes.length; i++) {
            if (nodes[i].id === id) return nodes[i]
            var f = _findNode(nodes[i].children, id)
            if (f) return f
        }
        return null
    }
    // Alle Kategorie-IDs des Teilbaums INKLUSIVE des Knotens selbst.
    function _subtreeIds(node) {
        var out = [node.id]
        for (var i = 0; i < node.children.length; i++)
            out = out.concat(_subtreeIds(node.children[i]))
        return out
    }
    // Alle Tags des Teilbaums (Knoten + rekursiv alle Unterkategorien).
    function _subtreeTags(node) {
        var out = node.tags.slice()
        for (var i = 0; i < node.children.length; i++) {
            var sub = _subtreeTags(node.children[i])
            for (var j = 0; j < sub.length; j++)
                if (out.indexOf(sub[j]) < 0) out.push(sub[j])
        }
        return out
    }
    // Vorfahren-IDs (Wurzel -> …) eines Knotens; null, wenn nicht gefunden.
    function _ancestorIds(nodes, id) {
        for (var i = 0; i < nodes.length; i++) {
            if (nodes[i].id === id) return []
            var sub = _ancestorIds(nodes[i].children, id)
            if (sub !== null) { sub.push(nodes[i].id); return sub }
        }
        return null
    }

    // ── Kategorie an-/abwählen (mit referenzbasierter Kaskade beim Abwählen) ──
    function toggleCategory(id, on) {
        var a = activeCategories.slice()
        var i = a.indexOf(id)

        if (on) {
            if (i < 0) a.push(id)
            activeCategories = a
            galleryModel.categoryFilter = a
            return
        }
        if (i < 0) return
        a.splice(i, 1)

        var node = _findNode(tree, id)
        if (node) {
            var subIds = _subtreeIds(node)

            // 1) Abhängige aktive Unterkategorien deaktivieren.
            //    AUSNAHME: Eine Unterkategorie bleibt aktiv, wenn ein weiterhin
            //    aktiver Vorfahre AUSSERHALB des abgewählten Teilbaums sie noch
            //    referenziert (z. B. eine aktive übergeordnete Wurzelkategorie).
            var kept = []
            for (var k = 0; k < a.length; k++) {
                var cid = a[k]
                if (subIds.indexOf(cid) < 0) { kept.push(cid); continue }   // unabhängig
                var anc = _ancestorIds(tree, cid)
                var referenced = false
                for (var j = 0; anc !== null && j < anc.length; j++) {
                    if (subIds.indexOf(anc[j]) >= 0) continue   // Referenz im entfernten Teilbaum zählt nicht
                    if (a.indexOf(anc[j]) >= 0) { referenced = true; break }
                }
                if (referenced) kept.push(cid)
            }
            a = kept

            // 2) Tag-Kaskade: Tags des abgewählten Teilbaums aus dem Tag-Filter
            //    entfernen - AUSSER ein verbleibender aktiver Kategorie-Teilbaum
            //    referenziert den Tag weiterhin (Referenzzählung über die
            //    Teilbaum-Tags aller noch aktiven Kategorien).
            var removedTags = _subtreeTags(node)
            if (removedTags.length > 0) {
                var stillRef = ({})
                for (k = 0; k < a.length; k++) {
                    var n2 = _findNode(tree, a[k])
                    if (!n2) continue
                    var ts = _subtreeTags(n2)
                    for (j = 0; j < ts.length; j++) stillRef[ts[j]] = true
                }
                var tf = galleryModel.tagFilter.slice()
                var out = []
                var changed = false
                for (k = 0; k < tf.length; k++) {
                    if (removedTags.indexOf(tf[k]) >= 0 && stillRef[tf[k]] !== true) {
                        changed = true
                        continue
                    }
                    out.push(tf[k])
                }
                if (changed) galleryModel.tagFilter = out
            }
        }

        activeCategories = a
        galleryModel.categoryFilter = a
    }

    function toggleTag(tag) {
        var a = galleryModel.tagFilter.slice()
        var i = a.indexOf(tag)
        if (i >= 0) a.splice(i, 1); else a.push(tag)
        galleryModel.tagFilter = a          // Spiegel folgt via onFilterChanged
    }
    function moveTag(tag, fromCat, toCat) { panel.tagsCtl.moveTagToCategory(tag, fromCat, toCat) }

    // ── Abgelegte Dateien zuordnen (gemeinsam für beide Abschnitte) ───────────
    //  Aufgerufen von den Chips DIESES Panels und von CategoryNode; die Regeln
    //  gehören deshalb an EINE Stelle. Zugewiesen wird immer nur HINZUFÜGEND -
    //  ein Zug ist eine Zuweisung, kein Umschalter (sonst nähme ein zweiter Zug
    //  einer Datei ihren Tag wieder weg).
    function dropFilesOnTag(urls, tag) {
        //  `mediaModel.addTag` überspringt Dateien, die nicht zum offenen Ordner
        //  gehören - von außen hereingezogene Fremddateien laufen also ins Leere.
        for (var i = 0; i < urls.length; i++)
            mediaModel.addTag(App.localPath(urls[i]), tag)
    }
    function dropFilesOnCategory(urls, catId) {
        for (var i = 0; i < urls.length; i++) {
            var p = App.localPath(urls[i])
            //  Kategorien sind über den DATEINAMEN adressiert (wie das
            //  Tag-System), gespeichert wird im Sidecar des offenen Ordners -
            //  deshalb hier die Ordnerprüfung, die addTag selbst mitbringt.
            if (!mediaModel.hasFile(p)) continue
            var name = String(p).split("/").pop()
            if (!panel.tagsCtl.fileInCategory(catId, name))
                panel.tagsCtl.toggleFileInCategory(catId, name)
        }
    }
    //  Ein Tag-Chip, der INS LEERE fällt: aus der Kategorie nehmen, aus der er
    //  kam - und nur aus dieser. Die Regel gehört ins Panel, wie alle anderen
    //  Ablege-Regeln auch; `CategoryNode` stellt nur fest, dass niemand den Zug
    //  angenommen hat.
    //  Ist der Optionen-Modus (Alt+S) dieser Hälfte an? Nur dann lassen sich
    //  Kategorien per Zug umhängen - im Normalbetrieb ist die Kopfzeile zum
    //  Anklicken da.
    readonly property bool editMode: (panel.folderSource
                                      && panel.folderSource.optionsVisible === true)

    //  Eine Kategorie unter eine andere hängen. Die Regel liegt im Panel wie
    //  alle Ablege-Regeln; `moveCategory` weist einen Zug in den eigenen
    //  Teilbaum selbst ab.
    function moveCategoryInto(catId, newParentId) {
        if (!catId || !newParentId || catId === newParentId) return
        //  **Nicht in den EIGENEN Teilbaum.** `moveCategory` weist das ohnehin
        //  ab, aber stillschweigend - auf dem Bildschirm sah es aus, als sei
        //  etwas eingefroren (Nutzerbefund 2026-09-03). Hier wird es früher
        //  entschieden, und der zurückspringende Kopf ist die Antwort.
        //  In den EIGENEN Teilbaum gezogen: dann TAUSCHEN die beiden ihre
        //  Plätze, statt dass gar nichts passiert - jede nimmt ihre Tags,
        //  Dateien und übrigen Unterkategorien mit (Festlegung des Nutzers
        //  2026-09-04). Ein echtes Verschieben ginge dort nicht: der Ast, an
        //  dem man zieht, hinge dann in sich selbst.
        const node = panel._findNode(panel.tree, catId)
        if (node && panel._subtreeIds(node).indexOf(newParentId) >= 0) {
            panel.tagsCtl.swapCategories(catId, newParentId)
            return
        }
        panel.tagsCtl.moveCategory(catId, newParentId)
    }

    //  Eine Kategorie, die INS LEERE fällt, wird zur Hauptkategorie - das
    //  Gegenstück zum Tag-Chip, der so seine Kategorie verlässt. Eine, die
    //  ohnehin schon oben liegt, bleibt unangetastet (sonst rutschte sie nur
    //  ans Ende der Liste).
    function dropCategoryOutside(catId) {
        if (!catId) return
        const oben = panel._ancestorIds(panel.tree, catId)
        if (!oben || oben.length === 0) return
        panel.tagsCtl.moveCategory(catId, "")
    }

    function dropTagOutside(tag, fromCat) {
        if (!fromCat || String(fromCat).length === 0) return
        panel.tagsCtl.removeTagFromCategory(fromCat, tag)
    }

    function requestAddToTagMode(tag) { panel.enterAddToTagMode(tag) }
    function requestGroupMode(tag)    { panel.enterGroupMode(tag) }

    function promptAddSubcategory(parentId) {
        namePrompt.title = App.uiText(App.language, "CatPanelNewSubcategory"); namePrompt.value = ""
        namePrompt.onAcceptFn = function(v) { panel.tagsCtl.addSubcategory(parentId, v, Qt.rgba(0,0.7,0.63,1), false) }
        namePrompt.open()
    }
    //  Tag zu einer Kategorie: ANLEGEN oben, AUSWÄHLEN unten.
    //  Vorher gab es nur ein Namensfeld - man musste den Tag also exakt
    //  abtippen, obwohl er schon existierte (Nutzerbefund 2026-09-03).
    function promptAddTag(catId) {
        tagPick.targetCat = catId
        tagPick.open()
    }
    function promptRename(id, oldName) {
        namePrompt.title = App.uiText(App.language, "CatPanelRename"); namePrompt.value = oldName
        namePrompt.onAcceptFn = function(v) { panel.tagsCtl.renameCategory(id, v) }
        namePrompt.open()
    }
    function promptUniformColor(id) {
        colorDialog.targetCat = id
        colorDialog.selectedColor = panel.tagsCtl.categoryColor(id)
        colorDialog.open()
    }
    function promptDelete(id) { deleteCatId = id; confirmDelete.open() }
    property string deleteCatId: ""
    //  Derselbe Weg für Tags: erst fragen. Ein gelöschter Tag verschwindet aus
    //  ALLEN Dateien des Ordners - das darf kein Versehen sein.
    function promptDeleteTag(name) { deleteTagName = name; confirmDeleteTag.open() }
    property string deleteTagName: ""

    // ── Gemeinsamer Abschnittskopf (Titel + „+"-Button) ───────────────────────
    //  EIN Kopf-Baustein für beide Abschnitte -> einheitliche Steuerung ohne
    //  UI-Duplikation.
    //  Einen neuen Tag anlegen (Kopf-Knopf UND Kontextmenü nutzen denselben Weg).
    function promptNewTag() {
        namePrompt.title = App.uiText(App.language, "CatPanelNewTag")
        namePrompt.value = ""
        namePrompt.onAcceptFn = function(v) { panel.tagsCtl.createTag(v, Qt.rgba(0, 0.7, 0.63, 1)) }
        namePrompt.open()
    }
    function promptNewCategory() {
        namePrompt.title = App.uiText(App.language, "CatPanelAddCategory")
        namePrompt.value = ""
        namePrompt.onAcceptFn = function(v) { panel.tagsCtl.addRootCategory(v, Qt.rgba(0, 0.7, 0.63, 1), false) }
        namePrompt.open()
    }

    //  Suchfeld eines Abschnitts - dieselbe Optik wie die Suche der Filterleiste.
    component SectionSearch: Rectangle {
        id: sf
        property alias text: sfInput.text
        property string placeholder: ""
        height: 26; radius: 6
        color: App.themeCard
        border.color: sfInput.activeFocus ? App.themeAccent : App.themeBorder
        border.width: 1

        DrawnIcon {
            id: sfIcon
            anchors { left: parent.left; leftMargin: 7; verticalCenter: parent.verticalCenter }
            name: "search"; size: 12
            color: sfInput.activeFocus ? App.themeAccent : App.themeTextMuted
        }
        TextField {
            id: sfInput
            anchors { left: sfIcon.right; leftMargin: 5; right: sfClear.left; rightMargin: 4
                      verticalCenter: parent.verticalCenter }
            height: parent.height - 2
            padding: 0
            font.pixelSize: 11
            color: App.themeTextPrimary
            placeholderText: sf.placeholder
            background: null
        }
        //  Leeren - solange etwas drinsteht, ist die „hinzufügen"-Zeile weg,
        //  der Knopf ist also der Weg zurück (Festlegung des Nutzers).
        Rectangle {
            id: sfClear
            anchors { right: parent.right; rightMargin: 5; verticalCenter: parent.verticalCenter }
            visible: sfInput.text.length > 0
            width: 16; height: 16; radius: 8
            color: sfClearHover.hovered ? Qt.rgba(1, 1, 1, 0.16) : "transparent"
            DrawnIcon { anchors.centerIn: parent; name: "close"; size: 9
                        color: App.themeTextMuted }
            HoverHandler { id: sfClearHover }
            TapHandler { onTapped: sfInput.text = "" }
        }
    }

    // ── Rückgängig für TAG-Vorgänge ──────────────────────────────────────────
    //  BEWUSST GETRENNT vom Rückgängig der Dateien (Strg+Z in der Galerie,
    //  `MediaModel`): dieser Stapel trägt ausschließlich Tag- und
    //  Kategorie-Vorgänge des EIGENEN Ordners, und er hat deshalb auch kein
    //  Tastenkürzel - ein zweites Strg+Z wäre nicht vorhersagbar (Festlegung
    //  des Nutzers 2026-09-03).
    //  ── Die beiden Knöpfe ────────────────────────────────────────────
    //  Gezeichnete Pfeile (Regel 28), 30x30 - groß genug, um sie ohne
    //  Zielen zu treffen.
    //  Eine Marke: gezeichnetes Symbol (Mülleimer beim Löschen) plus die
    //  Stücke aus C++, jedes in seiner Farbe und kursiv, wenn es gekürzt ist.
    //  Leere Marke = keine Breite; die andere Seite bekommt den Platz.
    component MarkRow: Row {
        id: mr
        property var    marks: []
        property string iconName: ""
        property int    maxW: 0
        spacing: 3
        clip: true
        visible: mr.marks.length > 0

        DrawnIcon {
            anchors.verticalCenter: parent.verticalCenter
            visible: mr.iconName.length > 0
            width: visible ? 14 : 0
            name: mr.iconName.length > 0 ? mr.iconName : "trash"
            size: 14
            //  Der Mülleimer trägt die Farbe des ersten Stückes - so gehört er
            //  zur Richtung, für die er steht.
            color: mr.marks.length > 0 && mr.marks[0].color ? mr.marks[0].color
                                                            : App.themeTextMuted
        }
        Repeater {
            model: mr.marks
            delegate: Text {
                required property var modelData
                anchors.verticalCenter: parent.verticalCenter
                text: modelData.text
                color: modelData.color !== undefined && modelData.color !== null
                       ? modelData.color : App.themeTextPrimary
                font.pixelSize: 12
                font.italic: modelData.italic === true
                font.bold: true
                //  Zusammen dürfen die Stücke die halbe Leiste nicht sprengen.
                width: Math.min(implicitWidth, Math.max(24, mr.maxW - 20))
                elide: Text.ElideRight
            }
        }
        //  Der volle Text (Namen und Pfade ungekürzt) beim Zeigen darauf.
        HoverHandler { id: mrHover }
        ToolTip.visible: mrHover.hovered && mr.fullText.length > 0
        ToolTip.delay: 400
        ToolTip.text: mr.fullText
        readonly property string fullText: {
            var out = ""
            for (var i = 0; i < mr.marks.length; ++i) {
                var p = mr.marks[i]
                out += (p.full && p.full.length > 0) ? p.full : p.text
            }
            return out
        }
    }

    component ArrowBtn: Rectangle {
        id: ab
        property string iconName: ""
        property bool   on: false
        property string tip: ""
        signal clicked()
        width: 30; height: 30; radius: 6
        color: !ab.on ? "transparent"
             : (abHover.hovered ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                          App.themeAccent.b, 0.34)
                                : Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                          App.themeAccent.b, 0.18))
        border.color: ab.on ? App.themeAccent : "transparent"
        border.width: 1
        DrawnIcon {
            anchors.centerIn: parent
            name: ab.iconName; size: 15
            color: ab.on ? App.themeAccent : App.themeTextMuted
        }
        HoverHandler { id: abHover; cursorShape: ab.on ? Qt.PointingHandCursor
                                                       : Qt.ArrowCursor }
        TapHandler { enabled: ab.on; onTapped: ab.clicked() }
        ToolTip.visible: abHover.hovered && ab.tip.length > 0
        ToolTip.delay: 500
        ToolTip.text: ab.tip
    }

    //  Zwei Knöpfe in der MITTE, links die Marke dessen, was ZURÜCK täte, rechts
    //  die dessen, was VOR täte.
    //
    //  **Warum nicht die vergangene Tat:** dort stand vorher „was passiert ist",
    //  während der Knopf daneben das Gegenteil tat - ein Löschen zeigte `-1 T:b`,
    //  und Zurück fügte hinzu. Jetzt steht auf jeder Seite, was ihr eigener Knopf
    //  bewirken würde, in der Farbe DIESER Richtung (Zurück eines Löschens ist
    //  also grün). Ist eine Seite nicht möglich, bleibt sie LEER und ihr Knopf
    //  grau (Festlegung des Nutzers 2026-09-03).
    component UndoBar: Rectangle {
        id: ub
        color: Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.10)

        //  Trennlinie nach oben.
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 1
            color: App.themeBorder
        }

        readonly property bool hasL: panel.tagsCtl ? panel.tagsCtl.canUndo : false
        readonly property bool hasR: panel.tagsCtl ? panel.tagsCtl.canRedo : false
        //  Der Platz, den sich die beiden Seiten teilen. Ist eine leer, bekommt
        //  die andere alles - „die aktive Seite bekommt den Platz".
        readonly property int freeW: Math.max(0, ub.width - 78)
        readonly property int sideW: (ub.hasL && ub.hasR) ? Math.floor(ub.freeW / 2)
                                                          : ub.freeW

        //  Alles in EINER zentrierten Reihe: so stehen die Knöpfe in der Mitte
        //  des Inhalts, und eine leere Seite verschenkt keinen Platz.
        Row {
            anchors.centerIn: parent
            width: Math.min(parent.width - 12, implicitWidth)
            spacing: 6

            MarkRow {
                anchors.verticalCenter: parent.verticalCenter
                marks:  panel.tagsCtl ? panel.tagsCtl.undoMark : []
                iconName: panel.tagsCtl ? panel.tagsCtl.undoIcon : ""
                maxW: ub.sideW
            }
            ArrowBtn {
                objectName: "undoBtn"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "undo"
                on: ub.hasL
                tip: App.uiText(App.language, "TagUndoTip")
                onClicked: panel.tagsCtl.undoLast()
            }
            ArrowBtn {
                objectName: "redoBtn"
                anchors.verticalCenter: parent.verticalCenter
                iconName: "redo"
                on: ub.hasR
                tip: App.uiText(App.language, "TagUndoTip2")
                onClicked: panel.tagsCtl.redoLast()
            }
            MarkRow {
                anchors.verticalCenter: parent.verticalCenter
                marks:  panel.tagsCtl ? panel.tagsCtl.redoMark : []
                iconName: panel.tagsCtl ? panel.tagsCtl.redoIcon : ""
                maxW: ub.sideW
            }
        }
    }

    component SectionHeader: Rectangle {
        id: hdr
        property string title: ""
        property string addTip: ""
        signal addClicked()

        height: 34
        color: App.themeToolbarBg

        Text {
            anchors.left: parent.left; anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: hdr.title
            color: App.themeTextPrimary
            font.pixelSize: 13; font.bold: true
        }
        Rectangle {
            id: addBtn
            anchors.right: parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 22; height: 22; radius: 11
            color: addHover.hovered ? Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.32)
                                    : Qt.rgba(App.themeAccent.r, App.themeAccent.g, App.themeAccent.b, 0.18)
            border.color: App.themeAccent; border.width: 1
            Text {
                anchors.centerIn: parent
                text: "+"
                color: App.themeAccent
                font.pixelSize: 14; font.bold: true
            }
            HoverHandler { id: addHover }
            TapHandler { onTapped: hdr.addClicked() }
            ToolTip.text: hdr.addTip
            ToolTip.visible: addHover.hovered && hdr.addTip.length > 0
        }
    }

    Column {
        anchors.fill: parent

        // ── Abschnitt 1: Tags ─────────────────────────────────────────────────
        SectionHeader {
            visible: panel.showTagsSection
            width: parent.width
            title: App.uiText(App.language, "PanelSectionTags")
            addTip: App.uiText(App.language, "PanelAddTagTip")
            onAddClicked: panel.promptNewTag()
        }

        ScrollView {
            id: tagsArea
            visible: panel.showTagsSection
            width: parent.width
            // Natürliche Höhe, gedeckelt auf ~35 % des Panels (bzw. volle Höhe,
            // wenn der Kategorien-Abschnitt ausgeblendet ist); darüber scrollbar.
            height: Math.min(tagsCol.implicitHeight,
                             panel.showCategoriesSection ? Math.floor(panel.height * 0.35)
                                                         : panel.height - 34 - panel.undoBarHeight)
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Column {
                id: tagsCol
                width: panel.width - 12
                x: 6
                topPadding: 6; bottomPadding: 6
                spacing: 6

                SectionSearch {
                    id: tagSearch
                    width: parent.width
                    placeholder: App.uiText(App.language, "PanelSearchTag")
                }

                Flow {
                    id: tagsFlow
                    width: parent.width
                    spacing: 4

                    Repeater {
                        model: panel.filterList(panel.allTagsModel, tagSearch.text)
                        delegate: Rectangle {
                            id: pChip
                            required property var modelData

                            readonly property color tc: panel.tagColorOf(pChip.modelData)
                            // Klarer Toggle-Zustand: aktiv = gefüllt + Häkchen + kräftiger Rand.
                            readonly property bool active: panel.isTagActive(pChip.modelData)

                            //  ── Chip in eine Kategorie ziehen ──────────────
                            //  Dieselben Nutzdaten wie die Chips UNTER einer
                            //  Kategorie (`CategoryNode`), damit die vorhandene
                            //  Ablegefläche des Kategorie-Kopfes beide annimmt.
                            //  `dragFromCat` bleibt leer: dieser Chip kommt aus
                            //  der Liste, nicht aus einer Kategorie - deshalb
                            //  wird beim Ablegen nur HINZUGEFÜGT, und ein Zug
                            //  ins Leere nimmt ihn nirgendwo weg.
                            property string dragTag: modelData
                            property string dragFromCat: ""
                            //  **Der Chip muss an seinen Platz zurück.** Ein
                            //  `DragHandler` VERSCHIEBT sein Ziel; das `Flow`
                            //  darüber setzt `x`/`y` aber nur beim Auslegen neu.
                            //  Ohne das Zurücksetzen blieb der Chip liegen, wo
                            //  man ihn fallen ließ - er sah aus, als sei er aus
                            //  der Tag-Liste verschwunden, und kam erst beim
                            //  Aus- und Einschalten des Panels wieder
                            //  (Nutzerbefund 2026-09-03).
                            property real homeX: 0
                            property real homeY: 0
                            Drag.active: pDrag.active
                            Drag.source: pChip
                            Drag.hotSpot.x: width / 2
                            Drag.hotSpot.y: height / 2
                            z: pDrag.active ? 10 : 0
                            DragHandler {
                                id: pDrag
                                onActiveChanged: {
                                    if (active) {
                                        pChip.homeX = pChip.x; pChip.homeY = pChip.y
                                        return
                                    }
                                    pChip.Drag.drop()          // erst zustellen …
                                    pChip.x = pChip.homeX      // … dann zurück
                                    pChip.y = pChip.homeY
                                }
                            }

                            height: 24; radius: 12
                            width: pRow.implicitWidth + 16
                            color: active ? Qt.rgba(tc.r, tc.g, tc.b, 0.42)
                                          : Qt.rgba(tc.r, tc.g, tc.b, 0.10)
                            border.color: active ? tc : App.themeBorder
                            border.width: active ? 2 : 1

                            Row {
                                id: pRow
                                anchors.centerIn: parent; spacing: 5
                                Text {
                                    visible: pChip.active
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "\u2713"; color: App.themeTextPrimary
                                    font.pixelSize: 10; font.bold: true
                                }
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 8; height: 8; radius: 4; color: pChip.tc
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: pChip.modelData
                                    color: pChip.active ? App.themeTextPrimary : App.themeTextMuted
                                    font.pixelSize: 11
                                }
                            }

                            //  ── Kachel auf den Tag ziehen ⇒ Datei bekommt ihn ──
                            //  Die Kachel zieht als PLATTFORM-Zug hinaus
                            //  (`Drag.Automatic`, `text/uri-list`, s. MediaTile) -
                            //  landet er wieder im eigenen Fenster, kommt er hier
                            //  als gewöhnlicher Datei-Drop an, genau wie einer aus
                            //  dem Dateimanager. Deshalb funktioniert dieselbe
                            //  Fläche auch für von außen hereingezogene Dateien.
                            //  Zugewiesen wird per `addTag` (nie umgeschaltet):
                            //  ein Zug ist eine Zuweisung, kein Schalter.
                            DropArea {
                                id: chipDrop
                                anchors.fill: parent
                                keys: ["text/uri-list"]
                                onDropped: function(drop) {
                                    if (!drop.hasUrls) { drop.accepted = false; return }
                                    panel.dropFilesOnTag(drop.urls, pChip.modelData)
                                    drop.acceptProposedAction()
                                }
                            }
                            //  Rückmeldung beim Ziehen darüber - sonst rät man,
                            //  ob der Chip den Zug überhaupt annimmt.
                            Rectangle {
                                anchors.fill: parent
                                radius: parent.radius
                                visible: chipDrop.containsDrag
                                color: "transparent"
                                border.color: App.themeAccent
                                border.width: 2
                            }

                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: panel.toggleTag(pChip.modelData)
                            }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: pChipMenu.open()
                            }
                            ThemedMenu {
                                id: pChipMenu
                                MenuItem { text: App.uiText(App.language, "ModeAddToTag"); onTriggered: panel.requestAddToTagMode(pChip.modelData) }
                                MenuItem { text: App.uiText(App.language, "ModeGroup");    onTriggered: panel.requestGroupMode(pChip.modelData) }
                                MenuSeparator {}
                                MenuItem { text: "+  " + App.uiText(App.language, "CatPanelNewTag")
                                           onTriggered: panel.promptNewTag() }
                                MenuItem { text: App.uiText(App.language, "SettingsTagDelete")
                                           onTriggered: panel.promptDeleteTag(pChip.modelData) }
                            }
                        }
                    }
                }

                Text {
                    visible: panel.allTagsModel.length === 0
                             || (tagSearch.text.length > 0
                                 && panel.filterList(panel.allTagsModel, tagSearch.text).length === 0)
                    text: tagSearch.text.length > 0
                          ? App.uiText(App.language, "PanelSearchNoHit")
                          : App.uiText(App.language, "PanelNoTags")
                    color: App.themeTextMuted; font.pixelSize: 12
                }

                //  Rechtsklick auf die FREIE Fläche des Abschnitts legt ebenfalls
                //  an - dort steht man, wenn noch kein Tag da ist. Liegt dagegen
                //  ein Chip unter dem Zeiger, gehört der Klick IHM (er bietet
                //  zusätzlich „Tag löschen"); ohne diese Prüfung öffnete sich
                //  hier das falsche Menü (Nutzerbild `deleteTag.png`).
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function(point) {
                        const p = tagsCol.mapToItem(tagsFlow, point.position.x,
                                                    point.position.y)
                        if (tagsFlow.childAt(p.x, p.y)) return
                        tagAreaMenu.open()
                    }
                }
                ThemedMenu {
                    id: tagAreaMenu
                    MenuItem { text: "+  " + App.uiText(App.language, "CatPanelNewTag")
                               onTriggered: panel.promptNewTag() }
                }
            }
        }

        // Visuelle Trennung der beiden Abschnitte.
        Rectangle {
            visible: panel.showTagsSection && panel.showCategoriesSection
            width: parent.width; height: 1; color: App.themeBorder
        }

        // ── Abschnitt 2: Kategorien ───────────────────────────────────────────
        SectionHeader {
            visible: panel.showCategoriesSection
            width: parent.width
            title: App.uiText(App.language, "SettingsTabCategories")
            addTip: App.uiText(App.language, "PanelAddCategoryTip")
            onAddClicked: panel.promptNewCategory()
        }

        ScrollView {
            visible: panel.showCategoriesSection
            width: parent.width
            // Resthöhe unter dem (ggf. ausgeblendeten) Tags-Abschnitt.
            height: panel.height
                    - (panel.showTagsSection ? 34 + tagsArea.height : 0)
                    - (panel.showTagsSection && panel.showCategoriesSection ? 1 : 0)
                    - 34 - panel.undoBarHeight
            clip: true

            Column {
                id: treeColumn
                width: panel.width - 12
                x: 6
                //  Gleiche Luft wie im Tag-Abschnitt darüber: dort stand das
                //  Suchfeld frei, hier klebte es an Kopfzeile und Baum.
                topPadding: 6; bottomPadding: 6
                spacing: 6

                SectionSearch {
                    id: catSearch
                    width: parent.width
                    placeholder: App.uiText(App.language, "PanelSearchCategory")
                }

                //  WÄHREND der Suche eine flache Trefferliste statt des Baums:
                //  wer sucht, will den Treffer anklicken und nicht erst den Pfad
                //  aufklappen. Ein Klick wählt die Kategorie wie im Baum.
                Repeater {
                    model: catSearch.text.length > 0 ? panel.filterCats(catSearch.text) : []
                    delegate: Rectangle {
                        id: hitRow
                        required property var modelData
                        width: treeColumn.width
                        height: 26
                        radius: 6
                        readonly property bool on: panel.activeCategories.indexOf(hitRow.modelData.id) >= 0
                        color: hitRow.on ? Qt.rgba(App.themeAccent.r, App.themeAccent.g,
                                                   App.themeAccent.b, 0.28)
                             : (hitHover.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent")
                        border.color: hitRow.on ? App.themeAccent : App.themeBorder
                        border.width: 1
                        Text {
                            anchors { left: parent.left; leftMargin: 8; right: parent.right
                                      rightMargin: 8; verticalCenter: parent.verticalCenter }
                            text: hitRow.modelData.name
                            color: App.themeTextPrimary
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        HoverHandler { id: hitHover }
                        TapHandler {
                            onTapped: panel.toggleCategory(hitRow.modelData.id, !hitRow.on)
                        }
                    }
                }

                Text {
                    visible: catSearch.text.length > 0
                             && panel.filterCats(catSearch.text).length === 0
                    text: App.uiText(App.language, "PanelSearchNoHit")
                    color: App.themeTextMuted; font.pixelSize: 12
                    topPadding: 8
                }

                Repeater {
                    model: catSearch.text.length > 0 ? [] : panel.tree
                    delegate: CategoryNode {
                        required property var modelData
                        width: treeColumn.width
                        node: modelData
                        depth: 0
                        panel: panel
                    }
                }

                Text {
                    visible: panel.tree.length === 0 && catSearch.text.length === 0
                    text: App.uiText(App.language, "TagPanelEmpty")
                    color: App.themeTextMuted; font.pixelSize: 12
                    topPadding: 12
                }

                //  Rechtsklick auf die freie Fläche legt eine Kategorie an -
                //  derselbe Griff wie im Tag-Abschnitt darüber, mit derselben
                //  Prüfung: liegt eine Kategorie (oder das Suchfeld) unter dem
                //  Zeiger, gehört der Klick dorthin.
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: function(point) {
                        if (treeColumn.childAt(point.position.x, point.position.y)) return
                        catAreaMenu.open()
                    }
                }
                ThemedMenu {
                    id: catAreaMenu
                    MenuItem { text: "+  " + App.uiText(App.language, "CatPanelAddCategory")
                               onTriggered: panel.promptNewCategory() }
                }
            }
        }
    }

    // ── Namens-Prompt ───────────────────────────────────────────────────────
    Popup {
        id: namePrompt
        modal: true; focus: true; anchors.centerIn: Overlay.overlay; padding: 16
        property string title: ""
        property string value: ""
        property var onAcceptFn: (function(v){})
        background: Rectangle { color: App.themeCard; radius: 10; border.color: App.themeBorder }
        contentItem: Column {
            spacing: 12
            Text { text: namePrompt.title; color: App.themeTextPrimary; font.pixelSize: 14; font.bold: true }
            TextField {
                id: promptField
                width: 260
                text: namePrompt.value
                color: App.themeTextPrimary
                onAccepted: namePrompt.commit()
            }
            Row {
                spacing: 8
                Button { text: App.uiText(App.language, "SettingsOk"); onClicked: namePrompt.commit() }
                Button { text: App.uiText(App.language, "SettingsCancel"); onClicked: namePrompt.close() }
            }
        }
        onOpened: { promptField.text = value; promptField.forceActiveFocus(); promptField.selectAll() }
        function commit() {
            var v = promptField.text.trim()
            if (v.length > 0) onAcceptFn(v)
            close()
        }
    }

    // ── Tag zu einer Kategorie hinzufügen ────────────────────────────────────
    //  ZWEI Wege in einem Fenster, durch eine Linie getrennt (Festlegung des
    //  Nutzers 2026-09-03):
    //   • oben ein Feld - Eingabe legt an UND weist zu; ein bereits vorhandener
    //     Name weist einfach den vorhandenen zu (`addTagToCategory` registriert
    //     ohnehin nur, was es noch nicht gibt).
    //   • unten die Tags des Ordners, die dieser Kategorie noch fehlen, als
    //     scrollbare Liste. Das Feld oben FILTERT sie zugleich - man tippt zwei
    //     Buchstaben und klickt, statt den Namen abzuschreiben.
    Popup {
        id: tagPick
        objectName: "tagPickPopup"
        modal: true; focus: true; anchors.centerIn: Overlay.overlay; padding: 14
        property string targetCat: ""

        //  Die Tags des Ordners OHNE die, die der Kategorie schon gehören.
        function candidates() {
            const node = panel._findNode(panel.tree, tagPick.targetCat)
            const drin = node ? node.tags : []
            const alle = panel.filterList(panel.allTagsModel, pickField.text)
            var out = []
            for (var i = 0; i < alle.length; ++i)
                if (drin.indexOf(alle[i]) < 0) out.push(alle[i])
            return out
        }
        function assign(tag) {
            const t = String(tag).trim()
            if (t.length === 0) return
            panel.tagsCtl.addTagToCategory(tagPick.targetCat, t)
            tagPick.close()
        }

        background: Rectangle { color: App.themeCard; radius: 10; border.color: App.themeBorder }
        onOpened: { pickField.text = ""; pickField.forceActiveFocus() }

        contentItem: Column {
            spacing: 10
            Text {
                text: App.uiText(App.language, "TagBarDropdownHeader")
                color: App.themeTextPrimary; font.pixelSize: 14; font.bold: true
            }
            TextField {
                id: pickField
                width: 280
                color: App.themeTextPrimary
                placeholderText: App.uiText(App.language, "CatPanelNewTag")
                onAccepted: tagPick.assign(pickField.text)
            }
            Button {
                //  Der ausdrückliche Weg „anlegen" - Enter im Feld tut dasselbe.
                enabled: pickField.text.trim().length > 0
                height: 26; font.pixelSize: 11
                text: App.uiText(App.language, "TagPickCreate")
                onClicked: tagPick.assign(pickField.text)
            }

            Rectangle { width: 280; height: 1; color: App.themeBorder }

            Text {
                text: App.uiText(App.language, "TagPickExisting")
                color: App.themeTextMuted; font.pixelSize: 11
            }
            //  Kleine, scrollbare Liste - sie darf das Fenster nicht sprengen.
            ScrollView {
                width: 280
                height: Math.min(180, Math.max(28, pickList.count * 28))
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ListView {
                    id: pickList
                    objectName: "tagPickList"
                    model: tagPick.candidates()
                    spacing: 2
                    delegate: Rectangle {
                        required property var modelData
                        width: 264; height: 26; radius: 5
                        color: rowHover.hovered
                               ? Qt.rgba(App.themeTextPrimary.r, App.themeTextPrimary.g,
                                         App.themeTextPrimary.b, 0.10)
                               : "transparent"
                        Row {
                            anchors { left: parent.left; leftMargin: 8
                                      verticalCenter: parent.verticalCenter }
                            spacing: 7
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 10; height: 10; radius: 5
                                color: panel.tagColorOf(parent.parent.modelData)
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: parent.parent.modelData
                                color: App.themeTextPrimary; font.pixelSize: 12
                            }
                        }
                        HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: tagPick.assign(parent.modelData) }
                    }
                }
            }
            Text {
                visible: pickList.count === 0
                width: 280
                text: App.uiText(App.language, "TagPickNone")
                color: App.themeTextMuted; font.pixelSize: 11; wrapMode: Text.WordWrap
            }

            Row {
                anchors.right: parent.right
                Button { text: App.uiText(App.language, "SettingsCancel")
                         height: 26; font.pixelSize: 11
                         onClicked: tagPick.close() }
            }
        }
    }

    // ── Farbwahl ────────────────────────────────────────────────────────────
    ColorDialog {
        id: colorDialog
        property string targetCat: ""
        // Einheitsfarbe aus dem Panel vererbt an den gesamten Teilbaum (Tags,
        // Unter- und verschachtelte Unterkategorien) - die erwartete Wirkung.
        onAccepted: panel.tagsCtl.setCategoryUniformColor(targetCat, true, selectedColor, true)
    }

    UndoBar {
        objectName: "undoBar"          // fuer `bench_tagpanel`
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                  leftMargin: 1; rightMargin: 1; bottomMargin: 1 }
        height: panel.undoBarHeight
        visible: height > 0
    }

    //  Rückfrage vor dem Löschen eines TAGS (s. `promptDeleteTag`). Ein
    //  gelöschter Tag verschwindet aus ALLEN Dateien des Ordners.
    Popup {
        id: confirmDeleteTag
        modal: true; focus: true; anchors.centerIn: Overlay.overlay; padding: 16
        //  **Eingabetaste bestaetigt** (Wunsch des Nutzers: JEDE Loeschrueckfrage,
        //  nicht nur die der Entf-Taste). Ein `Popup` bringt das - anders als ein
        //  `Dialog` - nicht mit, also von Hand. **Der Handler gehoert ans
        //  `contentItem`**, nicht ans Popup: dort feuert er nicht (gemessen,
        //  s. `GalleryView`).
        function confirm() {
            panel.tagsCtl.deleteTag(panel.deleteTagName)
            confirmDeleteTag.close()
        }

        background: Rectangle { color: App.themeCard; radius: 10; border.color: App.themeBorder }
        contentItem: Column {
            focus: true
            Keys.onReturnPressed: function(e) { confirmDeleteTag.confirm(); e.accepted = true }
            Keys.onEnterPressed:  function(e) { confirmDeleteTag.confirm(); e.accepted = true }
            spacing: 12
            Text {
                text: App.uiText(App.language, "SettingsTagDelete") + ": " + panel.deleteTagName
                color: App.themeTextPrimary; font.pixelSize: 14; font.bold: true
            }
            Row {
                spacing: 8
                Button {
                    text: App.uiText(App.language, "BookmarkDelete")
                    onClicked: confirmDeleteTag.confirm()
                }
                Button { text: App.uiText(App.language, "SettingsCancel")
                         onClicked: confirmDeleteTag.close() }
            }
        }
    }

    // ── Löschbestätigung ──────────────────────────────────────────────────────
    Popup {
        id: confirmDelete
        modal: true; focus: true; anchors.centerIn: Overlay.overlay; padding: 16
        //  **Eingabetaste bestaetigt** - s. `confirmDeleteTag`.
        function confirm() {
            panel.tagsCtl.deleteCategory(panel.deleteCatId)
            confirmDelete.close()
        }

        background: Rectangle { color: App.themeCard; radius: 10; border.color: App.themeBorder }
        contentItem: Column {
            focus: true
            Keys.onReturnPressed: function(e) { confirmDelete.confirm(); e.accepted = true }
            Keys.onEnterPressed:  function(e) { confirmDelete.confirm(); e.accepted = true }
            spacing: 12
            Text { text: App.uiText(App.language, "TagPanelDeleteTitle"); color: App.themeTextPrimary; font.pixelSize: 14; font.bold: true }
            Row {
                spacing: 8
                Button { text: App.uiText(App.language, "BookmarkDelete"); onClicked: confirmDelete.confirm() }
                Button { text: App.uiText(App.language, "SettingsCancel"); onClicked: confirmDelete.close() }
            }
        }
    }
}
