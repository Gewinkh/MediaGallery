#pragma once
#include <QString>
#include <QColor>
#include <QStringList>
#include <QList>
#include <QUuid>

// A recursive category node. Root categories have no parent id.
struct TagCategory {
    QString id;          // UUID, stable across renames
    QString name;
    bool uniformColor = false;
    bool inheritColorToChildren = false;  // propagate color to all subcategories
    QColor color { 100, 180, 160 };
    QStringList tags;                  // tags directly in this category
    QStringList files;                 // file names (media) directly in this category
    QList<TagCategory> children;       // subcategories

    static TagCategory create(const QString& name) {
        TagCategory c;
        c.id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
        c.name = name;
        return c;
    }

    // Recursive helpers
    bool isEmpty() const { return tags.isEmpty() && children.isEmpty(); }
    int  tagCount() const {
        int n = tags.size();
        for (const auto& ch : children) n += ch.tagCount();
        return n;
    }
};
