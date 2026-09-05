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

    QStringList allTags() const;
    QColor      tagColor(const QString& tag) const;
    void        setTagColor(const QString& tag, const QColor& c);

    void addTagToFile(const QString& fileName, const QString& tag);
    void removeTagFromFile(const QString& fileName, const QString& tag);
    QStringList tagsForFile(const QString& fileName) const;
    QStringList filesWithTag(const QString& tag) const;

    void createTag(const QString& name, const QColor& color); // create a global tag with a specific color
    void deleteTag(const QString& tag);
    // Denselben Tag aus den Sidecars ALLER Ordner unterhalb entfernen, im Hintergrund. Bewusst über alle
    // Unterordner, nicht nur die aufgeklappten - sonst hinge das Ergebnis daran, was gerade sichtbar ist.
    void sweepSubfolders(const QString& rootFolder, const QString& tag);
    void renameTag(const QString& oldName, const QString& newName);

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
    // Zwei Kategorien tauschen die Plätze, jede nimmt ihren Teilbaum mit. Ist eine VORFAHR der anderen, nimmt der
    // Nachfahr den Platz ein und der Vorfahr wird sein Kind - genau dafür gibt es die Funktion: `moveCategory` muss
    // so einen Zug abweisen, und "gar nichts passiert" ist die schlechtere Antwort.
    void swapCategories(const QString& aId, const QString& bId);
    QColor categoryColor(const QString& id) const;
    const TagCategory* categoryById(const QString& id) const;

    void addTagToCategory(const QString& catId, const QString& tag);
    void removeTagFromCategory(const QString& catId, const QString& tag);
    void moveTagToCategory(const QString& tag,
                           const QString& fromCatId,
                           const QString& toCatId);

    // Direkte Datei-Mitgliedschaft in einer Kategorie. Bisher entstanden solche Einträge nur über Legacy-JSON und
    // den Converter - ohne Setter konnte QML sie weder zuweisen noch anzeigen. Schlüssel ist der DATEINAME.
    void addFileToCategory(const QString& catId, const QString& fileName);
    void removeFileFromCategory(const QString& catId, const QString& fileName);
    bool fileInCategory(const QString& catId, const QString& fileName) const;
    QStringList categoriesForFile(const QString& fileName) const;   // Namen (rekursiv)
    QStringList categoryIdsForFile(const QString& fileName) const;  // IDs   (rekursiv)

    // Rückgängig für TAG-Vorgänge, bewusst getrennt vom Datei-Stapel (`MediaModel::m_undoOps`, Strg+Z): in einem
    // Stapel holte ein Strg+Z mal eine Datei und mal einen Tag zurück. Verfahren ist ein SCHNAPPSCHUSS des Sidecar-
    // Stands, kein Protokoll - das deckt jede Mutation ab, auch die mehrstufigen des Konverters.
    void beginUndoStep(const mg::tagmark::Mark& mark);
    // Mehrere Mutationen zu EINEM Schritt bündeln, auch über Ereignisdurchläufe hinweg (Konverter, Tag-Modi bis
    // "Fertig"). LAZY: der Schritt entsteht erst bei der ersten wirklichen Mutation. `counted` = Zuordnungs-
    // Sitzung, deren Marke jede Zuordnung fortschreibt - sonst überschriebe sie die Marke einer Umwandlung.
    void beginUndoGroup(const mg::tagmark::Mark& mark, bool counted = false);
    void endUndoGroup();
    // Den Sidecar eines FREMDEN Ordners in den offenen Schritt aufnehmen - für Änderungen an aufgeklappten
    // Unterordnern, die nicht über diesen Manager laufen. Ohne offenen Schritt wird einer geöffnet.
    void noteForeignFolder(const QString& folderPath, const mg::tagmark::Mark& mark);

    bool         canUndo() const { return !m_undo.isEmpty() && m_sweepsPending == 0; }
    QVariantList undoMark() const { return m_undo.isEmpty() ? QVariantList() : m_undo.last().mark.backward; }
    QString      undoIcon() const { return m_undo.isEmpty() ? QString() : m_undo.last().mark.iconBackward; }
    //  Wiederherstellen - dieselbe Mechanik rueckwaerts. Jede neue Mutation
    //  wirft den Wiederherstellen-Stapel weg (der Ast, den man verlassen hat,
    //  ist damit hinfaellig - so arbeitet jeder Editor).
    bool         canRedo() const { return !m_redo.isEmpty() && m_sweepsPending == 0; }
    QVariantList redoMark() const { return m_redo.isEmpty() ? QVariantList() : m_redo.last().mark.forward; }
    QString      redoIcon() const { return m_redo.isEmpty() ? QString() : m_redo.last().mark.iconForward; }
    void undoLastStep();
    void redoLastStep();
    void clearUndo();

    void notifyTagsChangedForBench() { emit tagsChanged(); }

    void flushPendingSignals();

signals:
    void tagsChanged();
    void tagColorChanged(const QString& tag, const QColor& color);
    void categoriesChanged();
    void subfolderSweepFinished(const QString& tag, int count);
    void tagDeleted(const QString& tag);
    void undoStackChanged();
    void tagUndoApplied(const QString& label, int subfolders, bool complete,
                        bool redo);

private:
    struct UndoStep {
        quint64             id = 0;
        mg::tagmark::Mark   mark;       // beide Richtungen des Vorgangs
        QString    folder;              // zu welchem Ordner der Stand gehoert
        QByteArray state;               // Sidecar des offenen Ordners VORHER
        QHash<QString, QByteArray> foreign;
        bool       foreignComplete = true;
        int        bytes = 0;           // grobe Groesse, fuer den RAM-Deckel
        // Nur für Zuordnungs-Schritte: wie viele Dateien dazu- oder weggekommen sind und worauf. Betrifft ein Schritt
        // mehrere Gegenstände, fällt der Gegenstand aus der Marke - `+5` ist ehrlicher als `+5 T:x`, wenn auch T:y dabei war.
        bool                addCounts = false;
        int                 addN = 0, delN = 0;
        mg::tagmark::Thing  cntThing = mg::tagmark::Thing::Tag;
        QString             cntName;
        QStringList         cntPath;
        bool                cntMixed = false;
    };
    //  Deckel. Ein Schnappschuss eines gut gefuellten Ordners liegt im
    //  einstelligen KB-Bereich; der Baum-Durchgang ist der Ausreisser, deshalb
    //  hat er einen eigenen.
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
    bool            m_undoStepOpen = false;
    quint64         m_undoSweepId = 0;
    int             m_sweepsPending = 0;

    void pruneUndo();
    void applyStep(QList<UndoStep>& from, QList<UndoStep>& to, bool redo);
    void beginCountedStep(bool added, mg::tagmark::Thing t, const QString& name,
                          const QStringList& path);
    UndoStep* undoStepById(quint64 id);
    void attachSweepUndo(quint64 stepId, const QHash<QString, QByteArray>& before,
                         bool complete);

    // SAMMELNDES Melden: `tagsChanged` löst bei jedem Empfänger einen vollständigen Durchgang aus - bei 2000 Dateien
    // 0,65 ms je Meldung, und wer 100 Dateien auf einen Tag zieht, zahlte es hundertmal.
    void scheduleTagsChanged();
    void scheduleCategoriesChanged();
    QTimer m_signalTimer;
    bool   m_tagsDirty = false;
    bool   m_catsDirty = false;

    JsonStorage* m_storage;
    QThreadPool m_sweepPool;

    static TagCategory* findById(QList<TagCategory>& list, const QString& id);
    static const TagCategory* findById(const QList<TagCategory>& list, const QString& id);
    static bool         removeById(QList<TagCategory>& list, const QString& id);
    static QList<int>   indexPath(const QList<TagCategory>& list, const QString& id);
    static TagCategory* atPath(QList<TagCategory>& list, const QList<int>& path);
    static bool         subtreeContains(const TagCategory& node, const QString& id);
};
