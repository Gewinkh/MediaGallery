#pragma once
#include <QString>
#include <QObject>
#include "core/AppSettings.h"

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
    MenuClosePane,             // Datei-Menü bei zwei Hälften: „Schließen"
    MenuSplitWindow,           // Ansicht: Fenster teilen (zweite Galerie)
    MenuUnsplitWindow,         // Ansicht: Teilung aufheben
    MenuSwapPanes,             // Ansicht: Hälften tauschen
    SettingsPaneLabel,         // Einstellungen: „Ordnerbezogene Angaben von:"
    //  ── Audio-Player mit Equalizer ──
    AudioPlayerMode,
    AudioShuffle,
    AudioRepeatOff,
    AudioRepeatOne,
    AudioRepeatAll,
    AudioEqTitle,
    AudioEqOn,
    AudioEqPreamp,
    SettingsShowHidden,
    SettingsShowHiddenDesc,
    AudioEqAutoPreamp,
    AudioEqAutoPreampHint,
    AudioEqReset,
    AudioEqPreset,
    AudioEqSavePreset,
    AudioEqPresetName,
    AudioEqClipHint,
    SettingsTabAudio,
    AudioShowVideos,
    //  ── „Ton aus Video sichern" (MP4 -> M4A, ohne Neukodierung) ──
    AudioExtractMenu,
    AudioExtractGroup,
    AudioExtractInheritTags,
    AudioExtractInheritHint,
    AudioExtractToQueue,
    AudioExtractToQueueHint,
    AudioExtractRunning,
    AudioExtractOk,
    AudioExtractFailNotMp4,
    AudioExtractFailCodec,
    AudioExtractFailFragmented,
    AudioExtractFailNoTrack,
    AudioExtractFailExternal,
    AudioExtractFailDamaged,
    AudioExtractFailTooLarge,
    AudioExtractFailWrite,
    AudioExtractFailRead,
    AudioExtractManyTracks,
    //  ── Auswahl der Tonspur (mehr als eine in der Datei) ──
    //  Datum ließ sich nicht an die Datei schreiben (schreibgeschützt o. ä.)
    DateNotWrittenToFile,
    AudioTrackChooseTitle,
    AudioTrackChooseHint,
    AudioTrackNumber,
    AudioTrackUnsupported,
    AudioChMono,
    AudioChStereo,
    AudioChMulti,
    //  ── DOCX -> PDF: Rand und Seitenzahl ──
    //  ── Randlineale des DOCX-Editors ────────────────────────────────────────
    DocxRulerResetTip,         // Hilfetext des Zurücksetzen-Knopfes
    DocxRulerTopTip,           // Hilfetext des waagerechten Lineals
    DocxRulerSideTip,          // Hilfetext des senkrechten Lineals
    DocxPdfNumberMenu,
    DocxPdfNumberOff,
    DocxPdfNumberLeft,
    DocxPdfNumberCenter,
    DocxPdfNumberRight,
    DocxPdfNumberStyleHead,
    DocxPdfNumberStylePlain,
    DocxPdfNumberStyleTotal,
    AudioRememberLast,
    AudioRememberHint,
    AudioVolume,
    AudioNoTrack,
    AudioPrevious,
    AudioNext,
    AudioQueueHeader,
    AudioNowPlaying,
    AudioBackToGallery,
    AudioOpenPlayer,
    AudioLayoutTitle,
    AudioLayoutTiles,
    AudioLayoutList,
    AudioLayoutHint,

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
    FilterBtn,                // Sammel-Knopf „Filter" in der Filterleiste

    // SettingsDialog
    SettingsTitle,
    SettingsTabGeneral,
    SettingsTabDesign,
    SettingsLanguageLabel,
    SettingsOk,
    SettingsCancel,
    SettingsTagDelete,
    SettingsTagDeleteSubfolders,      // Einstellung: Tag auch in Unterordnern löschen
    SettingsTagDeleteSubfoldersHint,  // … und was das bedeutet
    TagDeletedInSubfolders,           // Statuszeile: „%1" in %2 Unterordnern entfernt

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
    //  ── Gruppen im Menü „Ordner" und in Einstellungen ▸ Lesezeichen ──
    BookmarkGroupAdd,
    BookmarkGroupNewTitle,
    BookmarkGroupRenameTitle,
    BookmarkGroupNameLabel,
    BookmarkGroupDeleteTitle,
    BookmarkGroupDeleteText,
    BookmarkGroupUngrouped,
    BookmarkGroupLabel,
    BookmarkGroupNone,
    BookmarkGroupEmptyHint,
    BookmarkDragHint,
    BookmarkGroupNameTaken,
    BookmarkGroupSubAdd,
    BookmarkGroupNameInvalid,
    BookmarkGroupParentLabel,
    BookmarkGroupParentRoot,

    // Text editor / filter
    EditorAutoSave,
    EditorSave,
    TextExportPdf,            // Texteditor-Toolbar: Button „-> PDF"
    TextExportPdfTip,         // Tooltip des „-> PDF"-Buttons
    TextExportPdfOk,          // Statusmeldung: PDF geschrieben („%1" = Dateiname)
    TextExportPdfFail,        // Statusmeldung: Export fehlgeschlagen („%1" = Grund)
    TextPdfColorTitle,        // Titel des Farbwählers für die PDF-Schriftfarbe
    TextPdfColorTip,          // Tooltip des Farbfelds im Texteditor
    TextPdfColorResetTip,     // Tooltip: zurück auf die Vorgabe aus den Einstellungen
    TextPdfColorSetting,      // Einstellungen ▸ Editor: Beschriftung der Vorgabe
    TextPdfColorSettingHint,  // Einstellungen ▸ Editor: Erklärung der Vorgabe
    ViewMenuImmersive,        // Ansichts-Menü: immersives Vollbild (F)
    TrackMenuTitle,           // Kopfleiste: Knopf „Änderungen verfolgen"
    TrackRecord,              // Popup: Checkbox „Aufzeichnen"
    TrackRecordHint,          // Popup: was das Aufzeichnen bedeutet
    TrackOpenCount,           // Popup: „%1 offene Änderung(en)"
    TrackAcceptAll,           // Popup: alle annehmen
    TrackRejectAll,           // Popup: alle verwerfen
    TrackAcceptOne,           // Kontextmenü: diese Änderung annehmen
    TrackRejectOne,           // Kontextmenü: diese Änderung verwerfen
    SettingsFilesGroup,       // Einstellungen ▸ Ansicht: Gruppentitel „Dateien“
    SettingsShowAllFiles,     // Einstellungen: alle Dateien anzeigen
    SettingsShowAllFilesHint, // Einstellungen: was das bedeutet
    CtxRemoveEdits,           // Kontextmenü: Notizen/Zeichnungen löschen
    CtxRemoveBackup,          // Kontextmenü: vorherige Fassung (.bak) löschen
    CtxRemoveEditsAsk,        // Rückfrage vor dem Löschen der Notizen
    CtxRemoveBackupAsk,       // Rückfrage vor dem Löschen der vorherigen Fassung
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
    SettingsGenSeekStep,
    SettingsGenSeekStepLabel,
    SettingsGenSeekStepHint,
    //  Rechtschreibprüfung (unterkringeln + Vorschläge; nie automatisch ersetzen)
    SettingsGenSpell,
    SettingsGenSpellLabel,
    SettingsGenSpellHint,
    SettingsGenSpellLang,
    SettingsGenSpellNone,
    DocxSpellSuggestions,
    DocxSpellIgnore,
    DocxSpellNoSuggestion,
    //  Änderungsverfolgung: annehmen/verwerfen (Kontextmenü)
    DocxRevAccept,
    DocxRevReject,
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
    //  Eigener Datei-/Ordnerwähler (qml/common/FileChooser.qml)
    ChooserPlaces,
    ChooserName,
    ChooserOpen,
    ChooserSave,
    ChooserOverwrite,
    ChooserChoose,
    ChooserUp,
    ChooserEmpty,
    ChooserShowHidden,
    ChooserAllFiles,
    ChooserNewFolder,
    ChooserFolderExists,
    ChooserFolderFailed,
    ChooserColName,
    ChooserColSize,
    ChooserColDate,
    ChooserBookmarks,
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
    FilterSearchPlaceholder,
    DropBarMove,
    DropBarCopy,
    DropMoved,
    DropCopied,
    DropFailed,
    DropCollisionTitle,
    DropCollisionText,
    DropCollisionReplace,
    DropCollisionRename,
    SettingsDropMoveLabel,
    SettingsDropMoveDesc,
    FileUndoRestored,
    FileRedoDeleted,
    FileUndoNothing,
    FileRedoNothing,
    FilterSearchHits,
    FilterSortLabel,
    FilterSortField,
    FilterSortDirection,
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
    PdfEditToolCaret,          // Werkzeug-Tooltip: Text bearbeiten (Caret in der Textebene)
    PdfEditCaretLoading,       // Hinweis: Zeichen-Layout der Seite wird geladen
    PdfEditCaretUnavailable,   // Hinweis: Seite nicht zeichenweise bearbeitbar
    PdfEditTextOpFailed,       // Toast: Änderung an der Textebene nicht möglich
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
    DocxParagraphStyle,       // Tooltip Formatvorlagen-Combo
    DocxInsertSignature,      // Toolbar: Unterschrift/Stempel einfuegen
    DocxInsertTable,          // Toolbar: Tabelle einfügen
    DocxTableRows,            // Popup: Zeilen
    DocxTableCols,            // Popup: Spalten
    DocxInsert,               // Popup: Einfügen-Knopf
    DocxInsertImage,          // Toolbar: Bild einfügen
    DocxImageFilter,          // Dateidialog-Filter
    DocxImageError,           // Status: Bild konnte nicht eingefügt werden
    DocxImageFromFolder,      // Bild-Popup: aus dem Ordner der Datei wählen
    DocxNoImagesInFolder,     // Bild-Popup: Ordner enthält keine Bilder
    DocxImageBrowse,          // Bild-Popup: Dateidialog öffnen
    // Kontextmenü der Tabelle (Rechtsklick in der Fläche)
    DocxClearBreak,           // Kontextmenue: weiter unter der Tabelle
    DocxRowInsertAbove,
    DocxRowInsertBelow,
    DocxRowDelete,
    DocxColInsertLeft,
    DocxColInsertRight,
    DocxColDelete,
    DocxColWidths,            // öffnet den Breiten-Dialog
    DocxTableCopy,            // Kontextmenü: ganze Tabelle kopieren
    DocxTableCut,             // Kontextmenü: ganze Tabelle ausschneiden
    DocxTablePaste,           // Kontextmenü: Tabelle aus der Ablage einfügen
    DocxTableDelete,
    DocxTableLocked,          // verbundene Zellen -> Struktur unveränderlich
    // Bildgröße (Kontextmenü + Ziehpunkte)
    DocxImageSize,
    DocxImageInline,
    DocxImageFloating,
    DocxWrapBoth,
    DocxWrapLargest,
    DocxWrapLeft,
    DocxWrapRight,
    DocxImageWidth,
    DocxImageHeight,
    DocxKeepAspect,
    DocxApply,                // Übernehmen-Knopf der kleinen Popups
    // Bearbeitungs-Region (Körper / Kopfzeile / Fußzeile)
    DocxInsertToc,            // Toolbar: Inhaltsverzeichnis einfügen
    DocxRevisionsBanner,      // Streifen: N nachverfolgte Änderungen von …
    DocxRevisionsHint,        // Streifen: was Änderungsverfolgung hier tut
    DocxRevAcceptAll,         // Streifen: alle Änderungen annehmen
    DocxRevRejectAll,         // Streifen: alle Änderungen verwerfen
    DocxTocEmpty,             // Verzeichnis ohne Überschriften
    DocxHeadingLevel,         // Vorlagenliste: „Überschrift %1"
    DocxInsertPdfPage,        // Titel der Seitenauswahl beim Bild-Einfügen
    DocxInsertPdfPageBtn,     // Bestätigungsknopf derselben Auswahl
    DocxPdfPageError,         // PDF nicht lesbar
    DocxImageCopy,
    DocxImageCut,
    DocxImageDelete,
    DocxAlignJustify,         // Tooltip 4. Ausrichtungs-Button
    DocxLineSpacing,          // Tooltip Zeilenabstand-Combo
    DocxSpaceBefore,          // Tooltip SpinBox
    DocxSpaceAfter,           // Tooltip SpinBox
    DocxBullets,              // Tooltip Listen-Button
    DocxNumbered,             // Tooltip Listen-Button
    DocxSpacingGroup,         // DOCX-Toolbar: Sammel-Knopf (Zeilenabstand + davor/danach)
    DocxListNone,             // DOCX-Listen-Popup: Liste ausschalten
    DocxListType,             // DOCX-Toolbar: Tooltip des Listen-Knopfs
    DocxExportPdf,            // DOCX-Toolbar: Button „-> PDF" (Aufgabe 2)
    DocxExportPdfTip,         // Tooltip des „-> PDF"-Buttons
    DocxPdfExportedTo,        // Status nach PDF-Export (%1 = Dateiname)
    DocxFindPlaceholder,      // Suchen&Ersetzen: Platzhalter Suchfeld
    DocxReplacePlaceholder,   // Suchen&Ersetzen: Platzhalter Ersetzungsfeld
    DocxFindPrev,             // Tooltip: vorheriger Treffer
    DocxFindNext,             // Tooltip: nächster Treffer
    DocxReplaceOne,           // Button: aktuellen Treffer ersetzen
    DocxReplaceAll,           // Button: alle ersetzen
    DocxMatchCase,            // Tooltip: Groß-/Kleinschreibung beachten
    DocxFindNoMatch,          // Status: kein Treffer
    DocxReplacedCount,        // Status: %1 Ersetzungen
    PdfEditColorLabel,         // Panel: Textfarbe
    PdfEditHighlightLabel,     // Panel: Hervorhebung (Box-Hintergrund)
    PdfEditCoverLabel,         // Panel: Deckfläche (Cover-Farbe von „Text ersetzen")
    //  ── Dokument durchsuchbar machen (unsichtbare Textebene, Scans) ────────
    PdfSearchableMenu,         // Dokument-Menü: „Dokument durchsuchbar machen"
    PdfSearchableRunning,      // Toast: Fortschritt (%1 von %2 Seiten)
    PdfSearchableDoneToast,    // Toast: fertig (%1 Seiten, %2 Wörter)
    PdfSearchableSkippedNote,  // Anhang an den Fertig-Toast (%1 übersprungen)
    PdfSearchableNoneToast,    // Toast: gescannte Seiten da, aber nichts erkannt
    PdfSearchableAlreadyToast, // Toast: jede Seite hat schon Text - nichts zu tun
    PdfSearchableFailedToast,  // Toast: fehlgeschlagen (%1 = Grund)
    PdfSearchableAlreadyTip,   // Menü-Hinweis: schon durchsuchbar
    LibMissingZlib,            // Hover-Hinweis: Feature braucht ZLIB

    //  ── Unterordner in der Galerie ─────────────────────────────────────────
    FolderMediaCount,          // Ordnerkachel: „%1 Medien"
    FolderEmpty,               // Ordnerkachel: leerer Ordner
    FolderNew,                 // Knopf „+ Ordner"
    FolderNewTitle,            // Dialog: neuen Ordner anlegen
    FolderNamePlaceholder,     // Eingabefeld: Ordnername
    FolderNameInvalid,         // Meldung: Name unbrauchbar
    FolderExists,              // Meldung: Name schon vergeben
    FolderCreateFailed,        // Meldung: Anlegen fehlgeschlagen
    FolderRenameTitle,         // Dialog: Ordner umbenennen
    CtxFolderOpen,             // Kontextmenue: Ordner oeffnen
    CtxFolderRename,           // Kontextmenue: Ordner umbenennen
    CtxFolderDelete,           // Kontextmenue: Ordner loeschen
    DeleteFolderTitle,         // Rueckfrage: Ordner loeschen
    DeleteFolderText,          // Rueckfrage: Text mit Name und Anzahl
    DeleteFolderNoTrash,       // Meldung: ohne Papierkorb kein Loeschen
    FolderDropBar,             // Ablegeleiste: „Ordner hier"
    FolderDropMoved,           // Meldung: Datei in den Ordner verschoben
    FolderDropCopied,          // Meldung: Datei in den Ordner kopiert
    PdfChainLink,              // Panel: Box mit nächster verketten (Reflow)
    PdfChainUnlink,            // Panel: Kette lösen
    PdfChainPick,              // Toast: Zielbox anklicken
    PdfChainDone,              // Toast: Boxen verkettet
    PdfContentFallbackToast,   // Toast: auf Raster-Export zurückgefallen
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
    //  ── Mehrfachauswahl der Galerie ─────────────────────────────────────────
    CtxCopyFiles,              // Kontextmenü „Kopieren" (Strg+C)
    CtxDeleteSelection,        // „%1 Objekte löschen…" (Mehrfachauswahl)
    SelDeleteTitle,            // Rückfrage-Titel „Mehrere löschen?"
    SelDeleteText,             // „%1 Objekte wandern in den Papierkorb."
    SelCountStatus,            // Statuszeile „%1 ausgewählt"
    SelCopied,                 // Statuszeile „%1 in die Zwischenablage"
    SelDeleted,                // Statuszeile „%1 gelöscht"
    FileOpAndMore,             // Anhang „ und %1 weitere"
    DropBatchCount,            // „%1 Dateien" - Zug mit mehreren Dateien
    SelPasted,                 // „%1 eingefügt" (Strg+V in der Galerie)
    SelPasteEmpty,             // „Die Zwischenablage enthält keine Dateien"
    //  ── Equalizer-Voreinstellungen ──────────────────────────────────────────
    AudioEqPresetOverwriteTip, // Hilfetext: aktuelle Regler auf diese Zeile
    AudioEqPresetDeleteTip,    // Hilfetext: Eintrag entfernen
    AudioEqPresetResetTip,     // Hilfetext: mitgelieferte Vorlage zurückholen
    AudioEqPresetResetAll,     // Knopf „Alle mitgelieferten zurücksetzen"
    AudioEqPresetUpTip,        // Hilfetext: eine Zeile nach oben
    AudioEqPresetDownTip,      // Hilfetext: eine Zeile nach unten
    AudioEqPresetNewTitle,     // „Neue Voreinstellung"
    AudioEqPresetOverwriteTitle, // „Vorhandene überschreiben"
    CtxRenameFile,             // Kontextmenü-Eintrag „Umbenennen…"
    MenuDocument,              // Menü der Kachel-Kopfleiste („Dokument")
    PanelSearchTag,            // Suchfeld im Tag-Abschnitt
    PanelSearchCategory,       // Suchfeld im Kategorien-Abschnitt
    PanelSearchNoHit,          // Suche ohne Treffer
    DeleteMediaTitle,          // Dialog-Titel
    DeleteMediaText,           // Dialog-Text „%1 wird in den Papierkorb …"
    DeleteMediaConfirm,        // Dialog-Bestätigungsknopf „Löschen"

    // ── Live-Transliteration (Latein -> Arabisch/Kana) ──
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
    SettingsPdfExportLabel,     // Einstellungen ▸ Editor (PDF): Label über den Export-Radios
    SettingsPdfExportHint,      // Einstellungen: Hinweistext zum Export-Modus
    PdfExportLosslessMode,      // Radio: verlustfrei bevorzugen (Text bleibt vektoriell)
    PdfExportRasterMode,        // Radio: immer Raster (Seitenbild)
    PdfEditDeleteMarkup,        // Löschen-Knopf, wenn eine Markierung gewählt ist
    PdfFormSavedFlattenedToast, // Toast: gespeichert, aber Felder festgeschrieben
    PdfCaretPageNoText,         // Hinweis: auf dieser Seite gibt es keinen Text
    PdfSearchPlaceholder,       // Suchleiste: Platzhalter im Eingabefeld
    PdfSearchCount,             // Suchleiste: „%1 von %2"
    PdfSearchNone,              // Suchleiste: keine Treffer
    PdfSearchTip,               // Toolbar: Suche öffnen/schließen
    PdfEditToolStamp,           // Werkzeug: Signatur-/Stempelbild einfügen
    PdfStampFileTitle,          // Dateidialog: Bild auswählen
    PdfStampFailedToast,        // Toast: Bild nicht verwendbar
    PdfEditToolRedact,          // Werkzeug: Schwärzen (kurze Beschriftung)
    PdfRedactLimitHint,         // Hinweis: wogegen die Schwärzung schützt - und wogegen nicht
    PdfRedactNoTextToast,       // Toast: Seite ohne Textebene - Schwärzen greift dort nicht
    PdfEditToolMarkup,          // Werkzeug: Text markieren
    PdfMarkupHighlight,         // Stil: Markieren (Fläche)
    PdfMarkupUnderline,         // Stil: Unterstreichen
    PdfMarkupStrike,            // Stil: Durchstreichen
    PdfExportAsAnnotationsOption, // CheckBox: Notizen als PDF-Annotationen schreiben
    PdfExportAsAnnotationsHint,   // Hinweistext dazu
    PdfAddPageTip,              // „+"-Linie unter der Seite: Tooltip
    PdfRemovePage,              // Rechtsklick-Menü: „Seite entfernen"
    PdfRotatePage,              // Rechtsklick-Menü: „Drehen" (Richtung sagt das Symbol)
    PdfInsertPagesFrom,         // Rechtsklick-Menü: „Seiten aus PDF einfügen…"
    PdfInsertPagesDialogTitle,  // Auswahldialog: Titel beim Einfügen
    PdfInsertPagesFileTitle,    // Dateidialog: Titel („PDF auswählen")
    PdfPagesInsertedToast,      // Toast: „%1 Seite(n) eingefügt"
    PdfPagesInsertFailedToast,  // Toast: Einfügen fehlgeschlagen
    PdfMovePageTip,             // Vorschauleiste: Ziehen sortiert die Seite um
    PdfPageImportedBadge,       // Vorschauleiste: Marke „eingefügt"
    PdfCaretPageNotEditable,    // Hinweis: Seite nicht zeichenweise bearbeitbar
    PdfReflowOverflow,          // Hinweis: Absatz voll, Rest steht in der letzten Zeile
    // Formularfelder (AcroForm)
    PdfFormSaveTip,             // Tooltip: ausgefülltes Formular speichern
    PdfFormSavedToast,          // Toast: Formular in „%1" gespeichert
    PdfFormSaveFailedToast,     // Toast: Formular konnte nicht gespeichert werden (%1)
    // Einstellungen ▸ Allgemein: Tastenkürzel-Übersicht (Titel + Kontext-Köpfe)
    SettingsGenShortcuts,       // Gruppentitel „Tastenkürzel"
    ShortcutCtxGallery,         // Abschnitt: Galerie
    ShortcutCtxViewer,          // Abschnitt: Medienansicht
    ShortcutCtxPdf,             // Abschnitt: PDF-Editor
    ShortcutCtxImage,           // Abschnitt: Bild-Editor
    ShortcutCtxDocx,            // Abschnitt: DOCX-Editor
    ShortcutCtxText,            // Abschnitt: Texteditor
    // Einstellungen ▸ Ansicht: Layout des PDF-Extraktionsdialogs
    SettingsViewExtractLayout,     // Gruppentitel
    SettingsViewExtractLayoutHint, // Hinweistext
    ExtractLayoutWorkbench,        // Radio: Werkbank (neu)
    ExtractLayoutCompact,          // Radio: Kompakt (minimalistisch)
    // Werkbank-Dialog (Drei-Panel)
    ExtractWorkPdfs,               // Panel links: „PDFs"
    ExtractWorkPages,              // Panel rechts: „Seiten"
    ExtractWorkOrder,              // Auswahlleiste: „Auswahl · Reihenfolge = Ausgabe"
    ExtractWorkHint,               // Werkbank: Bedien-Hinweis (Klick/Drag&Drop)
    ExtractWorkDropHint,           // Leere Auswahlleiste: „Seiten hierher ziehen"
};
Q_ENUM_NS(StringKey)
}  // namespace SK
using StringKey = SK::StringKey;

class Strings {
public:
    static QString get(StringKey key);
    static QString get(StringKey key, const QString& arg1);
    static QString get(StringKey key, Language lang);          // explizite Sprache
    static QString byName(const QString& name, Language lang); // Name->Key->String (für QML uiText)

private:
    static const QString& de(StringKey key);
    static const QString& en(StringKey key);
};
