#pragma once
// Undo/Redo-Kommandos: jedes hält nur das Delta EINER Box, keine Snapshots - der Stack bleibt auch
// nach langen Sitzungen bei Kilobytes. push() führt redo() sofort aus; bei Session-Kommandos trägt
// das Modell den Wert schon, das erneute Anwenden ist ein idempotentes No-Op.

#include <QUndoCommand>
#include <QVariant>
#include <QRectF>
#include <QPointF>
#include <QVector>
#include "pdf/edit/PdfEditTypes.h"

class PdfEditModel;
class PdfEditController;

// Seiten-Plan ändern - EIN Kommando je Op (Delta alt -> neu). Der Plan ist eine Liste kleiner Structs (vier ints
// je Seite), auch bei tausend Seiten wenige Kilobyte.
class PdfEditPagePlanCommand : public QUndoCommand {
public:
    PdfEditPagePlanCommand(PdfEditController* ctl,
                           const QVector<PdfPlanPage>& oldPlan,
                           const QVector<PdfPlanPage>& newPlan);
    void redo() override;
    void undo() override;
private:
    PdfEditController*   m_ctl;
    QVector<PdfPlanPage> m_old;
    QVector<PdfPlanPage> m_new;
};

// Delta = genau EINE PdfTextOp; `undo()` entfernt die zuletzt angewendete. Das ist immer die richtige, weil der
// QUndoStack streng LIFO arbeitet - die Ops stehen in der Reihenfolge ihrer Kommandos.
class PdfEditTextOpCommand : public QUndoCommand {
public:
    PdfEditTextOpCommand(PdfEditController* ctl, const PdfTextOp& op);
    void redo() override;
    void undo() override;
private:
    PdfEditController* m_ctl;
    PdfTextOp          m_op;
};

//  Textbox hinzufügen (undo entfernt sie wieder - Zeile bleibt stabil).
class PdfEditAddCommand : public QUndoCommand {
public:
    PdfEditAddCommand(PdfEditModel* model, const PdfEditBox& box, int row);
    void redo() override;
    void undo() override;
private:
    PdfEditModel* m_model;
    PdfEditBox    m_box;
    int           m_row;
};

//  Textbox entfernen (undo fügt sie an der alten Zeile wieder ein).
class PdfEditRemoveCommand : public QUndoCommand {
public:
    PdfEditRemoveCommand(PdfEditModel* model, const PdfEditBox& box, int row);
    void redo() override;
    void undo() override;
private:
    PdfEditModel* m_model;
    PdfEditBox    m_box;
    int           m_row;
};

// EIN Kommando je Zieh-Session. Trägt neben dem Rechteck auch die SEITE (seitenübergreifendes Verschieben) und
// die PUNKTE (Freihand und Pfeil werden mit-transformiert); für punktlose Annotationen sind die Vektoren leer.
class PdfEditGeometryCommand : public QUndoCommand {
public:
    PdfEditGeometryCommand(PdfEditModel* model, int id,
                           int oldPage, const QRectF& oldRect,
                           const QVector<QPointF>& oldPts,
                           int newPage, const QRectF& newRect,
                           const QVector<QPointF>& newPts);
    void redo() override;
    void undo() override;
private:
    PdfEditModel*    m_model;
    int              m_id;
    int              m_oldPage;
    QRectF           m_old;
    QVector<QPointF> m_oldPts;
    int              m_newPage;
    QRectF           m_new;
    QVector<QPointF> m_newPts;
};

//  Textänderung - EIN Kommando je Bearbeitungs-Session (Delta alt->neu).
class PdfEditTextCommand : public QUndoCommand {
public:
    PdfEditTextCommand(PdfEditModel* model, int id,
                       const QString& oldText, const QString& newText);
    void redo() override;
    void undo() override;
private:
    PdfEditModel* m_model;
    int           m_id;
    QString       m_old;
    QString       m_new;
};

//  Reflow-Verkettung setzen/lösen (chainNext einer Box) - undo-fähig.
class PdfEditChainCommand : public QUndoCommand {
public:
    PdfEditChainCommand(PdfEditModel* model, int id, int oldNext, int newNext);
    void redo() override;
    void undo() override;
private:
    PdfEditModel* m_model;
    int           m_id;
    int           m_old;
    int           m_new;
};

// Aufeinanderfolgende Änderungen DESSELBEN Feldes derselben Box verschmelzen - eine SpinBox-Serie ergibt einen
// Undo-Schritt; hebt sie sich exakt auf, verwirft `setObsolete()` das Kommando ganz.
class PdfEditFieldCommand : public QUndoCommand {
public:
    static constexpr int kCommandId = 4711;

    PdfEditFieldCommand(PdfEditModel* model, int id, PdfEditField field,
                        const QVariant& oldValue, const QVariant& newValue);
    void redo() override;
    void undo() override;
    int  id() const override { return kCommandId; }
    bool mergeWith(const QUndoCommand* other) override;
private:
    PdfEditModel* m_model;
    int           m_id;
    PdfEditField  m_field;
    QVariant      m_old;
    QVariant      m_new;
};
