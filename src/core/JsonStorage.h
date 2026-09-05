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

    void loadFolder(const QString& folderPath);
    QString folderPath() const { return m_folderPath; }

    // Der gesamte Stand als QDataStream, nicht als JSON: der Baum kostete 10,3 ms je
    // Geste (5000 Dateien). Er enthaelt ALLE Tagfarben, auch ungenutzte - die Datei fuehrt
    // nur die benutzten, ein frischer Tag kaeme sonst nicht zurueck. Gepackt: 578 -> 78 KB.
    QByteArray tagStateSnapshot() const;
    //  Denselben Stand wieder herstellen. Schreibt NICHT auf die Platte - das
    //  entscheidet der Aufrufer (`TagManager::undoLastStep` speichert sofort).
    void restoreTagState(const QByteArray& snapshot);
    void saveFolder(const QString& folderPath);
    // SAMMELND: merkt nur, dass zu schreiben ist, und tut es am Ende des Ereignisdurchlaufs EINMAL. Jede Mutation
    // schrieb sonst die GANZE Ordner-JSON - bei 2000 Dateien 6,3 ms je Aufruf, 91 % der Kosten einer Zuordnung.
    void saveCurrentFolder();
    // Bewusst standardmäßig AUS: gesammelt wird nur der Sidecar des OFFENEN Ordners. Unterordner schreiben sofort
    // durch - sie werden je Zuordnung genau einmal angefasst, und ein anderer Leser muss den neuen Stand sehen.
    void setDeferredSaves(bool on) { m_deferSaves = on; }
    //  Ausstehenden Schreibvorgang sofort ausführen. Wird bei jedem
    //  Ordnerwechsel, beim Beenden und vor jedem Lesen von der Platte gerufen -
    //  wer die Datei anfasst, sieht immer den aktuellen Stand.
    void flushPendingSave();

    QStringList getTags(const QString& fileName) const;
    void        setTags(const QString& fileName, const QStringList& tags);


    // Text colour this file uses when exported to PDF (text editor "-> PDF").
    // Invalid colour == no own choice; the caller then falls back to the global
    // default in AppSettings.
    QColor textPdfColor(const QString& fileName) const;
    void   setTextPdfColor(const QString& fileName, const QColor& color);
    void   clearTextPdfColor(const QString& fileName);

    QHash<QString, QColor> tagColors() const { return m_tagColors; }
    QColor tagColor(const QString& tag) const;
    void   setTagColor(const QString& tag, const QColor& color);
    void   ensureTagRegistered(const QString& tag);

    QStringList allTags() const;
    QStringList filesWithTag(const QString& tag) const;

    void applyToItems(QVector<MediaItem>& items) const;

    void renameFile(const QString& oldName, const QString& newName);
    void removeFile(const QString& fileName);

    void deleteTag(const QString& tag);
    // Umbenennen OHNE die Datei-Zuordnungen zu verlieren: "löschen + neu registrieren" nahm den Tag jeder Datei weg
    // (`deleteTag` räumt auch die Datei-Einträge). Existiert der neue Name dort schon, wird der alte nur entfernt.
    void renameTag(const QString& oldName, const QString& newName);

    QList<TagCategory>&       categoriesRef()       { return m_categories; }
    const QList<TagCategory>& categoriesRef() const { return m_categories; }

private:
    QString m_folderPath;
    QTimer  m_saveTimer;
    bool    m_savePending = false;
    bool    m_deferSaves  = false;
    QString m_jsonPath;

    struct FileMeta {
        QStringList tags;
        // KEIN Datum: Änderungs- und Erstellungsdatum stehen an der DATEI selbst. Dieselbe Angabe zweimal zu führen
        // hieße nur, dass sie auseinanderläuft, sobald jemand die Datei außerhalb der App anfasst.
        QColor      textPdfColor;   // invalid == follow the global default
    };

    QHash<QString, FileMeta> m_fileMeta;
    QHash<QString, QColor>   m_tagColors;
    QList<TagCategory>       m_categories;

    // Seit dem Zwei-Fenster-Modus kann derselbe Ordner zweimal offen sein - wer zuletzt schreibt, überschriebe die
    // Änderung der anderen Seite. Deshalb wird der Dateistand beim Lesen gemerkt und vor dem Schreiben abgeglichen.
    QDateTime m_diskMTime;
    qint64    m_diskSize = -1;
    void noteDiskStamp(const QString& path);
    bool diskChangedSince(const QString& path) const;
    void mergeForeignChanges(const QString& path);

    static QColor randomTagColor();

    static QJsonObject categoryToJson(const TagCategory& cat);
    static TagCategory categoryFromJson(const QJsonObject& obj);


    void loadNewFormat(const QJsonObject& root);
};
