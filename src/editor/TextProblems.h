#pragma once
//  TextProblems - haelt die Fundstellen EINER offenen Datei und stoesst den
//  Durchgang an. Eigenes Objekt, weil zwei Zeichner dieselbe Liste brauchen:
//  die Unterstreichung (`TextDecorations`) und der Punkt im Minimap-Streifen.
//  Ein Lauf je Zeichner waere ein zweiter voller Durchgang ueber das Dokument.
#include "editor/SyntaxProblems.h"

#include <QHash>
#include <QObject>
#include <QQuickTextDocument>
#include <QTimer>
#include <QVariantList>

namespace mg::editor {

class TextProblems : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    //  Aus heisst: kein Durchgang, keine Liste, keine Marke. Der Schalter sitzt
    //  in den Einstellungen des Editors.
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int count READ count NOTIFY problemsChanged)

public:
    explicit TextProblems(QObject* parent = nullptr);

    QQuickTextDocument* document() const { return m_quickDoc; }
    void setDocument(QQuickTextDocument* d);
    QString path() const { return m_path; }
    void    setPath(const QString& p);
    bool enabled() const { return m_an; }
    void setEnabled(bool v);

    int count() const { return int(m_probleme.size()); }
    //  Fuer die Zeichner - ohne Kopie.
    const QList<Problem>& problems() const { return m_probleme; }

    //  Die Fundstellen EINER Zeile; leer, wenn dort nichts steht. Die
    //  Unterstreichung fragt das je sichtbarer Zeile.
    const QList<Problem>* inBlock(int block) const;

    //  Fuer die Oberflaeche und fuer Pruefstaende: {block, start, length, kind,
    //  detail} je Eintrag.
    Q_INVOKABLE QVariantList list() const;
    //  Blocknummer der ersten Fundstelle, -1 wenn es keine gibt.
    Q_INVOKABLE int firstBlock() const;

signals:
    void documentChanged();
    void pathChanged();
    void enabledChanged();
    void problemsChanged();

private:
    QTextDocument* doc() const;
    void neuErfassen();
    void anstossen();

    QQuickTextDocument* m_quickDoc = nullptr;
    QString             m_path;
    bool                m_an = true;
    QList<Problem>      m_probleme;
    //  Fundstellen je Blocknummer - die Unterstreichung fragt je sichtbarer
    //  Zeile, und eine Suche ueber die ganze Liste je Bild waere verschwendet.
    QHash<int, QList<Problem>> m_jeBlock;
    QTimer                     m_timer;
};

}  // namespace mg::editor
