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
    SettingsGenMonoPlay,
    SettingsGenMonoPlayLabel,
    SettingsGenMonoPlayHint,
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
    SettingsDesignEditorBgText, // Design: Hintergrund des TXT-/Code-Editors
    SettingsDesignEditorBgHtml, // Design: Hintergrund der HTML-Quellansicht
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
    PdfEditToolReplace,        // Werkzeug-Tooltip: Text ersetzen (weiße Deckfläche + Textbox)
    // ── DOCX-Editor (2026-07-16) ─────────────────────────────────────────────
    CreateFileTypeDocx,       // „+ Erstellen"-Auswahl: leeres DOCX
    SettingsDocxGroup,        // Einstellungen ▸ Editor: Gruppentitel
    DocxSaveModeLabel,        // Einstellungen: Label über den Radiobuttons
    DocxSaveDirect,           // Speichermodus 1 (Standard)
    DocxSaveDirectHint,       // Hinweistext Modus 1
    DocxSaveCopy,             // Speichermodus 2 + Toolbar-Button-Beschriftung
    DocxSaveCopyHint,         // Hinweistext Modus 2
    DocxLoadError,            // Ladefehler-Überschrift der Kachel
    DocxSaveError,            // Status bei fehlgeschlagenem Speichern
    DocxExportedTo,           // Status nach Kopie-Export (%1 = Dateiname)
    DocxTablePlaceholder,     // Platzhalterzeile für opake Blöcke
    DocxPageBreak,            // Marker-Beschriftung der gestrichelten Linie
    DocxAlignJustify,         // Tooltip 4. Ausrichtungs-Button
    DocxLineSpacing,          // Tooltip Zeilenabstand-Combo
    DocxSpaceBefore,          // Tooltip SpinBox
    DocxSpaceAfter,           // Tooltip SpinBox
    DocxBullets,              // Tooltip Listen-Button
    DocxNumbered,             // Tooltip Listen-Button
    DocxSpacingGroup,         // DOCX-Toolbar: Sammel-Knopf (Zeilenabstand + davor/danach)
    DocxListNone,             // DOCX-Listen-Popup: Liste ausschalten
    DocxListType,             // DOCX-Toolbar: Tooltip des Listen-Knopfs
    DocxExportPdf,            // DOCX-Toolbar: Button „→ PDF" (Aufgabe 2)
    DocxExportPdfTip,         // Tooltip des „→ PDF"-Buttons
    DocxPdfExportedTo,        // Status nach PDF-Export (%1 = Dateiname)
    DocxPdfError,             // Status bei fehlgeschlagenem PDF-Export
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

    // ── Geteilte Ansicht / Splitscreen (bis zu 4 Dateien nebeneinander) ──
    SplitAddFile,              // Tooltip: weitere Datei zur geteilten Ansicht hinzufügen
    SplitMaxReached,           // Statuszeile: Maximum von 4 offenen Dateien erreicht
    SplitPickPrompt,           // Galerie-Banner: Datei zum Hinzufügen anklicken
    SplitCancel,               // Galerie-Banner: Hinzufügen abbrechen

    // ── Bild-Editor ───────────────────────────────────────────────────────────
    ImageEditToggle,           // Toolbar: Bild-Bearbeitung umschalten
    ImageFitWindow,            // Toolbar: an Fenster anpassen
    ImageActualSize,           // Toolbar: 100 %
    ImageZoomIn,               // Toolbar: vergrößern
    ImageZoomOut,              // Toolbar: verkleinern
    ImageLoadError,            // Fehlermeldung: Bild nicht ladbar
    ImageEditPanelTitle,       // Panel-Titel
    ImageEditToolsLabel,       // Panel: Abschnitt „Werkzeuge"
    ImageEditToolSelect,       // Werkzeug: Auswählen
    ImageEditToolText,         // Werkzeug: Text
    ImageEditToolPen,          // Werkzeug: Stift (Freihand)
    ImageEditToolArrow,        // Werkzeug: Pfeil
    ImageEditToolRect,         // Werkzeug: Rechteck
    ImageEditToolEllipse,      // Werkzeug: Ellipse
    ImageEditPickHint,         // Panel-Hinweis: Werkzeug/Annotation wählen
    ImageEditEmptyHint,        // Platzhalter in leerer Text-Notiz
    ImageEditStrokeLabel,      // Linienfarbe
    ImageEditWidthLabel,       // Linienbreite
    ImageEditFillLabel,        // Füllung
    ImageEditCopyBtn,          // Annotation kopieren
    ImageEditPasteBtn,         // Annotation einfügen
    ImageEditDocSection,       // Panel: Abschnitt „Dokument"

    // ── PDF-Seiten-Extraktion ────────────────────────────────────────────────
    CtxExtractPage,            // Kontextmenü: „Seite extrahieren"
    CtxExtractPages,           // Kontextmenü: „Mehrere Seiten extrahieren…"
    ExtractNameTitle,          // Namensdialog: Titel
    ExtractNameLabel,          // Namensdialog: Feldbeschriftung
    ExtractCreateBtn,          // Namens-/Auswahldialog: „Extrahieren"
    ExtractDialogTitle,        // Auswahldialog (in-PDF): Titel
    ExtractGlobalTitle,        // Auswahldialog (global): Titel
    ExtractHintCtrl,           // Auswahldialog: Klick-/Strg-Hover-Hinweis
    ExtractSelectedCount,      // Auswahldialog: „%1 Seiten ausgewählt"
    ExtractNoPdfs,             // Global: keine PDFs im Ordner
    FilterExtractBtn,          // FilterBar-Button: „Extrahieren"
    ExtractOkToast,            // Erfolgsmeldung: „PDF erstellt: %1"
    ExtractFailToast,          // Fehlermeldung
    ExtractProgressToast,      // Fortschritt: „Extrahiere Seite %1/%2"
    ExtractPageShort,          // Kachel-Beschriftung: „S. %1"
    SettingsViewExtractStyle,      // Einstellungen: Gruppentitel
    SettingsViewExtractStyleHint,  // Einstellungen: Hinweistext
    ExtractStyleFrame,         // Einstellungen: „Rahmen"
    ExtractStyleOverlay,       // Einstellungen: „Überlagerung"
    // ── PDF: Seiten hinzufügen/entfernen (Aufgabe 3) ─────────────────────────
    SettingsPdfPageEditLabel,   // Einstellungen ▸ Editor (PDF): Label über den Radios
    SettingsPdfPageEditHint,    // Einstellungen: Hinweistext
    PdfPageEditNonDestructive,  // Radio: nicht-destruktiv (wirkt beim Export)
    PdfPageEditDestructiveMode, // Radio: destruktiv (Original sofort ändern)
    PdfAddPageTip,              // „+"-Linie unter der Seite: Tooltip
    PdfRemovePage,              // Rechtsklick-Menü: „Seite entfernen"
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
