#pragma once
#include <QObject>
#include <QTimer>
#include <QString>
#include <QVector>
#include <QHash>
#include <QColor>
#include <QList>
#include "media/MediaItem.h"
#include "tags/TagCategory.h"

struct TagInfo {
    QString name;
    QColor  color;
};

class JsonStorage : public QObject {
    Q_OBJECT
public:
    explicit JsonStorage(QObject* parent = nullptr);
    ~JsonStorage() override;

    // Load/save for a folder
    void loadFolder(const QString& folderPath);
    //  Schreibt SOFORT nach `folderPath` (expliziter Speicherbefehl).
    void saveFolder(const QString& folderPath);
    //  SAMMELND: merkt nur, dass zu schreiben ist, und tut es am Ende des
    //  laufenden Ereignisdurchlaufs EINMAL.
    //
    //  Warum: Jede einzelne Mutation rief das hier - jede Tag-Zuordnung, jede
    //  Kategorie-Änderung -, und jeder Aufruf serialisiert und schreibt die
    //  GANZE Ordner-JSON. Gemessen bei 2000 Dateien: 6,3 ms je Aufruf, also
    //  91 % der Kosten einer Zuordnung. 100 Dateien auf einen Tag zu ziehen
    //  kostete damit ~0,7 s, und es wuchs mit dem Ordner (§0-Priorität 2).
    //  Zusammengefasst wird nur, was im SELBEN Durchlauf anfällt - länger
    //  liegen bleibt nie etwas.
    void saveCurrentFolder();
    //  Schaltet das Sammeln ein. Bewusst standardmäßig AUS: gesammelt wird nur
    //  der Sidecar des OFFENEN Ordners (`MediaModel` schaltet ihn frei). Die
    //  Sidecars aufgeklappter UNTERordner schreiben weiter sofort durch - sie
    //  werden je Zuordnung genau einmal angefasst (nichts zu sammeln), und ein
    //  anderer Leser desselben Ordners muss den neuen Stand sehen, ohne von
    //  unserem Timer zu wissen.
    void setDeferredSaves(bool on) { m_deferSaves = on; }
    //  Ausstehenden Schreibvorgang sofort ausführen. Wird bei jedem
    //  Ordnerwechsel, beim Beenden und vor jedem Lesen von der Platte gerufen -
    //  wer die Datei anfasst, sieht immer den aktuellen Stand.
    void flushPendingSave();

    // Per-file metadata (displayName is NOT persisted - derived from filename)
    QStringList getTags(const QString& fileName) const;
    void        setTags(const QString& fileName, const QStringList& tags);


    // Text colour this file uses when exported to PDF (text editor "-> PDF").
    // Invalid colour == no own choice; the caller then falls back to the global
    // default in AppSettings.
    QColor textPdfColor(const QString& fileName) const;
    void   setTextPdfColor(const QString& fileName, const QColor& color);
    void   clearTextPdfColor(const QString& fileName);

    // Global tag registry
    QHash<QString, QColor> tagColors() const { return m_tagColors; }
    QColor tagColor(const QString& tag) const;
    void   setTagColor(const QString& tag, const QColor& color);
    void   ensureTagRegistered(const QString& tag);

    QStringList allTags() const;

    // Apply loaded data to MediaItem list
    void applyToItems(QVector<MediaItem>& items) const;

    // Update after rename
    void renameFile(const QString& oldName, const QString& newName);
    //  Persistierte Metadaten (Tags/Datum) einer gelöschten Datei entsorgen.
    void removeFile(const QString& fileName);

    // Tag management
    void deleteTag(const QString& tag);

    // Categories
    QList<TagCategory>&       categoriesRef()       { return m_categories; }
    const QList<TagCategory>& categoriesRef() const { return m_categories; }

private:
    QString m_folderPath;
    //  Sammelndes Speichern (s. saveCurrentFolder).
    QTimer  m_saveTimer;
    bool    m_savePending = false;
    bool    m_deferSaves  = false;
    QString m_jsonPath;

    struct FileMeta {
        QStringList tags;
        //  KEIN Datum: das Änderungs- und das Erstellungsdatum stehen an der
        //  DATEI selbst (Festlegung des Nutzers 2026-08-21). Dieselbe Angabe
        //  zweimal zu führen hieße nur, dass sie auseinanderläuft, sobald
        //  jemand die Datei außerhalb der App anfasst.
        QColor      textPdfColor;   // invalid == follow the global default
    };

    QHash<QString, FileMeta> m_fileMeta;
    QHash<QString, QColor>   m_tagColors;
    QList<TagCategory>       m_categories;

    //  ── Zwei Fassungen DERSELBEN Datei ──────────────────────────────────────
    //  Seit dem Zwei-Fenster-Modus kann derselbe Ordner zweimal offen sein; dann
    //  hält jede Hälfte ihre eigene Fassung dieses Sidecars im Speicher. Wer
    //  zuletzt schreibt, würde die Änderung der anderen Seite überschreiben.
    //  Deshalb merkt sich das Objekt den Stand der Datei beim Lesen und
    //  Schreiben und übernimmt vor dem Schreiben fremde Änderungen (dasselbe
    //  schützt gegen Änderungen von außen).
    QDateTime m_diskMTime;
    qint64    m_diskSize = -1;
    void noteDiskStamp(const QString& path);
    bool diskChangedSince(const QString& path) const;
    void mergeForeignChanges(const QString& path);

    static QColor randomTagColor();

    // Category JSON helpers
    static QJsonObject categoryToJson(const TagCategory& cat);
    static TagCategory categoryFromJson(const QJsonObject& obj);

    // Converter - ready for cleanup
    // Load old tag-centric format: { "tags": { "TagName": { "color":"#...", "files":[...] } } }

    // Load new file-centric format: { "files": { "f.jpg": { "tags":[...], "date":"..." } }, "tagColors":{...} }
    void loadNewFormat(const QJsonObject& root);
};
