#pragma once
// Listenmodell der Overlay-Annotationen EINES Bildes und einzige Wahrheitsquelle des Overlays: mutiert wird nur
// über die apply*/insert*/remove*-Methoden mit gezielten dataChanged-Rollen - kein Delegate-Neuaufbau beim Tippen.
// Reine Werte-Structs, keine Bitmaps; auch hunderte Striche bleiben im KB-Bereich.

#include <QAbstractListModel>
#include <QVector>
#include "image/edit/ImageEditTypes.h"

class ImageEditModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        AnnIdRole = Qt::UserRole + 1,
        KindRole,
        XRole, YRole, WRole, HRole,      // Bild-Pixel, Ursprung oben-links
        PointsRole,                      // Freihand/Pfeil: QVariantList<QPointF>
        StrokeRole, LineWidthRole, FillRole,
        TextRole,
        FontFamilyRole, FontSizeRole,
        BoldRole, ItalicRole, UnderlineRole,
        ColorRole, HighlightRole,
        AlignmentRole,
        TrackRole,                      // Nachverfolgung: 0 keine, 1 neu, 2 gelöscht
        VAlignRole
    };

    explicit ImageEditModel(QObject* parent = nullptr);

    int      rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Lesender Zugriff (Controller / Kommandos / Export)
    int  indexOfId(int id) const;
    const ImageAnnotation* annById(int id) const;
    QVector<ImageAnnotation> annotations() const { return m_anns; }   // Kopie (Export/Sidecar)
    int  count() const { return m_anns.size(); }

    // Mutationen - NUR ImageEditController + Undo-Kommandos
    void resetAnns(const QVector<ImageAnnotation>& anns);            // Sidecar-Load
    void insertAnnAt(int row, const ImageAnnotation& ann);
    bool removeById(int id, ImageAnnotation* removed = nullptr, int* removedRow = nullptr);
    bool applyGeometry(int id, const QRectF& r);
    //  applyGeometryPoints: Rechteck UND Punkte in einem Schritt (Strich
    //  verschieben/skalieren) - feuert nur die tatsächlich geänderten Rollen.
    bool applyGeometryPoints(int id, const QRectF& r, const QVector<QPointF>& pts);
    bool applyPoints(int id, const QVector<QPointF>& pts);           // Freihand live zeichnen
    bool applyText(int id, const QString& t);
    bool applyField(int id, ImageAnnField f, const QVariant& v);
    void clearAll();

signals:
    void countChanged();

private:
    QVector<ImageAnnotation> m_anns;
};
