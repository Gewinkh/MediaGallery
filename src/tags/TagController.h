#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QColor>
#include <QVariantList>

#include "tags/TagManager.h"

// ─────────────────────────────────────────────────────────────────────────────
//  TagController - C++->QML-Bridge (Singleton) für das Tag-/Kategorie-System.
//
//  Reine Delegation an TagManager (Backend bleibt unverändert). Ersetzt die
//  Widget-Klassen TagCategoryPanel/TagWidget als Datenquelle: QML rendert den
//  Baum aus categoriesTree() und mutiert über die Q_INVOKABLE-Methoden.
//
//  Registrierung via qmlRegisterSingletonInstance("MediaGallery",1,0,"Tags",…)
//  in main.cpp - keine QML_ELEMENT-Makros.
// ─────────────────────────────────────────────────────────────────────────────
class TagController : public QObject {
    Q_OBJECT
    //  ── Rueckgaengig fuer TAG-Vorgaenge (Seitenleiste) ──────────────────────
    //  BEWUSST GETRENNT vom Rueckgaengig der Dateien (Strg+Z, `MediaModel`):
    //  ein Stapel, der mal eine Datei und mal einen Tag zurueckholt, waere
    //  nicht vorhersagbar (Festlegung des Nutzers 2026-09-03). Der Stapel
    //  gehoert der HAELFTE - das Panel liest ihn ueber `PaneCtl.tags`.
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStackChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStackChanged)
    //  Die MARKE des jeweils obersten Schrittes - eine Liste von Stuecken
    //  (`text`/`color`/`italic`/`full`), s. `tags/TagUndoMark.h`. Sie ist keine
    //  Zeichenkette, weil jedes Stueck seine eigene Farbe hat.
    Q_PROPERTY(QVariantList undoMark READ undoMark NOTIFY undoStackChanged)
    Q_PROPERTY(QVariantList redoMark READ redoMark NOTIFY undoStackChanged)
    //  Gezeichnetes Symbol vor der Marke ("trash" beim Loeschen, sonst leer).
    Q_PROPERTY(QString undoIcon READ undoIcon NOTIFY undoStackChanged)
    Q_PROPERTY(QString redoIcon READ redoIcon NOTIFY undoStackChanged)
    //  Der volle Text zum Hover ueber der Leiste (Namen und Pfade ungekuerzt).
    Q_PROPERTY(QString undoTip READ undoTip NOTIFY undoStackChanged)
    Q_PROPERTY(QString redoTip READ redoTip NOTIFY undoStackChanged)
public:
    explicit TagController(TagManager& mgr, QObject* parent = nullptr);
    //  Auf einen anderen Manager umhängen (Fokuswechsel zwischen den Hälften).
    void setTagManager(TagManager& mgr);

    // ── Tags ────────────────────────────────────────────────────────────────
    Q_INVOKABLE QStringList allTags() const;
    Q_INVOKABLE QColor      tagColor(const QString& tag) const;
    Q_INVOKABLE void        setTagColor(const QString& tag, const QColor& c);
    Q_INVOKABLE void        createTag(const QString& name, const QColor& color);
    Q_INVOKABLE void        deleteTag(const QString& tag);
    Q_INVOKABLE void        renameTag(const QString& oldName, const QString& newName);

    // ── Kategorie-Baum (rekursiv, für QML-Repeater/TreeView) ─────────────────
    // Knoten: { id, name, color, uniform, inherit, tagUniform, tagColor,
    //          tags:[…], fileCount, children:[…] }
    //  `color`      = effektive Anzeigefarbe des Knotens (Kaskade oder Eigenfarbe).
    //  `tagUniform` = true -> die Tag-Chips dieses Knotens tragen `tagColor` statt
    //                 ihrer Eigenfarbe (Einheitsfarbe/Vererbung aktiv).
    Q_INVOKABLE QVariantList categoriesTree() const;
    Q_INVOKABLE QColor       categoryColor(const QString& id) const;

    // Liefert die ID der neu erstellten Wurzelkategorie (leer bei ungültigem
    // Namen) - ermöglicht QML, die Kategorie direkt weiterzuverwenden (z. B.
    // S-Modus: Kategorie erstellen UND Datei sofort zuordnen).
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
    //  Zwei Kategorien tauschen die Plaetze, jede nimmt ihren Inhalt mit
    //  (s. `TagManager::swapCategories`).
    Q_INVOKABLE void swapCategories(const QString& aId, const QString& bId);

    // ── Tag ↔ Kategorie ──────────────────────────────────────────────────────
    Q_INVOKABLE void addTagToCategory(const QString& catId, const QString& tag);
    Q_INVOKABLE void removeTagFromCategory(const QString& catId, const QString& tag);
    Q_INVOKABLE void moveTagToCategory(const QString& tag, const QString& fromCatId,
                                       const QString& toCatId);

    // ── Datei ↔ Kategorie (Kontextmenü „Kategorie hinzufügen" / Anzeige) ─────
    // Flache Kategorienliste [{id,name,color}] mit Pfadnamen („Eltern / Kind")
    // für Menüs; Mutation/Abfrage der direkten Datei-Mitgliedschaft. Schlüssel
    // ist der DATEINAME (konsistent zum Tag-System / JsonStorage).
    Q_INVOKABLE QVariantList categoriesFlat() const;
    Q_INVOKABLE void toggleFileInCategory(const QString& catId, const QString& fileName);
    Q_INVOKABLE bool fileInCategory(const QString& catId, const QString& fileName) const;
    Q_INVOKABLE QStringList categoriesForFile(const QString& fileName) const;   // Namen
    Q_INVOKABLE QStringList categoryIdsForFile(const QString& fileName) const;  // IDs
    //  ── Mehrfachauswahl ──────────────────────────────────────────────────────
    //  IDs, in denen ALLE genannten Dateien liegen (Schnittmenge) - daran haengt
    //  das Haekchen im Kontextmenue einer Mehrfachauswahl.
    Q_INVOKABLE QStringList categoryIdsForFiles(const QStringList& fileNames) const;
    //  Dieselbe Mitgliedschaft fuer alle genannten Dateien setzen bzw. loesen.
    Q_INVOKABLE void setFilesInCategory(const QString& catId,
                                        const QStringList& fileNames, bool on);

    // ── Converter: Tag ↔ Unterkategorie (Phase 4) ────────────────────────────
    // Kombinierte Mehrschritt-Mutationen - bleiben als Geschäftslogik in C++.
    Q_INVOKABLE void convertTagToSubcategory(const QString& tag,
                                             const QString& parentCatId,
                                             const QString& newSubcatName);
    // Tag -> eigenständige Hauptkategorie (Wurzelebene).
    Q_INVOKABLE void convertTagToRootCategory(const QString& tag,
                                              const QString& newName);
    // Funktioniert für JEDE Kategorie-ID (Unter- wie Hauptkategorie);
    // enthaltene Unterkategorien werden mit entfernt (deleteCategory).
    Q_INVOKABLE void convertSubcategoryToTag(const QString& subcatId);

    // ── Rueckgaengig ─────────────────────────────────────────────────────────
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
    //  Eine LAUFENDE Bedienung zu EINEM Rueckgaengig-Schritt buendeln - die
    //  Tag-Modi der Galerie (`GalleryView.enterAddToTagMode`/`enterGroupMode`):
    //  der Nutzer klickt dort Kachel um Kachel an, und was er bis „Fertig"
    //  gesetzt hat, gehoert zusammen. Muss gepaart aufgerufen werden; ein
    //  Ordnerwechsel schliesst eine offene Gruppe selbst (s. `TagManager`).
    //  `add` = im Modus wird zugeordnet (gruen), sonst entfernt (rot). Die
    //  Marke entsteht in C++ - QML setzt keine Zeichenketten zusammen.
    Q_INVOKABLE void beginTagModeGroup(const QString& tag);
    Q_INVOKABLE void endUndoGroup();

signals:
    void tagsChanged();
    void categoriesChanged();
    void undoStackChanged();

private:
    // `inherited` = gültige Farbe, wenn ein Vorfahr seine Einheitsfarbe an die
    // Kinder weitergibt (Vererbung). Der Vorfahr gewinnt -> der gesamte Teilbaum
    // (Unterkategorien, verschachtelte Unterkategorien und alle Tags) trägt
    // dieselbe Farbe. Ungültig = keine Kaskade aktiv.
    QVariantList buildNodes(const QList<TagCategory>& cats,
                            const QColor& inherited = QColor()) const;

    //  ZEIGER, nicht Referenz: die Fassade `Tags` (Einstellungen, Konverter)
    //  folgt der fokussierten Galerie-Hälfte - sie zeigt also mal auf den einen,
    //  mal auf den anderen Manager. Er ist NIE null (es gibt immer eine Hälfte).
    TagManager* m_mgr;
    bool        m_wired = false;
};
