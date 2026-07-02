#include "JsonStorage.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSet>

JsonStorage::JsonStorage(QObject* parent) : QObject(parent) {}

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
// Nur nicht-leere Felder werden geschrieben → minimale JSON auch bei großen
// Sammlungen. Ein Versions-Marker ("v") wird seit 2026-07 weder geschrieben
// noch ausgewertet — das Legacy-Format (tag-zentrisch) und die zugehörige
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

        if (o.contains("d")) {
            QDateTime dt = QDateTime::fromString(o["d"].toString(), Qt::ISODate);
            if (dt.isValid()) {
                meta.customDate    = dt;
                meta.hasCustomDate = true;
            }
        }
    }
}

// ── Load / Save ───────────────────────────────────────────────────────────────
void JsonStorage::loadFolder(const QString& folderPath) {
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
    if (!doc.isObject()) return;

    QJsonObject root = doc.object();

    loadNewFormat(root);

    // Categories are format-independent
    QJsonArray cats = root["categories"].toArray();
    for (const auto& c : cats) m_categories.append(categoryFromJson(c.toObject()));
}

void JsonStorage::saveFolder(const QString& folderPath) {
    QJsonObject root;

    // ── Compact file-centric section ──────────────────────────────────────────
    // Only writes entries that have actual data (tags or custom date).
    // Keys are short ("t", "d") to minimise file size across large collections.
    QJsonObject filesObj;
    for (auto it = m_fileMeta.cbegin(); it != m_fileMeta.cend(); ++it) {
        const FileMeta& meta = it.value();
        if (meta.tags.isEmpty() && !meta.hasCustomDate)
            continue;  // skip files with no metadata at all → saves space

        QJsonObject o;
        if (!meta.tags.isEmpty()) {
            QJsonArray tagsArr;
            for (const QString& t : meta.tags) tagsArr.append(t);
            o["t"] = tagsArr;
        }
        if (meta.hasCustomDate)
            o["d"] = meta.customDate.toString(Qt::ISODate);

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
    // noch Kategorien) → KEINE Leerdatei ("{}") anlegen. Das verhindert,
    // dass allein durch das Öffnen/Wechseln eines Ordners eine JSON entsteht.
    // Existiert bereits eine (nun leere gewordene) Datei — z. B. weil der
    // letzte Tag/die letzte Kategorie gerade gelöscht wurde — wird sie entfernt,
    // statt einen leeren Stub zu hinterlassen.
    const bool hasContent = root.contains("files") || root.contains("tagColors")
                             || root.contains("categories");
    if (!hasContent) {
        if (QFile::exists(path))
            QFile::remove(path);
        return;
    }

    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void JsonStorage::saveCurrentFolder() {
    if (!m_folderPath.isEmpty()) saveFolder(m_folderPath);
}

// ── File metadata ─────────────────────────────────────────────────────────────
QStringList JsonStorage::getTags(const QString& f) const {
    return m_fileMeta.value(f).tags;
}
void JsonStorage::setTags(const QString& f, const QStringList& tags) {
    m_fileMeta[f].tags = tags;
    for (const auto& t : tags) ensureTagRegistered(t);
}
bool JsonStorage::hasCustomDate(const QString& f) const {
    return m_fileMeta.value(f).hasCustomDate;
}
QDateTime JsonStorage::getCustomDate(const QString& f) const {
    return m_fileMeta.value(f).customDate;
}
void JsonStorage::setCustomDate(const QString& f, const QDateTime& dt) {
    m_fileMeta[f].customDate = dt;
    m_fileMeta[f].hasCustomDate = true;
}
void JsonStorage::clearCustomDate(const QString& f) {
    m_fileMeta[f].hasCustomDate = false;
    m_fileMeta[f].customDate = QDateTime();
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
void JsonStorage::deleteTag(const QString& tag) {
    m_tagColors.remove(tag);
    for (auto it = m_fileMeta.begin(); it != m_fileMeta.end(); ++it)
        it->tags.removeAll(tag);
}

// ── Apply to items ────────────────────────────────────────────────────────────
void JsonStorage::applyToItems(QVector<MediaItem>& items) const {
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
