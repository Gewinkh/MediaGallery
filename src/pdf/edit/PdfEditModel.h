#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfEditModel.h - Listenmodell der Overlay-Textboxen EINES Dokuments.
// ══════════════════════════════════════════════════════════════════════════════
//
//  ROLLE IM SYSTEM
//  ───────────────
//  Einzige Wahrheitsquelle des Overlays: QML (Repeater je Seite) bindet die
//  Rollen; Undo-Kommandos und der Controller mutieren AUSSCHLIESSLICH über die
//  apply*/insert*/remove*-Methoden (gezielte dataChanged-Rollen -> kein
//  Delegate-Neuaufbau beim Tippen/Ziehen, nur Property-Updates).
//
//  RAM: reine Werte-Structs (QVector<PdfEditBox>), keine Bitmaps, keine
//  Dokument-Referenzen. Ein Overlay mit hunderten Boxen bleibt im KB-Bereich.
// ══════════════════════════════════════════════════════════════════════════════

#include <QAbstractListModel>
#include <QVector>
#include "pdf/edit/PdfEditTypes.h"

class PdfEditModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        BoxIdRole = Qt::UserRole + 1,
        PageRole,
        KindRole,                       // PdfAnnKind (0 Text … 4 Ellipse)
        XRole, YRole, WRole, HRole,     // PDF-Punkte, Ursprung oben-links
        PointsRole,                     // Freihand/Pfeil: QVariantList<QPointF>
        StrokeRole, LineWidthRole, FillRole,
        TextRole,
        FontFamilyRole, FontSizeRole,
        BoldRole, ItalicRole, UnderlineRole,
        ColorRole, HighlightRole,
        AlignmentRole,
        VAlignRole,
        AnchoredRole,
        MarkupStyleRole,                // Markierung: 0 Fläche, 1 unter, 2 durch
        ImagePathRole,                  // Stempel: Pfad der Bilddatei
        TrackRole                       // Nachverfolgung: 0 keine, 1 neu, 2 gelöscht
    };

    explicit PdfEditModel(QObject* parent = nullptr);

    int      rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // ── Lesender Zugriff (Controller / Kommandos / Export) ────────────────────
    int  indexOfId(int id) const;
    const PdfEditBox* boxById(int id) const;
    QVector<PdfEditBox> boxes() const { return m_boxes; }   // Kopie (Export/Sidecar)
    int  count() const { return m_boxes.size(); }

    // ── Mutationen - NUR PdfEditController + Undo-Kommandos ───────────────────
    void resetBoxes(const QVector<PdfEditBox>& boxes);       // Sidecar-Load
    void insertBoxAt(int row, const PdfEditBox& box);
    bool removeById(int id, PdfEditBox* removed = nullptr, int* removedRow = nullptr);
    bool applyGeometry(int id, const QRectF& r);
    //  applyPlacement: Rechteck UND Seite in einem Schritt (seitenübergreifendes
    //  Verschieben) - feuert nur die tatsächlich geänderten Rollen.
    bool applyPlacement(int id, int page, const QRectF& r);
    //  applyPlacementPoints: Seite + Rechteck + Punkte in einem Schritt
    //  (Strich verschieben/skalieren - auch seitenübergreifend); feuert nur
    //  die tatsächlich geänderten Rollen.
    bool applyPlacementPoints(int id, int page, const QRectF& r,
                              const QVector<QPointF>& pts);
    //  applyPoints: Freihand/Pfeil live zeichnen (Bounds werden neu berechnet).
    bool applyPoints(int id, const QVector<QPointF>& pts);
    bool applyText(int id, const QString& t);
    bool applyField(int id, PdfEditField f, const QVariant& v);
    //  Reflow-Verkettung: chainNext der Box setzen (kein QML-Rollen-Binding ->
    //  keine dataChanged nötig; boxInfo liest den Wert rev-getrieben).
    bool setChainNext(int id, int next);
    //  Ursprungshöhe vor dem Ketten-Ende-Wachstum (s. PdfEditBox::growBaseH).
    //  Reine Buchführung - kein dataChanged nötig, die Geometrie selbst ändert
    //  sich über applyGeometry.
    void setGrowBaseH(int id, qreal h);
    void clearAll();

signals:
    void countChanged();

private:
    QVector<PdfEditBox> m_boxes;
};
