#pragma once
#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QThreadPool>
#include <QTimer>
#include <QString>
#include <QColor>
#include <QStringList>
#include <QList>
#include <QVariantList>

#include "core/JsonStorage.h"
#include "tags/TagCategory.h"
#include "tags/TagUndoMark.h"

class TagManager : public QObject {
    Q_OBJECT
public:
    explicit TagManager(JsonStorage* storage, QObject* parent = nullptr);

    // ── Tag basics ────────────────────────────────────────────────────────────
    QStringList allTags() const;
    QColor      tagColor(const QString& tag) const;
    void        setTagColor(const QString& tag, const QColor& c);

    void addTagToFile(const QString& fileName, const QString& tag);
    void removeTagFromFile(const QString& fileName, const QString& tag);
    QStringList tagsForFile(const QString& fileName) const;
    //  Umgekehrt: alle Dateien mit diesem Tag (alphabetisch). Der Konverter
    //  zieht damit die Dateien in die neue Kategorie.
    QStringList filesWithTag(const QString& tag) const;

    void createTag(const QString& name, const QColor& color); // create a global tag with a specific color
    //  Löscht den Tag im OFFENEN Ordner (Registry, Datei-Zuordnungen, alle
    //  Kategorien - auch die verschachtelten). Die Unterordner erledigt
    //  `sweepSubfolders`, s. dort; `deleteTag` allein lässt sie unberührt.
    void deleteTag(const QString& tag);
    //  Denselben Tag aus den Sidecars ALLER Ordner UNTERHALB von `rootFolder`
    //  entfernen - im Hintergrund (Regel 8/16: ein tiefer Baum bedeutet viele
    //  kleine Dateien). Bewusst über ALLE Unterordner, nicht nur die
    //  aufgeklappten: sonst hinge das Ergebnis daran, was gerade sichtbar ist.
    //  Meldet `subfolderSweepFinished(tag, geaenderteOrdner)` im GUI-Faden.
    void sweepSubfolders(const QString& rootFolder, const QString& tag);
    void renameTag(const QString& oldName, const QString& newName);

    // ── Categories ────────────────────────────────────────────────────────────
    QList<TagCategory>&       categories();
    const QList<TagCategory>& categories() const;

    void addCategory(const TagCategory& cat);
    void addSubcategory(const QString& parentId, const TagCategory& sub);
    void renameCategory(const QString& id, const QString& newName);
    void deleteCategory(const QString& id);
    void setCategoryUniformColor(const QString& id, bool uniform, const QColor& color,
                                 bool inheritToChildren = false);
    // Kategorie (inkl. Teilbaum) an eine neue Position im Baum verschieben.
    // newParentId leer = Wurzelebene. No-op, wenn das Ziel im eigenen Teilbaum
    // liegt (der Knoten würde sonst verloren gehen) oder id == newParentId.
    void moveCategory(const QString& id, const QString& newParentId);
    //  Zwei Kategorien die PLAETZE TAUSCHEN lassen - jede nimmt ihren eigenen
    //  Inhalt mit (Tags, Dateien, restliche Unterkategorien).
    //  Zwei Faelle:
    //   • unabhaengig voneinander -> jede setzt sich an die Stelle der anderen,
    //     ihr Teilbaum kommt mit.
    //   • eine ist VORFAHR der anderen -> der Nachfahr nimmt den Platz des
    //     Vorfahren ein, und der Vorfahr wird sein Kind. Genau dafuer gibt es
    //     die Funktion: `moveCategory` muss so einen Zug abweisen (der Knoten
    //     ginge sonst verloren), und „gar nichts passiert" ist die schlechtere
    //     Antwort (Festlegung des Nutzers 2026-09-04).
    void swapCategories(const QString& aId, const QString& bId);
    // Returns the effective display color for a category (uniform color if set,
    // or the default teal). Useful for dropdowns that need a live color lookup.
    QColor categoryColor(const QString& id) const;
    const TagCategory* categoryById(const QString& id) const;

    // Tag <-> category membership
    void addTagToCategory(const QString& catId, const QString& tag);
    void removeTagFromCategory(const QString& catId, const QString& tag);
    void moveTagToCategory(const QString& tag,
                           const QString& fromCatId,
                           const QString& toCatId);

    // ── Datei ↔ Kategorie (direkte Mitgliedschaft, TagCategory::files) ────────
    //  Bisher entstanden Datei-Einträge nur über Legacy-JSON bzw. den Converter -
    //  ohne Setter konnte QML Dateien weder Kategorien zuweisen noch die
    //  Mitgliedschaft anzeigen. Schlüssel ist der DATEINAME (wie beim Tag-System).
    void addFileToCategory(const QString& catId, const QString& fileName);
    void removeFileFromCategory(const QString& catId, const QString& fileName);
    bool fileInCategory(const QString& catId, const QString& fileName) const;
    QStringList categoriesForFile(const QString& fileName) const;   // Namen (rekursiv)
    QStringList categoryIdsForFile(const QString& fileName) const;  // IDs   (rekursiv)

    // ── Rueckgaengig fuer TAG-Vorgaenge (Seitenleiste) ───────────────────────
    //  BEWUSST GETRENNT vom Rueckgaengig der Galerie (`MediaModel::m_undoOps`,
    //  Strg+Z): das nimmt DATEI-Vorgaenge zurueck (verschieben, umbenennen,
    //  loeschen). Hier geht es um die Verschlagwortung - beides in einen Stapel
    //  zu werfen hiesse, dass ein Strg+Z mal eine Datei und mal einen Tag
    //  zurueckholt, ohne dass man vorher weiss, welches (Festlegung des
    //  Nutzers 2026-09-03).
    //
    //  Verfahren: SCHNAPPSCHUSS, kein Vorgangs-Protokoll. Vor jeder Mutation
    //  wird der Sidecar-Stand des offenen Ordners als JSON gesichert
    //  (`JsonStorage::tagStateSnapshot`); Rueckgaengig schreibt ihn zurueck.
    //  Das deckt JEDE Mutation ab - auch die mehrstufigen des Konverters -,
    //  ohne je Vorgang eine eigene Umkehrung pflegen zu muessen.
    //
    //  `beginUndoStep` oeffnet einen Schritt. Alles, was im SELBEN
    //  Ereignisdurchlauf folgt, faellt in denselben Schritt (100 Dateien auf
    //  einen Tag zu ziehen ist EIN Rueckgaengig, nicht hundert) - dieselbe
    //  Sammelgrenze wie bei den Aenderungsmeldungen.
    //  `mark` traegt BEIDE Richtungen des Vorgangs (s. `TagUndoMark.h`).
    void beginUndoStep(const mg::tagmark::Mark& mark);
    //  Mehrere Mutationen ausdruecklich zu EINEM Schritt buendeln - auch ueber
    //  Ereignisdurchlaeufe hinweg: der Konverter (vier Mutationen in einem Zug)
    //  und die Tag-Modi der Galerie (der Nutzer klickt Kachel um Kachel an,
    //  bis er „Fertig" drueckt). Zwischen `beginUndoGroup`/`endUndoGroup`
    //  oeffnet keine Mutation einen eigenen Schritt; Verschachtelung wird
    //  gezaehlt.
    //  **LAZY:** die Gruppe merkt sich nur ihre Beschriftung. Der Schritt
    //  entsteht erst bei der ERSTEN wirklichen Mutation - wer den Modus
    //  betritt und wieder verlaesst, ohne etwas anzuklicken, hinterlaesst
    //  keinen leeren Schritt.
    //  `counted` = die Gruppe ist eine ZUORDNUNGS-Sitzung; ihre Marke wird von
    //  jeder Zuordnung darin fortgeschrieben (`+3 -1 T:a`). Eine gewoehnliche
    //  Gruppe (Konverter) behaelt ihre Marke - sonst ueberschriebe eine
    //  Zuordnung, die nebenbei darin passiert, die Marke der Umwandlung.
    void beginUndoGroup(const mg::tagmark::Mark& mark, bool counted = false);
    void endUndoGroup();
    //  Den Sidecar eines FREMDEN Ordners in den offenen Schritt aufnehmen -
    //  fuer Aenderungen an aufgeklappten Unterordnern, die nicht ueber diesen
    //  Manager laufen (`MediaModel::setTagOnRow` mit `scope != 0`). Ohne
    //  offenen Schritt wird einer geoeffnet.
    void noteForeignFolder(const QString& folderPath, const mg::tagmark::Mark& mark);

    //  **Nicht, solange ein Unterordner-Durchgang laeuft.** Er haengt seine
    //  Schnappschuesse erst an, wenn er fertig ist (er laeuft im Hintergrund);
    //  wer vorher zurueckginge, holte den offenen Ordner zurueck und liesse den
    //  Baum geloescht. Die Leiste erscheint deshalb erst danach.
    //  **Die Leiste zeigt, was der KNOPF taete, nicht was geschehen ist.**
    //  Links also die UMKEHRUNG des Vorgangs, rechts den Vorgang selbst.
    bool         canUndo() const { return !m_undo.isEmpty() && m_sweepsPending == 0; }
    QVariantList undoMark() const { return m_undo.isEmpty() ? QVariantList() : m_undo.last().mark.backward; }
    QString      undoIcon() const { return m_undo.isEmpty() ? QString() : m_undo.last().mark.iconBackward; }
    //  **Wiederherstellen** - dieselbe Mechanik rueckwaerts. Jede neue Mutation
    //  wirft den Wiederherstellen-Stapel weg (der Ast, den man verlassen hat,
    //  ist damit hinfaellig - so arbeitet jeder Editor).
    bool         canRedo() const { return !m_redo.isEmpty() && m_sweepsPending == 0; }
    QVariantList redoMark() const { return m_redo.isEmpty() ? QVariantList() : m_redo.last().mark.forward; }
    QString      redoIcon() const { return m_redo.isEmpty() ? QString() : m_redo.last().mark.iconForward; }
    //  Nimmt den obersten Schritt zurueck bzw. stellt ihn wieder her und
    //  speichert sofort. Meldet `tagUndoApplied(text, Unterordner, vollstaendig,
    //  wiederhergestellt)`.
    void undoLastStep();
    void redoLastStep();
    //  Der Stapel gehoert zu EINEM Ordner - beim Ordnerwechsel ist er hinfaellig.
    void clearUndo();

    //  NUR fuer den Pruefstand (`bench_tags`): das Signal einzeln ausloesen,
    //  um seinen Anteil an einer Zuordnung zu messen. Kein Aufrufer in `src/`.
    void notifyTagsChangedForBench() { emit tagsChanged(); }

    //  Ausstehende Meldungen sofort abgeben - Ordnerwechsel, Beenden, und
    //  ueberall dort, wo ein Aufrufer den neuen Stand im SELBEN Durchlauf
    //  braucht.
    void flushPendingSignals();

signals:
    void tagsChanged();
    void tagColorChanged(const QString& tag, const QColor& color);
    void categoriesChanged();
    //  Die Unterordner sind durch - `count` sagt, in wie vielen Ordnern
    //  wirklich etwas geändert wurde (0 = der Tag kam dort nirgends vor).
    void subfolderSweepFinished(const QString& tag, int count);
    //  Ein Tag wurde im OFFENEN Ordner gelöscht. Wer die Unterordner mitziehen
    //  will, hängt sich hier ein - der `TagManager` selbst kennt weder die
    //  Einstellung dafür noch den Ordnerbaum (das weiß `PaneController`).
    void tagDeleted(const QString& tag);
    //  Der Rueckgaengig-Stapel hat sich geaendert (Schritt dazu, zurueckgenommen,
    //  geleert) - daran haengt der Knopf in der Seitenleiste.
    void undoStackChanged();
    //  Ein Schritt wurde zurueckgenommen. `subfolders` = Zahl der wieder
    //  hergestellten Unterordner-Sidecars, `complete` = false, wenn beim
    //  Sichern der Deckel gerissen war (dann ist der Baum nur teilweise zurueck).
    void tagUndoApplied(const QString& label, int subfolders, bool complete,
                        bool redo);

private:
    // ── Rueckgaengig-Stapel (s. oeffentlicher Teil) ──────────────────────────
    struct UndoStep {
        quint64             id = 0;
        mg::tagmark::Mark   mark;       // beide Richtungen des Vorgangs
        QString    folder;              // zu welchem Ordner der Stand gehoert
        QByteArray state;               // Sidecar des offenen Ordners VORHER
        //  Rohe Dateiinhalte fremder Sidecars (Unterordner-Durchgang,
        //  Aenderungen an aufgeklappten Unterordnern). Leerer Wert = die Datei
        //  gab es vorher nicht und muss beim Zurueckgehen geloescht werden.
        QHash<QString, QByteArray> foreign;
        bool       foreignComplete = true;
        int        bytes = 0;           // grobe Groesse, fuer den RAM-Deckel
        //  Nur fuer Zuordnungs-Schritte: wie viele Dateien dazu- bzw.
        //  weggekommen sind, und WORAUF. Die Marke wird daraus neu gebaut,
        //  solange der Schritt offen ist. Betrifft ein Schritt mehrere
        //  Gegenstaende (ein Drop, der gleich mehrere Tags vererbt), faellt der
        //  Gegenstand aus der Marke - `+5` allein ist ehrlicher als `+5 T:x`,
        //  wenn auch `T:y` dabei war.
        bool                addCounts = false;
        int                 addN = 0, delN = 0;
        mg::tagmark::Thing  cntThing = mg::tagmark::Thing::Tag;
        QString             cntName;
        QStringList         cntPath;
        bool                cntMixed = false;
    };
    //  Deckel (§0-Prioritaet 4). Ein Schnappschuss eines gut gefuellten Ordners
    //  liegt im einstelligen KB-Bereich; der Baum-Durchgang ist der Ausreisser,
    //  deshalb hat er einen eigenen.
    static constexpr int    kMaxUndoSteps    = 20;
    static constexpr qint64 kMaxUndoBytes    = 16 * 1024 * 1024;
    static constexpr int    kMaxSweepFolders = 512;
    static constexpr qint64 kMaxSweepBytes   = 8 * 1024 * 1024;

    QList<UndoStep> m_undo;
    QList<UndoStep> m_redo;
    qint64          m_undoBytes = 0;
    quint64         m_undoNextId = 1;
    int             m_undoGroupDepth = 0;
    mg::tagmark::Mark m_undoGroupMark;     // Marke der offenen Gruppe
    bool            m_undoGroupHasMark = false;
    bool            m_undoGroupCounted = false;
    quint64         m_undoGroupStepId = 0; // ihr Schritt, 0 = noch keiner
    //  In diesem Ereignisdurchlauf wurde bereits ein Schritt geoeffnet; weitere
    //  Mutationen fallen hinein. Zurueckgesetzt von `flushPendingSignals`.
    bool            m_undoStepOpen = false;
    //  Der Schritt, an den der naechste Unterordner-Durchgang seine
    //  Schnappschuesse haengt (0 = keiner).
    quint64         m_undoSweepId = 0;
    //  Wie viele Durchgaenge noch unterwegs sind (s. `canUndo`).
    int             m_sweepsPending = 0;

    void pruneUndo();
    //  Der gemeinsame Rumpf von `undoLastStep`/`redoLastStep`: den obersten
    //  Schritt von `from` anwenden und sein Gegenstueck auf `to` legen.
    void applyStep(QList<UndoStep>& from, QList<UndoStep>& to, bool redo);
    //  Zaehler fuer die `+n/-m`-Marke einer Zuordnungs-Sitzung.
    //  Eine Zuordnung eroeffnet bzw. erweitert einen Zaehl-Schritt.
    void beginCountedStep(bool added, mg::tagmark::Thing t, const QString& name,
                          const QStringList& path);
    UndoStep* undoStepById(quint64 id);
    //  Ergebnis des Unterordner-Durchgangs an seinen Schritt haengen (GUI-Faden).
    void attachSweepUndo(quint64 stepId, const QHash<QString, QByteArray>& before,
                         bool complete);

    //  SAMMELNDES Melden: `tagsChanged`/`categoriesChanged` lösen bei jedem
    //  Empfänger einen vollständigen Durchgang aus - `MediaModel` liest die
    //  Tags ALLER Zeilen neu, `MediaProxyModel` filtert alles neu. Bei 2000
    //  Dateien kostet das gemessen 0,65 ms je Meldung; wer 100 Dateien auf
    //  einen Tag zieht, zahlte es hundertmal. Zusammengefasst wird nur, was im
    //  SELBEN Ereignisdurchlauf anfällt (Null-Timer) - länger wartet nie etwas.
    void scheduleTagsChanged();
    void scheduleCategoriesChanged();
    QTimer m_signalTimer;
    bool   m_tagsDirty = false;
    bool   m_catsDirty = false;

    JsonStorage* m_storage;
    //  Eigener Pool fuer `sweepSubfolders` (Regel 8): EIN Faden genuegt - die
    //  Arbeit ist Datei-I/O auf vielen kleinen JSONs, mehr Faeden brachten
    //  beim OCR-Lauf nachweislich nichts als Speicher.
    QThreadPool m_sweepPool;

    static TagCategory* findById(QList<TagCategory>& list, const QString& id);
    static const TagCategory* findById(const QList<TagCategory>& list, const QString& id);
    static bool         removeById(QList<TagCategory>& list, const QString& id);
    //  Der Weg zu einem Knoten als Kette von Listenindizes (leer = nicht da).
    //  Ueber INDIZES und nicht ueber Zeiger, weil beim Tauschen zwei Stellen
    //  nacheinander beschrieben werden - ein Zeiger auf die zweite waere nach
    //  dem ersten Schreiben nicht mehr sicher.
    static QList<int>   indexPath(const QList<TagCategory>& list, const QString& id);
    static TagCategory* atPath(QList<TagCategory>& list, const QList<int>& path);
    static bool         subtreeContains(const TagCategory& node, const QString& id);
};
