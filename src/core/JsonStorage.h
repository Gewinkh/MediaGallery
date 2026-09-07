#pragma once
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QString>
#include <QVector>
#include <QHash>
#include <QColor>
#include <QList>
#include "core/MGStorage.h"
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

    //  Frueher `.json`; eine alte Datei wird beim ersten Speichern umgewandelt.
    static constexpr const char* kEndung = ".mgstore";

    void loadFolder(const QString& folderPath);
    QString folderPath() const { return m_folderPath; }

    // Der gesamte Stand als QDataStream, nicht als JSON: der Baum kostete 10,3 ms je
    // Geste (5000 Dateien). Er enthaelt ALLE Tagfarben, auch ungenutzte - die Datei fuehrt
    // nur die benutzten, ein frischer Tag kaeme sonst nicht zurueck. Gepackt: 578 -> 78 KB.
    QByteArray tagStateSnapshot() const;
    //  Schreibt NICHT auf die Platte - das entscheidet der Aufrufer.
    void restoreTagState(const QByteArray& snapshot);
    void saveFolder(const QString& folderPath);
    // SAMMELND: merkt nur, dass zu schreiben ist, und tut es am Ende des Ereignisdurchlaufs EINMAL. Jede Mutation
    // schrieb sonst die GANZE Ordner-JSON - bei 2000 Dateien 6,3 ms je Aufruf, 91 % der Kosten einer Zuordnung.
    void saveCurrentFolder();
    // Bewusst standardmäßig AUS: gesammelt wird nur der Sidecar des OFFENEN Ordners. Unterordner schreiben sofort
    // durch - sie werden je Zuordnung genau einmal angefasst, und ein anderer Leser muss den neuen Stand sehen.
    void setDeferredSaves(bool on) { m_deferSaves = on; }
    //  Wartet auch auf einen laufenden Vorgang im Arbeitsfaden. Laeuft bei
    //  jedem Ordnerwechsel, beim Beenden und vor jedem Lesen von der Platte.
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
    //  Nur fuer das Rueckgaengig: ein Delta-Schritt stellt die Farbtabelle als
    //  Ganzes her - sonst bliebe ein im Schritt angelegter Tag stehen.
    void setTagColors(const QHash<QString, QColor>& colors) { m_tagColors = colors; }
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

signals:
    //  Der Ordner ist auf die Platte geschrieben. Die andere Haelfte, die
    //  denselben Ordner offen hat, liest daraufhin nach - sonst zeigte sie den
    //  Stand von vor der Aenderung.
    void folderWritten(const QString& folderPath);

public:
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

    //  Im Arbeitsfaden: Bauen und Schreiben kosten bei 20.000 Dateien 40 ms,
    //  die sonst zwischen Geste und naechstem Bild liegen. EIN Faden, damit die
    //  Reihenfolge steht; er liest nur Kopien und fasst nichts an.
    QThreadPool m_savePool;
    int         m_schreibtGerade = 0;      // nur im GUI-Faden
    void        saveFolderAsync(const QString& folderPath);
    void        schreibvorgangFertig(const QString& path);
    void        saveTimerFired();
    //  Leer = die Datei gehoert geloescht.
    static mg::storage::Ablage baueAblage(const QHash<QString, FileMeta>& files,
                                          const QHash<QString, QColor>& colors,
                                          const QList<TagCategory>& cats);
    static QByteArray baueSidecar(const QHash<QString, FileMeta>& files,
                                  const QHash<QString, QColor>& colors,
                                  const QList<TagCategory>& cats);
    //  Umkehrung von `baueAblage`.
    void uebernimmAblage(const mg::storage::Ablage& a);
    QString sidecarPath(const QString& folderPath) const;
    QString altePath(const QString& folderPath) const;
    //  Eine `.mgstore`, die nicht zum Ordnernamen passt - der Ordner wurde
    //  umbenannt, ohne dass die Ablage mitzog.
    QString verwaisteAblage(const QString& folderPath) const;



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
    //  Nur noch zum UMWANDELN - geschrieben wird die alte JSON nicht mehr.
    void leseAlteJson(const QString& pfad);

    //  Deckel beim Lesen - so gross wird eine Ablage nie.
    static constexpr qint64 kMaxAblageBytes = 256LL * 1024 * 1024;
};
