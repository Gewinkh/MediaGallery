#include "editor/EditorController.h"
#include <QSaveFile>
#include <QFile>

#include "core/Strings.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace Qt::Literals::StringLiterals;

namespace mg::editor {
namespace {

//  Der aktive Controller. Ein Zeiger und kein Singleton-Muster: es gibt genau
//  eine Instanz, sie gehoert `main.cpp`, und der Faerber braucht nur Lesezugriff
//  auf die Palette. Ein eigenes `instance()` wuerde die Lebensdauer verschleiern
//  (die Bridges werden VOR der Engine erzeugt und NACH ihr zerstoert).
EditorController* s_active = nullptr;

EditorProfile toProfile(int p) {
    return (p >= 0 && p < kEditorProfileCount) ? EditorProfile(p)
                                               : EditorProfile::Nightfall;
}

}  // namespace

void setActiveController(EditorController* c) { s_active = c; }
EditorController* activeController() { return s_active; }

EditorController::EditorController(ISettings& settings, QObject* parent)
    : QObject(parent), m_settings(settings) {
    m_profile             = m_settings.editorProfile();
    m_lineNumbers         = m_settings.editorLineNumbers();
    m_highlightCurrentLine= m_settings.editorHighlightCurrentLine();
    m_softWrap            = m_settings.editorSoftWrap();
    m_minimap             = m_settings.editorMinimap();
    m_folding              = m_settings.editorFolding();
    m_indentGuides         = m_settings.editorIndentGuides();
    m_matchBrackets        = m_settings.editorMatchBrackets();
    m_tabWidth            = m_settings.editorTabWidth();
    m_tabSpaces           = m_settings.editorTabSpaces();
    ladePalette();
}

void EditorController::ladePalette() {
    const EditorProfile p = toProfile(m_profile);
    if (p != EditorProfile::Custom) {
        m_palette = paletteForProfile(p);
        return;
    }
    //  Custom: gespeicherte Farben lesen. Fehlt oder faellt die Datei aus,
    //  bleibt die eingebaute Vorgabe stehen - nie eine schwarze Flaeche.
    const QString json = m_settings.editorCustomPalette();
    if (json.isEmpty()) {
        m_palette = paletteForProfile(EditorProfile::Custom);
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    m_palette = doc.isObject() ? SyntaxPalette::fromJson(doc.object())
                               : paletteForProfile(EditorProfile::Custom);
}

void EditorController::setProfile(int p) {
    if (p == m_profile || p < 0 || p >= kEditorProfileCount) return;
    m_profile = p;
    m_settings.setEditorProfile(p);
    ladePalette();
    emit paletteChanged();
}

QColor EditorController::colorFor(const QString& key) const {
    if (key == QLatin1String("background"))       return m_palette.background;
    if (key == QLatin1String("text"))             return m_palette.text;
    if (key == QLatin1String("currentLine"))      return m_palette.currentLine;
    if (key == QLatin1String("selection"))        return m_palette.selection;
    if (key == QLatin1String("gutterBackground")) return m_palette.gutterBackground;
    if (key == QLatin1String("gutterText"))       return m_palette.gutterText;
    if (key == QLatin1String("gutterTextActive")) return m_palette.gutterTextActive;
    const Tok t = tokenFromKey(key);
    return t == Tok::Count ? QColor() : m_palette.colorFor(t);
}

QString EditorController::colorLabel(const QString& key) const {
    struct Zeile { QLatin1StringView key; StringKey s; };
    static const Zeile tabelle[] = {
        { "background"_L1,       StringKey::EditorColorBackground },
        { "text"_L1,             StringKey::EditorColorText },
        { "currentLine"_L1,      StringKey::EditorColorCurrentLine },
        { "selection"_L1,        StringKey::EditorColorSelection },
        { "gutterBackground"_L1, StringKey::EditorColorGutterBackground },
        { "gutterText"_L1,       StringKey::EditorColorGutterText },
        { "gutterTextActive"_L1, StringKey::EditorColorGutterTextActive },
        { "keyword"_L1,          StringKey::EditorColorKeyword },
        { "type"_L1,             StringKey::EditorColorType },
        { "string"_L1,           StringKey::EditorColorString },
        { "number"_L1,           StringKey::EditorColorNumber },
        { "comment"_L1,          StringKey::EditorColorComment },
        { "preproc"_L1,          StringKey::EditorColorPreproc },
        { "function"_L1,         StringKey::EditorColorFunction },
        { "operator"_L1,         StringKey::EditorColorOperator },
        { "heading"_L1,          StringKey::EditorColorHeading },
        { "emphasis"_L1,         StringKey::EditorColorEmphasis },
        { "link"_L1,             StringKey::EditorColorLink },
        { "code"_L1,             StringKey::EditorColorCode },
    };
    for (const Zeile& z : tabelle)
        if (key == z.key) return Strings::get(z.s);
    return key;
}

void EditorController::setColorFor(const QString& key, const QColor& c) {
    //  Nur im Custom-Profil: in einem mitgelieferten Profil waere die Aenderung
    //  beim naechsten Profilwechsel still weg. Die Oberflaeche graut den
    //  Konfigurator dort ohnehin aus (`customActive`).
    if (!customActive() || !c.isValid()) return;

    bool getroffen = true;
    if      (key == QLatin1String("background"))       m_palette.background = c;
    else if (key == QLatin1String("text"))           { m_palette.text = c;
                                                       m_palette.tok[int(Tok::Normal)] = c; }
    else if (key == QLatin1String("currentLine"))      m_palette.currentLine = c;
    else if (key == QLatin1String("selection"))        m_palette.selection = c;
    else if (key == QLatin1String("gutterBackground")) m_palette.gutterBackground = c;
    else if (key == QLatin1String("gutterText"))       m_palette.gutterText = c;
    else if (key == QLatin1String("gutterTextActive")) m_palette.gutterTextActive = c;
    else {
        const Tok t = tokenFromKey(key);
        if (t == Tok::Count || t == Tok::Normal) getroffen = false;
        else m_palette.tok[int(t)] = c;
    }
    if (!getroffen) return;

    m_settings.setEditorCustomPalette(
        QString::fromUtf8(QJsonDocument(m_palette.toJson()).toJson(QJsonDocument::Compact)));
    emit paletteChanged();
}

bool EditorController::exportPalette(const QUrl& fileUrl) const {
    const QString pfad = fileUrl.isLocalFile() ? fileUrl.toLocalFile()
                                               : fileUrl.toString();
    if (pfad.isEmpty()) return false;
    //  Gesichert wird, was gerade GILT - nicht nur die eigene Palette: wer
    //  „Nightfall" offen hat und sichert, erwartet Nightfall in der Datei.
    QSaveFile f(pfad);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QJsonDocument doc(m_palette.toJson());
    if (f.write(doc.toJson(QJsonDocument::Indented)) < 0) return false;
    return f.commit();
}

bool EditorController::importPalette(const QUrl& fileUrl) {
    const QString pfad = fileUrl.isLocalFile() ? fileUrl.toLocalFile()
                                               : fileUrl.toString();
    if (pfad.isEmpty()) return false;
    QFile f(pfad);
    if (!f.open(QIODevice::ReadOnly)) return false;
    //  Deckel: eine Themendatei hat ein paar hundert Bytes. Alles darueber ist
    //  keine - und soll nicht erst eingelesen werden.
    if (f.size() > 256 * 1024) return false;

    QJsonParseError fehler{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &fehler);
    if (fehler.error != QJsonParseError::NoError || !doc.isObject()) return false;

    m_palette = SyntaxPalette::fromJson(doc.object());
    m_settings.setEditorCustomPalette(
        QString::fromUtf8(QJsonDocument(m_palette.toJson()).toJson(QJsonDocument::Compact)));
    //  Ohne den Profilwechsel bliebe die geladene Palette unsichtbar.
    setProfile(int(EditorProfile::Custom));
    emit paletteChanged();
    return true;
}

void EditorController::resetCustom() {
    if (!customActive()) return;
    m_palette = paletteForProfile(EditorProfile::Custom);
    m_settings.setEditorCustomPalette(QString());
    emit paletteChanged();
}

void EditorController::seedCustomFrom(int profile) {
    SyntaxPalette p = paletteForProfile(toProfile(profile));
    p.name = profileName(EditorProfile::Custom);
    m_palette = p;
    m_settings.setEditorCustomPalette(
        QString::fromUtf8(QJsonDocument(p.toJson()).toJson(QJsonDocument::Compact)));
    //  Der Aufruf ist der Anfang eines eigenen Themas - also auch hinschalten.
    if (!customActive()) {
        m_profile = int(EditorProfile::Custom);
        m_settings.setEditorProfile(m_profile);
    }
    emit paletteChanged();
}

QString EditorController::profileLabel(int p) const {
    return profileName(toProfile(p));
}

QVariantList EditorController::profileSwatches(int p) const {
    //  Dieselben drei Proben wie bei den Oberflaechen-Profilen: Flaeche,
    //  Schluesselwort, Zeichenkette - daran erkennt man ein Editor-Thema.
    const SyntaxPalette t = (toProfile(p) == EditorProfile::Custom && customActive())
                                ? m_palette
                                : paletteForProfile(toProfile(p));
    return { t.background, t.tok[int(Tok::Keyword)], t.tok[int(Tok::String)] };
}

void EditorController::setLineNumbers(bool v) {
    if (v == m_lineNumbers) return;
    m_lineNumbers = v;
    m_settings.setEditorLineNumbers(v);
    emit behaviourChanged();
}

void EditorController::setHighlightCurrentLine(bool v) {
    if (v == m_highlightCurrentLine) return;
    m_highlightCurrentLine = v;
    m_settings.setEditorHighlightCurrentLine(v);
    emit behaviourChanged();
}

void EditorController::setMinimap(bool v) {
    if (v == m_minimap) return;
    m_minimap = v;
    m_settings.setEditorMinimap(v);
    emit behaviourChanged();
}

void EditorController::setFolding(bool v) {
    if (v == m_folding) return;
    m_folding = v;
    m_settings.setEditorFolding(v);
    emit behaviourChanged();
}

void EditorController::setIndentGuides(bool v) {
    if (v == m_indentGuides) return;
    m_indentGuides = v;
    m_settings.setEditorIndentGuides(v);
    emit behaviourChanged();
}

void EditorController::setMatchBrackets(bool v) {
    if (v == m_matchBrackets) return;
    m_matchBrackets = v;
    m_settings.setEditorMatchBrackets(v);
    emit behaviourChanged();
}

void EditorController::setTabWidth(int zeichen) {
    const int neu = qBound(2, zeichen, 8);
    if (neu == m_tabWidth) return;
    m_tabWidth = neu;
    m_settings.setEditorTabWidth(neu);
    emit behaviourChanged();
}

void EditorController::setTabSpaces(bool v) {
    if (v == m_tabSpaces) return;
    m_tabSpaces = v;
    m_settings.setEditorTabSpaces(v);
    emit behaviourChanged();
}

void EditorController::setSoftWrap(bool v) {
    if (v == m_softWrap) return;
    m_softWrap = v;
    m_settings.setEditorSoftWrap(v);
    emit behaviourChanged();
}

}  // namespace mg::editor
