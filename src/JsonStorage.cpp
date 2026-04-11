#include "JsonStorage.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QSet>
#include <functional>

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

    // Always write tags array (even if empty) so membership is never silently lost
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

    // ── Tag registry: { "TagName": { "color": "#rrggbb", "files": [...] } } ──
    QJsonObject tags = root["tags"].toObject();
    for (auto it = tags.begin(); it != tags.end(); ++it) {
        const QString& tagName = it.key();
        QJsonObject tagObj = it.value().toObject();

        // Color
        m_tagColors[tagName] = QColor(tagObj["color"].toString("#64b4a0"));

        // Files that have this tag
        QJsonArray files = tagObj["files"].toArray();
        for (const auto& fv : files) {
            QString fileName = fv.toString();
            if (!fileName.isEmpty())
                m_fileMeta[fileName].tags.append(tagName);
        }
    }

    // ── Backward-compat: old "files" format { file: { tags: [...] } } ────────
    if (root.contains("files")) {
        QJsonObject oldFiles = root["files"].toObject();
        for (auto it = oldFiles.begin(); it != oldFiles.end(); ++it) {
            QJsonObject o = it.value().toObject();
            QJsonArray tagsArr = o["tags"].toArray();
            for (const auto& tv : tagsArr) {
                QString tag = tv.toString();
                if (!m_fileMeta[it.key()].tags.contains(tag))
                    m_fileMeta[it.key()].tags.append(tag);
                ensureTagRegistered(tag);
            }
        }
    }

    // ── tagColors fallback (old format) ──────────────────────────────────────
    if (root.contains("tagColors")) {
        QJsonObject colors = root["tagColors"].toObject();
        for (auto it = colors.begin(); it != colors.end(); ++it)
            if (!m_tagColors.contains(it.key()))
                m_tagColors[it.key()] = QColor(it.value().toString());
    }

    // ── Custom dates ──────────────────────────────────────────────────────────
    if (root.contains("dates")) {
        QJsonObject dates = root["dates"].toObject();
        for (auto it = dates.begin(); it != dates.end(); ++it) {
            QDateTime dt = QDateTime::fromString(it.value().toString(), Qt::ISODate);
            if (dt.isValid()) {
                m_fileMeta[it.key()].customDate    = dt;
                m_fileMeta[it.key()].hasCustomDate = true;
            }
        }
    }

    // ── Categories ────────────────────────────────────────────────────────────
    QJsonArray cats = root["categories"].toArray();
    for (const auto& c : cats) m_categories.append(categoryFromJson(c.toObject()));
}

void JsonStorage::saveFolder(const QString& folderPath) {
    QJsonObject root;

    // ── Build tag → files mapping ─────────────────────────────────────────────
    // Invert the fileMeta structure: for each tag, collect all files that have it
    QHash<QString, QStringList> tagToFiles;
    for (auto it = m_fileMeta.cbegin(); it != m_fileMeta.cend(); ++it) {
        for (const QString& tag : it.value().tags)
            tagToFiles[tag].append(it.key());
    }

    // Collect all tags that appear anywhere (files or categories)
    QSet<QString> usedTags;
    for (const QString& tag : tagToFiles.keys()) usedTags.insert(tag);
    std::function<void(const QList<TagCategory>&)> collectCatTags = [&](const QList<TagCategory>& cats) {
        for (const TagCategory& cat : cats) {
            for (const QString& t : cat.tags) usedTags.insert(t);
            collectCatTags(cat.children);
        }
    };
    collectCatTags(m_categories);

    // ── Tags section: { "TagName": { "color": "#...", "files": [...] } } ─────
    QJsonObject tagsObj;
    for (const QString& tag : usedTags) {
        QJsonObject tagObj;
        tagObj["color"] = m_tagColors.value(tag, QColor(100, 180, 160)).name();
        if (tagToFiles.contains(tag)) {
            QStringList files = tagToFiles[tag];
            files.sort();  // deterministic output
            QJsonArray filesArr;
            for (const QString& f : files) filesArr.append(f);
            tagObj["files"] = filesArr;
        }
        tagsObj[tag] = tagObj;
    }
    root["tags"] = tagsObj;

    // ── Custom dates: { "filename": "ISO8601" } ───────────────────────────────
    QJsonObject datesObj;
    for (auto it = m_fileMeta.cbegin(); it != m_fileMeta.cend(); ++it) {
        if (it.value().hasCustomDate)
            datesObj[it.key()] = it.value().customDate.toString(Qt::ISODate);
    }
    if (!datesObj.isEmpty())
        root["dates"] = datesObj;

    // ── Categories ────────────────────────────────────────────────────────────
    QJsonArray cats;
    for (const auto& cat : m_categories) cats.append(categoryToJson(cat));
    root["categories"] = cats;

    QString path = m_jsonPath.isEmpty()
                       ? folderPath + "/" + QFileInfo(folderPath).fileName() + ".json"
                       : m_jsonPath;

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