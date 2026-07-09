#include "ImageEditModel.h"

// ─────────────────────────────────────────────────────────────────────────────
ImageEditModel::ImageEditModel(QObject* parent) : QAbstractListModel(parent) {}

int ImageEditModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_anns.size();
}

QVariant ImageEditModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_anns.size())
        return {};
    const ImageAnnotation& a = m_anns.at(index.row());
    switch (role) {
    case AnnIdRole:      return a.id;
    case KindRole:       return static_cast<int>(a.kind);
    case XRole:          return a.rect.x();
    case YRole:          return a.rect.y();
    case WRole:          return a.rect.width();
    case HRole:          return a.rect.height();
    case PointsRole: {
        // Für den QML-Canvas: Liste von QPointF (im Delegate über .x/.y lesbar).
        QVariantList list;
        list.reserve(a.points.size());
        for (const QPointF& p : a.points)
            list.append(QVariant::fromValue(p));
        return list;
    }
    case StrokeRole:     return a.stroke;
    case LineWidthRole:  return a.lineWidth;
    case FillRole:       return a.fill;
    case TextRole:       return a.text;
    case FontFamilyRole: return a.fontFamily;
    case FontSizeRole:   return a.fontSizePx;
    case BoldRole:       return a.bold;
    case ItalicRole:     return a.italic;
    case UnderlineRole:  return a.underline;
    case ColorRole:      return a.color;
    case HighlightRole:  return a.highlight;
    case AlignmentRole:  return a.alignment;
    case VAlignRole:     return a.vAlign;
    default:             return {};
    }
}

QHash<int, QByteArray> ImageEditModel::roleNames() const {
    // „annText" statt „text": vermeidet jede Kollision mit gleichnamigen
    // QML-Item-Properties in den Delegates (wie „boxText" im PDF-Editor).
    static const QHash<int, QByteArray> names = {
        { AnnIdRole,      "annId"          },
        { KindRole,       "annKind"        },
        { XRole,          "xPx"            },
        { YRole,          "yPx"            },
        { WRole,          "wPx"            },
        { HRole,          "hPx"            },
        { PointsRole,     "annPoints"      },
        { StrokeRole,     "strokeColor"    },
        { LineWidthRole,  "lineWidth"      },
        { FillRole,       "fillColor"      },
        { TextRole,       "annText"        },
        { FontFamilyRole, "fontFamily"     },
        { FontSizeRole,   "fontSizePx"     },
        { BoldRole,       "bold"           },
        { ItalicRole,     "italic"         },
        { UnderlineRole,  "underline"      },
        { ColorRole,      "textColor"      },
        { HighlightRole,  "highlightColor" },
        { AlignmentRole,  "alignment"      },
        { VAlignRole,     "vAlign"         },
    };
    return names;
}

// ─────────────────────────────────────────────────────────────────────────────
int ImageEditModel::indexOfId(int id) const {
    for (int i = 0; i < m_anns.size(); ++i)
        if (m_anns.at(i).id == id)
            return i;
    return -1;
}

const ImageAnnotation* ImageEditModel::annById(int id) const {
    const int row = indexOfId(id);
    return row >= 0 ? &m_anns.at(row) : nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
void ImageEditModel::resetAnns(const QVector<ImageAnnotation>& anns) {
    beginResetModel();
    m_anns = anns;
    endResetModel();
    emit countChanged();
}

void ImageEditModel::insertAnnAt(int row, const ImageAnnotation& ann) {
    const int r = qBound(0, row, m_anns.size());
    beginInsertRows(QModelIndex(), r, r);
    m_anns.insert(r, ann);
    endInsertRows();
    emit countChanged();
}

bool ImageEditModel::removeById(int id, ImageAnnotation* removed, int* removedRow) {
    const int row = indexOfId(id);
    if (row < 0)
        return false;
    if (removed)    *removed    = m_anns.at(row);
    if (removedRow) *removedRow = row;
    beginRemoveRows(QModelIndex(), row, row);
    m_anns.removeAt(row);
    endRemoveRows();
    emit countChanged();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool ImageEditModel::applyGeometry(int id, const QRectF& r) {
    const int row = indexOfId(id);
    if (row < 0)
        return false;
    ImageAnnotation& a = m_anns[row];
    if (a.rect == r)
        return false;
    a.rect = r;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { XRole, YRole, WRole, HRole });
    return true;
}

bool ImageEditModel::applyGeometryPoints(int id, const QRectF& r, const QVector<QPointF>& pts) {
    const int row = indexOfId(id);
    if (row < 0)
        return false;
    ImageAnnotation& a = m_anns[row];
    QList<int> roles;
    if (a.rect != r) { a.rect = r; roles << XRole << YRole << WRole << HRole; }
    if (a.points != pts) { a.points = pts; roles << PointsRole; }
    if (roles.isEmpty())
        return false;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, roles);
    return true;
}

bool ImageEditModel::applyPoints(int id, const QVector<QPointF>& pts) {
    const int row = indexOfId(id);
    if (row < 0)
        return false;
    ImageAnnotation& a = m_anns[row];
    a.points = pts;
    a.recomputeBounds();
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { PointsRole, XRole, YRole, WRole, HRole });
    return true;
}

bool ImageEditModel::applyText(int id, const QString& t) {
    const int row = indexOfId(id);
    if (row < 0)
        return false;
    ImageAnnotation& a = m_anns[row];
    if (a.text == t)
        return false;
    a.text = t;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { TextRole });
    return true;
}

bool ImageEditModel::applyField(int id, ImageAnnField f, const QVariant& v) {
    const int row = indexOfId(id);
    if (row < 0)
        return false;
    ImageAnnotation& a = m_anns[row];
    QList<int> roles;
    switch (f) {
    case ImageAnnField::Stroke:
        if (a.stroke == v.value<QColor>()) return false;
        a.stroke = v.value<QColor>();              roles = { StrokeRole };     break;
    case ImageAnnField::LineWidth:
        if (qFuzzyCompare(a.lineWidth, v.toReal())) return false;
        a.lineWidth = v.toReal();
        if (a.isStroke()) { a.recomputeBounds();   roles = { LineWidthRole, XRole, YRole, WRole, HRole }; }
        else                                       roles = { LineWidthRole };
        break;
    case ImageAnnField::Fill:
        if (a.fill == v.value<QColor>()) return false;
        a.fill = v.value<QColor>();                roles = { FillRole };       break;
    case ImageAnnField::FontFamily:
        if (a.fontFamily == v.toString()) return false;
        a.fontFamily = v.toString();               roles = { FontFamilyRole }; break;
    case ImageAnnField::FontSize:
        if (qFuzzyCompare(a.fontSizePx, v.toReal())) return false;
        a.fontSizePx = v.toReal();                 roles = { FontSizeRole };   break;
    case ImageAnnField::Bold:
        if (a.bold == v.toBool()) return false;
        a.bold = v.toBool();                       roles = { BoldRole };       break;
    case ImageAnnField::Italic:
        if (a.italic == v.toBool()) return false;
        a.italic = v.toBool();                     roles = { ItalicRole };     break;
    case ImageAnnField::Underline:
        if (a.underline == v.toBool()) return false;
        a.underline = v.toBool();                  roles = { UnderlineRole };  break;
    case ImageAnnField::Color:
        if (a.color == v.value<QColor>()) return false;
        a.color = v.value<QColor>();               roles = { ColorRole };      break;
    case ImageAnnField::Highlight:
        if (a.highlight == v.value<QColor>()) return false;
        a.highlight = v.value<QColor>();           roles = { HighlightRole };  break;
    case ImageAnnField::Alignment:
        if (a.alignment == v.toInt()) return false;
        a.alignment = v.toInt();                   roles = { AlignmentRole };  break;
    case ImageAnnField::VAlign:
        if (a.vAlign == v.toInt()) return false;
        a.vAlign = v.toInt();                      roles = { VAlignRole };     break;
    case ImageAnnField::Text:      return applyText(id, v.toString());
    case ImageAnnField::Geometry:  return applyGeometry(id, v.toRectF());
    case ImageAnnField::Points:    return false;   // eigener Weg (applyPoints)
    }
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, roles);
    return true;
}

void ImageEditModel::clearAll() {
    if (m_anns.isEmpty())
        return;
    beginResetModel();
    m_anns.clear();
    endResetModel();
    emit countChanged();
}
