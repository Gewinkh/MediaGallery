#include "core/JsonStorage.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSet>
#include <QDataStream>

JsonStorage::JsonStorage(QObject* parent) : QObject(parent) {
    //  Sammelndes Speichern: Ein Null-Timer feuert am Ende des laufenden
    //  Ereignisdurchlaufs - alles, was darin anfällt (100 Dateien auf einen
    //  Tag ziehen), wird zu EINEM Schreibvorgang.
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(0);
    connect(&m_saveTimer, &QTimer::timeout, this, &JsonStorage::flushPendingSave);
    //  Beenden: was noch aussteht, muss auf die Platte. Der Destruktor allein
    //  genügt nicht - beim regulären Beenden räumt Qt die Ereignisschleife ab,
    //  bevor lange lebende Objekte fallen.
    if (QCoreApplication* app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this,
                &JsonStorage::flushPendingSave);
}

JsonStorage::~JsonStorage() { flushPendingSave(); }

QColor JsonStorage::randomTagColor() {
    static const QList<QColor> palette = {
        {220, 80,  80},  {80,  200, 120}, {80,  140, 220},
        {220, 160, 60},  {160, 80,  220}, {60,  200, 200},
        {220, 100, 160}, {140, 200, 60},  {80,  180, 200},
        {200, 140, 80},  {100, 120, 220}, {180, 80,  120},
        {60,  180, 140}, {200, 80,  60},  {120, 200, 160}
    };
    return palette[QRandomGenerator::global()->bounded(palette.size())];
}

QJsonObject JsonStorage::categoryToJson(const TagCategory& cat) {
    QJsonObject obj;
    obj["id"]           = cat.id;
    obj["name"]         = cat.name;
    obj["uniformColor"]          = cat.uniformColor;
    obj["color"]                 = cat.color.name();
    if (cat.inheritColorToChildren)
        obj["inheritColorToChildren"] = true;

    QJsonArray tags;
    for (const QString& t : cat.tags) tags.append(t);
    obj["tags"] = tags;

    QJsonArray files;
    for (const QString& f : cat.files) files.append(f);
    if (!files.isEmpty()) obj["files"] = files;

    if (!cat.children.isEmpty()) {
        QJsonArray children;
        for (const auto& ch : cat.children) children.append(categoryToJson(ch));
        obj["children"] = children;
    }

    return obj;
}

TagCategory JsonStorage::categoryFromJson(const QJsonObject& obj) {
    TagCategory cat;
    cat.id           = obj["id"].toString();
    cat.name         = obj["name"].toString();
    cat.uniformColor             = obj["uniformColor"].toBool(false);
    cat.color                    = QColor(obj["color"].toString("#00b4a0"));
    cat.inheritColorToChildren   = obj["inheritColorToChildren"].toBool(false);

    QJsonArray tags = obj["tags"].toArray();
    for (const auto& t : tags) cat.tags.append(t.toString());

    QJsonArray files = obj["files"].toArray();
    for (const auto& f : files) cat.files.append(f.toString());

    QJsonArray children = obj["children"].toArray();
    for (const auto& ch : children) cat.children.append(categoryFromJson(ch.toObject()));

    return cat;
}

// Dateizentrisch: { files: { name: { t: [tags], d: ISO8601 } }, tagColors, categories }.
// Nur nicht-leere Felder werden geschrieben. Ein altes "v"-Feld wird ignoriert.
void JsonStorage::loadNewFormat(const QJsonObject& root) {
    QJsonObject tagColors = root["tagColors"].toObject();
    for (auto it = tagColors.begin(); it != tagColors.end(); ++it)
        m_tagColors[it.key()] = QColor(it.value().toString("#64b4a0"));

    QJsonObject files = root["files"].toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        QJsonObject o = it.value().toObject();
        FileMeta& meta = m_fileMeta[it.key()];

        QJsonArray tagsArr = o["t"].toArray();
        for (const auto& tv : tagsArr) {
            QString tag = tv.toString();
            meta.tags.append(tag);
            ensureTagRegistered(tag);
        }

        //  „d" (eigenes Datum) und „o" (Merker) werden NICHT mehr gelesen: das
        //  Datum steht an der Datei. Alte Einträge verschwinden beim nächsten
        //  Speichern von selbst - der Sidecar wird immer ganz neu geschrieben.

        if (o.contains("c")) {
            // Only a colour Qt can parse is taken over; garbage stays "no choice"
            // so the export falls back to the global default instead of black-on-
            // black from an unusable value.
            const QColor c(o["c"].toString());
            if (c.isValid()) meta.textPdfColor = c;
        }
    }
}

void JsonStorage::loadFolder(const QString& folderPath) {
    //  Ein ausstehender Schreibvorgang gehört zum BISHERIGEN Ordner - er muss
    //  raus, bevor `m_folderPath` weiterzeigt, sonst landet er im falschen
    //  Ordner oder fällt ganz unter den Tisch.
    flushPendingSave();
    m_folderPath = folderPath;
    m_fileMeta.clear();
    m_tagColors.clear();
    m_categories.clear();
    QFileInfo fi(folderPath);
    m_jsonPath = folderPath + "/" + fi.fileName() + ".json";

    QFile f(m_jsonPath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    noteDiskStamp(m_jsonPath);
    if (!doc.isObject()) return;

    QJsonObject root = doc.object();

    loadNewFormat(root);

    QJsonArray cats = root["categories"].toArray();
    for (const auto& c : cats) m_categories.append(categoryFromJson(c.toObject()));
}

void JsonStorage::noteDiskStamp(const QString& path) {
    const QFileInfo fi(path);
    m_diskMTime = fi.exists() ? fi.lastModified() : QDateTime();
    m_diskSize  = fi.exists() ? fi.size() : -1;
}

bool JsonStorage::diskChangedSince(const QString& path) const {
    const QFileInfo fi(path);
    if (!fi.exists()) return false;                 // nichts, was wir verlieren könnten
    if (m_diskSize < 0) return true;                // wir haben nie gelesen
    return fi.size() != m_diskSize || fi.lastModified() != m_diskMTime;
}

// Die Datei hat sich seit unserem Lesen geändert - jemand anderes hat geschrieben. Übernommen wird, was wir nicht
// kennen; wo beide etwas wissen, gewinnt das HINZUFÜGEN: der Verlust fremder Verschlagwortung wöge schwerer.
void JsonStorage::mergeForeignChanges(const QString& path) {
    if (!diskChangedSince(path)) return;

    JsonStorage disk;
    disk.loadFolder(m_folderPath);

    for (auto it = disk.m_fileMeta.cbegin(); it != disk.m_fileMeta.cend(); ++it) {
        const FileMeta& theirs = it.value();
        if (!m_fileMeta.contains(it.key())) { m_fileMeta.insert(it.key(), theirs); continue; }
        FileMeta& ours = m_fileMeta[it.key()];
        for (const QString& t : theirs.tags)
            if (!ours.tags.contains(t)) ours.tags.append(t);
        if (!ours.textPdfColor.isValid() && theirs.textPdfColor.isValid())
            ours.textPdfColor = theirs.textPdfColor;
    }
    for (auto it = disk.m_tagColors.cbegin(); it != disk.m_tagColors.cend(); ++it)
        if (!m_tagColors.contains(it.key())) m_tagColors.insert(it.key(), it.value());
    //  Kategorien sind ein BAUM; ihn zu verschmelzen wäre Raten. Kennen wir
    //  keinen, übernehmen wir den fremden - sonst bleibt unserer stehen.
    if (m_categories.isEmpty()) m_categories = disk.m_categories;
}

void JsonStorage::saveFolder(const QString& folderPath) {
    if (folderPath == m_folderPath) {
        m_saveTimer.stop();
        m_savePending = false;
    }
    mergeForeignChanges(m_jsonPath.isEmpty()
                            ? folderPath + "/" + QFileInfo(folderPath).fileName() + ".json"
                            : m_jsonPath);

    QJsonObject root;

    // Compact file-centric section: only entries with actual data (tags, custom date, PDF text colour). Keys are
    // short ("t", "d", "c") to keep large collections small.
    QJsonObject filesObj;
    for (auto it = m_fileMeta.cbegin(); it != m_fileMeta.cend(); ++it) {
        const FileMeta& meta = it.value();
        if (meta.tags.isEmpty() && !meta.textPdfColor.isValid())
            continue;

        QJsonObject o;
        if (!meta.tags.isEmpty()) {
            QJsonArray tagsArr;
            for (const QString& t : meta.tags) tagsArr.append(t);
            o["t"] = tagsArr;
        }
        if (meta.textPdfColor.isValid())
            o["c"] = meta.textPdfColor.name(QColor::HexRgb);

        filesObj[it.key()] = o;
    }
    if (!filesObj.isEmpty())
        root["files"] = filesObj;

    // Tag color registry
    // Collect tags used anywhere (files + categories) so the registry stays clean.
    QSet<QString> usedTags;
    for (auto it = m_fileMeta.cbegin(); it != m_fileMeta.cend(); ++it)
        for (const QString& t : it.value().tags) usedTags.insert(t);

    struct CatTagCollector {
        static void collect(const QList<TagCategory>& cats, QSet<QString>& out) {
            for (const TagCategory& cat : cats) {
                for (const QString& t : cat.tags) out.insert(t);
                collect(cat.children, out);
            }
        }
    };
    CatTagCollector::collect(m_categories, usedTags);

    QJsonObject tagColorsObj;
    for (const QString& tag : usedTags)
        tagColorsObj[tag] = m_tagColors.value(tag, QColor(100, 180, 160)).name();
    if (!tagColorsObj.isEmpty())
        root["tagColors"] = tagColorsObj;

    QJsonArray cats;
    for (const auto& cat : m_categories) cats.append(categoryToJson(cat));
    if (!cats.isEmpty())
        root["categories"] = cats;

    QString path = m_jsonPath.isEmpty()
                       ? folderPath + "/" + QFileInfo(folderPath).fileName() + ".json"
                       : m_jsonPath;

    // Keine tatsächlichen Daten -> KEINE Leerdatei anlegen: sonst entstünde allein durch das Öffnen eines Ordners
    // eine JSON. Eine bestehende, nun leere Datei wird entfernt statt als Stub zu bleiben.
    const bool hasContent = root.contains("files") || root.contains("tagColors")
                             || root.contains("categories");
    if (!hasContent) {
        if (QFile::exists(path))
            QFile::remove(path);
        noteDiskStamp(path);
        return;
    }

    // ATOMAR schreiben (QSaveFile): diese Datei ist die EINZIGE Quelle aller Tags und Daten eines Ordners und wird
    // bei jeder Mutation neu geschrieben. Mit `open(WriteOnly)` war sie zuerst auf 0 Bytes gekürzt - Totalverlust.
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    if (f.write(bytes) != bytes.size()) {
        f.cancelWriting();     // Original bleibt unangetastet
        return;
    }
    f.commit();
    noteDiskStamp(path);     // ab jetzt sind WIR der Stand der Datei
}

// Bewusst nicht das JSON der Platte: an 5000 Dateien kostete der Baum 10,3 ms plus
// 3,0 ms je Geste. Der Schnappschuss verlaesst den Prozess nie und muss MEHR
// enthalten - die Datei fuehrt nur benutzte Tagfarben.
namespace {
constexpr quint32 kSnapMagic   = 0x4D47'5447;   // "MGTG"
constexpr quint16 kSnapVersion = 1;

void writeCat(QDataStream& ds, const TagCategory& c) {
    ds << c.id << c.name << c.uniformColor << c.color << c.inheritColorToChildren
       << c.tags << c.files << quint32(c.children.size());
    for (const TagCategory& ch : c.children) writeCat(ds, ch);
}

TagCategory readCat(QDataStream& ds, int depth) {
    TagCategory c;
    quint32 n = 0;
    ds >> c.id >> c.name >> c.uniformColor >> c.color >> c.inheritColorToChildren
       >> c.tags >> c.files >> n;
    //  Der Strom stammt aus dem eigenen Prozess; die Grenze steht trotzdem -
    //  ein beschaedigter Puffer soll den Stapel nicht sprengen.
    if (ds.status() != QDataStream::Ok || depth > 64) return c;
    for (quint32 i = 0; i < n; ++i) {
        if (ds.status() != QDataStream::Ok) break;
        c.children.append(readCat(ds, depth + 1));
    }
    return c;
}
}  // namespace

QByteArray JsonStorage::tagStateSnapshot() const {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setVersion(QDataStream::Qt_6_0);
    ds << kSnapMagic << kSnapVersion;

    ds << quint32(m_fileMeta.size());
    for (auto it = m_fileMeta.cbegin(); it != m_fileMeta.cend(); ++it)
        ds << it.key() << it.value().tags << it.value().textPdfColor;

    ds << quint32(m_tagColors.size());
    for (auto it = m_tagColors.cbegin(); it != m_tagColors.cend(); ++it)
        ds << it.key() << it.value();

    ds << quint32(m_categories.size());
    for (const TagCategory& c : m_categories) writeCat(ds, c);

    //  GEPACKT im Stapel liegen (RAM zuerst). Gemessen am selben Ordner: 578
    //  KB roh -> 78 KB, dafuer 1,9 ms. Stufe 1 und nicht 9 - hoehere Stufen
    //  kosteten deutlich mehr Zeit fuer wenige Prozent.
    return qCompress(out, 1);
}

void JsonStorage::restoreTagState(const QByteArray& snapshot) {
    //  Muell ergibt hier leer, und die Kennung unten faellt dann durch.
    const QByteArray raw = qUncompress(snapshot);
    QDataStream ds(raw);
    ds.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0; quint16 version = 0;
    ds >> magic >> version;
    if (magic != kSnapMagic || version != kSnapVersion) return;

    //  ERST vollstaendig lesen, DANN uebernehmen: bricht der Strom mittendrin
    //  ab, bleibt der bisherige Stand stehen statt halb ueberschrieben zu sein.
    QHash<QString, FileMeta> files;
    QHash<QString, QColor>   colors;
    QList<TagCategory>       cats;

    quint32 n = 0;
    ds >> n;
    for (quint32 i = 0; i < n && ds.status() == QDataStream::Ok; ++i) {
        QString name; FileMeta meta;
        ds >> name >> meta.tags >> meta.textPdfColor;
        files.insert(name, meta);
    }
    ds >> n;
    for (quint32 i = 0; i < n && ds.status() == QDataStream::Ok; ++i) {
        QString tag; QColor c;
        ds >> tag >> c;
        colors.insert(tag, c);
    }
    ds >> n;
    for (quint32 i = 0; i < n && ds.status() == QDataStream::Ok; ++i)
        cats.append(readCat(ds, 0));

    if (ds.status() != QDataStream::Ok) return;
    m_fileMeta   = std::move(files);
    m_tagColors  = std::move(colors);
    m_categories = std::move(cats);
}

void JsonStorage::saveCurrentFolder() {
    if (m_folderPath.isEmpty()) return;
    // Sammeln setzt eine laufende Ereignisschleife voraus - der Null-Timer feuert sonst nie. Ohne sie (Testtreiber,
    // Abbau beim Beenden) wird SOFORT geschrieben; das ist der sichere Fall, nicht der Ausnahmefall.
    if (!m_deferSaves || !QCoreApplication::instance()) {
        saveFolder(m_folderPath);
        return;
    }
    m_savePending = true;
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void JsonStorage::flushPendingSave() {
    m_saveTimer.stop();
    if (!m_savePending || m_folderPath.isEmpty()) {
        m_savePending = false;
        return;
    }
    m_savePending = false;              // VOR dem Schreiben zurücksetzen -
    saveFolder(m_folderPath);           // saveFolder darf nicht erneut anstoßen
}

QStringList JsonStorage::getTags(const QString& f) const {
    return m_fileMeta.value(f).tags;
}
void JsonStorage::setTags(const QString& f, const QStringList& tags) {
    m_fileMeta[f].tags = tags;
    for (const auto& t : tags) ensureTagRegistered(t);
}
QColor JsonStorage::textPdfColor(const QString& f) const {
    return m_fileMeta.value(f).textPdfColor;
}
void JsonStorage::setTextPdfColor(const QString& f, const QColor& color) {
    m_fileMeta[f].textPdfColor = color;
}
void JsonStorage::clearTextPdfColor(const QString& f) {
    m_fileMeta[f].textPdfColor = QColor();
}


QColor JsonStorage::tagColor(const QString& tag) const {
    return m_tagColors.value(tag, QColor(100, 180, 160));
}
void JsonStorage::setTagColor(const QString& tag, const QColor& c) {
    m_tagColors[tag] = c;
}
void JsonStorage::ensureTagRegistered(const QString& tag) {
    if (!m_tagColors.contains(tag))
        m_tagColors.insert(tag, randomTagColor());
}
QStringList JsonStorage::allTags() const {
    QStringList list;
    for (auto it = m_tagColors.cbegin(); it != m_tagColors.cend(); ++it)
        list.append(it.key());
    list.sort(Qt::CaseInsensitive);
    return list;
}
QStringList JsonStorage::filesWithTag(const QString& tag) const {
    QStringList out;
    if (tag.isEmpty()) return out;
    for (auto it = m_fileMeta.cbegin(); it != m_fileMeta.cend(); ++it)
        if (it.value().tags.contains(tag)) out.append(it.key());
    out.sort(Qt::CaseInsensitive);
    return out;
}

void JsonStorage::renameTag(const QString& oldName, const QString& newName) {
    if (oldName.isEmpty() || newName.isEmpty() || oldName == newName) return;

    const QColor c = m_tagColors.value(oldName, QColor(100, 180, 160));
    m_tagColors.remove(oldName);
    m_tagColors.insert(newName, c);

    for (auto it = m_fileMeta.begin(); it != m_fileMeta.end(); ++it) {
        const int i = it->tags.indexOf(oldName);
        if (i < 0) continue;
        if (it->tags.contains(newName)) it->tags.removeAt(i);   // kein Duplikat
        else                            it->tags[i] = newName;
    }
}

void JsonStorage::deleteTag(const QString& tag) {
    m_tagColors.remove(tag);
    for (auto it = m_fileMeta.begin(); it != m_fileMeta.end(); ++it)
        it->tags.removeAll(tag);
}

void JsonStorage::applyToItems(QVector<MediaItem>& items) const {
    // Ordner OHNE Sidecar sind der Normalfall - dann gibt es nichts zu übertragen, und `fileName()` wird gar nicht
    // erst gerufen: bei einem Ordner mit 300 Dateien 300 Allokationen weniger.
    if (m_fileMeta.isEmpty()) return;

    for (auto& item : items) {
        auto it = m_fileMeta.constFind(item.fileName());
        if (it == m_fileMeta.constEnd()) continue;
        const FileMeta& meta = *it;
        item.tags = meta.tags;
    }
}

void JsonStorage::renameFile(const QString& oldName, const QString& newName) {
    if (m_fileMeta.contains(oldName))
        m_fileMeta[newName] = m_fileMeta.take(oldName);
}

void JsonStorage::removeFile(const QString& fileName) {
    m_fileMeta.remove(fileName);
}
