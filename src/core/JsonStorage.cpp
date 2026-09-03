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

// ── JSON helpers for categories ───────────────────────────────────────────────
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

// ── Dateizentrisches JSON-Format (kompakt, speichereffizient) ─────────────────
//   {
//     "files": { "img.jpg": { "t": ["tag1","tag2"], "d": "ISO8601" }, ... },
//     "tagColors": { "TagName": "#rrggbb", ... },
//     "categories": [...]
//   }
// Schlüssel sind abgekürzt: "t" = Tags, "d" = Datum.
// Nur nicht-leere Felder werden geschrieben -> minimale JSON auch bei großen
// Sammlungen. Ein Versions-Marker ("v") wird seit 2026-07 weder geschrieben
// noch ausgewertet - das Legacy-Format (tag-zentrisch) und die zugehörige
// Migration wurden entfernt; ältere Dateien mit "v"-Feld laden weiterhin,
// das Feld wird schlicht ignoriert und beim nächsten Speichern entfernt.
void JsonStorage::loadNewFormat(const QJsonObject& root) {
    // Tag colors
    QJsonObject tagColors = root["tagColors"].toObject();
    for (auto it = tagColors.begin(); it != tagColors.end(); ++it)
        m_tagColors[it.key()] = QColor(it.value().toString("#64b4a0"));

    // Per-file data
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

// ── Load / Save ───────────────────────────────────────────────────────────────
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

    // Categories are format-independent
    QJsonArray cats = root["categories"].toArray();
    for (const auto& c : cats) m_categories.append(categoryFromJson(c.toObject()));
}

// ── Stand der Datei merken / prüfen / fremde Änderungen übernehmen ───────────
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

//  Die Datei hat sich seit unserem Lesen geändert - also hat jemand anderes
//  geschrieben (die zweite Hälfte auf demselben Ordner, ein anderes Programm).
//  Übernommen wird, was wir NICHT kennen; wo beide etwas wissen, gewinnt das
//  HINZUFÜGEN: Tags werden vereinigt. Das kann im Grenzfall ein soeben
//  entferntes Tag zurückbringen - der umgekehrte Fehler wäre der Verlust einer
//  fremden Verschlagwortung, und Verlust wiegt schwerer.
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
    //  Ein expliziter Speicherbefehl erledigt zugleich, was gesammelt wurde -
    //  sonst schriebe der Timer gleich darauf ein zweites Mal dasselbe.
    if (folderPath == m_folderPath) {
        m_saveTimer.stop();
        m_savePending = false;
    }
    //  Erst fremde Änderungen übernehmen (s. oben), dann das Ganze schreiben.
    mergeForeignChanges(m_jsonPath.isEmpty()
                            ? folderPath + "/" + QFileInfo(folderPath).fileName() + ".json"
                            : m_jsonPath);

    QJsonObject root;

    // ── Compact file-centric section ──────────────────────────────────────────
    // Only writes entries that have actual data (tags, custom date, PDF text
    // colour). Keys are short ("t", "d", "c") to minimise file size across large
    // collections.
    QJsonObject filesObj;
    for (auto it = m_fileMeta.cbegin(); it != m_fileMeta.cend(); ++it) {
        const FileMeta& meta = it.value();
        //  Ohne Daten kein Eintrag - das spart bei großen Sammlungen viel.
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

    // ── Tag color registry ────────────────────────────────────────────────────
    // Collect tags used anywhere (files + categories) so the registry stays clean.
    QSet<QString> usedTags;
    for (auto it = m_fileMeta.cbegin(); it != m_fileMeta.cend(); ++it)
        for (const QString& t : it.value().tags) usedTags.insert(t);

    // Collect tags from category tree without std::function overhead
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

    // ── Categories ────────────────────────────────────────────────────────────
    QJsonArray cats;
    for (const auto& cat : m_categories) cats.append(categoryToJson(cat));
    if (!cats.isEmpty())
        root["categories"] = cats;

    QString path = m_jsonPath.isEmpty()
                       ? folderPath + "/" + QFileInfo(folderPath).fileName() + ".json"
                       : m_jsonPath;

    // Keine tatsächlichen Daten vorhanden (weder Datei-Metadaten noch Tags
    // noch Kategorien) -> KEINE Leerdatei ("{}") anlegen. Das verhindert,
    // dass allein durch das Öffnen/Wechseln eines Ordners eine JSON entsteht.
    // Existiert bereits eine (nun leere gewordene) Datei - z. B. weil der
    // letzte Tag/die letzte Kategorie gerade gelöscht wurde - wird sie entfernt,
    // statt einen leeren Stub zu hinterlassen.
    const bool hasContent = root.contains("files") || root.contains("tagColors")
                             || root.contains("categories");
    if (!hasContent) {
        if (QFile::exists(path))
            QFile::remove(path);
        noteDiskStamp(path);
        return;
    }

    // ATOMAR schreiben (QSaveFile: Temp-Datei + Rename). Diese Datei ist die
    // EINZIGE Quelle aller Tags, Kategorien und Custom-Daten eines Ordners und
    // wird bei jeder Mutation komplett neu geschrieben. Mit open(WriteOnly)
    // wurde sie dabei zuerst auf 0 Bytes gekuerzt - ein Absturz, ein voller
    // Datentraeger oder ein Stromausfall im Schreibfenster hinterliess eine
    // leere/halbe Datei und damit den TOTALVERLUST der Verschlagwortung.
    // Wie bei ViewerController::writeTextFile und den Editor-Exporten.
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

// ── Schnappschuss / Wiederherstellen (Rueckgaengig der Tag-Seitenleiste) ─────
//  BEWUSST NICHT das JSON der Platte. Gemessen an einem Ordner mit 5000
//  Dateien, 15.000 Zuordnungen und 300 Kategorien: den JSON-Baum aufzubauen
//  kostete 10,3 ms und ihn zu setzen weitere 3,0 ms - je Nutzergeste. Ein
//  `QDataStream` ueber dieselben drei Behaelter kommt ohne Zwischenbaum aus.
//
//  Zwei weitere Gruende sprechen dafuer: der Schnappschuss verlaesst den
//  Prozess NIE (er lebt nur im Rueckgaengig-Stapel), und er muss MEHR
//  enthalten als die Datei - die Platte fuehrt nur die tatsaechlich benutzten
//  Tagfarben, ein frisch angelegter Tag ohne Datei und ohne Kategorie stuende
//  also gar nicht darin und kaeme durch ein Rueckgaengig nicht zurueck.
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

    //  GEPACKT im Stapel liegen (Regel 9: RAM zuerst). Gemessen am selben
    //  Ordner: 578 KB roh -> 78 KB, dafuer 1,9 ms. Stufe 1 und nicht 9 -
    //  hoehere Stufen kosteten deutlich mehr Zeit fuer wenige Prozent.
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
    //  Sammeln setzt eine laufende Ereignisschleife voraus - der Null-Timer
    //  feuert sonst nie. Ohne sie (Testtreiber, Kommandozeilenwege, Abbau beim
    //  Beenden) wird SOFORT geschrieben; das ist der sichere Fall, nicht der
    //  Ausnahmefall.
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

// ── File metadata ─────────────────────────────────────────────────────────────
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


// ── Tag registry ──────────────────────────────────────────────────────────────
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

    //  Die Registrierung: Farbe mitnehmen, alten Namen loeschen.
    const QColor c = m_tagColors.value(oldName, QColor(100, 180, 160));
    m_tagColors.remove(oldName);
    m_tagColors.insert(newName, c);

    //  Die Datei-Zuordnungen: AN ORT UND STELLE umschreiben, nicht wegwerfen.
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

// ── Apply to items ────────────────────────────────────────────────────────────
void JsonStorage::applyToItems(QVector<MediaItem>& items) const {
    //  Ordner OHNE Sidecar sind der Normalfall - dann gibt es nichts zu
    //  übertragen, und `fileName()` (das je Aufruf eine Zeichenkette anlegt)
    //  wird gar nicht erst gerufen. Bei einem Ordner mit 300 Dateien sind das
    //  300 Allokationen weniger, beim Aufklappen eines Baumes entsprechend mehr.
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
