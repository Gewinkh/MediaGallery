#pragma once
#include <QString>
#include <QObject>
#include "AppSettings.h"

/**
 * Central translation system.
 * All UI strings are looked up here via Language enum.
 * Add new keys to the StringKey enum, then add translations in Strings.cpp.
 */
namespace SK { Q_NAMESPACE
enum class StringKey {
    // MainWindow / Menus
    MenuFile,
    MenuView,
    MenuSettings,
    MenuOpenFolder,
    MenuRefresh,
    MenuQuit,
    MenuToggleOptions,
    MenuSettingsItem,
    MenuLanguage,
    MenuVideoPlayback,
    MenuVideoNative,
    MenuVideoExternal,

    // FilterBar
    FilterAudio,
    FilterMedia,
    FilterDate,
    FilterName,
    FilterTags,
    FilterFileSize,
    FilterTagModeAnd,
    FilterTagModeOr,
    FilterTagModeNur,
    FilterTagModeInklusiv,

    // SettingsDialog
    SettingsTitle,
    SettingsTabGeneral,
    SettingsTabDesign,
    SettingsLanguageLabel,
    SettingsOk,
    SettingsCancel,
    SettingsTagDelete,

    // MetadataEditor
    MetaTitle,
    MetaReset,

    // TagBar / TagWidget
    TagBarPlaceholder,
    TagBarDropdownHeader,

    // Category panel / context menus
    CatPanelAddCategory,
    CatPanelNewSubcategory,
    CatPanelRename,
    CatPanelDelete,
    CatPanelSetColor,
    CatPanelNewTag,

    // FilterBar / HoverDropdown
    FilterCatNewName,
    FilterTagRenamePrompt,

    // SettingsDialog – Categories tab
    SettingsTabCategories,
    SettingsCatAdd,
    SettingsCatNewLabel,
    SettingsCatRenameTitle,
    SettingsTagColorTitle,

    // SettingsDialog – Design tab
    SettingsDesignBaseColors,
    SettingsDesignBgGradient,
    SettingsDesignAccent,
    SettingsDesignAccentSolid,
    SettingsDesignAccentGradient,
    SettingsDesignTileTransparent,
    SettingsDesignExportTitle,
    SettingsDesignImportTitle,

    // Converter (alle Richtungen: Tag ↔ Unterkategorie ↔ Kategorie)
    ConverterTagToSubcat,
    ConverterSubcatToTag,
    ConverterTagToCat,
    ConverterCatToTag,
    ConverterSubcatToCat,
    ConverterCatToSubcat,

    // Saved Folders / Bookmarks
    MenuBookmarks,
    MenuBookmarksEmpty,
    BookmarkAdd,
    BookmarkEdit,
    BookmarkDelete,
    BookmarkPathLabel,
    BookmarkBrowse,

    // Text editor / filter
    EditorAutoSave,
    EditorSave,
    // ── Migration: vereinheitlichte UI-Strings (vormals qsTr/hartcodiert) ──
    SettingsGenFullscreenAnim,
    SettingsGenAnimSlide,
    SettingsGenAnimFade,
    SettingsGenAudioPlayer,
    SettingsGenAudioAccentTheme,
    SettingsGenAudioAccentApple,
    SettingsGenRenderBackend,
    SettingsGenBackendLabel,
    SettingsGenBackendAuto,
    SettingsGenBackendSoftware,
    SettingsGenActiveBackend,
    SettingsGenSoftwareWarning,
    SettingsGenVideoInternal,
    SettingsGenVideoExternal,
    SettingsGenSaveClose,
    SettingsViewTileSize,
    SettingsViewWidth,
    SettingsViewHeight,
    SettingsViewTileArrangement,
    SettingsViewAlignLeft,
    SettingsViewAlignCenter,
    SettingsViewAlignRight,
    SettingsViewAlignManual,
    SettingsViewArrangementHint,
    SettingsViewZoomHint,
    SettingsDesignProfileLabel,
    SettingsDesignCustomHint,
    SettingsDesignTextPrimary,
    SettingsDesignTextMuted,
    SettingsDesignFrame,
    SettingsDesignBackground,
    SettingsDesignAccentType,
    SettingsDesignAccentColorLabel,
    SettingsDesignAccentGradEnd,
    SettingsDesignGradStartLabel,
    SettingsDesignGradEndLabel,
    SettingsDesignGradAngle,
    SettingsDesignAngleLabel,
    SettingsDesignGlow,
    SettingsDesignGlowRadiusLabel,
    SettingsDesignGlowIntensityLabel,
    SettingsDesignGlowHover,
    SettingsDesignCardTile,
    SettingsDesignTileBgLabel,
    SettingsDesignTileColorLabel,
    SettingsDesignTileGradEndLabel,
    SettingsDesignGradInsteadSolid,
    SettingsDesignTypeLabel,
    SettingsDesignBars,
    SettingsDesignMenuBar,
    SettingsDesignToolbar,
    SettingsDesignFilterBar,
    SettingsDesignStatusBar,
    SettingsDesignPdfViewer,
    SettingsDesignSidebar,
    SettingsDesignScrollbar,
    SettingsDesignViewerBg,
    SettingsDesignThumbBg,
    SettingsDesignThemeNameExport,
    SettingsDesignExportBtn,
    SettingsDesignImportBtn,
    SettingsDesignApplyBtn,
    SettingsDesignThemeFileFilter,
    SettingsDesignAllFilesFilter,
    SettingsConvTagLabel,
    SettingsConvTargetCat,
    SettingsConvSubcatLabel,
    SettingsConvCatLabel,
    SettingsConvModeLabel,
    SettingsConvConvertBtn,
    SettingsConvTagToSubHint,
    SettingsConvSubToTagHint,
    SettingsConvTagToCatHint,
    SettingsConvCatToTagHint,
    SettingsConvSubToCatHint,
    SettingsConvCatToSubHint,
    SettingsBookBtnAdd,
    SettingsBookHint,
    SettingsBookEmpty,
    SettingsBookAddTitle,
    SettingsBookEditTitle,
    SettingsBookDeleteTitle,
    SettingsBookDeleteConfirm,
    SettingsBookFolderPath,
    SettingsBookDisplayName,
    SettingsBookChooseFolder,
    SettingsCatBtnNew,
    SettingsCatHintNew,
    SettingsCatEmptyNew,
    SettingsCatColorLabel,
    SettingsCatInheritSub,
    SettingsCatDeleteConfirm,
    SettingsTagsBtnNew,
    SettingsTagsHintNew,
    SettingsTagsEmpty,
    SettingsTagsName,
    SettingsTagsDeleteConfirm,
    SettingsCatNodeAddSub,
    SettingsCatNodeRename,
    SettingsCatNodeSetUniform,
    SettingsCatNodeClearUniform,
    SettingsCatNodeColorTitle,
    SettingsTabViewLayout,
    SettingsTabEditorShort,
    SettingsTabConverter,
    SettingsTabBookmarks,
    SettingsClose,
    SettingsEditorAutoSaveGroup,
    SettingsEditorIntervalLabel,
    ColorPickerTitle,
    TileSizeHint,
    MenuTileSize,
    TagPanelEmpty,
    TagPanelDeleteTitle,
    CatNodeRemoveFromCat,
    FilterNoTagsShort,
    ViewerOpenedExternal,
    ViewerNoRenderer,
    PdfPageIndicator,
    PdfAudioPanelHeader,
    PdfNoAudioOnPage,
    PdfAudioItemLabel,

    // ── Batch 2: weitere vereinheitlichte UI-Strings ──
    FilterModeAnyDesc,
    FilterModeAllDesc,
    FilterModeExclusiveDesc,
    FilterModeInclusiveDesc,
    FilterSortLabel,
    FilterSortDesc,
    FilterSortAsc,
    FilterTagModeLabel,
    FilterActiveSuffix,
    FilterShowMediaTypes,
    FilterTagsToFilter,
    ModeGroup,
    ModeAddToTag,
    ViewerRandom,
    GalleryNoMedia,
    GalleryNoFolder,
    DateYear,
    DateMonth,
    DateDay,
    DateHour,
    DateMinute,
    PdfLoadError,
    PdfCollapsePreview,
    PdfExpandPreview,
    PdfHideAudioBar,
    PdfShowAudioBar,
    PdfFitPage,
    PdfFitWidth,
    PdfAudioActiveTitle,

    // HTML-Vorschau (FullscreenViewer-Umschalter Quelltext ⇄ gerenderte Seite)
    ViewerShowPreview,
    ViewerShowSource,
    ViewerPreviewCrashed,

    // ── Batch 3: Filter-Tag-Hinzufügen, Kachel-Kontextmenü, Optionen-Buttons ──
    FilterAddTagBtn,           // "Hinzufügen" (Tag zur Filterliste)
    FilterAddTagPlaceholder,   // Eingabefeld-Placeholder
    CtxAddTag,                 // Kontextmenü: Tag hinzufügen
    CtxAddCategory,            // Kontextmenü: Kategorie hinzufügen
    CtxNoCategories,           // Platzhalter: keine Kategorien vorhanden
    OverlayShowTags,           // Optionen-Modus: Tags anzeigen
    OverlayShowCategories,     // Optionen-Modus: Kategorien anzeigen
    OverlayNoValues,           // Platzhalter: keine Werte vorhanden

    // ── Batch 4: Panel-System Tags & Kategorien / S-Modus-Erweiterung ──
    PanelSectionTags,          // Panel-Abschnittstitel: Tags
    PanelAddTagTip,            // „+"-Button: neuen Tag erstellen
    PanelAddCategoryTip,       // „+"-Button: neue Kategorie erstellen
    PanelNoTags,               // Platzhalter: noch keine Tags vorhanden
    FilterTagsCatsLabel,       // Filter-Popup: zusammengelegter Eintrag „Tags & Kategorien"
    FilterPanelHeader,         // Filter-Popup: Überschrift der Panel-Steuerung
    FilterTagPanelRow,         // Filter-Popup: Toggle-Zeile „Tag-Panel"
    FilterCatPanelRow,         // Filter-Popup: Toggle-Zeile „Kategorie-Panel"

    // ── PDF-Editor (Overlay-Textboxen; PdfSurface/PdfEdit*-QML) ──
    PdfEditToggleTip,          // Toolbar: Bearbeitungsmodus ein/aus
    PdfEditUndoTip,            // Toolbar: Rückgängig
    PdfEditRedoTip,            // Toolbar: Wiederholen
    PdfEditSnapTip,            // Toolbar: Zeilenfang ein/aus
    PdfEditPanelHeader,        // Panel-Überschrift
    PdfEditNoSelection,        // Panel-Hinweis ohne Auswahl
    PdfEditFontLabel,          // Panel: Schriftart
    PdfEditSizeLabel,          // Panel: Schriftgröße
    PdfEditStyleLabel,         // Panel: Stil (B/I/U)
    PdfEditAlignLabel,         // Panel: Ausrichtung (horizontal)
    PdfEditVAlignLabel,        // Panel: vertikale Ausrichtung (oben/mittig)
    PdfEditColorLabel,         // Panel: Textfarbe
    PdfEditHighlightLabel,     // Panel: Hervorhebung (Box-Hintergrund)
    PdfEditNoHighlight,        // Panel/Toolbar: Hervorhebung entfernen
    PdfEditDeleteBtn,          // Textbox löschen
    PdfEditAnchoredChip,       // Panel-Chip: an Textzeile verankert
    PdfEditEmptyHint,          // Platzhalter in leerer Textbox
    PdfEditSaveBtn,            // Panel: Overlay speichern
    PdfEditSaveTip,            // Tooltip zum Speichern (Sidecar, editierbar)
    PdfEditExportBtn,          // Panel: PDF exportieren
    PdfEditExportTip,          // Tooltip zum Export (gerendertes PDF)
    PdfEditSavedToast,         // Toast: gespeichert
    PdfEditSaveFailedToast,    // Toast: Speichern fehlgeschlagen
    PdfEditExportDoneToast,    // Toast: exportiert nach %1
    PdfEditExportFailedToast,  // Toast: Export fehlgeschlagen (%1)
    PdfEditExportingToast,     // Toast: Fortschritt Seite %1/%2
    SettingsPdfEditGroup,      // Settings-Editor-Tab: Gruppentitel

    // ── PDF-Editor Runde 2: Post-it-Notizen + Panel-Position ──
    PdfEditNotesToggleTip,     // Toolbar: Notizen ein-/ausblenden (Alt+Q)
    PdfEditOpacityLabel,       // Panel: Deckkraft des Notiz-Hintergrunds
    PdfEditPanelPosLabel,      // Settings: Position der Text-Eigenschaften
    PdfEditPanelPosRight,      // Settings-Option: rechte Seitenleiste
    PdfEditPanelPosTop,        // Settings-Option: obere Leiste (Word-Stil)

    // ── Datei-Erstellung (FilterBar „Erstellen") ──
    CreateFileBtn,             // Button-Beschriftung
    CreateFileTitle,           // Popup-Titel
    CreateFileNameLabel,       // Eingabefeld-Label
    CreateFileTypePdf,         // Typ: leere PDF
    CreateFileTypeHtml,        // Typ: leere HTML-Datei
    CreateFileTypeTxt,         // Typ: leere Textdatei
    CreateFileDone,            // Statuszeile: Erstellt: %1
    CreateFileFailed,          // Statuszeile: Erstellen fehlgeschlagen

    // ── Galerie-Kontextmenü: Datei löschen ──
    CtxDeleteFile,             // Kontextmenü-Eintrag „Datei löschen…"
    DeleteMediaTitle,          // Dialog-Titel
    DeleteMediaText,           // Dialog-Text „%1 wird in den Papierkorb …"
    DeleteMediaConfirm,        // Dialog-Bestätigungsknopf „Löschen"

    // ── Live-Transliteration (Latein → Arabisch/Kana) ──
    TranslitTip,               // Button-Tooltip
    TranslitOff,               // Popup: Aus
    TranslitArabic,            // Popup: Arabisch (mit Harakat)
    TranslitHiragana,          // Popup: Japanisch – Hiragana
    TranslitKatakana,          // Popup: Japanisch – Katakana
    SettingsTranslitGroup,     // Settings-Gruppentitel
    TranslitSchemeLabel,       // Settings: Auswahl des zu bearbeitenden Schemas
    TranslitKeyPlaceholder,    // Settings: Platzhalter Eingabe (Latein)
    TranslitValuePlaceholder,  // Settings: Platzhalter Ausgabe (Zielschrift)
    TranslitAddBtn,            // Settings: Zuordnung hinzufügen
    TranslitResetBtn,          // Settings: Schema auf Standard zurücksetzen
    TranslitHint,              // Settings: Erklärungstext
};
Q_ENUM_NS(StringKey)
}  // namespace SK
using StringKey = SK::StringKey;

class Strings {
public:
    static QString get(StringKey key);
    static QString get(StringKey key, const QString& arg1);
    static QString get(StringKey key, Language lang);          // explizite Sprache
    static QString byName(const QString& name, Language lang); // Name→Key→String (für QML uiText)

private:
    static const QString& de(StringKey key);
    static const QString& en(StringKey key);
};
