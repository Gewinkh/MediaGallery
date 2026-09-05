#include "pdf/edit/PdfEditModel.h"

PdfEditModel::PdfEditModel(QObject* parent) : QAbstractListModel(parent) {}

int PdfEditModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_boxes.size();
}

QVariant PdfEditModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_boxes.size())
        return {};
    const PdfEditBox& b = m_boxes.at(index.row());
    switch (role) {
    case BoxIdRole:      return b.id;
    case PageRole:       return b.page;
    case KindRole:       return static_cast<int>(b.kind);
    case PointsRole: {
        // Für den QML-Canvas: Liste von QPointF (im Delegate über .x/.y lesbar).
        QVariantList list;
        list.reserve(b.points.size());
        for (const QPointF& p : b.points)
            list.append(QVariant::fromValue(p));
        return list;
    }
    case StrokeRole:     return b.stroke;
    case LineWidthRole:  return b.lineWidth;
    case FillRole:       return b.fill;
    case XRole:          return b.rect.x();
    case YRole:          return b.rect.y();
    case WRole:          return b.rect.width();
    case HRole:          return b.rect.height();
    case TextRole:       return b.text;
    case FontFamilyRole: return b.fontFamily;
    case FontSizeRole:   return b.fontSizePt;
    case BoldRole:       return b.bold;
    case ItalicRole:     return b.italic;
    case UnderlineRole:  return b.underline;
    case ColorRole:      return b.color;
    case HighlightRole:  return b.highlight;
    case AlignmentRole:  return b.alignment;
    case VAlignRole:     return b.vAlign;
    case AnchoredRole:   return b.anchored;
    case MarkupStyleRole: return b.markupStyle;
    case ImagePathRole:  return b.imagePath;
    case TrackRole:      return static_cast<int>(b.track);
    default:             return {};
    }
}

QHash<int, QByteArray> PdfEditModel::roleNames() const {
    // „boxText" statt „text": vermeidet jede Kollision mit gleichnamigen
    // QML-Item-Properties in den Delegates.
    static const QHash<int, QByteArray> names = {
        { BoxIdRole,      "boxId"          },
        { PageRole,       "page"           },
        { KindRole,       "boxKind"        },
        { PointsRole,     "boxPoints"      },
        { StrokeRole,     "strokeColor"    },
        { LineWidthRole,  "lineWidth"      },
        { FillRole,       "fillColor"      },
        { XRole,          "xPt"            },
        { YRole,          "yPt"            },
        { WRole,          "wPt"            },
        { HRole,          "hPt"            },
        { TextRole,       "boxText"        },
        { FontFamilyRole, "fontFamily"     },
        { FontSizeRole,   "fontSizePt"     },
        { BoldRole,       "bold"           },
        { ItalicRole,     "italic"         },
        { UnderlineRole,  "underline"      },
        { ColorRole,      "textColor"      },
        { HighlightRole,  "highlightColor" },
        { AlignmentRole,  "alignment"      },
        { VAlignRole,     "vAlign"         },
        { AnchoredRole,   "anchored"       },
        { MarkupStyleRole, "markupStyle"   },
        { ImagePathRole,  "imagePath"     },
        { TrackRole,      "trackState"    },
    };
    return names;
}

int PdfEditModel::indexOfId(int id) const {
    for (int i = 0; i < m_boxes.size(); ++i)
        if (m_boxes.at(i).id == id)
            return i;
    return -1;
}

const PdfEditBox* PdfEditModel::boxById(int id) const {
    const int row = indexOfId(id);
    return row >= 0 ? &m_boxes.at(row) : nullptr;
}

void PdfEditModel::resetBoxes(const QVector<PdfEditBox>& boxes) {
    beginResetModel();
    m_boxes = boxes;
    endResetModel();
    emit countChanged();
}

void PdfEditModel::insertBoxAt(int row, const PdfEditBox& box) {
    const int r = qBound(0, row, m_boxes.size());
    beginInsertRows(QModelIndex(), r, r);
    m_boxes.insert(r, box);
    endInsertRows();
    emit countChanged();
}

bool PdfEditModel::removeById(int id, PdfEditBox* removed, int* removedRow) {
    const int row = indexOfId(id);
    if (row < 0)
        return false;
    if (removed)    *removed    = m_boxes.at(row);
    if (removedRow) *removedRow = row;
    beginRemoveRows(QModelIndex(), row, row);
    m_boxes.removeAt(row);
    endRemoveRows();
    emit countChanged();
    return true;
}

bool PdfEditModel::applyGeometry(int id, const QRectF& r) {
    const int row = indexOfId(id);
    if (row < 0)
        return false;
    PdfEditBox& b = m_boxes[row];
    if (b.rect == r)
        return false;
    b.rect = r;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { XRole, YRole, WRole, HRole });
    return true;
}

bool PdfEditModel::applyPlacement(int id, int page, const QRectF& r) {
    const int row = indexOfId(id);
    if (row < 0)
        return false;
    PdfEditBox& b = m_boxes[row];
    QList<int> roles;
    if (b.page != page && page >= 0) { b.page = page; roles << PageRole; }
    if (b.rect != r) { b.rect = r; roles << XRole << YRole << WRole << HRole; }
    if (roles.isEmpty())
        return false;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, roles);
    return true;
}

bool PdfEditModel::applyPlacementPoints(int id, int page, const QRectF& r,
                                        const QVector<QPointF>& pts) {
    const int row = indexOfId(id);
    if (row < 0)
        return false;
    PdfEditBox& b = m_boxes[row];
    QList<int> roles;
    if (b.page != page && page >= 0) { b.page = page; roles << PageRole; }
    if (b.rect != r) { b.rect = r; roles << XRole << YRole << WRole << HRole; }
    if (b.points != pts) { b.points = pts; roles << PointsRole; }
    if (roles.isEmpty())
        return false;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, roles);
    return true;
}

bool PdfEditModel::applyPoints(int id, const QVector<QPointF>& pts) {
    const int row = indexOfId(id);
    if (row < 0)
        return false;
    PdfEditBox& b = m_boxes[row];
    b.points = pts;
    b.recomputeBounds();
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { PointsRole, XRole, YRole, WRole, HRole });
    return true;
}

bool PdfEditModel::applyText(int id, const QString& t) {
    const int row = indexOfId(id);
    if (row < 0)
        return false;
    PdfEditBox& b = m_boxes[row];
    if (b.text == t)
        return false;
    b.text = t;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { TextRole });
    return true;
}

bool PdfEditModel::setChainNext(int id, int next) {
    const int row = indexOfId(id);
    if (row < 0 || m_boxes[row].chainNext == next)
        return false;
    m_boxes[row].chainNext = next;
    return true;                                   // kein QML-Rollen-Binding
}
void PdfEditModel::setGrowBaseH(int id, qreal h) {
    const int row = indexOfId(id);
    if (row < 0)
        return;
    m_boxes[row].growBaseH = h;
}


bool PdfEditModel::applyField(int id, PdfEditField f, const QVariant& v) {
    const int row = indexOfId(id);
    if (row < 0)
        return false;
    PdfEditBox& b = m_boxes[row];
    QList<int> roles;
    switch (f) {
    case PdfEditField::Track: {
        const int t = v.toInt();
        const PdfTrackState ts = (t == 1 || t == 2) ? static_cast<PdfTrackState>(t)
                                                    : PdfTrackState::None;
        if (b.track == ts) return false;
        b.track = ts;                              roles = { TrackRole };      break;
    }
    case PdfEditField::Stroke:
        if (b.stroke == v.value<QColor>()) return false;
        b.stroke = v.value<QColor>();              roles = { StrokeRole };     break;
    case PdfEditField::LineWidth:
        if (qFuzzyCompare(b.lineWidth, v.toReal())) return false;
        b.lineWidth = v.toReal();
        // Striche tragen einen Linienbreiten-Rand in der Bounding-Box ->
        // neu berechnen und die Rechteck-Rollen mitfeuern.
        if (b.isStroke()) { b.recomputeBounds();   roles = { LineWidthRole, XRole, YRole, WRole, HRole }; }
        else                                       roles = { LineWidthRole };
        break;
    case PdfEditField::Fill:
        if (b.fill == v.value<QColor>()) return false;
        b.fill = v.value<QColor>();                roles = { FillRole };       break;
    case PdfEditField::FontFamily:
        if (b.fontFamily == v.toString()) return false;
        b.fontFamily = v.toString();               roles = { FontFamilyRole }; break;
    case PdfEditField::FontSize:
        if (qFuzzyCompare(b.fontSizePt, v.toReal())) return false;
        b.fontSizePt = v.toReal();                 roles = { FontSizeRole };   break;
    case PdfEditField::Bold:
        if (b.bold == v.toBool()) return false;
        b.bold = v.toBool();                       roles = { BoldRole };       break;
    case PdfEditField::Italic:
        if (b.italic == v.toBool()) return false;
        b.italic = v.toBool();                     roles = { ItalicRole };     break;
    case PdfEditField::Underline:
        if (b.underline == v.toBool()) return false;
        b.underline = v.toBool();                  roles = { UnderlineRole };  break;
    case PdfEditField::Color:
        if (b.color == v.value<QColor>()) return false;
        b.color = v.value<QColor>();               roles = { ColorRole };      break;
    case PdfEditField::Highlight:
        if (b.highlight == v.value<QColor>()) return false;
        b.highlight = v.value<QColor>();           roles = { HighlightRole };  break;
    case PdfEditField::Alignment:
        if (b.alignment == v.toInt()) return false;
        b.alignment = v.toInt();                   roles = { AlignmentRole };  break;
    case PdfEditField::VAlign:
        if (b.vAlign == v.toInt()) return false;
        b.vAlign = v.toInt();                      roles = { VAlignRole };     break;
    case PdfEditField::Text:      return applyText(id, v.toString());
    case PdfEditField::Geometry:  return applyGeometry(id, v.toRectF());
    case PdfEditField::Points:    return false;   // eigener Weg (applyPoints)
    }
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, roles);
    return true;
}

void PdfEditModel::clearAll() {
    if (m_boxes.isEmpty())
        return;
    beginResetModel();
    m_boxes.clear();
    endResetModel();
    emit countChanged();
}
