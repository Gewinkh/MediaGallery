#pragma once
#include "core/ISettings.h"
#include "editor/SyntaxPalette.h"

#include <QColor>
#include <QObject>
#include <QUrl>
#include <QVariantList>

// ─────────────────────────────────────────────────────────────────────────────
//  EditorController.h - QML-Singleton `Editor`: die globalen Einstellungen des
//  Texteditors, Farben WIE Verhalten.
//
//  Muster wie `PdfEdit` und `Docx`: ein Singleton fuer das, was appweit gilt,
//  waehrend der eigentliche Faerber dezentral je Kachel lebt (`CodeHighlighter`
//  als QML-TYP). Der Faerber hoert auf `paletteChanged` und faerbt neu.
//
//  Die Farben sind vom App-Theme GETRENNT (Festlegung des Nutzers 2026-09-02):
//  eigene Profile, eigener Konfigurator, eigener Ex-/Import. Ein Wechsel des
//  Oberflaechen-Themas laesst den Editor unberuehrt.
// ─────────────────────────────────────────────────────────────────────────────
namespace mg::editor {

class EditorController : public QObject {
    Q_OBJECT

    //  0…3 = Nightfall / Paper / Ember / Custom.
    Q_PROPERTY(int  profile READ profile WRITE setProfile NOTIFY paletteChanged)
    Q_PROPERTY(bool customActive READ customActive NOTIFY paletteChanged)

    //  Die Flaechenfarben der AKTIVEN Palette - QML bindet direkt darauf.
    Q_PROPERTY(QColor background       READ background       NOTIFY paletteChanged)
    Q_PROPERTY(QColor text             READ text             NOTIFY paletteChanged)
    Q_PROPERTY(QColor currentLine      READ currentLine      NOTIFY paletteChanged)
    Q_PROPERTY(QColor selection        READ selection        NOTIFY paletteChanged)
    Q_PROPERTY(QColor gutterBackground READ gutterBackground NOTIFY paletteChanged)
    Q_PROPERTY(QColor gutterText       READ gutterText       NOTIFY paletteChanged)
    Q_PROPERTY(QColor gutterTextActive READ gutterTextActive NOTIFY paletteChanged)

    //  Verhalten (KEINE Farbe - steht deshalb im Editor-Reiter, nicht im
    //  Design-Reiter).
    Q_PROPERTY(bool lineNumbers READ lineNumbers WRITE setLineNumbers NOTIFY behaviourChanged)
    Q_PROPERTY(bool highlightCurrentLine READ highlightCurrentLine
                   WRITE setHighlightCurrentLine NOTIFY behaviourChanged)
    Q_PROPERTY(bool softWrap READ softWrap WRITE setSoftWrap NOTIFY behaviourChanged)
    Q_PROPERTY(bool minimap READ minimap WRITE setMinimap NOTIFY behaviourChanged)
    Q_PROPERTY(bool folding READ folding WRITE setFolding NOTIFY behaviourChanged)
    Q_PROPERTY(bool indentGuides READ indentGuides WRITE setIndentGuides NOTIFY behaviourChanged)
    Q_PROPERTY(bool matchBrackets READ matchBrackets WRITE setMatchBrackets NOTIFY behaviourChanged)
    Q_PROPERTY(int  tabWidth READ tabWidth WRITE setTabWidth NOTIFY behaviourChanged)
    Q_PROPERTY(bool tabSpaces READ tabSpaces WRITE setTabSpaces NOTIFY behaviourChanged)

public:
    explicit EditorController(ISettings& settings, QObject* parent = nullptr);

    //  Die aktive Palette - vom Faerber gelesen, nicht von QML.
    const SyntaxPalette& palette() const { return m_palette; }

    int  profile() const { return m_profile; }
    void setProfile(int p);
    bool customActive() const { return m_profile == int(EditorProfile::Custom); }

    QColor background()       const { return m_palette.background; }
    QColor text()             const { return m_palette.text; }
    QColor currentLine()      const { return m_palette.currentLine; }
    QColor selection()        const { return m_palette.selection; }
    QColor gutterBackground() const { return m_palette.gutterBackground; }
    QColor gutterText()       const { return m_palette.gutterText; }
    QColor gutterTextActive() const { return m_palette.gutterTextActive; }

    //  ── Farbkonfigurator (Design-Reiter, Block „Text-Editor") ──────────────
    //  Die Oberflaeche spricht die Farben ueber ihren Schluessel an ("keyword",
    //  "background"), damit dreizehn Namen nicht in QML und C++ doppelt stehen.
    Q_INVOKABLE QColor colorFor(const QString& key) const;
    //  Der uebersetzte Anzeigename zu einem Farbschluessel. Er kommt aus C++
    //  und nicht aus QML, weil ein dort ZUSAMMENGESETZTER String-Key
    //  (`"EditorColor" + …`) fuer den Katalog-Treiber ein Torso ist - er
    //  prueft, dass jeder in QML benutzte Schluessel wirklich existiert, und
    //  das soll er auch koennen.
    Q_INVOKABLE QString colorLabel(const QString& key) const;
    //  Setzt eine Farbe. Wirkt NUR im Custom-Profil; in einem mitgelieferten
    //  Profil waere es ein stiller Verlust beim naechsten Profilwechsel.
    Q_INVOKABLE void   setColorFor(const QString& key, const QColor& c);
    //  Setzt das Custom-Profil auf die Vorgabe zurueck.
    Q_INVOKABLE void   resetCustom();
    //  Die eigene Palette als JSON sichern bzw. laden. Dasselbe Format, das in
    //  `[editor]/customPalette` steht - eine gesicherte Datei laesst sich also
    //  auch von Hand lesen. Beim Laden wird das Profil auf „Custom" gestellt,
    //  sonst saehe man von der geladenen Palette nichts.
    Q_INVOKABLE bool   exportPalette(const QUrl& fileUrl) const;
    Q_INVOKABLE bool   importPalette(const QUrl& fileUrl);
    //  Uebernimmt die Farben eines mitgelieferten Profils ins Custom-Profil -
    //  der uebliche Weg, ein eigenes Thema anzufangen.
    Q_INVOKABLE void   seedCustomFrom(int profile);

    //  Name eines Profils fuer die Karten im Design-Reiter.
    Q_INVOKABLE QString profileLabel(int p) const;
    //  Drei Vorschaufarben je Profilkarte (wie bei den Oberflaechen-Profilen).
    Q_INVOKABLE QVariantList profileSwatches(int p) const;

    bool lineNumbers() const { return m_lineNumbers; }
    void setLineNumbers(bool v);
    bool highlightCurrentLine() const { return m_highlightCurrentLine; }
    void setHighlightCurrentLine(bool v);
    bool softWrap() const { return m_softWrap; }
    void setSoftWrap(bool v);
    bool minimap() const { return m_minimap; }
    void setMinimap(bool v);
    bool folding() const { return m_folding; }
    void setFolding(bool v);
    bool indentGuides() const { return m_indentGuides; }
    void setIndentGuides(bool v);
    bool matchBrackets() const { return m_matchBrackets; }
    void setMatchBrackets(bool v);
    int  tabWidth() const { return m_tabWidth; }
    void setTabWidth(int zeichen);
    bool tabSpaces() const { return m_tabSpaces; }
    void setTabSpaces(bool v);

signals:
    void paletteChanged();
    void behaviourChanged();

private:
    void ladePalette();

    ISettings&    m_settings;
    SyntaxPalette m_palette;
    int  m_profile = 0;
    bool m_lineNumbers = true;
    bool m_highlightCurrentLine = true;
    bool m_softWrap = true;
    bool m_minimap = false;
    bool m_folding = true;
    bool m_indentGuides = true;
    bool m_matchBrackets = true;
    int  m_tabWidth = 4;
    bool m_tabSpaces = true;
};

//  Der Faerber braucht die aktive Palette, ohne den Controller zu kennen.
//  Gesetzt EINMAL in main.cpp, gleich nach dem Erzeugen.
void setActiveController(EditorController* c);
EditorController* activeController();

}  // namespace mg::editor
