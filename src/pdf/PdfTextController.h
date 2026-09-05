#pragma once
// Browser-artige Textauswahl aus der eingebetteten Textebene; eigene Brücke statt QML-PdfSelection, weil die
// Hauptansicht über fitScale*zoom skaliert und die Koordinaten-Abbildung hier in der Hand bleibt.
// Lazy: das Auswahl-Dokument entsteht erst beim ersten Markieren, höchstens eines.

#include "core/SearchPattern.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QThreadPool>
#include <QTimer>
#include <QRectF>
#include <QList>
#include <QHash>


class QPdfDocument;
class QPdfSearchModel;
class QPdfSelection;

class PdfTextController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    Q_PROPERTY(QString selectedText READ selectedText NOTIFY selectedTextChanged)
    Q_PROPERTY(int  searchCount READ searchCount NOTIFY searchChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchChanged)
    Q_PROPERTY(QString searchTerm READ searchTerm NOTIFY searchChanged)
public:
    explicit PdfTextController(QObject* parent = nullptr);
    ~PdfTextController() override;

    bool    isReady() const { return m_doc != nullptr; }
    QString selectedText() const { return m_selText; }
    int     searchCount() const { return m_hits.size(); }
    bool    searching() const { return m_searchPage >= 0; }
    QString searchTerm() const { return m_searchTerm; }

    // Sorgt (lazy, asynchron) dafuer, dass fuer pathOrUrl ein Auswahl-Dokument
    // geladen wird. Idempotent: laeuft bereits ein Laden/ist es aktiv -> No-Op.
    // Ein anderer Pfad verwirft das vorherige Dokument.
    Q_INVOKABLE void prepare(const QString& pathOrUrl);

    Q_INVOKABLE void releaseDocument();

    // Markiert Text zwischen zwei normalisierten Punkten und liefert die Highlight-Rechtecke `{ x, y, w, h }`.
    // Merkt sich zugleich den Text für `copyToClipboard()`.
    Q_INVOKABLE QVariantList selectionBetween(int page,
                                              double nx0, double ny0,
                                              double nx1, double ny1);

    Q_INVOKABLE QVariantList selectAllOnPage(int page);

    Q_INVOKABLE void clearSelection();

    // Zeilenfang: die erkannten Textzeilen einer Seite als normalisierte Rechtecke, aus `getAllText()` nach
    // vertikaler Mitte gruppiert. Leer, wenn das Dokument noch nicht geladen ist - der Editor platziert dann frei.
    Q_INVOKABLE QVariantList textLineRects(int page);

    // Prueft den aufgezogenen Bereich gegen die erkannten Textzeilen; getroffene werden
    // vereinigt, die Box schnappt auf die Zeilen-Bounds. Ohne Treffer faellt der Editor
    // STILL auf die unbefuellte Box zurueck. Seiteneffektfrei - selectedText bleibt.
    Q_INVOKABLE QVariantMap replaceProbe(int page, double nx0, double ny0,
                                         double nx1, double ny1);

    // Datei NEU einlesen, obwohl der Pfad derselbe blieb: `prepare` ist bewusst idempotent und täte hier nichts.
    // Nötig, wenn die Datei SELBST umgeschrieben wurde - Seitenoperationen und OCR ändern sie unter gleichem Namen.
    Q_INVOKABLE void reload();

    Q_INVOKABLE void copyToClipboard();

    // `search` startet eine neue Suche (leerer Begriff hebt sie auf). Die Seiten werden STÜCKWEISE durchsucht,
    // damit die Oberfläche nicht stehenbleibt; Fortschritt und Ende meldet `searchChanged`.
    Q_INVOKABLE void search(const QString& needle);
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE QVariantList searchHitsOnPage(int page) const;
    Q_INVOKABLE QVariantMap searchHit(int index) const;

    // Intern (vom Worker-Thread per QueuedConnection aufgerufen)
    //  Nimmt ein fertig geladenes (oder fehlgeschlagenes = nullptr) Dokument auf
    //  dem GUI-Thread entgegen. NICHT direkt aus QML aufrufen.
    void adoptDocument(QPdfDocument* doc, const QString& localPath, int generation);

signals:
    void readyChanged();
    void selectedTextChanged();
    void searchChanged();

private:
    QVariantList applySelection(const QPdfSelection& sel, int page,
                                double pageWidthPts, double pageHeightPts);

    // Erkannte Textzeilen einer Seite in PDF-PUNKTEN (gemeinsamer Kern von
    // textLineRects und replaceProbe): Fragment-Rechtecke aus getAllText()
    // nach vertikaler Mitte gruppiert und je Zeile vereinigt.
    QList<QRectF> lineRectsPts(int page) const;

    QPdfDocument* m_doc = nullptr;   // aktives Auswahl-Dokument (GUI-Thread-Affinitaet)
    QString       m_activePath;      // lokaler Pfad des aktiven Dokuments
    QString       m_pendingPath;     // lokaler Pfad eines gerade ladenden Dokuments
    int           m_generation = 0;  // verwirft veraltete Async-Ladevorgaenge

    // 1 Thread -> nie zwei QPdfDocument-Ladevorgaenge gleichzeitig (RAM-Peak gedeckelt).
    QThreadPool   m_pool;

    QString       m_selText;         // zuletzt markierter Text

    // EIN Treffer = EINE Fundstelle, auch wenn sie über mehrere Rechtecke gezeichnet wird: PDFium liefert je
    // Fundstelle so viele, wie sie Zeige-Operatoren berührt, und manche Erzeuger setzen JEDES Zeichen einzeln -
    // ein Rechteck je Treffer ergab dort "7 Treffer" für das eine Wort "Stellen".
    struct SearchHit {
        int             page = 0;
        QList<QRectF>   rects;      // alle Teilstücke DIESER Fundstelle
        QString         before;
        QString         after;

        QRectF bounds() const {
            QRectF b;
            for (const QRectF& r : rects) b = b.isNull() ? r : b.united(r);
            return b;
        }
    };
    QVector<SearchHit> m_hits;
    QString            m_searchTerm;
    //  Derselbe Begriff als Muster - der WOERTLICHE Zweig laeuft weiter ueber
    //  `QPdfSearchModel` (der liefert Rechtecke und Kontext gratis), der
    //  Muster-Zweig ueber den Seitentext. Siehe `core/SearchPattern.h`.
    mg::search::Pattern m_searchPattern;
    int                m_searchPage = -1;
    QPdfSearchModel*   m_searchModel = nullptr;
    // Dokument, zu dem die Treffer gehören: ohne diesen Vergleich verschluckte der Kurzschluss "derselbe Begriff
    // -> nichts tun" eine Suche, die VOR dem Öffnen eingetippt wurde.
    QPdfDocument*      m_searchedDoc = nullptr;
    QTimer             m_searchTimer;
    void stepSearch();
};
