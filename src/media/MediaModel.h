#pragma once
#include <QAbstractListModel>
#include <QDir>
#include <QVector>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <QTimer>
#include <QColor>
#include <QDateTime>
#include <QFileInfo>
#include <QSet>
#include <memory>
#include <atomic>
#include "media/MediaItem.h"
#include "media/MediaProxyModel.h"

class JsonStorage;
class TagManager;
class ThumbnailLoader;
class QFileSystemWatcher;
class QDirIterator;
class QThreadPool;

// ─────────────────────────────────────────────────────────────────────────────
//  MediaModel - QAbstractListModel (Phase 2/3, RAM-kritisch).
//
//  Hält reine MediaItem-DATEN (KEINE QPixmaps, KEINE Widgets, 1 Struct/Datei).
//  QML liest über Rollen; es werden keine Datenkopien nach QML geschoben.
//  Thumbnails kommen als "file:///...".URL aus dem Disk-Cache des ThumbnailLoader
//  und werden pro Zeile lazy (sichtbarkeitsgesteuert via ensureThumbnail) gefüllt;
//  Updates laufen über dataChanged(ThumbUrlRole).
//
//  Performance (Ordner öffnen):
//   Statt eines einzigen beginResetModel/endResetModel über den GESAMTEN Ordner
//   wird INKREMENTELL befüllt: ein leeres Modell wird sofort publiziert, danach
//   werden Zeilen in Chargen (beginInsertRows) eingespeist - die erste Charge
//   synchron (Viewport sofort sichtbar), der Rest gechunkt über einen 0-ms-Timer,
//   der zwischen den Chargen an die Event-Loop zurückgibt. Dadurch erscheinen die
//   ersten Kacheln nahezu sofort, auch bei 10–50k Dateien, statt erst nach der
//   kompletten Enumeration.
//
//  Mutationen werden per Dateipfad adressiert (robust gegen Proxy-Sortierung/
//  Filterung): renameItem / toggleTag suchen die Zeile über einen Pfad->Row-Hash.
//
//  Ein QFileSystemWatcher beobachtet den Ordner und löst (entprellt) ein Reload
//  aus; interne Mutationen unterdrücken diesen Reload kurzzeitig.
// ─────────────────────────────────────────────────────────────────────────────
class MediaModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int  count       READ count       NOTIFY countChanged)
    Q_PROPERTY(QString folder   READ folder      NOTIFY folderChanged)
    //  Wie viele Kacheln sind gerade ausgewaehlt? Die Oberflaeche haengt daran
    //  ihre Meldung und die Beschriftung der Sammel-Eintraege im Kontextmenue.
    Q_PROPERTY(int  selectionCount READ selectionCount NOTIFY selectionChanged)
    //  Zaehlt JEDE Aenderung der Auswahl hoch. Eine Kachel bindet ihren
    //  Auswahlzustand daran (`selectionRevision >= 0 && isSelected(pfad)`) -
    //  `selectionCount` genuegt dafuer NICHT: ein wandernder Auswahlrahmen kann
    //  gleich viele, aber andere Kacheln treffen.
    Q_PROPERTY(int  selectionRevision READ selectionRevision NOTIFY selectionChanged)

public:
    enum Roles {
        FilePathRole = Qt::UserRole + 1,
        FileNameRole,
        DisplayNameRole,
        MediaTypeRole,     // int (MediaType)
        TypeLabelRole,     // "MP4"/"MP3"/"PDF"/… - Badge-Text, sonst ""
        TagsRole,          // QStringList
        DateTimeRole,      // QDateTime (effektiv: custom > Dateidatum)
        FileSizeRole,      // qint64
        ThumbUrlRole,      // "file:///…" oder "" solange ausstehend
        ThumbStateRole,    // 0=pending/none, 1=ready, 2=failed
        //  ── Unterordner ────────────────────────────────────────────────────
        OwnerFolderRole,   // Ordner, DEM die Zeile gehoert (nicht ihr Pfad)
        DepthRole,         // 0 = geoeffneter Ordner, 1 = erste Aufklapp-Ebene, …
        ExpandedRole,      // nur Ordnerzeilen: ist dieser Ordner aufgeklappt?
        ChildCountRole,    // nur Ordnerzeilen: Medien darin (−1 = noch ungezaehlt)
        SelectedRole       // Mehrfachauswahl der Galerie (bool)
    };

    //  Ein Ordner-GELTUNGSBEREICH: der geoeffnete Ordner (Index 0) oder ein
    //  aufgeklappter Unterordner. Jede Zeile traegt den Index ihres Bereichs
    //  (MediaItem::scope); Sortierung und Filter lesen die Elternkette darueber.
    //
    //  Bereiche werden NIE aus der Tabelle entfernt, auch nicht beim Zuklappen -
    //  sonst verschoeben sich alle Indizes und jede Zeile muesste umgeschrieben
    //  werden. Ein zugeklappter Bereich hat `active == false`; klappt derselbe
    //  Ordner wieder auf, bekommt er seinen alten Index zurueck.
    struct FolderScope {
        QString path;            // absoluter Ordnerpfad
        QString sidecar;         // "<Ordnername>.json" - beim Einlesen uebergehen
        int     parent    = -1;  // Elternbereich; −1 nur fuer die Wurzel
        int     depth     = 0;   // 0 = geoeffneter Ordner
        int     folderRow = -1;  // Zeile der ORDNERKACHEL (−1 fuer die Wurzel)
        bool    active    = false;
    };

    explicit MediaModel(JsonStorage& storage,
                        TagManager& tagManager,
                        ThumbnailLoader& loader,
                        QObject* parent = nullptr);
    //  Out-of-line: m_pendingIt ist ein unique_ptr auf den nur VORWÄRTS
    //  deklarierten QDirIterator - der implizite Destruktor bräuchte hier den
    //  vollständigen Typ und würde <QDirIterator> in jede einbindende
    //  Übersetzungseinheit ziehen.
    ~MediaModel() override;

    // QAbstractListModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int     count()  const { return m_items.size(); }
    //  Laeuft gerade ein GROSSEINLESEN (Ordner oeffnen, Unterordner aufklappen,
    //  Treffer einer Suche)? Das Zeilenmodell fasst seine Neuaufbauten dann
    //  zusammen, statt nach jeder Charge einen zu machen - s. GalleryRowModel.
    bool    isFilling() const { return hasMoreToFill(); }
    QString folder() const { return m_folder; }

    // Direktzugriff auf die Quelldaten einer Zeile (nullptr bei ungültigem Index).
    // Ausschliesslich fuer MediaProxyModel: Filter und Sortierung laufen ueber
    // ALLE Zeilen und wuerden ueber data()/QVariant je Zeile QStringList- und
    // QDateTime-Kopien in QVariants boxen. Ueber die Struct-Referenz entfaellt
    // das vollstaendig (gleiche Semantik, da beide im GUI-Thread laufen).
    const MediaItem* itemAt(int row) const {
        return (row >= 0 && row < m_items.size()) ? &m_items.at(row) : nullptr;
    }

    // Quellzeile zu einem Dateipfad (O(1) ueber den Pfad->Zeile-Hash), −1 wenn
    // nicht vorhanden. Auch von MediaProxyModel::rowForPath genutzt.
    int rowForPath(const QString& filePath) const;

    // ── Bereichs-Auskunft (fuer MediaProxyModel::lessThan) ───────────────────
    //  Tiefe eines Bereichs; 0 fuer die Wurzel und fuer unbekannte Indizes.
    int scopeDepthOf(int scope) const;
    //  Die ORDNERKACHEL, unter der ein Bereich haengt - nullptr fuer die Wurzel.
    //  Damit klettert der Vergleich die Elternkette hoch, ohne Pfade zu zerlegen.
    const MediaItem* folderItemOfScope(int scope) const;
    //  Elternbereich (−1 nur oberhalb der Wurzel) und Ordnerpfad eines Bereichs
    //  - das Zeilenmodell baut daraus die Kette fuer die Baender.
    int     scopeParentOf(int scope) const;
    QString folderOfScope(int scope) const;
    //  Ist die Zeile eine DATEI (keine Ordnerkachel)? Ordner-Vorgaenge sind
    //  eigene Dinge und kommen mit den Ordner-Operationen.
    bool isFileRow(int row) const;
    //  Gehoert die Zeile zum geoeffneten Ordner SELBST (Bereich 0)? Nur dort
    //  greifen `m_tagManager` und der Kategorienbaum der Seitenleiste.
    bool isRootFileRow(int row) const;
    //  Tag einer Zeile setzen/entfernen - routet auf das Sidecar ihres Ordners.
    void setTagOnRow(int row, const QString& tag, bool on);

    // ── Ordner-Steuerung (von AppController-Signalen getrieben) ──────────────
    void loadFolder(const QString& rawFolderPath);
    void reload();   // aktuellen Ordner neu einlesen (Drop/Refresh/Watcher)
    //  Die Sidecars der aufgeklappten Unterordner sind AUF DER PLATTE geändert
    //  worden (Tag über den Baum gelöscht, s. `TagManager::sweepSubfolders`) -
    //  die im Speicher gehaltenen Kopien wegwerfen und neu einlesen. Ohne das
    //  zeigten die Filterleiste und die Kacheln der Unterordner den Tag weiter.
    //  Verworfen wird OHNE Speichern, und das ist sicher: Unterordner
    //  schreiben sofort, es steht nie etwas Ungesichertes darin.
    void dropScopeSidecars();

    //  Begleitdateien der App mitzeigen (Ordner-JSON, `.mgedit.json`, `.bak`)?
    //  Kommt aus den Einstellungen; das Umschalten liest den Ordner neu.
    void setShowAllFiles(bool v);
    bool showAllFiles() const { return m_showAllFiles; }

    // ── Unterordner AN ORT UND STELLE aufklappen ─────────────────────────────
    //  Ein aufgeklappter Ordner bleibt eine Kachel; sein Inhalt kommt als
    //  weitere Zeilen DESSELBEN Modells dazu (eigener Bereich, eigene Tiefe).
    //  Angehaengt wird immer am Ende - die Reihenfolge macht der Proxy.
    //
    //  `m_expanded` ist die Wahrheitsquelle und haelt PFADE, nicht Bereiche:
    //  dadurch ueberlebt der Zustand ein reload() (Watcher, Ansichtswechsel),
    //  und das Zuklappen eines Ordners vergisst die Enkel NICHT - klappt man
    //  ihn wieder auf, steht der Unterbaum wieder so da wie vorher.
    Q_INVOKABLE bool expandFolder(const QString& folderPath);
    Q_INVOKABLE bool collapseFolder(const QString& folderPath);
    Q_INVOKABLE bool toggleFolder(const QString& folderPath);
    Q_INVOKABLE bool isFolderExpanded(const QString& folderPath) const {
        return m_expanded.contains(folderPath);
    }
    Q_INVOKABLE void collapseAll();

    //  ── Wie viele Medien liegen in diesem Unterordner? ──────────────────────
    //  Sichtbarkeitsgesteuert wie ein Thumbnail: die Kachel fragt beim
    //  Erscheinen, gezaehlt wird ASYNCHRON (ein Verzeichnis kann Zehntausende
    //  Eintraege haben - auf dem GUI-Thread waere das ein Ruckler je Kachel).
    //  Das Ergebnis kommt als `ChildCountRole` zurueck.
    Q_INVOKABLE void ensureFolderCount(const QString& folderPath);
    //  Bereits gezaehlter Stand (−1 = noch unbekannt) - die Loesch-Rueckfrage
    //  nennt damit die Anzahl, ohne selbst zu zaehlen.
    Q_INVOKABLE int  folderCount(const QString& folderPath) const {
        return m_folderCounts.value(folderPath, -1);
    }

    // ── Ordner-Operationen ───────────────────────────────────────────────────
    //  Alle drei lesen den Ordner danach neu ein (`reload`): das kostet bei
    //  2000 Dateien ~10 ms und erspart es, Bereichstabelle, Aufklapp-Zustand,
    //  Beobachtung und Zeilen einzeln nachzuziehen - Ordner-Vorgaenge sind
    //  selten, ein Fehler darin waere teuer.
    //  `parentFolder` muss der geoeffnete Ordner oder ein AUFGEKLAPPTER
    //  Unterordner sein; sonst legte ein Knopf Ordner an beliebiger Stelle an.
    //  Rueckgabe: 0 ok · 1 Name unbrauchbar · 2 gibt es schon · 3 fehlgeschlagen
    Q_INVOKABLE int  createFolder(const QString& parentFolder, const QString& name);
    Q_INVOKABLE int  renameFolder(const QString& folderPath, const QString& newName);
    //  In den PAPIERKORB - mitsamt Inhalt, und ueber denselben Stapel wie eine
    //  geloeschte Datei zuruecknehmbar.
    Q_INVOKABLE bool deleteFolder(const QString& folderPath);

    // ── Tags über den SICHTBAREN Baum ────────────────────────────────────────
    //  Jeder Ordner fuehrt seine eigene Tag-Liste. Was die Filterleiste
    //  anbietet, muss aber das sein, was gerade zu sehen IST - sonst gaebe es
    //  einen Tag in einem aufgeklappten Unterordner, nach dem man nicht filtern
    //  kann. Beides vereinigt ueber den offenen Ordner und alle aufgeklappten.
    // ── Rekursive Suche (Stufe 5) ────────────────────────────────────────────
    //  Ist ein Filter aktiv, wird der Baum UNTERHALB des offenen Ordners
    //  durchsucht und jeder Ordner mit einem Treffer samt seiner Kette
    //  aufgeklappt. Beim Leeren des Filters kehrt der Aufklapp-Zustand auf den
    //  Stand von VOR der Suche zurueck.
    //
    //  Geurteilt wird mit `MediaProxyModel::acceptsFile` - derselben Funktion,
    //  die auch die Anzeige benutzt. Eine eigene, „ungefaehre" Regel im Worker
    //  liesse Ordner mit leerem Band aufgehen oder Treffer verborgen bleiben.
    void applyDeepFilter(const MediaProxyModel::FilterCriteria& c,
                         const QStringList& categoryNames);
    //  Ergebnis eines Suchlaufs (nur vom Worker, ueber die Ereignisschleife).
    void noteDeepMatches(const QStringList& folders, int generation);
    //  Laeuft gerade eine gefilterte Ansicht mit Tiefensuche?
    bool deepFilterActive() const { return m_deepActive; }
    //  Liegt der Ordner auf dem WEG zu einem Treffer? Nur diese duerfen bei
    //  aktiver Suche stehen bleiben - ein von Hand geoeffneter Ordner ohne
    //  Treffer gehoert nicht ins Ergebnis (vom Nutzer gemeldet).
    bool isOnDeepChain(const QString& folderPath) const {
        return m_deepChain.contains(folderPath);
    }

    Q_INVOKABLE QStringList visibleTags() const;
    //  Farbe eines Tags - erst der offene Ordner, dann die aufgeklappten.
    //  Ungueltig, wenn ihn niemand kennt (der Aufrufer nimmt dann seine Vorgabe).
    Q_INVOKABLE QColor      visibleTagColor(const QString& tag) const;
    //  Fuer den Kategorie-Filter: je AUFGEKLAPPTEM Bereich die Dateinamen, die
    //  dort in einer gleichnamigen Kategorie liegen. Bereich 0 fuellt der Proxy
    //  selbst (er hat den TagManager).
    void fillCategoryFilesByScope(const QStringList& categoryNames,
                                  QHash<int, QSet<QString>>& out) const;
    //  Ergebnis eines Zaehl-Auftrags (nur vom Worker, ueber die Ereignisschleife).
    void noteFolderCount(const QString& folderPath, int count, int generation, int ticket);
    //  Momentaufnahme fuer den Rueckweg (Alt+<-) und fuer die Suche, die den
    //  Zustand vor ihrem Auto-Aufklappen wiederherstellen muss.
    Q_INVOKABLE QStringList expandedFolders() const;
    Q_INVOKABLE void        setExpandedFolders(const QStringList& folderPaths);

    // Alle Thumbnails auf „ausstehend" zurücksetzen (z. B. nach einem Wechsel
    // der Thumbnail-Zielgröße): sichtbare Delegates fordern via
    // thumbnailsInvalidated -> ensureThumbnail neu an; der Rest bleibt lazy.
    void refreshThumbnails();

    // ── QML-Invokables (per Dateipfad) ───────────────────────────────────────
    //  ── Miniaturen: anfordern und abbestellen ────────────────────────────
    //  Beide arbeiten ueber den PFAD, nicht ueber die Kachel - und genau daraus
    //  entstand ein Wettlauf: wandert eine Datei beim Neuaufbau von Kachel A zu
    //  Kachel B, fordert B sie an und A bestellt sie unmittelbar danach ab. B
    //  haelt sich fuer fertig und fragt nie wieder; die Miniatur blieb bis zum
    //  naechsten Ordnerwechsel leer (vom Nutzer gemeldet, am Pruefstand als
    //  „39 von 40" reproduziert).
    //  Deshalb ist das Abbestellen VERZOEGERT: es wird vorgemerkt und erst im
    //  naechsten Durchlauf der Ereignisschleife ausgefuehrt. Dabei zaehlt der
    //  ganze Durchlauf, nicht die Reihenfolge darin: `ensureThumbnail` traegt
    //  den Pfad in `m_thumbWanted` ein, und die Vormerkung wird nur ausgefuehrt,
    //  wenn ihn in DEMSELBEN Durchlauf niemand angefordert hat - egal, ob die
    //  Anforderung vor oder nach dem Abbestellen kam. Nur „erst abbestellen,
    //  dann anfordern" abzufangen genuegte nicht: die uebernehmende Kachel
    //  fordert ZUERST an (sie merkt sich das und fragt nie wieder), die
    //  abgebende bestellt danach ab - genau diese Reihenfolge liess die
    //  Miniatur verschwinden. Der Zweck des Abbestellens (kein Dekodieren fuer
    //  weggescrollte Kacheln) bleibt: ohne neue Anforderung wirkt es wie zuvor.
    Q_INVOKABLE void ensureThumbnail(const QString& filePath);
    Q_INVOKABLE void cancelThumbnail(const QString& filePath);   // weggescrollte Kachel
    Q_INVOKABLE void renameItem(const QString& filePath, const QString& newBaseName);
    //  Datei löschen (Kontextmenü): verschiebt in den PAPIERKORB (reversibel;
    //  Fallback: endgültig, falls das System keinen Papierkorb bietet), räumt
    //  ein evtl. PDF-Editor-Sidecar (<pfad>.mgedit.json) und die persistierten
    //  Metadaten (Tags/Datum) mit ab und entfernt die Zeile aus dem Modell.
    Q_INVOKABLE bool deleteItem(const QString& filePath);

    //  Begleitdateien EINER Datei: der Editor-Sidecar `<pfad>.mgedit.json`
    //  (Notizen/Zeichnungen) und die DOCX-Sicherungskopie `<pfad>.bak`.
    //  `companionKinds` meldet, was vorhanden ist (Bitmaske 1 = Sidecar,
    //  2 = Sicherungskopie), damit die Oberfläche nur anbietet, was es gibt.
    Q_INVOKABLE int  companionKinds(const QString& filePath) const;
    //  Entfernt eine Begleitdatei - über den PAPIERKORB und auf denselben
    //  Undo-Stapel wie das Löschen einer Datei (`Strg+Z` holt sie zurück).
    Q_INVOKABLE bool removeCompanion(const QString& filePath, int kind);
    // ── Datei-Metadaten ÜBER DEN PFAD ────────────────────────────────────────
    //  Alles hier routet auf das Sidecar des Ordners, dem die Datei GEHOERT.
    //  Die frueheren Wege ueber `App.*` nahmen den blanken DATEINAMEN und
    //  trafen damit immer den geoeffneten Ordner - fuer eine Datei aus einem
    //  aufgeklappten Unterordner also das falsche Sidecar.
    //  Kennt das Modell die Datei nicht, passiert nichts (Lesen: leer/ungueltig).
    Q_INVOKABLE QStringList tagsOfFile(const QString& filePath) const;
    Q_INVOKABLE void        removeTag(const QString& filePath, const QString& tag);
    //  „Gibt es etwas zurückzusetzen?" - aus der DATEI beantwortet
    //  (Änderungsdatum weicht vom Erstellungsdatum ab).
    Q_INVOKABLE bool        hasCustomDate(const QString& filePath) const;
    Q_INVOKABLE QDateTime   customDate(const QString& filePath) const;
    Q_INVOKABLE void        setCustomDate(const QString& filePath, const QDateTime& dt);
    Q_INVOKABLE void        clearCustomDate(const QString& filePath);
    //  Schriftfarbe des TXT->PDF-Exports je Datei. UNGUELTIG = keine eigene Wahl;
    //  der Aufrufer nimmt dann die globale Vorgabe (`App.textPdfColor`).
    Q_INVOKABLE QColor      fileTextPdfColor(const QString& filePath) const;
    Q_INVOKABLE bool        hasFileTextPdfColor(const QString& filePath) const;
    Q_INVOKABLE void        setFileTextPdfColor(const QString& filePath, const QColor& c);
    Q_INVOKABLE void        clearFileTextPdfColor(const QString& filePath);

    Q_INVOKABLE void toggleTag(const QString& filePath, const QString& tag);
    //  Tag NUR hinzufügen (nie entfernen) - für das Ablegen einer Kachel auf
    //  einem Tag: ein Zug ist eine Zuweisung, kein Umschalter. Liegt der Tag
    //  schon an, passiert nichts.
    Q_INVOKABLE void addTag(const QString& filePath, const QString& tag);
    //  Gehört die Datei zum aktuell geladenen Ordner? Die Seitenleiste fragt das,
    //  bevor sie eine gezogene Datei einer Kategorie zuordnet: Kategorien merken
    //  sich DATEINAMEN im Sidecar DIESES Ordners - der Name einer fremden Datei
    //  bliebe dort als Waise liegen. `addTag` prüft das intern selbst.
    //  Gehoert die Datei zur ANSICHT (offener Ordner ODER ein aufgeklappter
    //  Unterordner)? Damit unterscheidet die Galerie beim Ablegen einen
    //  app-internen Zug von einer Datei, die von aussen kommt.
    //  `QDir::Hidden` oder nichts - je nach Einstellung (s. `.cpp`).
    static QDir::Filters hiddenFlag();

    Q_INVOKABLE bool ownsFile(const QString& filePath) const {
        return rowForPath(filePath) >= 0;
    }
    Q_INVOKABLE bool hasFile(const QString& filePath) const {
        return isRootFileRow(rowForPath(filePath));
    }

    // ── Mehrfachauswahl (Strg-/Umschalt-Klick, Auswahlrahmen) ────────────────
    //  Die Auswahl liegt HIER und nicht in QML: sie wird je sichtbarer Kachel
    //  gelesen (Rolle `SelectedRole`) und von mehreren Stellen geschrieben
    //  (Kachel, Auswahlrahmen, Strg+A, Kontextmenue). Gehalten wird sie als
    //  PARALLELVEKTOR zu `m_items` - dasselbe Muster wie `m_thumbUrls`/
    //  `m_thumbState` und ein Byte je Zeile, waehrend ein `bool` im Struct
    //  wegen dessen 8-Byte-Ausrichtung acht gekostet haette.
    //
    //  Sie gilt nur fuer die AKTUELLE Ansicht: ein Ordnerwechsel, ein Neu-
    //  Einlesen und jede Filteraenderung raeumen sie ab (s. `clearSelection`).
    //  Eine Auswahl, die man nicht sieht, waere sonst eine Falle - `Strg+C`
    //  und „Loeschen" wuerden Dateien treffen, die gar nicht auf dem Schirm
    //  stehen.
    int  selectionCount() const { return m_selCount; }
    int  selectionRevision() const { return m_selRevision; }
    Q_INVOKABLE bool isSelected(const QString& filePath) const;
    Q_INVOKABLE void setSelected(const QString& filePath, bool on);
    Q_INVOKABLE void toggleSelected(const QString& filePath);
    Q_INVOKABLE void clearSelection();
    //  Alles Ausgewaehlte in MODELL-Reihenfolge. `filesOnly` laesst Ordner weg -
    //  Ordner sind zwar auswaehlbar, aber weder ziehbar noch kopierbar.
    Q_INVOKABLE QStringList selectedPaths(bool filesOnly = false) const;
    //  Dateinamen der ausgewaehlten DATEIEN - Kategorien sind ueber den Namen
    //  adressiert, nicht ueber den Pfad.
    Q_INVOKABLE QStringList selectedFileNames() const;
    //  Tags, die ALLE ausgewaehlten Dateien tragen (Schnittmenge). Daran haengt
    //  das Haekchen im Kontextmenue: angehakt heisst „alle haben ihn".
    Q_INVOKABLE QStringList tagsOfSelection() const;
    //  Denselben Tag an ALLEN ausgewaehlten Dateien setzen bzw. entfernen.
    Q_INVOKABLE void setTagOnSelection(const QString& tag, bool on);
    //  Die Auswahl auf GENAU diese Quellzeilen setzen (aufsteigend sortiert,
    //  ohne Dubletten). Meldet nur, was sich wirklich geaendert hat - der
    //  Auswahlrahmen ruft das je Mausbewegung. Nicht Q_INVOKABLE: QML kennt
    //  Quellzeilen nicht, es geht ueber `MediaProxyModel`.
    void setSelectedRows(const QVector<int>& sortedRows);
    //  Quellzeilen der Auswahl (aufsteigend) - Ausgangspunkt des Rahmens.
    QVector<int> selectedRows() const;

    //  Alles Ausgewaehlte in den Papierkorb - als EIN Schritt auf dem
    //  Rueckgaengig-Stapel (`Strg+Z` holt die ganze Gruppe zurueck).
    //  Rueckgabe: Anzahl der wirklich geloeschten Dateien.
    Q_INVOKABLE int deleteSelected();

    // ── Rückholbare Datei-Vorgänge (Galerie-Undo) ────────────────────────────
    //  Für das Dateisystem gab es bisher kein Undo: ein Fehlgriff war endgültig.
    //  Gelöscht wird in den PAPIERKORB - genau das macht den Rückweg möglich.
    //  Der Stapel lebt nur für die SITZUNG und nur für den offenen Ordner (ein
    //  Ordnerwechsel leert ihn): eine Rücknahme in einen Ordner, den man gerade
    //  nicht sieht, wäre nicht nachvollziehbar. Mitgesichert werden Tags,
    //  Kategorien-Mitgliedschaften und ein eigenes Datum - sie verschwinden beim
    //  Löschen mit und müssen beim Zurückholen wieder da sein.
    // ── Kachel auf ein LESEZEICHEN gezogen: verschieben oder kopieren ────────
    //  `collision`: 0 = fragen (Rückgabe 1, wenn der Name schon vergeben ist),
    //  1 = ersetzen, 2 = umbenennen („Name (2)").
    //  Rückgabe: 0 = erledigt · 1 = Namenskollision (Aufrufer fragt nach) ·
    //  2 = nicht möglich (fremder Pfad, Zielordner fehlt, gleicher Ordner, I/O).
    //  Beim VERSCHIEBEN wandern Tags, Kategorie-Mitgliedschaft und eigenes Datum
    //  mit in den Zielordner (die Zuordnungen liegen JE ORDNER im Sidecar);
    //  beim KOPIEREN nicht - dort entsteht drüben eine unverschlagwortete Kopie
    //  (Festlegung des Nutzers).
    Q_INVOKABLE int     transferToFolder(const QString& filePath, const QString& destFolder,
                                         bool move, int collision = 0);
    //  Wie hieße die Datei drüben (mit „ (2)" bei Kollision)? Für die Rückfrage.
    Q_INVOKABLE QString transferTargetName(const QString& filePath,
                                           const QString& destFolder) const;

    Q_INVOKABLE bool    undoFileOp();
    Q_INVOKABLE bool    redoFileOp();
    //  Dateiname des jeweils nächsten Schrittes ("" = nichts vorhanden) - die
    //  Oberfläche baut daraus ihre Meldung.
    Q_INVOKABLE QString undoFileOpName() const;
    Q_INVOKABLE QString redoFileOpName() const;
    //  Wie viele Dateien haengen am naechsten Schritt? 1 bei einem
    //  Einzelvorgang, N bei einer geloeschten Mehrfachauswahl - die Meldung
    //  der Oberflaeche nennt damit „und N weitere".
    Q_INVOKABLE int     undoFileOpCount() const;
    Q_INVOKABLE int     redoFileOpCount() const;

signals:
    void countChanged();
    void selectionChanged();
    void folderChanged();
    void folderContentsChanged();   // externe Änderung (für Statusmeldung)
    void thumbnailsInvalidated();   // Zielgröße gewechselt -> Delegates fordern neu an
    void fileHistoryChanged();      // Undo-/Redo-Stapel der Datei-Vorgänge
    void expansionChanged();        // ein Unterordner wurde auf-/zugeklappt
    //  Das Datum konnte NICHT an die Datei geschrieben werden (schreibgeschützt,
    //  fremdes Dateisystem) - es steht dann nur im Sidecar.
    void fileDateNotWritten(const QString& fileName);

private slots:
    void onThumbnailReady(const QString& filePath, const QString& thumbUrl);
    void onThumbnailFailed(const QString& filePath);
    void onDirectoryChanged();

private:
    //  Ein rückholbarer Datei-Vorgang (heute: Löschen). `trashPath` leer heißt:
    //  das System hat keinen Papierkorb geboten, die Datei ist endgültig weg -
    //  ein solcher Vorgang kommt gar nicht erst auf den Stapel.
    struct FileOp {
        //  Löschen (Papierkorb) ODER Verschieben in einen anderen Ordner -
        //  beide sind rückholbar und teilen sich denselben Stapel.
        //  Companion = eine Begleitdatei allein (Sidecar/Sicherungskopie);
        //  `path` ist dann die Begleitdatei selbst, die Medien-Datei bleibt.
        //  Folder = ein ganzer Ordner im Papierkorb (mit Inhalt). Er braucht
        //  keine Metadaten: die liegen in SEINEM Sidecar und wandern mit.
        enum class Kind { Delete, Move, Companion, Folder };
        Kind        kind = Kind::Delete;
        //  Gruppennummer: 0 = Einzelvorgang. Vorgaenge mit derselben Nummer
        //  gehoeren zusammen (Mehrfachauswahl geloescht) und gehen als EIN
        //  Schritt zurueck. Sie liegen im Stapel unmittelbar nebeneinander.
        int         group = 0;
        QString     path;                 // ursprünglicher Pfad im Ordner
        QString     movedTo;              // nur Kind::Move: neuer Pfad
        QString     trashPath;            // Ablage im Papierkorb
        QString     sidecarPath;          // "<pfad>.mgedit.json" (falls vorhanden)
        QString     sidecarTrashPath;
        //  Die DOCX-Sicherungskopie "<pfad>.bak". Sie gehoert zur Datei und
        //  hat ohne sie keinen Sinn - blieb sie liegen, stand neben der
        //  geloeschten DOCX eine verwaiste Sicherung (vom Nutzer gemeldet).
        QString     bakPath;
        QString     bakTrashPath;
        QStringList tags;
        QStringList categoryIds;          // DIREKTE Mitgliedschaften (IDs, eigener Ordner)
        QStringList categoryNames;        // dieselben als NAMEN (für einen fremden Ordner)
        //  KEIN Datum: es hängt an der Datei und wandert mit ihr - Verschieben
        //  und Papierkorb erhalten die Zeitstempel.
    };
    //  Deckel gegen unbegrenztes Wachstum (RAM = Priorität 1): der Stapel hält
    //  nur Pfade und Metadaten, aber er soll auch bei Massenlöschungen nicht
    //  ungebremst wachsen.
    static constexpr int kMaxFileOps = 50;

    bool trashFile(const QString& filePath, FileOp* op);  // Datei + Metadaten weg
    bool restoreFile(const FileOp& op);                   // Papierkorb -> Ordner
    bool restoreFolder(const FileOp& op, bool reloadNow = true);  // Papierkorb -> Ordner (Verzeichnis)
    //  Metadaten einer Datei aus dem OFFENEN Ordner einsammeln bzw. entfernen -
    //  gemeinsame Grundlage von Löschen und Verschieben.
    void collectMeta(const QString& fileName, FileOp* op) const;
    void dropMeta(const QString& fileName, const FileOp& op);
    void restoreMeta(const QString& fileName, const FileOp& op);
    //  Metadaten in den Sidecar eines FREMDEN Ordners schreiben bzw. daraus
    //  entfernen (eigene, kurzlebige JsonStorage-Instanz - die laufende gehört
    //  dem offenen Ordner und darf dabei nicht umgeschaltet werden).
    static void writeMetaToFolder(const QString& folder, const QString& fileName,
                                  const FileOp& op, const QHash<QString, QColor>& tagColors);
    static void removeMetaFromFolder(const QString& folder, const QString& fileName);
    //  Pfade in `m_expanded` und im Gedaechtnis umhaengen, wenn ein Ordner
    //  umbenannt oder verschoben wurde (Praefix-Ersetzung, mitsamt Enkeln).
    void remapExpanded(const QString& oldPath, const QString& newPath);
    //  Gehoert der Ordner zur aktuellen Ansicht (offener Ordner oder ein
    //  aufgeklappter Unterordner)?
    bool isVisibleFolder(const QString& folderPath) const;
    bool pushUndo(const FileOp& op);                      // Stapel + Signal
    bool undoMove(const FileOp& op);                      // Verschiebung zurücknehmen
    void appendRowFor(const QString& filePath);           // Zeile ans Ende (Proxy sortiert)
    bool dropRowFor(const QString& filePath);             // Zeile gezielt entfernen
    //  Auswahlzaehler nach einer STRUKTURaenderung (Zeilen entfernt, Ordner neu
    //  eingelesen) nachziehen und `selectionChanged` melden, falls er sich
    //  geaendert hat. Ein O(n)-Lauf, aber nur bei Strukturaenderungen - die
    //  kosten ohnehin schon `rebuildPathIndex`.
    void recountSelection();
    //  Fortschreibungszahl hochzaehlen UND melden. IMMER hierueber, nie
    //  `emit selectionChanged()` von Hand: die Kacheln haengen an der Zahl, und
    //  eine Aenderung ohne sie bliebe auf dem Schirm unsichtbar.
    void noteSelectionChanged();
    //  Einen Ordner in den Papierkorb geben. `reloadNow == false` verschiebt das
    //  Neu-Einlesen an den Aufrufer - `deleteSelected` loescht mehrere Ordner
    //  hintereinander und liest danach EINMAL neu ein.
    bool trashFolderAt(const QString& folderPath, bool reloadNow);
    void clearFileHistory();

    void rebuild(const QString& folderPath);   // startet inkrementelle Befüllung
    void feedChunk(bool firstChunk);           // eine Charge Zeilen einspeisen
    void finishFill();                         // Aufräumen nach letzter Charge
    void emitRow(int row, const QVector<int>& roles);
    //  Dasselbe fuer einen zusammenhaengenden BEREICH - die Auswahl aendert
    //  meist mehrere Zeilen am Stueck (Rahmen, Umschalt-Klick, Strg+A).
    void emitRows(int first, int last, const QVector<int>& roles);

    // ── Bereiche & Scan-Warteschlange ────────────────────────────────────────
    //  Bereich fuer einen Ordner besorgen (vorhandenen reaktivieren oder neu
    //  anlegen) und seinen Inhalt zum Einlesen vormerken.
    int  ensureScope(const QString& folderPath, int parentScope, int folderRow);
    void startScan(int scope);                 // Iterator fuer EINEN Bereich
    bool hasMoreToFill() const;
    //  Nach dem Einspeisen einer Charge: Ordnerzeilen, die laut `m_expanded`
    //  offen sein sollen, sofort in die Warteschlange geben (greift auch nach
    //  einem reload() und beim Wiederaufklappen eines ganzen Unterbaums).
    void queueExpandedFolders(int firstRow);
    void collectDescendantScopes(int scope, QSet<int>& out) const;
    void removeRowsOfScopes(const QSet<int>& scopes);
    void rebuildPathIndex();                   // m_pathToRow + FolderScope::folderRow
    //  Sidecar-Name des Bereichs, zu dem eine Datei gehoert ("" = unbekannt).
    QString sidecarOfScope(int scope) const;

    static QString typeLabel(const MediaItem& item);

    JsonStorage&      m_storage;
    TagManager&       m_tagManager;
    ThumbnailLoader&  m_loader;

    QVector<MediaItem>     m_items;       // reine Daten, keine Pixmaps
    QVector<QString>       m_thumbUrls;   // parallel: Cache-URL je Zeile ("" = none)
    QVector<int>           m_thumbState;  // parallel: 0/1/2
    //  parallel: Mehrfachauswahl je Zeile (0/1). quint8 statt bool im Struct -
    //  s. Kasten bei `selectionCount()`.
    QVector<quint8>        m_selected;
    int                    m_selCount = 0;
    int                    m_selRevision = 0;
    //  Zaehlt die Gruppen des Undo-Stapels hoch (0 bleibt „Einzelvorgang").
    int                    m_opGroup = 0;
    QHash<QString, int>    m_pathToRow;   // schnelle Adressierung für Updates

    // ── Inkrementelle Befüllung ──────────────────────────────────────────────
    //  STREAMEND statt als Liste: der Iterator hält immer nur EINEN Eintrag,
    //  jede Charge liest genau so viele, wie sie einspeist (s. rebuild()).
    //  Es laeuft immer nur EIN Iterator - die weiteren Ordner warten als
    //  Bereichs-Indizes in der Schlange. Anders liefen beim Aufklappen mehrerer
    //  Ordner mehrere Iteratoren gleichzeitig, und jeder haette seinen eigenen
    //  Verzeichnis-Deskriptor offen gehalten.
    std::unique_ptr<QDirIterator> m_pendingIt;   // nullptr = kein Iterator aktiv
    int           m_pendingScope = -1;      // Bereich des laufenden Iterators
    QList<int>    m_scanQueue;              // wartende Bereiche
    bool          m_showAllFiles = false;   // s. setShowAllFiles
    QTimer        m_fillTimer;        // 0-ms-Timer: speist Chargen, gibt dazwischen ab
    //  Vorgemerkte Abbestellungen (s. cancelThumbnail) + der 0-ms-Timer, der
    //  sie ausfuehrt.
    //  „Nach dem Befuellen die Miniaturen neu anfordern lassen" (s. rebuild).
    bool          m_pendingInvalidate = false;
    QSet<QString> m_cancelPending;
    //  Was in DIESEM Durchlauf angefordert wurde (s. oben). Wird zusammen mit
    //  den Vormerkungen geleert, sobald der Timer feuert.
    QSet<QString> m_thumbWanted;
    QTimer        m_cancelTimer;

    //  ── Sidecar-SAMMLUNG ────────────────────────────────────────────────────
    //  Der geoeffnete Ordner behaelt `m_storage` (daran haengen TagManager,
    //  Filter und Seitenleiste). Jeder AUFGEKLAPPTE Unterordner bekommt eine
    //  eigene, lazy erzeugte Instanz auf sein eigenes Sidecar - so tragen seine
    //  Dateien ihre echten Zuordnungen, und wer den Unterordner spaeter direkt
    //  oeffnet, findet sie unveraendert vor. Beim Zuklappen wird die Instanz
    //  wieder abgeraeumt (ein Sidecar kann gross sein).
    QHash<int, JsonStorage*> m_scopeStorage;   // nur Bereiche > 0
    JsonStorage* storageForScope(int scope);
    //  Sidecar zu einem ORDNERPFAD - nullptr, wenn der Ordner gerade nicht
    //  offen ist (dann nimmt der Aufrufer den kurzlebigen Weg, s. writeMetaToFolder).
    JsonStorage* storageForFolder(const QString& folder);
    //  Sidecar einer DATEI (ueber ihren Bereich); nullptr, wenn unbekannt.
    JsonStorage* storageOfFile(const QString& filePath, int* row = nullptr);
    //  Metadaten einer Zeile aus dem Sidecar IHRES Ordners einsammeln/entfernen/
    //  zurueckgeben. Fuer Bereich 0 laeuft das ueber `m_tagManager` (damit die
    //  Seitenleiste mitbekommt, was passiert), sonst direkt auf dem Sidecar.
    void collectMetaAt(const QString& folder, const QString& fileName, FileOp* op) const;
    void dropMetaAt(const QString& folder, const QString& fileName, const FileOp& op);
    void restoreMetaAt(const QString& folder, const QString& fileName, const FileOp& op);
    //  Kategorie-Mitgliedschaften eines Sidecars ueber NAMEN lesen/setzen/loeschen
    //  (die IDs eines fremden Ordners sind andere - s. writeMetaToFolder).
    static QStringList categoryNamesOf(JsonStorage& st, const QString& fileName);
    static void        attachCategories(JsonStorage& st, const QString& fileName,
                                        const QStringList& names);
    static void        stripCategories(JsonStorage& st, const QString& fileName);

    QVector<FolderScope> m_scopes;        // [0] = geoeffneter Ordner
    QHash<QString, int>  m_scopeOfPath;   // Ordnerpfad -> Bereichs-Index
    QSet<QString>        m_expanded;      // aufgeklappte Ordner (Wahrheitsquelle)

    //  Aufklapp-Gedaechtnis JE ORDNER, nur fuer die Sitzung. Wer einen Ordner
    //  verlaesst und (per Alt+<- oder auf jedem anderen Weg) zurueckkehrt, findet
    //  ihn so vor, wie er ihn verlassen hat. Bewusst NICHT auf Platte: das
    //  schriebe bei jedem Auf- und Zuklappen in einen fremden Ordner.
    static constexpr int kMaxFolderMemory = 32;
    //  Ergebnis und laufende Auftraege der Ordner-Zaehlung. Die Generation
    //  verwirft Antworten, die zu einem frueheren Ordnerstand gehoeren.
    QHash<QString, int> m_folderCounts;
    //  Ordner, nach deren Anzahl schon einmal gefragt wurde. Überlebt einen
    //  Neuaufbau - die Kachel fragt danach nämlich NICHT erneut (sie überlebt
    //  ihn ja auch, mit unverändertem Pfad), s. finishFill().
    QSet<QString>       m_countWanted;
    QSet<QString>       m_countPending;
    int                 m_countGeneration = 0;
    //  Je Ordner eine Marke. Sie steigt, sobald ein bereits gezaehlter Stand
    //  ungueltig wird (Datei hinein/heraus) - das Ergebnis eines noch laufenden
    //  Auftrags faellt damit durch, ohne dass alle anderen Ordner ihren Stand
    //  verlieren (das taete die globale Generation).
    QHash<QString, int> m_countTicket;
    //  Der gespeicherte Stand eines Ordners stimmt nicht mehr: wegwerfen und -
    //  wenn eine Kachel je danach gefragt hat - sofort neu zaehlen.
    void invalidateFolderCount(const QString& folderPath);
    QThreadPool*        m_countPool = nullptr;

    //  ── Zustand der rekursiven Suche ───────────────────────────────────────
    bool         m_deepActive = false;     // laeuft gerade eine gefilterte Ansicht?
    QStringList  m_deepSnapshot;           // Aufklapp-Zustand VOR der Suche
    //  Die Treffer-Ordner UND ihre Kette - ohne die von Hand geoeffneten.
    QSet<QString> m_deepChain;
    int          m_deepGeneration = 0;
    QThreadPool* m_deepPool = nullptr;
    QTimer       m_deepTimer;              // entprellt das Tippen
    //  Nur fuer die Diagnose (`MG_DEEPLOG=1`): misst das Aufklappen nach einer
    //  Suche. Ungueltig, solange nicht gemessen wird - kostet dann nichts.
    QElapsedTimer m_deepFillTimer;
    MediaProxyModel::FilterCriteria m_deepCriteria;
    QStringList  m_deepCategoryNames;
    std::shared_ptr<std::atomic<bool>> m_deepCancel;
    void startDeepScan();

    QHash<QString, QStringList> m_expandedMemory;
    QStringList                 m_memoryOrder;   // aeltester vorn (Deckel)
    void rememberExpansion(const QString& folderPath);

    QString             m_folder;
    QFileSystemWatcher* m_watcher;
    QTimer              m_watchDebounce;
    int                 m_suppressWatch = 0;  // >0 -> Watcher-Reload ignorieren

    QVector<FileOp>     m_undoOps;        // zuletzt gelöscht = hinten
    QVector<FileOp>     m_redoOps;        // zurückgeholt, kann erneut gelöscht werden
};
