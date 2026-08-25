#pragma once
#include <QObject>
#include <QTimer>
#include <QString>
#include <QColor>
#include <QStringList>
#include <QList>
#include "core/JsonStorage.h"
#include "tags/TagCategory.h"

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

    void createTag(const QString& name, const QColor& color); // create a global tag with a specific color
    void deleteTag(const QString& tag);
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

private:
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

    static TagCategory* findById(QList<TagCategory>& list, const QString& id);
    static const TagCategory* findById(const QList<TagCategory>& list, const QString& id);
    static bool         removeById(QList<TagCategory>& list, const QString& id);
};
