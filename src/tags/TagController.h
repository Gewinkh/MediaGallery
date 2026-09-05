#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QColor>
#include <QVariantList>

#include "tags/TagManager.h"

// C++-nach-QML-Bridge des Tag- und Kategoriesystems, reine Delegation an TagManager: QML rendert den Baum aus
// `categoriesTree()` und mutiert über die Q_INVOKABLE-Methoden.
class TagController : public QObject {
    Q_OBJECT
    // Rückgängig für TAG-Vorgänge, bewusst getrennt vom Datei-Stapel: einer, der mal eine Datei und mal einen Tag
    // zurückholt, wäre nicht vorhersagbar. Der Stapel gehört der HÄLFTE - das Panel liest ihn über `PaneCtl.tags`.
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStackChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStackChanged)
    //  Die MARKE des jeweils obersten Schrittes - eine Liste von Stuecken
    //  (`text`/`color`/`italic`/`full`), s. `tags/TagUndoMark.h`. Sie ist keine
    //  Zeichenkette, weil jedes Stueck seine eigene Farbe hat.
    Q_PROPERTY(QVariantList undoMark READ undoMark NOTIFY undoStackChanged)
    Q_PROPERTY(QVariantList redoMark READ redoMark NOTIFY undoStackChanged)
    Q_PROPERTY(QString undoIcon READ undoIcon NOTIFY undoStackChanged)
    Q_PROPERTY(QString redoIcon READ redoIcon NOTIFY undoStackChanged)
    Q_PROPERTY(QString undoTip READ undoTip NOTIFY undoStackChanged)
    Q_PROPERTY(QString redoTip READ redoTip NOTIFY undoStackChanged)
public:
    explicit TagController(TagManager& mgr, QObject* parent = nullptr);
    void setTagManager(TagManager& mgr);

    Q_INVOKABLE QStringList allTags() const;
    Q_INVOKABLE QColor      tagColor(const QString& tag) const;
    Q_INVOKABLE void        setTagColor(const QString& tag, const QColor& c);
    Q_INVOKABLE void        createTag(const QString& name, const QColor& color);
    Q_INVOKABLE void        deleteTag(const QString& tag);
    Q_INVOKABLE void        renameTag(const QString& oldName, const QString& newName);

    // Knoten: `{ id, name, color, uniform, inherit, tagUniform, tagColor, tags, fileCount, children }`. `color` ist
    // die effektive Anzeigefarbe; `tagUniform` heißt, die Chips tragen `tagColor` statt ihrer Eigenfarbe.
    Q_INVOKABLE QVariantList categoriesTree() const;
    Q_INVOKABLE QColor       categoryColor(const QString& id) const;

    Q_INVOKABLE QString addRootCategory(const QString& name, const QColor& color, bool uniform);
    Q_INVOKABLE void addSubcategory(const QString& parentId, const QString& name,
                                    const QColor& color, bool uniform);
    Q_INVOKABLE void renameCategory(const QString& id, const QString& newName);
    Q_INVOKABLE void deleteCategory(const QString& id);
    Q_INVOKABLE void setCategoryUniformColor(const QString& id, bool uniform,
                                             const QColor& color, bool inheritToChildren);
    // Kategorie (inkl. Teilbaum) verschieben: newParentId leer = Hauptebene.
    // Deckt die Konverter-Richtungen Unterkategorie ↔ Kategorie ab.
    Q_INVOKABLE void moveCategory(const QString& id, const QString& newParentId);
    Q_INVOKABLE void swapCategories(const QString& aId, const QString& bId);

    Q_INVOKABLE void addTagToCategory(const QString& catId, const QString& tag);
    Q_INVOKABLE void removeTagFromCategory(const QString& catId, const QString& tag);
    Q_INVOKABLE void moveTagToCategory(const QString& tag, const QString& fromCatId,
                                       const QString& toCatId);

    // Flache Kategorienliste mit Pfadnamen ("Eltern / Kind") für Menüs, dazu Abfrage und Mutation der direkten
    // Datei-Mitgliedschaft. Schlüssel ist der DATEINAME, konsistent zum Tag-System.
    Q_INVOKABLE QVariantList categoriesFlat() const;
    Q_INVOKABLE void toggleFileInCategory(const QString& catId, const QString& fileName);
    Q_INVOKABLE bool fileInCategory(const QString& catId, const QString& fileName) const;
    Q_INVOKABLE QStringList categoriesForFile(const QString& fileName) const;   // Namen
    Q_INVOKABLE QStringList categoryIdsForFile(const QString& fileName) const;  // IDs
    //  Mehrfachauswahl
    //  IDs, in denen ALLE genannten Dateien liegen (Schnittmenge) - daran haengt
    //  das Haekchen im Kontextmenue einer Mehrfachauswahl.
    Q_INVOKABLE QStringList categoryIdsForFiles(const QStringList& fileNames) const;
    Q_INVOKABLE void setFilesInCategory(const QString& catId,
                                        const QStringList& fileNames, bool on);

    // Converter: Tag ↔ Unterkategorie (Phase 4)
    // Kombinierte Mehrschritt-Mutationen - bleiben als Geschäftslogik in C++.
    Q_INVOKABLE void convertTagToSubcategory(const QString& tag,
                                             const QString& parentCatId,
                                             const QString& newSubcatName);
    Q_INVOKABLE void convertTagToRootCategory(const QString& tag,
                                              const QString& newName);
    Q_INVOKABLE void convertSubcategoryToTag(const QString& subcatId);

    bool         canUndo() const;
    bool         canRedo() const;
    QVariantList undoMark() const;
    QVariantList redoMark() const;
    QString      undoIcon() const;
    QString      redoIcon() const;
    QString      undoTip() const;
    QString      redoTip() const;
    Q_INVOKABLE void undoLast();
    Q_INVOKABLE void redoLast();
    // Eine laufende Bedienung zu EINEM Rückgängig-Schritt bündeln (die Tag-Modi der Galerie: Kachel um Kachel bis
    // "Fertig"). Muss gepaart gerufen werden; ein Ordnerwechsel schließt eine offene Gruppe selbst.
    Q_INVOKABLE void beginTagModeGroup(const QString& tag);
    Q_INVOKABLE void endUndoGroup();

signals:
    void tagsChanged();
    void categoriesChanged();
    void undoStackChanged();

private:
    // `inherited` = gültige Farbe, wenn ein Vorfahr seine Einheitsfarbe weitergibt. Der Vorfahr gewinnt, der
    // gesamte Teilbaum trägt dieselbe Farbe; ungültig = keine Kaskade aktiv.
    QVariantList buildNodes(const QList<TagCategory>& cats,
                            const QColor& inherited = QColor()) const;

    TagManager* m_mgr;
    bool        m_wired = false;
};
