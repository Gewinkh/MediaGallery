#include "pdf/edit/PdfEditController.h"

#include "core/FolderImages.h"
#include "pdf/edit/PdfVectorExport.h"
#include "pdf/edit/PdfEditCommands.h"
#include "core/ISettings.h"
#include "core/AppSettings.h"      // AppSettings::instance() für den Default-Ctor (QML-Instanzen)
#include "core/PathUtils.h"
#include "pdf/extract/PdfPageCopier.h"   // PdfAssembler - destruktiver Seiten-Neuschrieb
#include "pdf/edit/PdfOcrLayer.h"         // unsichtbare Textebene fuer Scans
#include "pdf/OcrEngine.h"                // mg::ocr - Erkennung (optional)
#include "pdf/edit/PdfContentEditor.h" // verlustfreies Content-Stream-Editing (Aufgabe)
#include "pdf/edit/PdfTextEditor.h"   // zeichenweises Bearbeiten (Caret-Werkzeug)
#include "pdf/edit/PdfTextReflow.h"   // Absatz-Umbruch nach dem Tippen
#include "pdf/edit/PdfAnnotations.h"   // übernommene Annotationen streichen

#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPdfWriter>
#include <QPageSize>
#include <QPageLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QTextLayout>
#include <QTextOption>
#include <QImage>
#include <QFont>
#include <QFontInfo>
#include <QImageReader>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
#include <QRunnable>
#include <QTemporaryFile>
#include <QMetaObject>
#include <QCoreApplication>
#include <QtMath>
#include <utility>

// Rendert Original + Overlay in ein neues PDF im Worker. Eigene
// QPdfDocument-Instanz, damit der Render-Mutex der Anzeige unberuehrt bleibt.
namespace {

// 200 dpi: der Punkt, an dem Tesseract zuverlaessig liest, ohne dass die Bilder ausufern.
constexpr double kSearchableDpi = 200.0;

// Content-Stream parsen gehoert nicht in den GUI-Thread - eine dichte Seite
// kostet Millisekunden, und geklickt wird beim Setzen des Carets.
class PdfCaretLayoutTask : public QRunnable {
public:
    // srcPage ist die Seite in der Quelldatei, viewPage die angezeigte; bei
    // geaendertem Plan fallen sie auseinander. Zurueckgemeldet wird die Ansichts-Seite.
    PdfCaretLayoutTask(PdfEditController* owner, QString source,
                       int srcPage, int viewPage, int generation)
        : m_owner(owner), m_source(std::move(source))
        , m_src(srcPage), m_view(viewPage), m_gen(generation) {
        setAutoDelete(true);
    }

    void run() override {
        QVector<mg::PdfGlyph> glyphs;
        QString err;
        if (!mg::PdfTextLayout::buildForPage(m_source, m_src, &glyphs, &err))
            glyphs.clear();
        PdfEditController* owner = m_owner;
        const int page = m_view, gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, page, glyphs, err, gen]() {
            owner->caretLayoutFinished(page, glyphs, err, gen);
        }, Qt::QueuedConnection);
    }
private:
    PdfEditController* m_owner;
    QString            m_source;
    int                m_src;
    int                m_view;
    int                m_gen;
};

// Gibt die Textebenen-Ops auf der pristinen Datei wieder. Von vorne statt
// inkrementell, weil jede Aenderung sonst auf einer bereits geaenderten
// Glyphenlage aufsetzen muesste.
class PdfTextOpsTask : public QRunnable {
public:
    PdfTextOpsTask(PdfEditController* owner, QString source, QString target,
                   QVector<PdfTextOp> ops, int generation)
        : m_owner(owner), m_source(std::move(source)), m_target(std::move(target))
        , m_ops(std::move(ops)), m_gen(generation) { setAutoDelete(true); }

    void run() override {
        QString err;
        const bool ok = replay(&err);
        PdfEditController* owner = m_owner;
        const int gen = m_gen;
        const int  caretTo   = m_caretTo;
        const int  caretPage = m_caretPage;
        const bool overflow  = m_overflow;
        QMetaObject::invokeMethod(owner, [owner, ok, err, gen, caretTo, caretPage, overflow]() {
            owner->textOpsTaskFinished(ok, err, gen, caretTo, caretPage, overflow);
        }, Qt::QueuedConnection);
    }

private:
    bool replay(QString* err) {
        const QString part = m_target + QStringLiteral(".part");
        // Ein- und Ausgabe muessen verschiedene Dateien sein - PdfTextEditor liest die
        // Quelle komplett.
        const QString scratch[2] = {
            QDir::tempPath() + QStringLiteral("/mg-textops-%1-a.pdf")
                .arg(reinterpret_cast<quintptr>(this), 0, 16),
            QDir::tempPath() + QStringLiteral("/mg-textops-%1-b.pdf")
                .arg(reinterpret_cast<quintptr>(this), 0, 16)
        };

        QString cur = m_source;
        bool ok = true;
        for (int i = 0; ok && i < m_ops.size(); ++i) {
            const PdfTextOp& op = m_ops.at(i);
            const QString out = (i == m_ops.size() - 1) ? part : scratch[i % 2];
            QFile::remove(out);
            ok = op.isInsert()
                     ? mg::PdfTextEditor::insertText(cur, out, op.page, op.index, op.text, err)
                     : mg::PdfTextEditor::deleteText(cur, out, op.page, op.index, op.removed, err);
            cur = out;
        }
        if (!ok) {
            QFile::remove(scratch[0]);
            QFile::remove(scratch[1]);
            QFile::remove(part);
            return false;
        }

        // Zweiter Gang fuer den Absatz-Umbruch: erst jetzt traegt jedes getippte
        // Zeichen seine im Dokument gemessene Breite.
        if (!m_ops.isEmpty()) {
            const PdfTextOp& last = m_ops.constLast();
            const int caretAt = last.isInsert() ? last.index + last.text.size()
                                                : last.index;
            // Zwischendatei neben die Arbeitsdatei, nicht nach /tmp: nur im selben
            // Verzeichnis ist das rename ein atomarer Austausch.
            const QString rp = part + QStringLiteral(".rf");
            QFile::remove(rp);
            mg::PdfReflowPlan plan;
            QString rerr;
            if (mg::PdfTextReflow::reflowParagraph(part, rp, last.page, caretAt,
                                                   &plan, &rerr)) {
                QFile::remove(part);
                if (QFile::rename(rp, part)) {
                    m_caretTo   = mg::PdfTextReflow::mapCaretIndex(plan, caretAt);
                    m_caretPage = last.page;
                    m_overflow  = plan.overflow;
                } else {
                    // Austausch gescheitert - ehrlich melden statt eine halbe Datei zu hinterlassen.
                    QFile::remove(rp);
                    QFile::remove(scratch[0]);
                    QFile::remove(scratch[1]);
                    if (err) *err = QStringLiteral("reflow swap failed");
                    return false;
                }
            } else {
                QFile::remove(rp);          // Umbruch nicht möglich/nicht nötig
            }
        }

        QFile::remove(scratch[0]);
        QFile::remove(scratch[1]);
        QFile::remove(m_target);
        if (!QFile::rename(part, m_target)) {
            QFile::remove(part);
            if (err) *err = QStringLiteral("rename failed");
            return false;
        }
        return true;
    }

    PdfEditController* m_owner;
    QString            m_source;
    QString            m_target;
    QVector<PdfTextOp> m_ops;
    int                m_gen;
    int                m_caretTo   = -1;
    int                m_caretPage = -1;
    bool               m_overflow  = false;
};

// Datei-I/O und Objektbaum-Parsen im Pool; ein grosses Formular kostet spuerbar Zeit.
class PdfFormReadTask : public QRunnable {
public:
    PdfFormReadTask(PdfEditController* owner, QString path, int generation)
        : m_owner(owner), m_path(std::move(path)), m_gen(generation) { setAutoDelete(true); }

    void run() override {
        QVector<mg::PdfFormField> fields;
        // Ein verschluesseltes PDF ist kein Fehlerfall - dann gibt es eben keine Felder.
        if (!mg::PdfFormFields::read(m_path, &fields, nullptr))
            fields.clear();
        PdfEditController* owner = m_owner;
        const int gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, fields, gen]() {
            owner->formReadFinished(fields, gen);
        }, Qt::QueuedConnection);
    }
private:
    PdfEditController* m_owner;
    QString            m_path;
    int                m_gen;
};

class PdfAnnotReadTask : public QRunnable {
public:
    PdfAnnotReadTask(PdfEditController* owner, QString path, int generation)
        : m_owner(owner), m_path(std::move(path)), m_gen(generation) { setAutoDelete(true); }

    void run() override {
        QVector<mg::PdfAnnotation> annots;
        if (!mg::PdfAnnotations::read(m_path, &annots, nullptr))
            annots.clear();            // nicht lesbar ⇒ es gibt eben keine
        PdfEditController* owner = m_owner;
        const int gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, annots, gen]() {
            owner->annotReadFinished(annots, gen);
        }, Qt::QueuedConnection);
    }
private:
    PdfEditController* m_owner;
    QString            m_path;
    int                m_gen;
};

// Inkrementelles Update: die Quelle bleibt byteweise unangetastet.
class PdfFormSaveTask : public QRunnable {
public:
    // applyPlan = die angezeigte Seitenfolge weicht ab; der Zusammenbau geschieht
    // danach im GUI-Thread, dem der Plan gehoert.
    PdfFormSaveTask(PdfEditController* owner, QString source, QString target,
                    QHash<QString, QString> values, int generation, bool applyPlan)
        : m_owner(owner), m_source(std::move(source)), m_target(std::move(target))
        , m_values(std::move(values)), m_gen(generation), m_applyPlan(applyPlan) {
        setAutoDelete(true);
    }

    void run() override {
        QString err;
        bool ok = mg::PdfFormFields::fillAndSave(m_source, m_target, m_values, &err);
        // Vor dem Umbau festschreiben: PdfAssembler baut einen neuen Katalog und nimmt
        // /AcroForm nicht mit - sonst traege die Kopie Widgets ohne Formular.
        if (ok && m_applyPlan) {
            const QString flat = m_target + QStringLiteral(".flat");
            if (mg::PdfFormFields::flatten(m_target, flat, &err)) {
                QFile::remove(m_target);
                ok = QFile::rename(flat, m_target);
                if (!ok) err = QStringLiteral("Zwischendatei nicht ersetzbar");
            } else {
                QFile::remove(flat);
                ok = false;
            }
        }
        PdfEditController* owner = m_owner;
        const QString target = m_target;
        const int gen = m_gen;
        const bool plan = m_applyPlan;
        QMetaObject::invokeMethod(owner, [owner, ok, target, err, gen, plan]() {
            owner->formSaveFinished(ok, target, err, gen, plan);
        }, Qt::QueuedConnection);
    }
private:
    PdfEditController*      m_owner;
    QString                 m_source;
    QString                 m_target;
    QHash<QString, QString> m_values;
    int                     m_gen;
    bool                    m_applyPlan = false;
};

// Meldet nur Erfolg; bei Misserfolg startet der Controller den Raster-Export.
class PdfContentEditTask : public QRunnable {
public:
    PdfContentEditTask(PdfEditController* owner, QString source, QString target,
                       QVector<mg::PdfTextEdit> edits, int generation)
        : m_owner(owner), m_source(std::move(source)), m_target(std::move(target))
        , m_edits(std::move(edits)), m_gen(generation) { setAutoDelete(true); }

    void run() override {
        QString err;
        const bool ok = mg::PdfContentEditor::editText(m_source, m_target, m_edits, &err);
        PdfEditController* owner = m_owner;
        const QString target = m_target;
        const int gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, ok, target, gen]() {
            owner->contentEditTaskFinished(ok, target, gen);
        }, Qt::QueuedConnection);
    }
private:
    PdfEditController*        m_owner;
    QString                   m_source;
    QString                   m_target;
    QVector<mg::PdfTextEdit>  m_edits;
    int                       m_gen;
};

// Je Seite ohne Textebene rendern und erkennen, danach einmal PdfOcrLayer::write
// als inkrementelles Update.
class PdfSearchableTask : public QRunnable {
public:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    PdfSearchableTask(PdfEditController* owner, QString sourcePath, QString targetPath,
                      int generation, CancelFlag cancel)
        : m_owner(owner)
        , m_source(std::move(sourcePath))
        , m_target(std::move(targetPath))
        , m_gen(generation)
        , m_cancel(std::move(cancel)) {
        setAutoDelete(true);
    }

    void run() override {
        QString err;
        int pages = 0, words = 0, skipped = 0;
        const bool ok = build(&pages, &words, &skipped, &err);

        PdfEditController* owner = m_owner;
        const int gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, ok, pages, words, skipped, err, gen] {
            owner->searchableTaskFinished(ok, pages, words, skipped, err, gen);
        }, Qt::QueuedConnection);
    }

private:
    bool cancelled() const {
        return m_cancel && m_cancel->load(std::memory_order_relaxed);
    }
    void report(int done, int total) {
        PdfEditController* owner = m_owner;
        const int gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, done, total, gen] {
            owner->searchableTaskProgress(done, total, gen);
        }, Qt::QueuedConnection);
    }

    bool build(int* pagesOut, int* wordsOut, int* skippedOut, QString* err) {
        if (!mg::ocr::available()) { *err = QStringLiteral("noocr"); return false; }

        // Eine Instanz fuer den ganzen Lauf. Zwischenzeitliches Neuoeffnen brachte
        // gemessen nichts (198/212/268 MB).
        auto doc = std::make_unique<QPdfDocument>();
        if (doc->load(m_source) != QPdfDocument::Error::None
            || doc->status() != QPdfDocument::Status::Ready) {
            *err = QStringLiteral("load");
            return false;
        }
        const int n = doc->pageCount();
        QVector<QVector<mg::PdfOcrWord>> perPage(n);
        // Nur Seiten ohne Textebene sind Kandidaten; die Zahl entscheidet zwischen
        // schon durchsuchbar und nichts erkannt.
        int scanned = 0;

        // Seriell, gemessen die richtige Wahl: parallel brachte an 16 Seiten nur
        // 13378 -> 12120 ms, mit 4 Faeden war es wieder langsamer als mit 2.
        for (int p = 0; p < n; ++p) {
            if (cancelled()) { *err = QStringLiteral("cancel"); return false; }
            report(p, n);

            // Eine zweite Textebene wuerde jeden Treffer verdoppeln.
            if (!doc->getAllText(p).text().trimmed().isEmpty())
                continue;
            ++scanned;

            const QSizeF ps = doc->pagePointSize(p);
            if (ps.isEmpty())
                continue;
            const QSize px(qRound(ps.width()  * kSearchableDpi / 72.0),
                           qRound(ps.height() * kSearchableDpi / 72.0));
            const QImage img = doc->render(p, px);
            if (img.isNull())
                continue;

            const QList<mg::OcrLine> found =
                mg::ocr::recognize(img, kSearchableDpi, mg::OcrLevel::Word);
            QVector<mg::PdfOcrWord> words;
            words.reserve(found.size());
            for (const mg::OcrLine& w : found)
                words.append({ w.rectPts, w.text });
            if (!words.isEmpty()) {
                ++(*pagesOut);
                *wordsOut += words.size();
                perPage[p] = std::move(words);      // erst zaehlen, dann leeren
            }
        }
        report(n, n);
        if (cancelled())    { *err = QStringLiteral("cancel"); return false; }
        if (*pagesOut == 0) {
            // Gab es keine textlose Seite, war nichts zu tun - kein Fehlschlag.
            *err = (scanned == 0) ? QStringLiteral("alreadytext")
                                  : QStringLiteral("notext");
            return false;
        }

        return mg::PdfOcrLayer::write(m_source, m_target, perPage, err, skippedOut);
    }

    PdfEditController* m_owner;
    QString            m_source;
    QString            m_target;
    int                m_gen;
    CancelFlag         m_cancel;
};

class PdfExportTask : public QRunnable {
public:
    using CancelFlag = std::shared_ptr<std::atomic<bool>>;

    // sourcePath ist die Datei, die der Nutzer SIEHT (renderSourcePath).
    PdfExportTask(PdfEditController* owner, QString sourcePath, QString targetPath,
                  QVector<PdfEditBox> boxes, int generation, CancelFlag cancel,
                  QVector<int> removeAnnots = {},
                  QVector<mg::PdfAnnotation> asAnnots = {},
                  QVector<mg::PdfTextEdit> redactions = {},
                  QVector<mg::PdfRedactArea> redactAreas = {})
        : m_owner(owner)
        , m_source(std::move(sourcePath))
        , m_target(std::move(targetPath))
        , m_boxes(std::move(boxes))
        , m_gen(generation)
        , m_cancel(std::move(cancel))
        , m_removals(std::move(removeAnnots))
        , m_asAnnots(std::move(asAnnots))
        , m_redactions(std::move(redactions))
        , m_redactAreas(std::move(redactAreas)) {
        setAutoDelete(true);
    }

    void run() override {
        QString err;
        const bool ok = writePdf(&err);

        PdfEditController* owner = m_owner;
        const QString target = m_target;
        const int gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, ok, target, err, gen]() {
            owner->exportTaskFinished(ok, target, err, gen);
        }, Qt::QueuedConnection);
    }

private:
    bool cancelled() const {
        return m_cancel && m_cancel->load(std::memory_order_relaxed);
    }

    void reportProgress(int done, int total) {
        PdfEditController* owner = m_owner;
        const int gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, done, total, gen]() {
            owner->exportTaskProgress(done, total, gen);
        }, Qt::QueuedConnection);
    }

    // Zuerst eine Zwischendatei ohne die gestrichenen Annotationen; schlaegt das
    // fehl, wird nicht still weitergemacht.
    bool pruneAnnotations(QString* err) {
        if (m_removals.isEmpty())
            return true;
        m_pruned = std::make_unique<QTemporaryFile>(
            QDir::tempPath() + QStringLiteral("/mgexportXXXXXX.pdf"));
        if (!m_pruned->open())
            return (*err = QStringLiteral("Zwischendatei"), false);
        const QString path = m_pruned->fileName();
        m_pruned->close();
        QString ae;
        if (!mg::PdfAnnotations::write(m_source, path, {}, m_removals, &ae))
            return (*err = ae, false);
        m_source = path;
        return true;
    }

    void reportRedactionFallback() {
        PdfEditController* owner = m_owner;
        const int gen = m_gen;
        QMetaObject::invokeMethod(owner, [owner, gen]() {
            owner->redactionFellBack(gen);
        }, Qt::QueuedConnection);
    }

    // Alle Schreibwege haengen inkrementell an, die alten Bytes bleiben stehen -
    // fuer geschwaerzten Text muss die Ausgabe deshalb neu geschrieben werden.
    bool compactRedacted(const QString& path) {
        const QString tmpFlat    = path + QStringLiteral(".mgflat");
        const QString tmpCompact = path + QStringLiteral(".mgcompact");
        QFile::remove(tmpFlat);
        QFile::remove(tmpCompact);

        QString src = path;
        QVector<mg::PdfFormField> fields;
        if (mg::PdfFormFields::read(path, &fields, nullptr) && !fields.isEmpty()) {
            QString fe;
            if (!mg::PdfFormFields::flatten(path, tmpFlat, &fe)) {
                qInfo("Schwärzung: Formular nicht festschreibbar (%s)", qPrintable(fe));
                QFile::remove(tmpFlat);
                return false;
            }
            src = tmpFlat;
        }

        QString re;
        const bool built = PdfAssembler::rebuild(src, tmpCompact, &re);
        QFile::remove(tmpFlat);
        if (!built) {
            qInfo("Schwärzung: Neuschreiben fehlgeschlagen (%s)", qPrintable(re));
            QFile::remove(tmpCompact);
            return false;
        }
        if (!QFile::remove(path) || !QFile::rename(tmpCompact, path)) {
            QFile::remove(tmpCompact);
            return false;
        }
        return true;
    }

    bool writePdf(QString* err) {
        if (!pruneAnnotations(err))
            return false;

        // Schwaerzungen zuerst: ohne diesen Schritt liegt am Ende nur ein schwarzer
        // Balken ueber weiterhin lesbarem Text.
        bool forceRaster = false;

        // Die Differenz ist der gefaehrliche Rest: ohne Text kann der Textweg nichts entfernen.
        int redactBoxes = 0;
        for (const PdfEditBox& b : m_boxes)
            if (b.kind == PdfAnnKind::Redact) ++redactBoxes;

        // Textweg zuerst - nur er trifft Stellen, die ueber den Balken hinausreichen,
        // etwa eine Auswahl ueber einen Zeilenumbruch.
        bool textRedactOk = false;
        bool textStillThere = false;
        if (!m_redactions.isEmpty()) {
            m_redacted = std::make_unique<QTemporaryFile>(
                QDir::tempPath() + QStringLiteral("/mgredactXXXXXX.pdf"));
            QString rerr;
            if (!m_redacted->open()) {
                textStillThere = true;                 // ohne Zwischendatei kein Beweis
            } else {
                const QString path = m_redacted->fileName();
                m_redacted->close();
                if (mg::PdfContentEditor::editText(m_source, path, m_redactions, &rerr)) {
                    m_source = path;
                    textRedactOk = true;
                }
            }
        }

        // Geometrischer Weg: entfernt, was unter dem Balken liegt, und braucht keinen
        // erkannten Originaltext - rettet die Textebene des restlichen Dokuments.
        bool geoRedactOk = false;
        if (!m_redactAreas.isEmpty()) {
            m_redactedGeo = std::make_unique<QTemporaryFile>(
                QDir::tempPath() + QStringLiteral("/mgredactgeoXXXXXX.pdf"));
            if (m_redactedGeo->open()) {
                const QString path = m_redactedGeo->fileName();
                m_redactedGeo->close();
                QString gerr;
                if (mg::PdfContentEditor::redactAreas(m_source, path, m_redactAreas, &gerr)) {
                    m_source = path;
                    geoRedactOk = true;
                } else {
                    qInfo("PdfExportTask: Schwärzung geometrisch nicht möglich (%s)",
                          qPrintable(gerr));
                }
            }
        }

        // Sonde nach beiden Wegen auf der Datei, die gleich hinausgeht: der Textweg
        // scheitert bei einer Auswahl ueber mehrere Zeilen regelmaessig.
        if (!textRedactOk && !m_redactions.isEmpty()) {
            const auto squeeze = [](const QString& s2) {
                QString o;
                o.reserve(s2.size());
                for (const QChar c : s2)
                    if (!c.isSpace()) o += c;
                return o;
            };
            for (const mg::PdfTextEdit& ed : m_redactions) {
                const QString needle = squeeze(ed.original);
                if (needle.isEmpty()) {
                    textStillThere = true;      // nichts Prüfbares -> nicht beweisbar
                    break;
                }
                QVector<mg::PdfGlyph> glyphs;
                if (!mg::PdfTextLayout::buildForPage(m_source, ed.page, &glyphs, nullptr)) {
                    textStillThere = true;      // nicht lesbar -> nicht beweisbar
                    break;
                }
                QString pageText;
                pageText.reserve(glyphs.size());
                for (const mg::PdfGlyph& g : glyphs)
                    pageText += g.ch;
                if (squeeze(pageText).contains(needle)) { textStillThere = true; break; }
            }
        }

        // Der geometrische Weg ist der volle Beweis - er bearbeitet jede Flaeche und
        // misst die Ausgabe danach nach.
        if (redactBoxes > 0) {
            forceRaster = textStillThere
                       || (redactBoxes > m_redactions.size() && !geoRedactOk);
            if (forceRaster)
                reportRedactionFallback();
        }

        // Wurde geschwaerzt, muss die Ausgabe verdichtet werden - sonst stuende die
        // entfernte Fassung noch darin.
        const bool didRedact = textRedactOk || geoRedactOk;

        // Ohne Anmerkungen lehnt der Vektor-Export ab; einfaches Kopieren ist dann der
        // einzige verlustfreie Weg.
        if (m_boxes.isEmpty() && !forceRaster) {
            QFile::remove(m_target);                 // Ziel ist ein frischer Pfad
            if (!QFile::copy(m_source, m_target)) {
                *err = QStringLiteral("Kopie");
                return false;
            }
            if (!didRedact || compactRedacted(m_target)) {
                reportProgress(1, 1);
                return true;
            }
            forceRaster = true;                  // s. compactRedacted()
            reportRedactionFallback();
        }

        // Nur wenn der Controller ALLE Notizen abbilden konnte - sonst waere die Ausgabe
        // halb Annotation, halb gemalt.
        if (!forceRaster && !m_asAnnots.isEmpty()) {
            QString ae;
            if (mg::PdfAnnotations::write(m_source, m_target, m_asAnnots, {}, &ae)) {
                if (!didRedact || compactRedacted(m_target)) {
                    reportProgress(1, 1);
                    return true;
                }
                forceRaster = true;
                reportRedactionFallback();
            } else {
            qInfo("PdfAnnotations::write: %s -> gemalter Export", qPrintable(ae));
            }
        }

        // Der Vektor-Weg laesst den Originalinhalt byteweise stehen und haengt nur die
        // Zeichenbefehle an.
        if (!forceRaster) {
            QString vecErr;
            if (mg::PdfVectorExport::exportAnnotations(m_source, m_target, m_boxes,
                                                       &vecErr)) {
                if (!didRedact || compactRedacted(m_target)) {
                    reportProgress(1, 1);
                    return true;
                }
                forceRaster = true;
                reportRedactionFallback();
            } else {
                qInfo("PdfVectorExport: %s -> Raster-Export", qPrintable(vecErr));
            }
        }

        // Rasterweg: Annotationen mitrendern - Qt PDF zeichnet sie sonst gar nicht
        // (gemessen), Fremdnotizen fielen still aus dem Dokument.
        QPdfDocumentRenderOptions annOpts;
        annOpts.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);

        QPdfDocument doc;   // Affinität: dieser Pool-Thread (wie PdfThumbRenderTask)
        if (doc.load(m_source) != QPdfDocument::Error::None
            || doc.status() != QPdfDocument::Status::Ready) {
            *err = QStringLiteral("PDF");
            return false;
        }
        const int pageCount = doc.pageCount();
        if (pageCount <= 0) {
            *err = QStringLiteral("0");
            return false;
        }

        QSaveFile out(m_target);
        if (!out.open(QIODevice::WriteOnly)) {
            *err = out.errorString();
            return false;
        }

        QPdfWriter writer(&out);
        writer.setResolution(72);                       // 1 Einheit = 1 PDF-Punkt
        writer.setCreator(QStringLiteral("MediaGallery"));
        writer.setTitle(QFileInfo(m_source).completeBaseName());

        const int viewCount = pageCount;

        QPainter p;
        for (int vi = 0; vi < viewCount; ++vi) {
            if (cancelled()) {
                if (p.isActive()) p.end();
                out.cancelWriting();
                *err = QStringLiteral("cancel");
                return false;
            }

            QSizeF pts = doc.pagePointSize(vi);
            if (pts.isEmpty())
                pts = QSizeF(612.0, 792.0);                 // Fallback: US Letter

            writer.setPageSize(QPageSize(pts, QPageSize::Point,
                                         QString(), QPageSize::ExactMatch));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));

            if (vi == 0) {
                if (!p.begin(&writer)) {
                    out.cancelWriting();
                    *err = QStringLiteral("begin");
                    return false;
                }
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setRenderHint(QPainter::TextAntialiasing, true);
                p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            } else if (!writer.newPage()) {
                p.end();
                out.cancelWriting();
                *err = QStringLiteral("newPage");
                return false;
            }

            // Eine eingefuegte Leerseite ist in der Quelle eine echte leere Seite.
            const QSize px(qMax(1, qRound(pts.width()  / 72.0 * PdfEditController::kExportRenderDpi)),
                           qMax(1, qRound(pts.height() / 72.0 * PdfEditController::kExportRenderDpi)));
            const QImage img = doc.render(vi, px, annOpts);
            if (!img.isNull())
                p.drawImage(QRectF(QPointF(0, 0), pts), img);
            else
                p.fillRect(QRectF(QPointF(0, 0), pts), Qt::white);

            for (const PdfEditBox& b : std::as_const(m_boxes))
                if (b.page == vi)
                    drawBox(p, b);

            reportProgress(vi + 1, viewCount);
        }

        p.end();
        doc.close();            // Lese-Handle VOR dem Commit freigeben (Windows)
        if (!out.commit()) {
            *err = out.errorString();
            return false;
        }
        return true;
    }

    // Laenge und Winkel aus der Linienbreite; identische Geometrie im QML-Delegate.
    static void drawArrowHead(QPainter& p, const QPointF& from, const QPointF& to,
                              qreal lineWidth) {
        const qreal ang = std::atan2(to.y() - from.y(), to.x() - from.x());
        const qreal len = qMax<qreal>(10.0, lineWidth * 4.0);
        const qreal spread = M_PI / 7.0;                 // ~25.7°
        const QPointF a(to.x() - len * std::cos(ang - spread),
                        to.y() - len * std::sin(ang - spread));
        const QPointF b(to.x() - len * std::cos(ang + spread),
                        to.y() - len * std::sin(ang + spread));
        p.drawLine(to, a);
        p.drawLine(to, b);
    }

    // Zeichnet eine Annotation exakt wie die QML-Anzeige - Post-it-Optik und
    // derselbe QTextLayout-Weg, damit Anzeige und Ausgabe zusammenfallen.
    void drawBox(QPainter& p, const PdfEditBox& b) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);

        switch (b.kind) {
        case PdfAnnKind::Freehand: {
            if (b.points.size() >= 2) {
                QPen pen(b.stroke, qMax<qreal>(0.2, b.lineWidth));
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);
                p.setPen(pen);
                QPainterPath path(b.points.first());
                for (int i = 1; i < b.points.size(); ++i)
                    path.lineTo(b.points.at(i));
                p.drawPath(path);
            } else if (b.points.size() == 1) {
                p.setPen(Qt::NoPen);
                p.setBrush(b.stroke);
                const qreal r = qMax<qreal>(0.2, b.lineWidth) * 0.5;
                p.drawEllipse(b.points.first(), r, r);
            }
            p.restore();
            return;
        }
        case PdfAnnKind::Arrow: {
            if (b.points.size() >= 2) {
                QPen pen(b.stroke, qMax<qreal>(0.2, b.lineWidth));
                pen.setCapStyle(Qt::RoundCap);
                pen.setJoinStyle(Qt::RoundJoin);
                p.setPen(pen);
                const QPointF from = b.points.first();
                const QPointF to   = b.points.at(1);
                p.drawLine(from, to);
                drawArrowHead(p, from, to, b.lineWidth);
            }
            p.restore();
            return;
        }
        case PdfAnnKind::Rect: {
            if (b.fill.alpha() > 0) p.setBrush(b.fill); else p.setBrush(Qt::NoBrush);
            p.setPen(QPen(b.stroke, qMax<qreal>(0.2, b.lineWidth)));
            p.drawRect(b.rect);
            p.restore();
            return;
        }
        case PdfAnnKind::Ellipse: {
            if (b.fill.alpha() > 0) p.setBrush(b.fill); else p.setBrush(Qt::NoBrush);
            p.setPen(QPen(b.stroke, qMax<qreal>(0.2, b.lineWidth)));
            p.drawEllipse(b.rect);
            p.restore();
            return;
        }
        case PdfAnnKind::Stamp: {
            // Ohne diesen Zweig fiel der Stempel im Rasterweg ersatzlos aus.
            if (!b.imagePath.isEmpty()) {
                QImage img(b.imagePath);
                if (!img.isNull()) {
                    const qreal op = (b.fill.alpha() > 0 && b.fill.alpha() < 255)
                                         ? b.fill.alpha() / 255.0 : 1.0;
                    p.setOpacity(op);
                    p.drawImage(b.rect, img);
                    p.setOpacity(1.0);
                }
            }
            p.restore();
            return;
        }
        case PdfAnnKind::Redact: {
            // Schwaerzung: eine deckende Flaeche macht sichtbar, DASS etwas entfernt wurde.
            const QColor cover = b.highlight.alpha() > 0 ? b.highlight
                                                         : QColor(0, 0, 0);
            p.fillRect(b.rect, cover);
            p.restore();
            return;
        }
        case PdfAnnKind::Markup: {
            // Mehrere Bereiche in einem Objekt; Markieren multipliziert, damit der Text lesbar bleibt.
            if (b.points.size() >= 2) {
                if (b.markupStyle == 0) {
                    p.setCompositionMode(QPainter::CompositionMode_Multiply);
                    p.setPen(Qt::NoPen);
                    p.setBrush(b.stroke);
                    for (int i = 0; i + 1 < b.points.size(); i += 2)
                        p.drawRect(QRectF(b.points.at(i), b.points.at(i + 1)).normalized());
                    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
                } else {
                    for (int i = 0; i + 1 < b.points.size(); i += 2) {
                        const QRectF q =
                            QRectF(b.points.at(i), b.points.at(i + 1)).normalized();
                        const qreal t = qMax(0.5, q.height() / 14.0);
                        const qreal y = (b.markupStyle == 1)
                                            ? q.y() + q.height() - t
                                            : q.y() + q.height() / 2.0;
                        p.setPen(QPen(b.stroke, t));
                        p.drawLine(QPointF(q.x(), y),
                                   QPointF(q.x() + q.width(), y));
                    }
                }
            }
            p.restore();
            return;
        }
        case PdfAnnKind::Replace:
            // Deckende Flaeche exakt ueber dem Rechteck, bewusst ohne Post-it-Optik.
            p.fillRect(b.rect, b.highlight);
            break;
        case PdfAnnKind::Text:
            break;                                   // fällt in die Notiz-Zeichnung
        }

        // Post-it-Optik nur fuer klassische Notizen - die Replace-Deckflaeche ist flach.
        const bool paper = b.kind == PdfAnnKind::Text && b.highlight.alpha() > 0;
        if (paper) {
            p.fillRect(b.rect.translated(PdfEditController::kNoteShadowDxPt,
                                         PdfEditController::kNoteShadowDyPt),
                       QColor(0, 0, 0, 52));
            p.fillRect(b.rect, b.highlight);
            // Eselsohr mit identischer Geometrie wie der QML-Canvas.
            const qreal fold = qMin(PdfEditController::kNoteFoldPt,
                                    qMin(b.rect.width(), b.rect.height()) / 3.0);
            if (fold > 2.0) {
                const QPointF pA(b.rect.right() - fold, b.rect.bottom());
                const QPointF pB(b.rect.right(),        b.rect.bottom() - fold);
                const QPointF pC(b.rect.right(),        b.rect.bottom());
                QPainterPath flap;
                flap.moveTo(pA);
                flap.lineTo(pB);
                flap.lineTo(pC);
                flap.closeSubpath();
                QColor flapCol = b.highlight.darker(118);
                flapCol.setAlpha(b.highlight.alpha());
                p.setPen(Qt::NoPen);
                p.fillPath(flap, flapCol);
                QColor lineCol = b.highlight.darker(150);
                lineCol.setAlpha(b.highlight.alpha());
                p.setPen(QPen(lineCol, 0.7));
                p.drawLine(pA, pB);
            }
        }

        QFont f(b.fontFamily);
        f.setPointSizeF(qMax(1.0, b.fontSizePt));       // 72-dpi-Gerät: 1 pt = 1 Einheit
        f.setBold(b.bold);
        f.setItalic(b.italic);
        f.setUnderline(b.underline);

        const qreal pad    = PdfEditController::kBoxPaddingPt;
        const qreal availW = qMax<qreal>(4.0, b.rect.width() - 2.0 * pad);

        QTextOption opt;
        opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        opt.setAlignment(b.alignment == 1 ? Qt::AlignHCenter
                       : b.alignment == 2 ? Qt::AlignRight
                                          : Qt::AlignLeft);

        QTextLayout layout(b.text, f);
        layout.setTextOption(opt);
        layout.beginLayout();
        qreal totalH = 0.0;
        for (;;) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(availW);
            line.setPosition(QPointF(0.0, totalH));
            totalH += line.height();
        }
        layout.endLayout();

        // 0 = oben (Text startet an der Innenkante), 1 = zentriert; der Ueberlauf wird
        // bewusst nicht geklemmt, symmetrisch zur Anzeige.
        const qreal availH = b.rect.height() - 2.0 * pad;
        const qreal yOff   = (b.vAlign == 1) ? (availH - totalH) / 2.0 : 0.0;

        p.setPen(b.color);
        layout.draw(&p, QPointF(b.rect.left() + pad, b.rect.top() + pad + yOff));

        p.restore();
    }

    PdfEditController*  m_owner;
    QString             m_source;
    QString             m_target;
    QVector<PdfEditBox> m_boxes;   // Seiten bereits als ANSICHTS-Indizes
    int                 m_gen;
    CancelFlag          m_cancel;
    // Objektnummern uebernommener Annotationen, die aus der Ausgabe muessen.
    QVector<int>        m_removals;
    // Leer = der gewohnte Weg, sie zu malen.
    QVector<mg::PdfAnnotation> m_asAnnots;
    QVector<mg::PdfTextEdit>   m_redactions;
    // Der geometrische Weg braucht den Text nicht und ist der erste Versuch.
    QVector<mg::PdfRedactArea> m_redactAreas;
    std::unique_ptr<QTemporaryFile> m_redacted;
    // Der geometrische Weg arbeitet auf dem Ergebnis des Textwegs - beide muessen leben.
    std::unique_ptr<QTemporaryFile> m_redactedGeo;
    std::unique_ptr<QTemporaryFile> m_pruned;
};
} // namespace

// Default-Ctor fuer die QML-Instanziierung je PdfSurface.
PdfEditController::PdfEditController(QObject* parent)
    : PdfEditController(AppSettings::instance(), parent) {}

PdfEditController::PdfEditController(ISettings& settings, QObject* parent)
    : QObject(parent), m_settings(settings) {
    m_stack.setUndoLimit(kUndoLimit);
    // 1 Worker: nie zwei Export-Laeufe gleichzeitig, der RAM-Peak bleibt gedeckelt.
    m_pool.setMaxThreadCount(1);

    connect(&m_stack, &QUndoStack::canUndoChanged, this, [this] { emit undoStateChanged(); });
    connect(&m_stack, &QUndoStack::canRedoChanged, this, [this] { emit undoStateChanged(); });
    connect(&m_stack, &QUndoStack::cleanChanged,   this, [this] { emit dirtyChanged(); });
    connect(&m_model, &PdfEditModel::countChanged, this, [this] { emit boxCountChanged(); });
    // Auch Undo/Redo und das Laden des Sidecars - sonst zeigte der Streifen die alte Zahl.
    connect(&m_model, &QAbstractItemModel::dataChanged,   this, [this] { emit trackedChanged(); });
    connect(&m_model, &QAbstractItemModel::rowsInserted,  this, [this] { emit trackedChanged(); });
    connect(&m_model, &QAbstractItemModel::rowsRemoved,   this, [this] { emit trackedChanged(); });
    connect(&m_model, &QAbstractItemModel::modelReset,    this, [this] { emit trackedChanged(); });
    // Formularfelder tragen Quellseiten; jede Plan-Aenderung muss die Liste neu lesen lassen.
    connect(this, &PdfEditController::planChanged,
            this, &PdfEditController::formFieldsChanged);

    // Beenden aus dem Editor heraus: releaseDocument laeuft dann nicht mehr.
    if (QCoreApplication* app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, [this] {
            if (!m_docPath.isEmpty())
                releaseDocument();
        });

    // Verschwundene Auswahl (Undo eines Hinzufuegens, Sidecar-Reset) aufraeumen.
    connect(&m_model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& tl, const QModelIndex&, const QList<int>&) {
                if (m_selectedId >= 0 && tl.isValid()
                    && tl.row() == m_model.indexOfId(m_selectedId))
                    bumpSelectionRev();
            });
    auto dropVanishedSelection = [this] {
        if (m_selectedId >= 0 && m_model.indexOfId(m_selectedId) < 0)
            setSelectedId(-1);
        else
            bumpSelectionRev();
    };
    connect(&m_model, &QAbstractItemModel::rowsRemoved,  this, dropVanishedSelection);
    connect(&m_model, &QAbstractItemModel::rowsInserted, this, dropVanishedSelection);
    connect(&m_model, &QAbstractItemModel::modelReset,   this, dropVanishedSelection);

    // Jeder Neubau schreibt die ganze Datei und laedt sie neu - erst die Tipp-Pause
    // materialisiert das Ergebnis.
    m_textFlush.setSingleShot(true);
    m_textFlush.setInterval(kTextFlushMs);
    connect(&m_textFlush, &QTimer::timeout, this, [this] { startTextRebuild(); });
}

PdfEditController::~PdfEditController() {
    // Laufenden Export kooperativ stoppen, bevor der Controller verschwindet.
    if (m_cancel)
        m_cancel->store(true, std::memory_order_relaxed);
    m_pool.clear();
    m_pool.waitForDone();
}

void PdfEditController::setEditMode(bool on) {
    if (m_editMode == on)
        return;
    finishOpenSessions();
    finishDrawSession();
    m_editMode = on;
    if (!on) { setTool(Select); setSelectedId(-1); }
    emit editModeChanged();
}

void PdfEditController::setTool(int t) {
    // Obergrenze immer am letzten Enum-Wert festmachen: hier stand einmal CaretTool
    // fest verdrahtet, und das neue Markier-Werkzeug wurde still verworfen.
    if (t < Select || t > MarkupTool || m_tool == t)
        return;
    finishOpenSessions();
    finishDrawSession();
    if (m_tool == CaretTool)
        clearCaret();                   // Werkzeug verlassen -> Tipp-Session zu
    m_tool = t;
    // Keine schwebende Toolbar ueber einer alten Auswahl waehrend des Zeichnens.
    if (t != Select)
        setSelectedId(-1);
    emit toolChanged();
}

void PdfEditController::setSelectedId(int id) {
    if (m_selectedId == id)
        return;
    m_selectedId = id;
    emit selectedIdChanged();
    bumpSelectionRev();
}

void PdfEditController::bumpSelectionRev() {
    ++m_selectionRev;
    emit selectionRevChanged();
}

bool PdfEditController::panelOnTop() const {
    return m_settings.pdfEditPanelTop();
}

void PdfEditController::setPanelOnTop(bool v) {
    if (m_settings.pdfEditPanelTop() == v)
        return;
    m_settings.setPdfEditPanelTop(v);
    emit panelOnTopChanged();
}

QVariantList PdfEditController::folderImages() const {
    return mg::folderImages(m_docPath, 300, false);
}

bool PdfEditController::exportAsAnnotations() const {
    return m_settings.pdfExportAsAnnotations();
}
void PdfEditController::setExportAsAnnotations(bool v) {
    if (m_settings.pdfExportAsAnnotations() == v)
        return;
    m_settings.setPdfExportAsAnnotations(v);
    emit exportAsAnnotationsChanged();
}

bool PdfEditController::exportLossless() const {
    return m_settings.pdfExportLossless();
}
void PdfEditController::setExportLossless(bool v) {
    if (m_settings.pdfExportLossless() == v)
        return;
    m_settings.setPdfExportLossless(v);
    emit exportLosslessChanged();
}

QString PdfEditController::backupPath(const QString& pdfPath) {
    return pdfPath + QStringLiteral(".mgorig");
}
QString PdfEditController::previewPath(const QString& pdfPath) {
    return pdfPath + QStringLiteral(".mgpreview.pdf");
}
QString PdfEditController::assetPath(const QString& pdfPath) {
    return pdfPath + QStringLiteral(".mgpages.pdf");
}

QString PdfEditController::pristinePath() const {
    // Sobald eine Seitenoperation laeuft, liegt die Quelle in der Sicherung .mgorig.
    const QString bak = backupPath(m_docPath);
    if (QFile::exists(bak))
        return bak;
    return m_docPath;
}

int PdfEditController::takeStructureFocus() {
    const int v = m_structureFocus;
    m_structureFocus = -1;
    return v;
}

QString PdfEditController::renderSourcePath() const {
    // Seitenoperationen wirken sofort in der PDF selbst - so bleibt Ansichts-Index
    // gleich Seitenindex der gerenderten Datei, die Ansicht wendet den Plan nie an.
    if (m_docPath.isEmpty() || planIsIdentity())
        return textSourcePath();            // ggf. mit bearbeiteter Textebene
    return m_docPath;
}

bool PdfEditController::planIsIdentity() const {
    if (m_plan.size() != m_srcPageCount)
        return false;
    for (int i = 0; i < m_plan.size(); ++i) {
        const PdfPlanPage& p = m_plan.at(i);
        if (p.src != i || p.doc != 0 || p.rot != 0)
            return false;
    }
    return true;
}

int PdfEditController::keyOfView(int viewIndex) const {
    return (viewIndex >= 0 && viewIndex < m_plan.size())
               ? m_plan.at(viewIndex).key : -1;
}

int PdfEditController::pageKeyForView(int viewIndex) const {
    if (viewIndex < 0)
        return -1;
    // Ohne gemeldete Seitenzahl steht kein Plan - dann ist die Ansichts-Seite der Key.
    if (m_plan.isEmpty())
        return viewIndex;
    return keyOfView(viewIndex);
}

int PdfEditController::srcOfView(int viewIndex) const {
    return (viewIndex >= 0 && viewIndex < m_plan.size())
               ? m_plan.at(viewIndex).src : -1;
}

int PdfEditController::viewOfKey(int key) const {
    for (int i = 0; i < m_plan.size(); ++i)
        if (m_plan.at(i).key == key)
            return i;
    return -1;
}

void PdfEditController::assignPlanKeys() {
    // Pristine Seiten tragen key == src, alles Neue Keys darueber. Doppelte Keys aus
    // einem defekten Sidecar werden neu vergeben.
    m_nextPageKey = m_srcPageCount;
    for (const PdfPlanPage& p : std::as_const(m_plan))
        if (p.key >= m_nextPageKey)
            m_nextPageKey = p.key + 1;

    QSet<int> used;
    for (PdfPlanPage& p : m_plan) {
        if (p.isPristine())
            p.key = p.src;                      // Invariante der pristinen Seiten
        if (p.key < 0 || used.contains(p.key))
            p.key = m_nextPageKey++;
        used.insert(p.key);
    }
}

void PdfEditController::setSourcePageCount(int n) {
    if (n < 0) n = 0;
    m_srcPageCount = n;
    if (m_plan.isEmpty()) {
        m_plan.resize(n);
        for (int i = 0; i < n; ++i)
            m_plan[i] = PdfPlanPage{ i, 0, 0, i };  // Identitäts-Plan
        m_nextPageKey = n;
    } else {
        // Seiten ausserhalb [0,n) verwerfen - die Datei hat sich hinter unserem Ruecken geaendert.
        const int assetPages = QFile::exists(assetPath(m_docPath))
                                   ? PdfAssembler::probePageCount(assetPath(m_docPath))
                                   : 0;
        QVector<PdfPlanPage> v;
        v.reserve(m_plan.size());
        for (const PdfPlanPage& p : std::as_const(m_plan)) {
            if (p.isBlank())                              v.append(p);
            else if (p.doc == 1 && p.src < assetPages)    v.append(p);
            else if (p.doc == 0 && p.src < n)             v.append(p);
        }
        if (v.isEmpty())
            for (int i = 0; i < n; ++i)
                v.append(PdfPlanPage{ i, 0, 0, i });
        m_plan = v;
        assignPlanKeys();
    }
    emit planChanged();
    // Arbeitsdatei erzeugen, damit die Anzeige sie sofort rendern kann.
    if (!planIsIdentity()) {
        bakeWorking();
        emit pageStructureChanged();
    }
}

int PdfEditController::viewPageKey(int viewIndex) const {
    return keyOfView(viewIndex);
}

int PdfEditController::viewSourcePage(int viewIndex) const {
    return srcOfView(viewIndex);
}

int PdfEditController::pageRotation(int viewIndex) const {
    return (viewIndex >= 0 && viewIndex < m_plan.size())
               ? m_plan.at(viewIndex).rot : 0;
}

QVariantMap PdfEditController::pageInfo(int viewIndex) const {
    QVariantMap m;
    if (viewIndex < 0 || viewIndex >= m_plan.size()) {
        m.insert(QStringLiteral("exists"), false);
        return m;
    }
    const PdfPlanPage& p = m_plan.at(viewIndex);
    m.insert(QStringLiteral("exists"),   true);
    m.insert(QStringLiteral("key"),      p.key);
    m.insert(QStringLiteral("src"),      p.src);
    m.insert(QStringLiteral("doc"),      p.doc);
    m.insert(QStringLiteral("rot"),      p.rot);
    m.insert(QStringLiteral("blank"),    p.isBlank());
    m.insert(QStringLiteral("imported"), p.isImported());
    // Caret setzt eine ungedrehte Seite des Hauptdokuments voraus: die Ops
    // adressieren Glyphen der pristinen Datei.
    m.insert(QStringLiteral("textEditable"), p.isPristine() && p.rot == 0);
    return m;
}

void PdfEditController::addBlankPageAfter(int viewIndex) {
    if (m_docPath.isEmpty())
        return;
    int pos = viewIndex + 1;
    if (pos < 0)              pos = 0;
    if (pos > m_plan.size())  pos = m_plan.size();
    QVector<PdfPlanPage> next = m_plan;
    next.insert(pos, PdfPlanPage{ -1, 0, 0, m_nextPageKey++ });
    m_structureFocus = pos;                     // die NEUE Seite soll man sehen
    pushCommand(new PdfEditPagePlanCommand(this, m_plan, next));
}

void PdfEditController::removePage(int viewIndex) {
    if (m_docPath.isEmpty() || viewIndex < 0 || viewIndex >= m_plan.size())
        return;
    if (m_plan.size() <= 1)
        return;                                 // mindestens eine Seite bleibt
    QVector<PdfPlanPage> next = m_plan;
    next.removeAt(viewIndex);
    // Nach dem Entfernen rueckt die folgende Seite auf diesen Platz.
    m_structureFocus = qMin(viewIndex, next.size() - 1);
    pushCommand(new PdfEditPagePlanCommand(this, m_plan, next));
}

void PdfEditController::movePage(int from, int to) {
    if (m_docPath.isEmpty() || from < 0 || from >= m_plan.size())
        return;
    if (to < 0)               to = 0;
    if (to >= m_plan.size())  to = m_plan.size() - 1;
    if (to == from)
        return;
    QVector<PdfPlanPage> next = m_plan;
    next.move(from, to);
    m_structureFocus = to;                      // die verschobene Seite bleibt im Blick
    pushCommand(new PdfEditPagePlanCommand(this, m_plan, next));
}

void PdfEditController::rotatePage(int viewIndex, int deltaDeg,
                                   qreal pageWPt, qreal pageHPt) {
    if (m_docPath.isEmpty() || viewIndex < 0 || viewIndex >= m_plan.size())
        return;
    int delta = ((deltaDeg % 360) + 360) % 360;
    delta = (delta / 90) * 90;
    if (delta == 0)
        return;
    if (pageWPt <= 0.0) pageWPt = 595.276;
    if (pageHPt <= 0.0) pageHPt = 841.890;

    finishOpenSessions();
    finishDrawSession();

    QVector<PdfPlanPage> next = m_plan;
    PdfPlanPage& e = next[viewIndex];
    e.rot = (e.rot + delta) % 360;

    // Die Notizen drehen mit - sonst stuende eine Anmerkung quer zum Inhalt.
    // Geometrie und Plan werden ein Undo-Schritt.
    const int key = m_plan.at(viewIndex).key;
    const QVector<PdfEditBox> boxes = m_model.boxes();
    QVector<int> affected;
    for (const PdfEditBox& b : boxes)
        if (b.page == key)
            affected.append(b.id);

    const bool macro = !affected.isEmpty();
    if (macro)
        m_stack.beginMacro(QStringLiteral("rotatePage"));
    pushCommand(new PdfEditPagePlanCommand(this, m_plan, next));
    if (macro) {
        // Nach jedem Viertel tauschen Breite und Hoehe die Rolle; exakt und damit
        // verlustfrei umkehrbar.
        for (int id : std::as_const(affected)) {
            const PdfEditBox* b = m_model.boxById(id);
            if (!b)
                continue;
            QRectF r = b->rect;
            QVector<QPointF> pts = b->points;
            qreal w = pageWPt, h = pageHPt;
            for (int step = 0; step < delta / 90; ++step) {
                // 90 Grad im Uhrzeigersinn, Ursprung oben links: (x|y) -> (h-y|x).
                const QRectF nr(h - (r.y() + r.height()), r.x(),
                                r.height(), r.width());
                for (QPointF& p : pts)
                    p = QPointF(h - p.y(), p.x());
                r = nr;
                std::swap(w, h);
            }
            pushCommand(new PdfEditGeometryCommand(&m_model, id,
                                                   b->page, b->rect, b->points,
                                                   b->page, r, pts));
        }
        m_stack.endMacro();
    }
}

void PdfEditController::insertPagesFrom(const QString& pathOrUrl,
                                        const QVariantList& pages,
                                        int afterViewIndex) {
    if (m_docPath.isEmpty()) {
        emit pagesInserted(0, QStringLiteral("nodoc"));
        return;
    }
    const QString src = mg::toLocalPath(pathOrUrl);
    if (src.isEmpty() || !QFile::exists(src)) {
        emit pagesInserted(0, QStringLiteral("nofile"));
        return;
    }
    const int srcPages = PdfAssembler::probePageCount(src);
    if (srcPages <= 0) {
        emit pagesInserted(0, QStringLiteral("unreadable"));
        return;
    }

    // addSourcePages erwartet je Aufruf eine aufsteigende Liste ohne Dubletten.
    QVector<int> want;
    if (pages.isEmpty()) {
        want.reserve(srcPages);
        for (int i = 0; i < srcPages; ++i)
            want.append(i);
    } else {
        QSet<int> seen;
        for (const QVariant& v : pages) {
            bool okNum = false;
            const int p = v.toInt(&okNum);
            if (okNum && p >= 0 && p < srcPages && !seen.contains(p)) {
                seen.insert(p);
                want.append(p);
            }
        }
        std::sort(want.begin(), want.end());
    }
    if (want.isEmpty()) {
        emit pagesInserted(0, QStringLiteral("nopages"));
        return;
    }

    // Die Begleitdatei ist bewusst append-only: die Plan-Eintraege verweisen ueber
    // feste Indizes hinein, die sich dadurch nie verschieben.
    const QString asset  = assetPath(m_docPath);
    const int     before = QFile::exists(asset) ? PdfAssembler::probePageCount(asset) : 0;

    QString err;
    {
        QSaveFile out(asset);
        if (!out.open(QIODevice::WriteOnly)) {
            emit pagesInserted(0, QStringLiteral("asset"));
            return;
        }
        PdfAssembler asmbl(&out);
        bool ok = asmbl.begin(&err);
        if (ok && before > 0) {
            QVector<int> all;
            all.reserve(before);
            for (int i = 0; i < before; ++i)
                all.append(i);
            // QSaveFile schreibt in eine temporaere Datei, die Quelle bleibt bis zum commit lesbar.
            ok = asmbl.addSourcePages(asset, all, &err);
        }
        ok = ok && asmbl.addSourcePages(src, want, &err);
        ok = ok && asmbl.finish(&err);
        if (!(ok && out.commit())) {
            out.cancelWriting();
            emit pagesInserted(0, err.isEmpty() ? QStringLiteral("copy") : err);
            return;
        }
    }

    int pos = afterViewIndex + 1;
    if (pos < 0)              pos = 0;
    if (pos > m_plan.size())  pos = m_plan.size();
    QVector<PdfPlanPage> next = m_plan;
    for (int i = 0; i < want.size(); ++i)
        next.insert(pos + i, PdfPlanPage{ before + i, 1, 0, m_nextPageKey++ });
    m_structureFocus = pos;                     // die erste eingefügte Seite
    pushCommand(new PdfEditPagePlanCommand(this, m_plan, next));
    emit pagesInserted(want.size(), QString());
}

int PdfEditController::probePageCount(const QString& pathOrUrl) const {
    const QString p = mg::toLocalPath(pathOrUrl);
    if (p.isEmpty() || !QFile::exists(p))
        return -1;
    return PdfAssembler::probePageCount(p);
}

void PdfEditController::applyPlan(const QVector<PdfPlanPage>& plan) {
    m_plan = plan;
    // Keys werden nicht neu vergeben - nur der Zaehler bleibt oberhalb, damit Redo
    // nach einem Undo keine Kollision erzeugt.
    for (const PdfPlanPage& p : std::as_const(m_plan))
        if (p.key >= m_nextPageKey)
            m_nextPageKey = p.key + 1;
    emit planChanged();
    bakeWorking();                              // Arbeitsdatei + Reload (beide Modi)
    emit pageStructureChanged();                // Vorschauleiste neu rendern
}

QVector<PdfEditBox> PdfEditController::exportBoxes() const {
    // page traegt intern den stabilen Key, die Ausgabe zaehlt Ansichts-Seiten.
    QVector<PdfEditBox> out;
    const QVector<PdfEditBox> boxes = m_model.boxes();
    out.reserve(boxes.size());
    QHash<int, int> keyToView;
    keyToView.reserve(m_plan.size());
    for (int i = 0; i < m_plan.size(); ++i)
        keyToView.insert(m_plan.at(i).key, i);
    for (const PdfEditBox& b : boxes) {
        const int vi = keyToView.value(b.page, -1);
        if (vi < 0)
            continue;
        // Unveraendert uebernommene Annotation steht schon in der Datei - nochmal
        // zeichnen hiesse verdoppeln.
        if (b.srcObjNum > 0 && !importChanged(b))
            continue;
        PdfEditBox c = b;
        c.page = vi;
        out.append(c);
    }
    return out;
}

QString PdfEditController::planSourceFile(int doc, bool preferTextWork) const {
    if (doc == 1)
        return assetPath(m_docPath);            // importierte Seiten
    if (preferTextWork && m_textWorkValid && QFile::exists(textWorkPath(m_docPath)))
        return textWorkPath(m_docPath);         // bearbeitete Textebene
    return pristinePath();
}

void PdfEditController::bakeWorking() {
    if (m_docPath.isEmpty())
        return;

    if (planIsIdentity()) {
        // Kein Plan mehr: die gebackene PDF wird durch die Sicherung ersetzt.
        const QString bak = backupPath(m_docPath);
        if (QFile::exists(bak)) {
            QFile::remove(m_docPath);
            QFile::copy(bak, m_docPath);
        }
        emit documentRewritten();
        return;
    }

    // Einmalige Sicherung - Quelle fuer Strg+Z, solange die Datei offen ist.
    const QString bak = backupPath(m_docPath);
    if (!QFile::exists(bak) && !QFile::copy(m_docPath, bak))
        return;                                 // ohne Sicherung nicht schreiben

    // planSourceFile liefert je Eintrag die Quelle: pristine bzw. Textebenen-
    // Arbeitsdatei oder die Begleitdatei der importierten Seiten.
    QString err;
    if (!assemblePlanTo(m_docPath, QString(), &err)) {
        qWarning("PdfEditController::bakeWorking: %s", qPrintable(err));
        return;
    }
    emit documentRewritten();
}

bool PdfEditController::assemblePlanTo(const QString& targetPath,
                                       const QString& sourceOverride, QString* err) {
    const QSizeF a4(595.276, 841.890);
    QSaveFile out(targetPath);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    QString e;
    PdfAssembler asmbl(&out);
    bool ok = asmbl.begin(&e);

    // Aufeinanderfolgende aufsteigende Seiten derselben Quelle werden gebuendelt.
    int runDoc = -2;
    QVector<int> runPages;
    QVector<int> runRots;
    auto flush = [&]() -> bool {
        if (runPages.isEmpty())
            return true;
        // sourceOverride ersetzt nur das Hauptdokument (doc 0).
        const QString from = (!sourceOverride.isEmpty() && runDoc == 0)
                                 ? sourceOverride : planSourceFile(runDoc, true);
        const bool r = asmbl.addSourcePages(from, runPages, runRots, &e);
        runPages.clear();
        runRots.clear();
        return r;
    };
    for (int i = 0; ok && i < m_plan.size(); ++i) {
        const PdfPlanPage& p = m_plan.at(i);
        if (p.isBlank()) {
            ok = flush() && asmbl.addBlankPage(a4, &e);
            runDoc = -2;
            continue;
        }
        if (p.doc != runDoc || (!runPages.isEmpty() && p.src <= runPages.last())) {
            ok = flush();
            if (!ok) break;
            runDoc = p.doc;
        }
        runPages.append(p.src);
        runRots.append(p.rot);
    }
    ok = ok && flush();
    ok = ok && asmbl.finish(&e);
    if (ok && out.commit())
        return true;
    out.cancelWriting();                        // pristine bleibt unversehrt
    if (err) *err = e;
    return false;
}

// Caret - direktes Bearbeiten der eingebetteten Textebene. m_caretGlyphs ist
// stets das Layout des Textes, den der Nutzer gerade sieht; zwischen zwei
// Neubauten wird es lokal fortgeschrieben (Naeherung).
QString PdfEditController::textWorkPath(const QString& pdfPath) {
    return pdfPath + QStringLiteral(".mgtext.pdf");
}

QString PdfEditController::textSourcePath() const {
    if (!m_docPath.isEmpty() && m_textWorkValid) {
        const QString w = textWorkPath(m_docPath);
        if (QFile::exists(w))
            return w;
    }
    return pristinePath();
}

void PdfEditController::setPendingValid(bool v) {
    if (m_pendingValid == v)
        return;
    m_pendingValid = v;
    emit undoStateChanged();
    emit dirtyChanged();
}

QVector<PdfTextOp> PdfEditController::effectiveTextOps() const {
    QVector<PdfTextOp> v = m_textOps;
    if (m_pendingValid)
        v.append(m_pending);
    return v;
}

bool PdfEditController::textRebuildNeeded() const {
    return effectiveTextOps() != m_builtOps;
}

QRectF PdfEditController::caretRectPt() const {
    if (m_caretPage < 0 || m_caretIndex < 0 || m_caretGlyphs.isEmpty())
        return {};
    // Der Index darf hinter der Naeherung liegen - dann zeigt das Caret ans Ende,
    // statt zu verschwinden.
    const int idx = qBound(0, m_caretIndex, m_caretGlyphs.size());
    return mg::PdfTextLayout::caretRect(m_caretGlyphs, idx);
}

void PdfEditController::requestCaretLayout(int page) {
    if (m_docPath.isEmpty() || page < 0)
        return;
    // Das Layout kommt aus der Quelldatei - bei geaendertem Plan eine andere Nummer.
    const int src = m_plan.isEmpty() ? page : srcOfView(page);
    if (src < 0)
        return;
    m_pool.start(new PdfCaretLayoutTask(this, textSourcePath(), src, page, ++m_caretGen));
}

int PdfEditController::caretSrcPage() const {
    if (m_caretPage < 0)
        return -1;
    return m_plan.isEmpty() ? m_caretPage : srcOfView(m_caretPage);
}

bool PdfEditController::pageTextEditable(int viewIndex) const {
    // Leerseiten, importierte und gedrehte Seiten sind ausgenommen: die Ops
    // adressieren Glyphen der pristinen Datei.
    if (m_plan.isEmpty())
        return viewIndex >= 0;                  // Plan noch nicht gemeldet
    if (viewIndex < 0 || viewIndex >= m_plan.size())
        return false;
    const PdfPlanPage& p = m_plan.at(viewIndex);
    return p.isPristine() && p.rot == 0;
}

void PdfEditController::prepareCaretPage(int page) {
    if (m_docPath.isEmpty() || page < 0)
        return;
    if (!pageTextEditable(page)) {
        clearCaret();
        m_caretError = QStringLiteral("pagenotext");
        emit caretReadyChanged();
        return;
    }
    if (page == m_caretPage && (m_caretReady || m_caretGen > 0))
        return;                                 // läuft bereits/liegt vor
    commitPendingTextOp();
    m_caretPage  = page;
    m_caretIndex = -1;
    m_caretGlyphs.clear();
    m_caretReady = false;
    m_caretError.clear();
    emit caretReadyChanged();
    emit caretChanged();
    requestCaretLayout(page);
}

void PdfEditController::caretLayoutFinished(int page, const QVector<mg::PdfGlyph>& glyphs,
                                            const QString& err, int generation) {
    if (generation != m_caretGen || page != m_caretPage)
        return;                                 // veraltet (Seite gewechselt)
    m_caretGlyphs = glyphs;
    m_caretReady  = !glyphs.isEmpty();
    // Drei Ausgaenge: gelesen, gelesen ohne Text, nicht lesbar. Der mittlere lief
    // als leerer Fehlertext durch, und der Klick wirkte verschluckt.
    if (m_caretReady)
        m_caretError.clear();
    else
        m_caretError = err.isEmpty() ? QStringLiteral("pagenotext_empty") : err;
    if (!m_caretReady) {
        qInfo("PdfEditController: Seite %d nicht zeichenweise bearbeitbar - %s",
              page, qPrintable(m_caretError));
    }
    if (m_caretReady)
        m_caretIndex = qBound(0, m_caretIndex, m_caretGlyphs.size());
    else
        m_caretIndex = -1;
    if (m_caretReady && m_caretHitPending) {
        m_caretHitPending = false;
        const QPointF p = m_caretHitPt;
        m_caretIndex = hitIndexAt(p);
    }
    emit caretReadyChanged();
    emit caretChanged();
}

// hitTest liefert das naechstliegende Zeichen; die getroffene Haelfte
// entscheidet, ob das Caret davor oder dahinter gehoert.
int PdfEditController::hitIndexAt(const QPointF& ptPt) const {
    const int g = mg::PdfTextLayout::hitTest(m_caretGlyphs, ptPt);
    if (g < 0)
        return 0;
    const QRectF b = m_caretGlyphs.at(g).box;
    return (ptPt.x() > b.center().x()) ? g + 1 : g;
}

void PdfEditController::placeCaret(int page, qreal xPt, qreal yPt) {
    if (m_docPath.isEmpty() || page < 0)
        return;
    if (!pageTextEditable(page)) {
        clearCaret();
        m_caretError = QStringLiteral("pagenotext");
        emit caretReadyChanged();
        return;
    }
    commitPendingTextOp();                      // Klick beendet die Tipp-Session
    if (page != m_caretPage) {
        m_caretPage  = page;
        m_caretIndex = -1;
        m_caretGlyphs.clear();
        m_caretReady = false;
        m_caretError.clear();
        m_caretHitPt      = QPointF(xPt, yPt);
        m_caretHitPending = true;
        emit caretReadyChanged();
        emit caretChanged();
        requestCaretLayout(page);
        return;
    }
    if (!m_caretReady) {                        // Layout noch unterwegs
        m_caretHitPt      = QPointF(xPt, yPt);
        m_caretHitPending = true;
        return;
    }
    setCaretIndex(hitIndexAt(QPointF(xPt, yPt)));
}

void PdfEditController::setCaretIndex(int idx) {
    const int n = m_caretGlyphs.size();
    idx = qBound(0, idx, n);
    if (idx == m_caretIndex)
        return;
    m_caretIndex = idx;
    emit caretChanged();
}

void PdfEditController::moveCaret(int delta) {
    if (!m_caretReady || m_caretIndex < 0)
        return;
    commitPendingTextOp();                      // Pfeiltaste beendet die Session
    setCaretIndex(m_caretIndex + (delta < 0 ? -1 : 1));
}

// Glyphen derselben Grundlinie haben dieselbe Oberkante; Toleranz halbe Zeilenhoehe.
bool PdfEditController::sameLine(const mg::PdfGlyph& a, const mg::PdfGlyph& b) {
    const qreal tol = qMax(qreal(1.0), qMin(a.box.height(), b.box.height()) * 0.5);
    return qAbs(a.box.top() - b.box.top()) <= tol;
}

void PdfEditController::caretHome() {
    if (!m_caretReady || m_caretIndex < 0 || m_caretGlyphs.isEmpty())
        return;
    commitPendingTextOp();
    int i = qBound(0, m_caretIndex, m_caretGlyphs.size() - 1);
    while (i > 0 && sameLine(m_caretGlyphs.at(i), m_caretGlyphs.at(i - 1)))
        --i;
    setCaretIndex(i);
}

void PdfEditController::caretEnd() {
    if (!m_caretReady || m_caretIndex < 0 || m_caretGlyphs.isEmpty())
        return;
    commitPendingTextOp();
    const int n = m_caretGlyphs.size();
    int i = qBound(0, m_caretIndex, n - 1);
    while (i + 1 < n && sameLine(m_caretGlyphs.at(i), m_caretGlyphs.at(i + 1)))
        ++i;
    setCaretIndex(i + 1);                       // hinter das letzte Zeichen
}

void PdfEditController::moveCaretLine(int delta) {
    if (!m_caretReady || m_caretIndex < 0 || m_caretGlyphs.isEmpty())
        return;
    commitPendingTextOp();
    const int n   = m_caretGlyphs.size();
    const int cur = qBound(0, m_caretIndex, n - 1);
    const QRectF curBox = m_caretGlyphs.at(cur).box;
    const qreal  wantX  = (m_caretIndex >= n) ? curBox.right() : curBox.left();
    const int    step   = (delta < 0) ? -1 : 1;

    int i = cur;
    while (i + step >= 0 && i + step < n
           && sameLine(m_caretGlyphs.at(i + step), m_caretGlyphs.at(cur)))
        i += step;
    i += step;
    if (i < 0 || i >= n)
        return;                                 // keine weitere Zeile
    int best = i;
    qreal bestD = qAbs(m_caretGlyphs.at(i).box.left() - wantX);
    int j = i;
    while (j + step >= 0 && j + step < n
           && sameLine(m_caretGlyphs.at(j + step), m_caretGlyphs.at(i))) {
        j += step;
        const qreal d = qAbs(m_caretGlyphs.at(j).box.left() - wantX);
        if (d < bestD) { bestD = d; best = j; }
    }
    setCaretIndex(best);
}

void PdfEditController::clearCaret() {
    commitPendingTextOp();
    if (m_caretPage < 0 && m_caretIndex < 0 && m_caretGlyphs.isEmpty())
        return;
    m_caretPage  = -1;
    m_caretIndex = -1;
    m_caretGlyphs.clear();
    m_caretGlyphs.squeeze();                    // RAM sofort zurückgeben
    m_caretReady = false;
    m_caretError.clear();
    m_caretHitPending = false;
    emit caretReadyChanged();
    emit caretChanged();
}

void PdfEditController::spliceGlyphsInsert(int index, const QString& text) {
    if (text.isEmpty())
        return;
    const int n = m_caretGlyphs.size();
    index = qBound(0, index, n);
    // Ohne Nachbarn ist keine sinnvolle Naeherung moeglich.
    const int refIdx = (index > 0) ? index - 1 : (n > 0 ? 0 : -1);
    if (refIdx < 0)
        return;
    const mg::PdfGlyph ref = m_caretGlyphs.at(refIdx);
    const qreal advance = (ref.box.width() > 0.0) ? ref.box.width()
                                                  : qMax(qreal(1.0), ref.fontSizePt * 0.5);
    qreal x = (index > 0) ? ref.box.right() : ref.box.left();

    QVector<mg::PdfGlyph> add;
    add.reserve(text.size());
    for (const QChar ch : text) {
        mg::PdfGlyph g = ref;
        g.ch  = ch;
        g.box = QRectF(x, ref.box.top(), advance, ref.box.height());
        x += advance;
        add.append(g);
    }
    // Folgeglyphen derselben Zeile mitschieben, sonst ueberlagert der neue Text den alten.
    const qreal shift = advance * text.size();
    for (int i = index; i < n; ++i) {
        if (!sameLine(m_caretGlyphs.at(i), ref))
            break;
        m_caretGlyphs[i].box.translate(shift, 0.0);
    }
    for (int i = 0; i < add.size(); ++i)
        m_caretGlyphs.insert(index + i, add.at(i));
}

void PdfEditController::spliceGlyphsRemove(int index, int count) {
    const int n = m_caretGlyphs.size();
    if (index < 0 || count <= 0 || index >= n)
        return;
    count = qMin(count, n - index);
    qreal shift = 0.0;
    for (int i = index; i < index + count; ++i)
        shift += m_caretGlyphs.at(i).box.width();
    const mg::PdfGlyph ref = m_caretGlyphs.at(index);
    for (int i = index + count; i < n; ++i) {
        if (!sameLine(m_caretGlyphs.at(i), ref))
            break;
        m_caretGlyphs[i].box.translate(-shift, 0.0);
    }
    m_caretGlyphs.remove(index, count);
}

void PdfEditController::insertAtCaret(const QString& text) {
    if (!m_caretReady || m_caretPage < 0 || m_caretIndex < 0 || text.isEmpty())
        return;
    // Die Ops adressieren die Quellseite, nicht die Ansichts-Seite.
    const int srcPage = caretSrcPage();
    if (srcPage < 0)
        return;
    const int at = qBound(0, m_caretIndex, m_caretGlyphs.size());
    // Fortlaufendes Tippen sammelt sich in einer Op - ein Undo-Schritt.
    if (m_pendingValid && m_pending.page == srcPage && m_pending.isInsert()
        && m_pending.index + m_pending.text.size() == at) {
        m_pending.text += text;
    } else {
        commitPendingTextOp();
        m_pending = PdfTextOp{ srcPage, at, text, 0 };
        setPendingValid(true);
    }
    spliceGlyphsInsert(at, text);
    m_caretIndex = at + text.size();
    emit caretChanged();
    scheduleTextRebuild();
}

void PdfEditController::deleteAtCaret(int dir) {
    if (!m_caretReady || m_caretPage < 0 || m_caretIndex < 0)
        return;
    const int srcPage = caretSrcPage();          // s. insertAtCaret
    if (srcPage < 0)
        return;
    const int n  = m_caretGlyphs.size();
    const int at = (dir < 0) ? m_caretIndex - 1 : m_caretIndex;
    if (at < 0 || at >= n)
        return;
    const QString removed(m_caretGlyphs.at(at).ch);

    const bool contBack = m_pendingValid && !m_pending.isInsert()
                          && m_pending.page == srcPage && dir < 0
                          && m_pending.index == at + 1;
    const bool contFwd  = m_pendingValid && !m_pending.isInsert()
                          && m_pending.page == srcPage && dir >= 0
                          && m_pending.index == at;
    if (contBack) {
        m_pending.index = at;
        m_pending.text.prepend(removed);
        m_pending.removed += 1;
    } else if (contFwd) {
        m_pending.text.append(removed);
        m_pending.removed += 1;
    } else {
        commitPendingTextOp();
        m_pending = PdfTextOp{ srcPage, at, removed, 1 };
        setPendingValid(true);
    }
    spliceGlyphsRemove(at, 1);
    m_caretIndex = at;
    emit caretChanged();
    scheduleTextRebuild();
}

// Die schwebende Op wird erst zum Kommando, wenn der Neubau sie akzeptiert hat.
void PdfEditController::commitPendingTextOp() {
    if (!m_pendingValid)
        return;
    if (m_builtOps.size() == m_textOps.size() + 1 && m_builtOps.constLast() == m_pending) {
        const PdfTextOp op = m_pending;
        setPendingValid(false);
        m_pendingCommit = false;
        pushCommand(new PdfEditTextOpCommand(this, op));   // redo() hängt sie an
        return;
    }
    m_pendingCommit = true;
    startTextRebuild();                         // sofort, nicht entprellt
}

void PdfEditController::applyTextOp(const PdfTextOp& op) {
    m_textOps.append(op);
    emit textOpsChanged();
    scheduleTextRebuild();                      // No-Op, wenn nichts fehlt
}

void PdfEditController::revokeLastTextOp() {
    if (m_textOps.isEmpty())
        return;
    m_textOps.removeLast();
    setPendingValid(false);                     // Undo verwirft Schwebendes
    emit textOpsChanged();
    startTextRebuild();
}

void PdfEditController::scheduleTextRebuild() {
    emit textOpsChanged();
    if (textRebuildNeeded())
        m_textFlush.start();
}

void PdfEditController::startTextRebuild() {
    m_textFlush.stop();
    if (m_docPath.isEmpty())
        return;
    if (m_textOpsBusy) {                        // Läufer abwarten, danach erneut
        m_textFlush.start();
        return;
    }
    const QVector<PdfTextOp> ops = effectiveTextOps();
    if (ops == m_builtOps) {
        if (m_pendingCommit)
            commitPendingTextOp();
        resumePendingExport();
        return;
    }
    if (ops.isEmpty()) {                        // alles zurückgenommen
        QFile::remove(textWorkPath(m_docPath));
        m_textWorkValid = false;
        m_builtOps.clear();
        afterTextRebuild();
        return;
    }
    m_buildingOps = ops;
    m_textOpsBusy = true;
    emit textOpsBusyChanged();
    m_pool.start(new PdfTextOpsTask(this, pristinePath(), textWorkPath(m_docPath),
                                    ops, ++m_textOpsGen));
}

void PdfEditController::textOpsTaskFinished(bool ok, const QString& err, int generation,
                                            int caretTo, int caretPage, bool overflow) {
    if (generation != m_textOpsGen)
        return;                                 // veraltet
    m_textOpsBusy = false;
    emit textOpsBusyChanged();

    if (ok) {
        m_builtOps      = m_buildingOps;
        m_textWorkValid = true;
        m_textOpsLoaded = false;                // geladene Ops sind bestätigt
        // Der Umbruch hat Zeichen verschoben, die Schreibmarke wandert mit - nur wenn
        // seither nicht weitergetippt wurde.
        if (caretTo >= 0 && caretPage >= 0 && caretPage == caretSrcPage()
            && !textRebuildNeeded())
            setCaretIndex(caretTo);
        if (overflow)
            emit reflowOverflow();
        if (m_pendingCommit)
            commitPendingTextOp();
    } else {
        // Nicht schreibbar: die schwebende Op wird verworfen, der Stack kennt sie noch nicht.
        m_pendingCommit = false;
        if (m_pendingValid) {
            setPendingValid(false);
        } else if (m_textOpsLoaded) {
            // Defekte Sidecar-Ops verwerfen - der Undo-Stack ist beim Laden leer.
            m_textOps.clear();
            m_textOpsLoaded = false;
        } else if (!m_textOps.isEmpty()) {
            m_textOps.removeLast();
        }
        emit textOpsChanged();
        emit textEditFailed(err);
    }
    m_buildingOps.clear();
    afterTextRebuild();
}

// Ist die Textebene noch nicht materialisiert, wird der Neubau angestossen und
// der Export in resumePendingExport fortgesetzt.
bool PdfEditController::flushTextForExport(int kind) {
    commitPendingTextOp();
    if (!textRebuildNeeded() && !m_textOpsBusy)
        return true;
    m_exportPending = kind;
    startTextRebuild();
    return false;
}

void PdfEditController::resumePendingExport() {
    if (m_exportPending == 0 || m_busy)
        return;
    const int kind = m_exportPending;
    m_exportPending = 0;
    if (kind == 2) exportContentEdited();
    else           exportPdf();
}

void PdfEditController::afterTextRebuild() {
    if (planIsIdentity())
        emit documentRewritten();
    else
        bakeWorking();
    if (m_caretPage >= 0)
        requestCaretLayout(m_caretPage);
    if (textRebuildNeeded())
        m_textFlush.start();
    else
        resumePendingExport();
}

void PdfEditController::setDocument(const QString& pathOrUrl) {
    const QString local = mg::toLocalPath(pathOrUrl);
    if (local == m_docPath)
        return;                                     // idempotent

    finishOpenSessions();
    finishDrawSession();
    // Auto-Sicherung des vorherigen Dokuments; gepufferte Formularwerte zaehlen dazu,
    // sie stehen noch in keiner PDF.
    if (!m_docPath.isEmpty()) {
        commitPendingTextOp();
        const bool hadPlan = !planIsIdentity();
        consumePlan();
        if (hadPlan || !m_stack.isClean() || !m_formEdits.isEmpty())
            saveOverlay();
    }

    if (!m_docPath.isEmpty()) {
        QFile::remove(previewPath(m_docPath));  // Altlast früherer Fassungen
        QFile::remove(textWorkPath(m_docPath)); // Textebenen-Arbeitsdatei ebenso
    }

    m_docPath = local;
    setSelectedId(-1);
    setTool(Select);
    // Der Aufzeichnungs-Schalter gehoert zum Dokument - ohne Zuruecksetzen zeichnete
    // eine Datei ohne Sidecar den Zustand der vorherigen weiter auf.
    if (m_recording) { m_recording = false; emit recordingChanged(); }
    m_stack.clear();                                // setzt zugleich auf „clean"
    m_model.clearAll();
    m_nextId = 1;
    m_plan.clear();                                 // Seiten-Plan (Aufgabe 3)
    m_srcPageCount = 0;
    resetTextState();
    // Laufendes Lesen wird ueber die Generationszahl verworfen.
    ++m_formReadGen;
    ++m_annotReadGen;                               // laufenden Annotations-Lauf verwerfen
    m_importBaseline.clear();
    m_formFields.clear();
    m_formEdits.clear();
    setFormDirty(false);
    ++m_formValueRev;
    emit formValueRevChanged();
    emit formFieldsChanged();

    if (!m_docPath.isEmpty()) {
        loadOverlay(m_docPath);                     // lädt auch gepufferte Feldwerte
        startFormRead();                            // Felder der pristinen Datei
        startAnnotRead();                           // vorhandene Annotationen übernehmen
        // Geladene Textebenen-Ops kommen als Kommandos auf den Stack: anders als Notizen
        // liesse sich eine Textebenen-Aenderung sonst nicht mehr entfernen.
        if (!m_textOps.isEmpty()) {
            const QVector<PdfTextOp> loaded = m_textOps;
            m_textOps.clear();
            for (const PdfTextOp& op : loaded)
                m_stack.push(new PdfEditTextOpCommand(this, op));
            m_stack.setClean();
            startTextRebuild();
        }
    }
}

void PdfEditController::resetTextState() {
    m_textFlush.stop();
    ++m_textOpsGen;                                 // laufenden Neubau verwerfen
    ++m_caretGen;                                   // laufendes Layout verwerfen
    m_textOps.clear();
    m_builtOps.clear();
    m_buildingOps.clear();
    setPendingValid(false);
    m_pendingCommit = false;
    m_textWorkValid = false;
    m_textOpsLoaded = false;
    m_textOpsBusy   = false;
    m_exportPending = 0;
    m_caretGlyphs.clear();
    m_caretGlyphs.squeeze();
    m_caretPage  = -1;
    m_caretIndex = -1;
    m_caretReady = false;
    m_caretError.clear();
    m_caretHitPending = false;
    emit textOpsChanged();
    emit textOpsBusyChanged();
    emit caretReadyChanged();
    emit caretChanged();
}

// Beim Schliessen ist die Datei fertig - der Plan beschreibt nur noch, wie sie entstand.
void PdfEditController::consumePlan() {
    if (m_docPath.isEmpty())
        return;

    if (!planIsIdentity()) {
        // Nach dem Verbrauchen ist die Ansichtsreihenfolge die Dateireihenfolge.
        QHash<int, int> keyToNew;
        for (int i = 0; i < m_plan.size(); ++i)
            keyToNew.insert(m_plan.at(i).key, i);

        QVector<PdfEditBox> boxes = m_model.boxes();
        for (PdfEditBox& b : boxes)
            b.page = keyToNew.value(b.page, -1);
        boxes.removeIf([](const PdfEditBox& b) { return b.page < 0; });
        m_model.resetBoxes(boxes);

        // Die gebackene PDF traegt die Ops bereits - ein zweites Anwenden verdoppelte den Text.
        m_textOps.clear();
        m_textOpsLoaded = false;
        m_builtOps.clear();
        setPendingValid(false);
        m_textWorkValid = false;

        // Ab jetzt gilt: Ansichtsseite == Dateiseite.
        const int n = m_plan.size();
        m_plan.resize(n);
        for (int i = 0; i < n; ++i)
            m_plan[i] = PdfPlanPage{ i, 0, 0, i };
        m_srcPageCount = n;
        m_nextPageKey  = n;
        emit planChanged();
    }

    QFile::remove(backupPath(m_docPath));
    QFile::remove(assetPath(m_docPath));
}

void PdfEditController::releaseDocument() {
    if (m_docPath.isEmpty())
        return;
    finishOpenSessions();
    finishDrawSession();
    commitPendingTextOp();                      // Tipp-Session abschließen
    // Erst verbrauchen, dann speichern: das Sidecar soll die neuen Keys sehen.
    const bool hadPlan = !planIsIdentity();
    consumePlan();
    if (hadPlan || !m_stack.isClean() || !m_formEdits.isEmpty())
        saveOverlay();
    QFile::remove(previewPath(m_docPath));      // Altlast früherer Fassungen
    // Die Textebenen-Arbeitsdatei ist ableitbar und bleibt nicht liegen.
    QFile::remove(textWorkPath(m_docPath));
    resetTextState();
    m_docPath.clear();
    setSelectedId(-1);
    setTool(Select);
    m_stack.clear();
    m_model.clearAll();
    m_nextId = 1;
    m_plan.clear();
    m_srcPageCount = 0;
    ++m_formReadGen;                            // laufendes Lesen verwerfen
    ++m_annotReadGen;
    m_importBaseline.clear();
    m_formFields.clear();
    m_formEdits.clear();
    setFormDirty(false);
    ++m_formValueRev;
    emit formValueRevChanged();
    emit formFieldsChanged();
}

int PdfEditController::addTextBox(int page, qreal xPt, qreal yPt,
                                  qreal pageWPt, qreal pageHPt) {
    if (m_docPath.isEmpty() || page < 0)
        return -1;
    // Gespeichert wird der stabile Seiten-Key, damit die Notiz beim Umsortieren bleibt.
    const int key = pageKeyForView(page);
    if (key < 0)
        return -1;
    if (pageWPt <= 0.0) pageWPt = 612.0;
    if (pageHPt <= 0.0) pageHPt = 792.0;

    PdfEditBox b = seededBox();                     // Stil der letzten Notiz erben
    b.id   = m_nextId++;
    b.page = key;
    // Standardgroesse ~1/3 Seitenbreite, zwei Zeilenhoehen, in die Seite geklemmt.
    const qreal w = qMin(qMax(90.0, pageWPt * 0.32), qMax(90.0, pageWPt - 16.0));
    const qreal h = qMax(kMinBoxHPt, b.fontSizePt * 2.0 + 2.0 * kBoxPaddingPt);
    const qreal x = qMax(2.0, qMin(xPt, qMax(2.0, pageWPt - w - 2.0)));
    const qreal y = qMax(2.0, qMin(yPt, qMax(2.0, pageHPt - h - 2.0)));
    b.rect = QRectF(x, y, w, h);

    pushAdd(b);
    setSelectedId(b.id);
    return b.id;
}

void PdfEditController::setMarkupStyle(int style) {
    style = qBound(0, style, 2);
    if (m_markupStyle == style)
        return;
    m_markupStyle = style;
    ++m_defaultRev;
    emit defaultRevChanged();
}


int PdfEditController::addStamp(const QString& pathOrUrl, int page,
                                qreal xPt, qreal yPt, qreal wPt) {
    if (m_docPath.isEmpty() || page < 0)
        return -1;
    const int key = pageKeyForView(page);
    if (key < 0)
        return -1;
    const QString local = mg::toLocalPath(pathOrUrl);
    if (local.isEmpty() || !QFile::exists(local))
        return -1;

    // Gelesen wird nur der Kopf der Datei (QImageReader::size), nicht das Bild.
    const QSize px = QImageReader(local).size();
    if (!px.isValid() || px.width() <= 0 || px.height() <= 0)
        return -1;

    PdfEditBox b;
    b.id        = m_nextId++;
    b.page      = key;
    b.kind      = PdfAnnKind::Stamp;
    b.imagePath = local;
    b.stroke    = QColor(0, 0, 0, 0);
    b.lineWidth = 0.0;
    b.fill      = QColor(0, 0, 0, 255);              // Alpha = Deckkraft des Bildes
    const qreal w = qBound(kMinBoxWPt, wPt > 0.0 ? wPt : 160.0, 2000.0);
    const qreal h = qMax(kMinBoxHPt, w * px.height() / double(px.width()));
    b.rect = QRectF(qMax(0.0, xPt), qMax(0.0, yPt), w, h);

    pushAdd(b);
    setSelectedId(b.id);
    return b.id;
}

int PdfEditController::addMarkup(int page, int style, const QVariantList& quads) {
    if (m_docPath.isEmpty() || page < 0 || quads.isEmpty())
        return -1;
    const int key = pageKeyForView(page);
    if (key < 0)
        return -1;
    style = qBound(0, style, 2);

    PdfEditBox b;
    b.id          = m_nextId++;
    b.page        = key;
    b.kind        = PdfAnnKind::Markup;
    b.markupStyle = style;
    b.stroke      = m_markupColors[style];
    b.lineWidth   = 0.0;                            // Markierungen haben keinen Rand

    // Entartete Rechtecke fliegen raus, damit kein unsichtbares Objekt entsteht.
    QRectF hull;
    for (const QVariant& v : quads) {
        const QVariantMap m = v.toMap();
        const QRectF r(m.value(QStringLiteral("x")).toDouble(),
                       m.value(QStringLiteral("y")).toDouble(),
                       m.value(QStringLiteral("w")).toDouble(),
                       m.value(QStringLiteral("h")).toDouble());
        if (r.width() <= 0.5 || r.height() <= 0.5)
            continue;
        b.points.push_back(r.topLeft());
        b.points.push_back(r.bottomRight());
        hull = hull.isNull() ? r : hull.united(r);
    }
    if (b.points.isEmpty())
        return -1;
    b.rect = hull;

    if (m_markupStyle != style) {
        m_markupStyle = style;
        ++m_defaultRev;
        emit defaultRevChanged();
    }

    pushAdd(b);
    setSelectedId(b.id);
    return b.id;
}

int PdfEditController::addAnchoredTextBox(int page, qreal xPt, qreal yPt,
                                          qreal wPt, qreal hPt) {
    if (m_docPath.isEmpty() || page < 0)
        return -1;
    const int key = pageKeyForView(page);           // s. addTextBox
    if (key < 0)
        return -1;

    PdfEditBox b = seededBox();                     // Stil der letzten Notiz erben
    b.id       = m_nextId++;
    b.page     = key;
    b.anchored = true;
    b.rect     = QRectF(qMax(0.0, xPt), qMax(0.0, yPt),
                        qMax(kMinBoxWPt, wPt), qMax(kMinBoxHPt, hPt));
    // Schriftgroesse aus der Zeilenhoehe (typografisch etwa 72 % der Zeile).
    b.fontSizePt = qBound(6.0, hPt * 0.72, 72.0);

    pushAdd(b);
    setSelectedId(b.id);
    return b.id;
}

int PdfEditController::beginDraw(int kind, int page, qreal xPt, qreal yPt) {
    if (m_docPath.isEmpty() || page < 0
        || kind < static_cast<int>(PdfAnnKind::Freehand)
        || kind > static_cast<int>(PdfAnnKind::Replace))
        return -1;
    finishOpenSessions();
    finishDrawSession();

    // Erbt die eigene Replace-Vorlage statt der Zeichen-Defaults.
    const int key = pageKeyForView(page);            // s. addTextBox
    if (key < 0)
        return -1;

    PdfEditBox b = (kind == static_cast<int>(PdfAnnKind::Replace))
                       ? seededReplace()
                       : seededDraw(static_cast<PdfAnnKind>(kind));
    b.id   = m_nextId++;
    b.page = key;
    m_drawStart = QPointF(xPt, yPt);
    m_drawPage  = page;                              // ANSICHTS-Seite der Session
    switch (b.kind) {
    case PdfAnnKind::Freehand:
        b.points = { QPointF(xPt, yPt) };
        b.recomputeBounds();
        break;
    case PdfAnnKind::Arrow:
        b.points = { QPointF(xPt, yPt), QPointF(xPt, yPt) };
        b.recomputeBounds();
        break;
    default:                                        // Rect / Ellipse
        b.rect = QRectF(xPt, yPt, 0.0, 0.0);
        break;
    }
    // Live einfuegen als Vorschau - kein Kommando, das kommt erst bei endDraw().
    m_model.insertBoxAt(m_model.count(), b);
    m_drawId = b.id;
    return b.id;
}

void PdfEditController::updateDraw(int id, qreal xPt, qreal yPt) {
    if (id != m_drawId)
        return;
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    switch (b->kind) {
    case PdfAnnKind::Freehand: {
        QVector<QPointF> pts = b->points;
        pts.append(QPointF(xPt, yPt));
        m_model.applyPoints(id, pts);
        break;
    }
    case PdfAnnKind::Arrow: {
        QVector<QPointF> pts = b->points;
        if (pts.size() < 2) pts.resize(2);
        pts[0] = m_drawStart;
        pts[1] = QPointF(xPt, yPt);
        m_model.applyPoints(id, pts);
        break;
    }
    default: {                                      // Rect / Ellipse
        const QRectF r = QRectF(m_drawStart, QPointF(xPt, yPt)).normalized();
        m_model.applyGeometry(id, r);
        break;
    }
    }
}

void PdfEditController::endDraw(int id) {
    if (id != m_drawId)
        return;
    m_drawId = -1;
    const PdfEditBox* b = m_model.boxById(id);
    if (!b) return;
    PdfEditBox copy = *b;

    // Entartete Zeichnungen verwerfen (reiner Klick ohne Zug).
    const bool tooSmall =
        (copy.isStroke() && copy.points.size() < 2)
        || (!copy.isStroke() && (copy.rect.width() < 1.5 && copy.rect.height() < 1.5));
    m_model.removeById(id);
    if (tooSmall)
        return;
    pushAdd(copy);
    // Laeuft nur, wenn eine Replace-Session ueber das generische endDraw endet.
    if (copy.kind == PdfAnnKind::Replace) {
        m_replaceTpl = copy;
        m_replaceTpl.text.clear();
        m_replaceTpl.points.clear();
    } else {
        m_defStroke = copy.stroke; m_defLineWidth = copy.lineWidth; m_defFill = copy.fill;
    }
    ++m_defaultRev; emit defaultRevChanged();
    setSelectedId(copy.id);
}


int PdfEditController::endRedactDraw(int id, bool snapped,
                                     qreal xPt, qreal yPt, qreal wPt, qreal hPt,
                                     const QString& text) {
    if (id != m_drawId)
        return -1;
    m_drawId = -1;
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return -1;
    PdfEditBox copy = *b;
    m_model.removeById(id);                          // Live-Instanz weg (kein Kommando)

    if (snapped) {
        // Die Flaeche deckt genau das, was auch aus dem Strom verschwindet.
        copy.rect = QRectF(qMax(0.0, xPt), qMax(0.0, yPt),
                           qMax(kMinBoxWPt, wPt), qMax(kMinBoxHPt, hPt));
        copy.anchored = true;
    } else if (copy.rect.width() < 1.5 && copy.rect.height() < 1.5) {
        return -1;                                   // entarteter Klick ohne Zug
    } else {
        copy.rect.setWidth (qMax(kMinBoxWPt,  copy.rect.width()));
        copy.rect.setHeight(qMax(kMinBoxHPt, copy.rect.height()));
    }

    copy.kind      = PdfAnnKind::Redact;
    copy.text.clear();                               // eine Schwärzung zeigt nichts
    copy.origText  = text;                           // …und entfernt DAS hier
    copy.stroke    = QColor(0, 0, 0, 0);             // kein Rahmen
    copy.lineWidth = 0.0;
    // Immer schwarz und deckend: die Replace-Vorlage traegt deckendes Weiss - geerbt
    // waere die Schwaerzung weiss auf weissem Papier, also unsichtbar.
    copy.highlight = QColor(0, 0, 0, 255);

    pushAdd(copy);
    setSelectedId(copy.id);
    return copy.id;
}

int PdfEditController::endReplaceDraw(int id, bool snapped,
                                      qreal xPt, qreal yPt, qreal wPt, qreal hPt,
                                      qreal lineHPt, const QString& text) {
    if (id != m_drawId)
        return -1;
    m_drawId = -1;
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return -1;
    PdfEditBox copy = *b;
    m_model.removeById(id);
    if (copy.kind != PdfAnnKind::Replace)
        return -1;                                   // defensiv: falscher Aufruf

    if (snapped) {
        // Auf die erkannten Zeilen-Bounds einschnappen; Schriftgroesse aus der mittleren Zeilenhoehe.
        copy.rect = QRectF(qMax(0.0, xPt), qMax(0.0, yPt),
                           qMax(kMinBoxWPt, wPt), qMax(kMinBoxHPt, hPt));
        copy.anchored = true;
        if (lineHPt > 0.0)
            copy.fontSizePt = qBound(6.0, lineHPt * 0.72, 72.0);
        copy.text = text;                            // Vorbefüllung: erkannter Text
        copy.origText = text;                        // Original merken (Content-Stream-Editing)
    } else if (copy.rect.width() < 1.5 && copy.rect.height() < 1.5) {
        return -1;                                   // entarteter Klick ohne Zug
    } else {
        // Ohne Texterkennung bleibt die Box der aufgezogene Bereich - bewusst still.
        copy.rect.setWidth(qMax(kMinBoxWPt,  copy.rect.width()));
        copy.rect.setHeight(qMax(kMinBoxHPt, copy.rect.height()));
    }
    // Deckflaeche muss deckend sein; die Farbe bleibt konfigurierbar.
    if (copy.highlight.isValid() && copy.highlight.alpha() > 0) copy.highlight.setAlpha(255);
    else                                                        copy.highlight = QColor(255, 255, 255, 255);
    // Vorbefuellter Text muss vollstaendig in die Box passen.
    if (!copy.text.isEmpty()) {
        const qreal need = requiredHeightPt(copy);
        if (need > copy.rect.height())
            copy.rect.setHeight(need);
    }

    pushAdd(copy);
    m_replaceTpl = copy;
    m_replaceTpl.text.clear();
    m_replaceTpl.points.clear();
    ++m_defaultRev; emit defaultRevChanged();
    setSelectedId(copy.id);
    return copy.id;
}

void PdfEditController::finishDrawSession() {
    if (m_drawId >= 0)
        endDraw(m_drawId);
}

void PdfEditController::removeBox(int id) {
    const int row = m_model.indexOfId(id);
    const PdfEditBox* b = m_model.boxById(id);
    if (row < 0 || !b)
        return;
    const PdfEditBox copy = *b;                     // vor der Mutation kopieren
    finishOpenSessions();
    if (m_selectedId == id)
        setSelectedId(-1);
    // Bei laufender Aufzeichnung wird markiert statt entfernt. Eine in derselben
    // Sitzung entstandene Box verschwindet ganz - sie hebt sich selbst auf.
    if (m_recording && copy.track != PdfTrackState::Added) {
        setTrack(id, PdfTrackState::Deleted);
        return;
    }
    pushCommand(new PdfEditRemoveCommand(&m_model, copy, row));
}

PdfEditBox PdfEditController::seededBox() const {
    PdfEditBox b = m_textTpl;                        // Schrift/Farben/Deckkraft/Ausrichtung
    b.id       = 0;
    b.kind     = PdfAnnKind::Text;
    b.text.clear();                                  // aber OHNE Text
    b.points.clear();
    b.anchored = false;
    return b;
}

PdfEditBox PdfEditController::seededDraw(PdfAnnKind kind) const {
    PdfEditBox b;
    b.kind      = kind;
    b.stroke    = m_defStroke;
    b.lineWidth = m_defLineWidth;
    b.fill      = m_defFill;
    return b;
}

PdfEditBox PdfEditController::seededReplace() const {
    PdfEditBox b = m_replaceTpl;                     // eigene Replace-Vorlage
    b.id       = 0;
    b.kind     = PdfAnnKind::Replace;
    b.text.clear();                                  // aber OHNE Text
    b.points.clear();
    b.anchored = false;
    // Deckflaeche immer deckend erzwingen, Farbe konfigurierbar.
    if (b.highlight.isValid() && b.highlight.alpha() > 0) b.highlight.setAlpha(255);
    else                                                  b.highlight = QColor(255, 255, 255, 255);
    return b;
}

PdfEditBox PdfEditController::makeReplaceTpl() {
    // Schwarzer Text auf deckendem Weiss - ersetzt gedruckten Text, kein Post-it-Gelb.
    PdfEditBox b;
    b.kind      = PdfAnnKind::Replace;
    b.color     = QColor(0, 0, 0);
    b.highlight = QColor(255, 255, 255, 255);
    b.alignment = 0;
    b.vAlign    = 0;
    return b;
}

// Dieselbe QTextLayout-Mathematik wie drawBox, nur ohne Geraet: QFont::setPixelSize
// nimmt Ganzzahlen, daher der Skalierungsfaktor; PreferNoHinting haelt die Metriken linear.
qreal PdfEditController::requiredHeightPt(const PdfEditBox& b) {
    if (b.text.isEmpty())
        return b.rect.height();
    const qreal k = 8.0;
    QFont f(b.fontFamily);
    f.setPixelSize(qMax(1, qRound(qMax(1.0, b.fontSizePt) * k)));
    f.setBold(b.bold);
    f.setItalic(b.italic);
    f.setUnderline(b.underline);
    f.setHintingPreference(QFont::PreferNoHinting);

    const qreal availW = qMax<qreal>(4.0, b.rect.width() - 2.0 * kBoxPaddingPt) * k;

    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    QTextLayout layout(b.text, f);
    layout.setTextOption(opt);
    layout.beginLayout();
    qreal totalH = 0.0;
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(availW);
        line.setPosition(QPointF(0.0, totalH));
        totalH += line.height();
    }
    layout.endLayout();
    return totalH / k + 2.0 * kBoxPaddingPt;
}

int PdfEditController::fitCharCount(const PdfEditBox& box, const QString& text) {
    if (text.isEmpty())
        return 0;
    const qreal k = 8.0;
    QFont f(box.fontFamily);
    f.setPixelSize(qMax(1, qRound(qMax(1.0, box.fontSizePt) * k)));
    f.setBold(box.bold);
    f.setItalic(box.italic);
    f.setUnderline(box.underline);
    f.setHintingPreference(QFont::PreferNoHinting);

    const qreal availW = qMax<qreal>(4.0, box.rect.width()  - 2.0 * kBoxPaddingPt) * k;
    const qreal availH = qMax<qreal>(1.0, box.rect.height() - 2.0 * kBoxPaddingPt) * k;

    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    QTextLayout layout(text, f);
    layout.setTextOption(opt);
    layout.beginLayout();
    qreal y = 0.0;
    int   fitEnd = 0;
    for (;;) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) { fitEnd = text.size(); break; }   // alles passt
        line.setLineWidth(availW);
        // Die erste Zeile zaehlt immer, jede weitere nur bei passender Resthoehe.
        if (fitEnd > 0 && y + line.height() > availH)
            break;
        y += line.height();
        fitEnd = line.textStart() + line.textLength();
    }
    layout.endLayout();
    return fitEnd;
}

int PdfEditController::chainHead(int id) const {
    const QVector<PdfEditBox> boxes = m_model.boxes();
    int cur = id;
    int guard = 0;
    for (;;) {
        int prev = 0;
        for (const PdfEditBox& b : boxes)
            if (b.chainNext == cur) { prev = b.id; break; }
        if (prev == 0 || prev == id || ++guard > boxes.size())
            break;                                  // Kopf erreicht / Zyklus
        cur = prev;
    }
    return cur;
}

QVector<int> PdfEditController::chainOrder(int headId) const {
    QVector<int> order;
    QSet<int> seen;
    int cur = headId;
    while (cur != 0 && !seen.contains(cur)) {
        const PdfEditBox* b = m_model.boxById(cur);
        if (!b)
            break;
        order.append(cur);
        seen.insert(cur);
        cur = b->chainNext;
    }
    return order;
}

bool PdfEditController::isChainMember(int id) const {
    const PdfEditBox* b = m_model.boxById(id);
    if (b && b->chainNext != 0)
        return true;
    for (const PdfEditBox& x : m_model.boxes())
        if (x.chainNext == id)
            return true;
    return false;
}

void PdfEditController::reflowChain(int anyId, int editedId,
                                    const QString& editedOldText,
                                    const QRectF& editedOldRect) {
    const QVector<int> chain = chainOrder(chainHead(anyId));
    if (chain.size() < 2)
        return;                                     // keine echte Kette

    QString combined;
    for (int cid : chain)
        if (const PdfEditBox* b = m_model.boxById(cid))
            combined += b->text;

    struct Delta { int id; int page; QString oldT, newT; QRectF oldR, newR;
                   bool textCh; bool rectCh; };
    QVector<Delta> deltas;
    // growBaseH ist reine Buchfuehrung und gehoert nicht in ein Undo-Kommando.
    QHash<int, qreal> grow;
    int pos = 0;
    for (int i = 0; i < chain.size(); ++i) {
        const int cid = chain[i];
        const PdfEditBox* b = m_model.boxById(cid);
        if (!b)
            continue;
        QString newT;
        QRectF  newR = b->rect;
        if (i == chain.size() - 1) {
            newT = combined.mid(pos);               // Rest in die letzte Box …
            PdfEditBox tmp = *b; tmp.text = newT;    // … die mit dem Inhalt wächst
            const qreal need = requiredHeightPt(tmp);
            if (need > newR.height() + 0.5) {
                // Ursprungshoehe einmalig merken, bevor sie ueberschrieben wird.
                if (grow.value(cid, 0.0) <= 0.0 && b->growBaseH <= 0.0)
                    grow.insert(cid, newR.height());
                newR.setHeight(need);
            }
        } else {
            // War die Box einmal Kettenende und dabei gewachsen, muss sie zurueckschrumpfen -
            // sonst fasst sie weiter den Resttext und die Folgeboxen bleiben leer.
            if (b->growBaseH > 0.0 && b->growBaseH < newR.height() - 0.5) {
                newR.setHeight(b->growBaseH);
                grow.insert(cid, 0.0);              // Merker verbraucht
            }
            // Auf dem moeglicherweise geschrumpften Rechteck rechnen, nicht auf dem alten.
            PdfEditBox probe = *b; probe.rect = newR;
            const int fit = fitCharCount(probe, combined.mid(pos));
            newT = combined.mid(pos, fit);
            pos += fit;
        }
        const QString oldT = (cid == editedId) ? editedOldText : b->text;
        const QRectF  oldR = (cid == editedId) ? editedOldRect : b->rect;
        Delta d{ cid, b->page, oldT, newT, oldR, newR, newT != oldT, newR != oldR };
        if (d.textCh || d.rectCh)
            deltas.append(d);
    }
    if (deltas.isEmpty())
        return;

    m_stack.beginMacro(QStringLiteral("reflow"));
    for (const Delta& d : deltas) {
        if (d.textCh)
            m_stack.push(new PdfEditTextCommand(&m_model, d.id, d.oldT, d.newT));
        if (d.rectCh)
            m_stack.push(new PdfEditGeometryCommand(&m_model, d.id,
                             d.page, d.oldR, {}, d.page, d.newR, {}));
    }
    for (auto it = grow.cbegin(); it != grow.cend(); ++it)
        m_model.setGrowBaseH(it.key(), it.value());
    m_stack.endMacro();
    ++m_selectionRev; emit selectionRevChanged();
}

void PdfEditController::linkChain(int fromId, int toId) {
    if (fromId == toId)
        return;
    const PdfEditBox* from = m_model.boxById(fromId);
    const PdfEditBox* to   = m_model.boxById(toId);
    if (!from || !to || !from->hasText() || !to->hasText())
        return;
    // Zyklus verhindern: from darf nicht bereits hinter to liegen.
    for (int cid : chainOrder(toId))
        if (cid == fromId)
            return;
    if (from->chainNext == toId)
        return;                                     // schon verkettet
    // Verkettung und Reflow als einen Undo-Schritt buchen.
    const int oldNext = from->chainNext;
    m_stack.beginMacro(QStringLiteral("chain-link"));
    m_stack.push(new PdfEditChainCommand(&m_model, fromId, oldNext, toId));
    reflowChain(fromId, -1, QString(), QRectF());
    m_stack.endMacro();
    ++m_selectionRev; emit selectionRevChanged();
}

void PdfEditController::unlinkChain(int fromId) {
    const PdfEditBox* from = m_model.boxById(fromId);
    if (!from || from->chainNext == 0)
        return;
    pushCommand(new PdfEditChainCommand(&m_model, fromId, from->chainNext, 0));
    ++m_selectionRev; emit selectionRevChanged();
}

void PdfEditController::mirrorToTemplate(PdfEditField f, const QVariant& v, PdfAnnKind kind) {
    if (kind == PdfAnnKind::Text || kind == PdfAnnKind::Replace) {
        PdfEditBox& tpl = (kind == PdfAnnKind::Replace) ? m_replaceTpl : m_textTpl;
        switch (f) {
        case PdfEditField::FontFamily: tpl.fontFamily = v.toString();     break;
        case PdfEditField::FontSize:   tpl.fontSizePt = v.toReal();       break;
        case PdfEditField::Bold:       tpl.bold       = v.toBool();       break;
        case PdfEditField::Italic:     tpl.italic     = v.toBool();       break;
        case PdfEditField::Underline:  tpl.underline  = v.toBool();       break;
        case PdfEditField::Color:      tpl.color      = v.value<QColor>();break;
        case PdfEditField::Highlight: {
            // Die Cover-Farbe wird mitgezogen, aber deckend erzwungen.
            QColor hc = v.value<QColor>();
            if (kind == PdfAnnKind::Replace && hc.isValid()) hc.setAlpha(255);
            tpl.highlight = hc;
            break;
        }
        case PdfEditField::Alignment:  tpl.alignment  = v.toInt();        break;
        case PdfEditField::VAlign:     tpl.vAlign     = v.toInt();        break;
        default: break;
        }
    } else {
        switch (f) {
        case PdfEditField::Stroke:    m_defStroke    = v.value<QColor>(); break;
        case PdfEditField::LineWidth: m_defLineWidth = v.toReal();        break;
        case PdfEditField::Fill:      m_defFill      = v.value<QColor>(); break;
        default: break;
        }
    }
    ++m_defaultRev;
    emit defaultRevChanged();
}

void PdfEditController::copySelected() {
    const PdfEditBox* b = m_model.boxById(m_selectedId);
    if (!b)
        return;
    m_clip = *b;
    if (!m_hasClip) { m_hasClip = true; emit clipboardChanged(); }
}

void PdfEditController::paste() {
    if (!m_hasClip || m_docPath.isEmpty())
        return;
    finishOpenSessions();
    finishDrawSession();
    PdfEditBox b = m_clip;                           // inkl. Text + allen Einstellungen
    b.id = m_nextId++;
    // Leicht versetzt, damit die Kopie sichtbar liegt; Strich-Punkte wandern mit.
    b.rect.translate(14.0, 14.0);
    for (QPointF& p : b.points)
        p += QPointF(14.0, 14.0);
    setTool(Select);
    pushAdd(b);
    setSelectedId(b.id);
}

void PdfEditController::beginGeometryEdit(int id) {
    finishOpenSessions();
    finishDrawSession();
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    m_geoEditId  = id;
    m_geoOldPage = b->page;
    m_geoOld     = b->rect;
    m_geoOldPts  = b->points;
}

// Punkte proportional vom Basis- ins neue Rechteck abbilden - dieselbe Mathematik
// wie ImageEditController::updateGeometry.
static QVector<QPointF> transformPoints(const QVector<QPointF>& oldPts,
                                        const QRectF& oldRect, const QRectF& r) {
    QVector<QPointF> pts;
    if (oldPts.isEmpty() || oldRect.width() <= 0.0 || oldRect.height() <= 0.0)
        return pts;
    const qreal sx = r.width()  / oldRect.width();
    const qreal sy = r.height() / oldRect.height();
    pts.reserve(oldPts.size());
    for (const QPointF& p : oldPts)
        pts.append(QPointF(r.x() + (p.x() - oldRect.x()) * sx,
                           r.y() + (p.y() - oldRect.y()) * sy));
    return pts;
}

void PdfEditController::updateGeometry(int id, qreal xPt, qreal yPt,
                                       qreal wPt, qreal hPt) {
    if (id != m_geoEditId)
        return;                                     // nur innerhalb einer Session
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    const qreal minW = b->hasText() ? kMinBoxWPt : kMinDrawPt;
    const qreal minH = b->hasText() ? kMinBoxHPt : kMinDrawPt;
    QRectF r(xPt, yPt, qMax(minW, wPt), qMax(minH, hPt));
    if (r.x() < 0.0) r.moveLeft(0.0);
    if (r.y() < 0.0) r.moveTop(0.0);
    if (!m_geoOldPts.isEmpty())
        m_model.applyPlacementPoints(id, b->page, r,
                                     transformPoints(m_geoOldPts, m_geoOld, r));
    else
        m_model.applyGeometry(id, r);               // live, KEIN Kommando je Move
}

void PdfEditController::updatePlacement(int id, int page, qreal xPt, qreal yPt,
                                        qreal wPt, qreal hPt) {
    if (id != m_geoEditId)
        return;                                     // nur innerhalb einer Session
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    // Zielseite ist eine Ansichts-Seite; gespeichert wird ihr stabiler Key.
    const int key = pageKeyForView(page);
    if (key < 0)
        return;
    page = key;
    const qreal minW = b->hasText() ? kMinBoxWPt : kMinDrawPt;
    const qreal minH = b->hasText() ? kMinBoxHPt : kMinDrawPt;
    QRectF r(xPt, yPt, qMax(minW, wPt), qMax(minH, hPt));
    if (r.x() < 0.0) r.moveLeft(0.0);
    // y bewusst nicht klemmen: seitenuebergreifendes Verschieben braucht Zwischen-
    // zustaende ueber dem Seitenrand.
    if (!m_geoOldPts.isEmpty())
        m_model.applyPlacementPoints(id, page, r,
                                     transformPoints(m_geoOldPts, m_geoOld, r));
    else
        m_model.applyPlacement(id, page, r);        // live, KEIN Kommando je Move
}

void PdfEditController::endGeometryEdit(int id) {
    if (id == m_geoEditId)
        finishGeometrySession();
}

void PdfEditController::finishGeometrySession() {
    if (m_geoEditId < 0)
        return;
    const int id = m_geoEditId;
    m_geoEditId = -1;
    const PdfEditBox* b = m_model.boxById(id);
    if (b && (b->rect != m_geoOld || b->page != m_geoOldPage
              || b->points != m_geoOldPts))
        pushCommand(new PdfEditGeometryCommand(&m_model, id,
                                               m_geoOldPage, m_geoOld, m_geoOldPts,
                                               b->page, b->rect, b->points));
    m_geoOldPts.clear();
}

void PdfEditController::beginTextEdit(int id) {
    finishOpenSessions();
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    m_textEditId  = id;
    m_textOld     = b->text;
    m_textOldRect = b->rect;      // Basis des Höhenwachstums („Text ersetzen")
    emit textEditingChanged();
}

void PdfEditController::updateText(int id, const QString& text) {
    if (id != m_textEditId)
        return;
    m_model.applyText(id, text);                    // live, KEIN Kommando je Taste

    // Text-ersetzen-Boxen wachsen mit dem Inhalt: feste Breite, Umbruch, Hoehe
    // folgt dem Text - nie automatisches Schrumpfen.
    const PdfEditBox* b = m_model.boxById(id);
    if (b && b->kind == PdfAnnKind::Replace) {
        const qreal need = requiredHeightPt(*b);
        if (need > b->rect.height() + 0.5) {
            QRectF r = b->rect;
            r.setHeight(need);
            m_model.applyGeometry(id, r);
        }
    }
}

void PdfEditController::endTextEdit(int id) {
    if (id == m_textEditId)
        finishTextSession();
}

void PdfEditController::finishTextSession() {
    if (m_textEditId < 0)
        return;
    const int id = m_textEditId;
    m_textEditId = -1;
    emit textEditingChanged();
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    // Verkettete Box: der ganze Ketten-Reflow wird ein Undo-Schritt.
    if (isChainMember(id)) {
        reflowChain(id, id, m_textOld, m_textOldRect);
        return;
    }
    const bool textChanged = b->text != m_textOld;
    const bool rectChanged = b->rect != m_textOldRect;
    if (textChanged && rectChanged) {
        // Text- und Geometrie-Delta zu einem Undo-Schritt zusammenfassen.
        m_stack.beginMacro(QStringLiteral("text"));
        m_stack.push(new PdfEditTextCommand(&m_model, id, m_textOld, b->text));
        m_stack.push(new PdfEditGeometryCommand(&m_model, id,
                                                b->page, m_textOldRect, {},
                                                b->page, b->rect, {}));
        m_stack.endMacro();
    } else if (textChanged) {
        pushCommand(new PdfEditTextCommand(&m_model, id, m_textOld, b->text));
    } else if (rectChanged) {
        pushCommand(new PdfEditGeometryCommand(&m_model, id,
                                               b->page, m_textOldRect, {},
                                               b->page, b->rect, {}));
    }
}

void PdfEditController::setBoxField(int id, PdfEditField f, const QVariant& v) {
    if (id < 0) {
        // Nur die Vorlage fuer neue Annotationen setzen, kein Kommando.
        const bool draw = (f == PdfEditField::Stroke || f == PdfEditField::LineWidth
                           || f == PdfEditField::Fill);
        const PdfAnnKind tplKind = draw ? PdfAnnKind::Freehand
                                 : (m_tool == ReplaceTool ? PdfAnnKind::Replace
                                                          : PdfAnnKind::Text);
        mirrorToTemplate(f, v, tplKind);
        return;
    }
    const PdfEditBox* b = m_model.boxById(id);
    if (!b)
        return;
    // Cover-Farbe ist waehlbar, muss aber deckend bleiben - Alpha auf 255 ziehen.
    QVariant vEff = v;
    if (f == PdfEditField::Highlight && b->kind == PdfAnnKind::Replace) {
        QColor hc = v.value<QColor>();
        if (hc.isValid()) { hc.setAlpha(255); vEff = hc; }
    }
    QVariant old;
    switch (f) {
    case PdfEditField::Stroke:     old = b->stroke;     break;
    case PdfEditField::LineWidth:  old = b->lineWidth;  break;
    case PdfEditField::Fill:       old = b->fill;       break;
    case PdfEditField::FontFamily: old = b->fontFamily; break;
    case PdfEditField::FontSize:   old = b->fontSizePt; break;
    case PdfEditField::Bold:       old = b->bold;       break;
    case PdfEditField::Italic:     old = b->italic;     break;
    case PdfEditField::Underline:  old = b->underline;  break;
    case PdfEditField::Color:      old = b->color;      break;
    case PdfEditField::Highlight:  old = b->highlight;  break;
    case PdfEditField::Alignment:  old = b->alignment;  break;
    case PdfEditField::VAlign:     old = b->vAlign;     break;
    default: return;                                // Text/Geometry/Points: eigene Wege
    }
    mirrorToTemplate(f, vEff, b->kind);
    if (old == vEff)
        return;
    pushCommand(new PdfEditFieldCommand(&m_model, id, f, old, vEff));
}

void PdfEditController::setBoxStroke(int id, const QColor& c) {
    if (c.isValid())
        setBoxField(id, PdfEditField::Stroke, c);
}
void PdfEditController::setBoxLineWidth(int id, qreal wPt) {
    setBoxField(id, PdfEditField::LineWidth, qBound(0.2, wPt, 72.0));
}
void PdfEditController::setBoxFill(int id, const QColor& c) {
    if (c.isValid())
        setBoxField(id, PdfEditField::Fill, c);
}
void PdfEditController::setBoxFont(int id, const QString& family) {
    setBoxField(id, PdfEditField::FontFamily, family);
}
void PdfEditController::setBoxFontSize(int id, qreal sizePt) {
    setBoxField(id, PdfEditField::FontSize, qBound(4.0, sizePt, 200.0));
}
void PdfEditController::setBoxBold(int id, bool v) {
    setBoxField(id, PdfEditField::Bold, v);
}
void PdfEditController::setBoxItalic(int id, bool v) {
    setBoxField(id, PdfEditField::Italic, v);
}
void PdfEditController::setBoxUnderline(int id, bool v) {
    setBoxField(id, PdfEditField::Underline, v);
}
void PdfEditController::setBoxColor(int id, const QColor& c) {
    if (c.isValid())
        setBoxField(id, PdfEditField::Color, QColor(c.red(), c.green(), c.blue()));
}
void PdfEditController::setBoxHighlight(int id, const QColor& c) {
    if (c.isValid())
        setBoxField(id, PdfEditField::Highlight, c);
}
void PdfEditController::setBoxAlignment(int id, int align) {
    setBoxField(id, PdfEditField::Alignment, qBound(0, align, 2));
}
void PdfEditController::setBoxVAlign(int id, int vAlign) {
    setBoxField(id, PdfEditField::VAlign, qBound(0, vAlign, 1));
}

QVariantMap PdfEditController::boxInfo(int id) const {
    QVariantMap m;
    const PdfEditBox* b = m_model.boxById(id);
    m.insert(QStringLiteral("exists"), b != nullptr);
    if (!b)
        return m;
    m.insert(QStringLiteral("page"),           b->page);
    // track: 0 keine, 1 neu, 2 geloescht.
    m.insert(QStringLiteral("track"),          static_cast<int>(b->track));
    m.insert(QStringLiteral("kind"),           static_cast<int>(b->kind));
    m.insert(QStringLiteral("isText"),         b->kind == PdfAnnKind::Text);
    m.insert(QStringLiteral("isReplace"),      b->kind == PdfAnnKind::Replace);
    m.insert(QStringLiteral("isStroke"),       b->isStroke());
    m.insert(QStringLiteral("isShape"),        b->kind == PdfAnnKind::Rect
                                               || b->kind == PdfAnnKind::Ellipse);
    // chainNext (0 = keine) plus chained (Teil einer Kette).
    m.insert(QStringLiteral("chainNext"),      b->chainNext);
    m.insert(QStringLiteral("chained"),        isChainMember(id));
    m.insert(QStringLiteral("xPt"),            b->rect.x());
    m.insert(QStringLiteral("yPt"),            b->rect.y());
    m.insert(QStringLiteral("wPt"),            b->rect.width());
    m.insert(QStringLiteral("hPt"),            b->rect.height());
    m.insert(QStringLiteral("strokeColor"),    b->stroke);
    m.insert(QStringLiteral("lineWidth"),      b->lineWidth);
    m.insert(QStringLiteral("fillColor"),      b->fill);
    m.insert(QStringLiteral("hasFill"),        b->fill.alpha() > 0);
    m.insert(QStringLiteral("text"),           b->text);
    m.insert(QStringLiteral("fontFamily"),     b->fontFamily);
    m.insert(QStringLiteral("fontSizePt"),     b->fontSizePt);
    m.insert(QStringLiteral("bold"),           b->bold);
    m.insert(QStringLiteral("italic"),         b->italic);
    m.insert(QStringLiteral("underline"),      b->underline);
    m.insert(QStringLiteral("textColor"),      b->color);
    m.insert(QStringLiteral("highlightColor"), b->highlight);
    m.insert(QStringLiteral("hasHighlight"),   b->highlight.alpha() > 0);
    m.insert(QStringLiteral("alignment"),      b->alignment);
    m.insert(QStringLiteral("vAlign"),         b->vAlign);
    m.insert(QStringLiteral("anchored"),       b->anchored);
    return m;
}

QVariantMap PdfEditController::defaultInfo() const {
    // Die Text-Felder kommen aus der Vorlage des AKTIVEN Werkzeugs.
    const PdfEditBox& tpl = (m_tool == ReplaceTool) ? m_replaceTpl : m_textTpl;
    QVariantMap m;
    m.insert(QStringLiteral("strokeColor"),    m_defStroke);
    m.insert(QStringLiteral("lineWidth"),      m_defLineWidth);
    m.insert(QStringLiteral("fillColor"),      m_defFill);
    m.insert(QStringLiteral("hasFill"),        m_defFill.alpha() > 0);
    m.insert(QStringLiteral("fontFamily"),     tpl.fontFamily);
    m.insert(QStringLiteral("fontSizePt"),     tpl.fontSizePt);
    m.insert(QStringLiteral("bold"),           tpl.bold);
    m.insert(QStringLiteral("italic"),         tpl.italic);
    m.insert(QStringLiteral("underline"),      tpl.underline);
    m.insert(QStringLiteral("textColor"),      tpl.color);
    m.insert(QStringLiteral("highlightColor"), tpl.highlight);
    m.insert(QStringLiteral("hasHighlight"),   tpl.highlight.alpha() > 0);
    m.insert(QStringLiteral("alignment"),      tpl.alignment);
    m.insert(QStringLiteral("vAlign"),         tpl.vAlign);
    m.insert(QStringLiteral("isReplace"),      m_tool == ReplaceTool);
    return m;
}

void PdfEditController::pushAdd(PdfEditBox& b) {
    if (m_recording)
        b.track = PdfTrackState::Added;
    pushCommand(new PdfEditAddCommand(&m_model, b, m_model.count()));
}

void PdfEditController::setTrack(int id, PdfTrackState st) {
    const PdfEditBox* b = m_model.boxById(id);
    if (!b || b->track == st)
        return;
    pushCommand(new PdfEditFieldCommand(&m_model, id, PdfEditField::Track,
                                        static_cast<int>(b->track),
                                        static_cast<int>(st)));
}

void PdfEditController::setRecording(bool on) {
    if (m_recording == on)
        return;
    m_recording = on;
    emit recordingChanged();
    saveOverlay();          // der Schalter gehört zum Dokument, nicht zur Sitzung
}

int PdfEditController::trackedCount() const {
    int n = 0;
    const QVector<PdfEditBox> boxes = m_model.boxes();
    for (const PdfEditBox& b : boxes)
        if (b.track != PdfTrackState::None) ++n;
    return n;
}

void PdfEditController::discardAllAnnotations() {
    if (m_model.count() == 0)
        return;
    finishOpenSessions();
    finishDrawSession();
    setSelectedId(-1);
    m_stack.beginMacro(QStringLiteral("discardAllAnnotations"));
    const QVector<PdfEditBox> boxes = m_model.boxes();
    for (const PdfEditBox& b : boxes) {
        const int row = m_model.indexOfId(b.id);
        if (row >= 0)
            pushCommand(new PdfEditRemoveCommand(&m_model, b, row));
    }
    m_stack.endMacro();
    saveOverlay();
}

void PdfEditController::acceptChange(int id) {
    const PdfEditBox* b = m_model.boxById(id);
    if (!b || b->track == PdfTrackState::None)
        return;
    if (b->track == PdfTrackState::Deleted) {
        const int row = m_model.indexOfId(id);
        const PdfEditBox copy = *b;
        if (m_selectedId == id)
            setSelectedId(-1);
        pushCommand(new PdfEditRemoveCommand(&m_model, copy, row));
        return;
    }
    setTrack(id, PdfTrackState::None);              // angenommene Neuerung bleibt
}

void PdfEditController::rejectChange(int id) {
    const PdfEditBox* b = m_model.boxById(id);
    if (!b || b->track == PdfTrackState::None)
        return;
    if (b->track == PdfTrackState::Added) {
        const int row = m_model.indexOfId(id);
        const PdfEditBox copy = *b;
        if (m_selectedId == id)
            setSelectedId(-1);
        pushCommand(new PdfEditRemoveCommand(&m_model, copy, row));
        return;
    }
    setTrack(id, PdfTrackState::None);              // verworfene Löschung bleibt
}

// Alle ist ein Undo-Schritt - wie im DOCX-Aenderungsstreifen.
void PdfEditController::acceptAllChanges() {
    if (trackedCount() == 0)
        return;
    finishOpenSessions();
    m_stack.beginMacro(QStringLiteral("acceptAllChanges"));
    const QVector<PdfEditBox> boxes = m_model.boxes();
    for (const PdfEditBox& b : boxes)
        if (b.track != PdfTrackState::None) acceptChange(b.id);
    m_stack.endMacro();
}

void PdfEditController::rejectAllChanges() {
    if (trackedCount() == 0)
        return;
    finishOpenSessions();
    m_stack.beginMacro(QStringLiteral("rejectAllChanges"));
    const QVector<PdfEditBox> boxes = m_model.boxes();
    for (const PdfEditBox& b : boxes)
        if (b.track != PdfTrackState::None) rejectChange(b.id);
    m_stack.endMacro();
}

void PdfEditController::pushCommand(QUndoCommand* cmd) {
    m_stack.push(cmd);                              // führt redo() sofort aus
}

void PdfEditController::undo() {
    finishOpenSessions();                           // deterministisch abschließen
    finishDrawSession();
    // Eine schwebende Tipp-Session ist der zuletzt getane Schritt und wird
    // verworfen: Festschreiben wartet ggf. auf den Neubau, und der Undo-Klick
    // traefe dann sichtbar das aeltere Kommando.
    if (m_pendingValid) {
        setPendingValid(false);
        m_pendingCommit = false;
        emit textOpsChanged();
        startTextRebuild();
        return;
    }
    m_stack.undo();
}

void PdfEditController::redo() {
    finishOpenSessions();
    finishDrawSession();
    commitPendingTextOp();
    m_stack.redo();
}

QString PdfEditController::sidecarPath(const QString& pdfPath) {
    return pdfPath + QStringLiteral(".mgedit.json");
}

bool PdfEditController::saveOverlay() {
    if (m_docPath.isEmpty())
        return false;
    finishOpenSessions();
    finishDrawSession();

    const QString sc = sidecarPath(m_docPath);
    bool ok = false;

    commitPendingTextOp();                  // schwebende Tipp-Session festschreiben

    const bool hasBoxes = m_model.count() > 0;
    const bool hasOps   = !m_textOps.isEmpty();
    const bool hasVals  = !m_formEdits.isEmpty();

    // Der Seiten-Plan gehoert bewusst nicht ins Sidecar: Seitenoperationen wirken
    // sofort, ein gespeicherter Plan wuerde beim naechsten Oeffnen zweimal wirken.
    if (!hasBoxes && !hasOps && !hasVals) {
        ok = !QFile::exists(sc) || QFile::remove(sc);
        // Ohne Plan verweist nichts mehr auf die Begleitdatei - sie wird aufgeraeumt.
        QFile::remove(assetPath(m_docPath));
    } else {
        QJsonObject rootObj;
        rootObj.insert(QStringLiteral("format"),  QStringLiteral("mediagallery-pdf-overlay"));
        rootObj.insert(QStringLiteral("version"), 1);
        if (m_recording)
            rootObj.insert(QStringLiteral("recording"), true);
        if (hasBoxes) {
            QJsonArray arr;
            const QVector<PdfEditBox> boxes = m_model.boxes();
            for (const PdfEditBox& b : boxes)
                arr.append(b.toJson());
            rootObj.insert(QStringLiteral("boxes"), arr);
            // IDs sind sitzungslokal, deshalb als Index-Array: chains[i] = Folgebox von i.
            bool anyChain = false;
            QHash<int, int> idToIndex;
            for (int i = 0; i < boxes.size(); ++i) idToIndex.insert(boxes[i].id, i);
            QJsonArray chains;
            for (const PdfEditBox& b : boxes) {
                const int idx = (b.chainNext != 0) ? idToIndex.value(b.chainNext, -1) : -1;
                chains.append(idx);
                if (idx >= 0) anyChain = true;
            }
            if (anyChain)
                rootObj.insert(QStringLiteral("chains"), chains);
            // Nur schreiben, wenn eine Box gewachsen ist - aeltere Sidecars laden unveraendert.
            bool anyGrow = false;
            QJsonArray growArr;
            for (const PdfEditBox& b : boxes) {
                growArr.append(b.growBaseH);
                if (b.growBaseH > 0.0) anyGrow = true;
            }
            if (anyGrow)
                rootObj.insert(QStringLiteral("growBase"), growArr);
        }
        if (hasOps) {
            // Textebenen-Aenderungen sind ein Delta auf die pristine Datei.
            QJsonArray oarr;
            for (const PdfTextOp& op : std::as_const(m_textOps))
                oarr.append(op.toJson());
            rootObj.insert(QStringLiteral("textops"), oarr);
        }
        if (hasVals) {
            // Gepufferte Formularwerte ueberleben hier das Schliessen; geschrieben wird
            // erst ueber saveFormValues in eine Kopie.
            QJsonObject vals;
            for (auto it = m_formEdits.cbegin(); it != m_formEdits.cend(); ++it)
                vals.insert(it.key(), it.value());
            rootObj.insert(QStringLiteral("formvals"), vals);
        }

        QSaveFile f(sc);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QByteArray bytes = QJsonDocument(rootObj).toJson(QJsonDocument::Compact);
            if (f.write(bytes) == bytes.size())
                ok = f.commit();
            else
                f.cancelWriting();
        }
    }

    if (ok)
        m_stack.setClean();
    emit overlaySaved(ok);
    return ok;
}

bool PdfEditController::loadOverlay(const QString& pdfPath) {
    const QString sc = sidecarPath(pdfPath);
    QFile f(sc);
    if (!f.exists() || f.size() > kMaxSidecarBytes)
        return false;
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument jd = QJsonDocument::fromJson(f.readAll());
    if (!jd.isObject())
        return false;
    const QJsonObject o = jd.object();
    if (o.value(QStringLiteral("format")).toString()
        != QLatin1String("mediagallery-pdf-overlay"))
        return false;

    if (m_recording != o.value(QStringLiteral("recording")).toBool(false)) {
        m_recording = o.value(QStringLiteral("recording")).toBool(false);
        emit recordingChanged();
    }

    QVector<PdfEditBox> boxes;
    const QJsonArray arr = o.value(QStringLiteral("boxes")).toArray();
    boxes.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        if (!v.isObject())
            continue;
        PdfEditBox b = PdfEditBox::fromJson(v.toObject());
        b.id = m_nextId++;                          // IDs sind sitzungslokal
        boxes.append(b);
    }
    const QJsonArray chains = o.value(QStringLiteral("chains")).toArray();
    for (int i = 0; i < chains.size() && i < boxes.size(); ++i) {
        const int idx = chains.at(i).toInt(-1);
        if (idx >= 0 && idx < boxes.size() && idx != i)
            boxes[i].chainNext = boxes[idx].id;
    }
    const QJsonArray growArr = o.value(QStringLiteral("growBase")).toArray();
    for (int i = 0; i < growArr.size() && i < boxes.size(); ++i)
        boxes[i].growBaseH = growArr.at(i).toDouble(0.0);
    m_model.resetBoxes(boxes);

    // Die Validierung gegen die echte Seitenzahl macht setSourcePageCount, sobald
    // QML sie meldet. Altformat-Sidecars tragen keinen Plan.
    m_plan.clear();
    const QJsonArray parr = o.value(QStringLiteral("pageplan")).toArray();
    for (const QJsonValue& v : parr) {
        if (v.isObject())
            m_plan.append(PdfPlanPage::fromJson(v.toObject()));
        else
            m_plan.append(PdfPlanPage{ v.toInt(-1), 0, 0, -1 });
    }

    // setDocument legt sie danach als Kommandos auf den wieder sauberen Stack.
    m_textOps.clear();
    const QJsonArray oarr = o.value(QStringLiteral("textops")).toArray();
    for (const QJsonValue& v : oarr) {
        if (!v.isObject())
            continue;
        const PdfTextOp op = PdfTextOp::fromJson(v.toObject());
        if (op.isInsert() ? !op.text.isEmpty() : op.removed > 0)
            m_textOps.append(op);
    }
    m_textOpsLoaded = !m_textOps.isEmpty();

    // Wird gegen die wirklich vorhandenen Felder abgeglichen, sobald der Lesevorgang
    // zurueckkommt - ein fremdes Sidecar kann nichts erzwingen.
    m_formEdits.clear();
    const QJsonObject vals = o.value(QStringLiteral("formvals")).toObject();
    for (auto it = vals.constBegin(); it != vals.constEnd(); ++it)
        if (!it.key().isEmpty() && it.value().isString())
            m_formEdits.insert(it.key(), it.value().toString());
    setFormDirty(!m_formEdits.isEmpty());

    return true;
}

QString PdfEditController::uniqueSuffixPath(const QString& pdfPath,
                                            const QString& suffix) {
    const QFileInfo fi(pdfPath);
    const QString dir  = fi.absolutePath();
    const QString base = fi.completeBaseName() + suffix;
    QString candidate = dir + QLatin1Char('/') + base + QStringLiteral(".pdf");
    int n = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = dir + QLatin1Char('/') + base
                    + QStringLiteral(" (%1).pdf").arg(n);
        ++n;
    }
    return candidate;
}

QString PdfEditController::uniqueCopyPath(const QString& pdfPath) {
    return uniqueSuffixPath(pdfPath, QStringLiteral("_bearbeitet"));
}

QString PdfEditController::exportTargetPath() const {
    if (m_docPath.isEmpty())
        return {};
    // Immer eine Kopie neben dem Original - nie destruktiv.
    return uniqueCopyPath(m_docPath);
}

void PdfEditController::exportPdf() {
    if (m_busy || m_docPath.isEmpty())
        return;
    finishOpenSessions();
    finishDrawSession();

    const QString target = exportTargetPath();
    if (target.isEmpty())
        return;
    // Nicht materialisierte Textebenen-Aenderungen zuerst schreiben, sonst waere der
    // Export aelter als das Bild.
    if (!flushTextForExport(1))
        return;

    m_busy = true;
    emit busyChanged();

    const int gen = ++m_exportGen;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    // Exportiert wird, was der Nutzer sieht (renderSourcePath).
    m_pool.start(new PdfExportTask(this, renderSourcePath(), target,
                                   exportBoxes(), gen, m_cancel, importRemovals(),
                                   exportAnnotations(), redactionEdits(),
                                   redactionAreas()));
}

void PdfEditController::exportContentEdited() {
    if (m_busy || m_docPath.isEmpty())
        return;
    finishOpenSessions();
    finishDrawSession();

    // Verlustfrei nur bei Identitaets-Plan und wenn alle Annotationen
    // stream-editierbare Boxen mit erkanntem Originaltext sind.
    if (!flushTextForExport(2))
        return;

    bool eligible = planIsIdentity();
    QVector<mg::PdfTextEdit> edits;
    if (eligible) {
        const QVector<PdfEditBox> boxes = m_model.boxes();
        for (const PdfEditBox& b : boxes) {
            // Ersetzen und Schwaerzen kennen beide ihren Originaltext; bei der Schwaerzung
            // ist der Ersatz leer.
            const bool streamable = (b.kind == PdfAnnKind::Replace
                                     || b.kind == PdfAnnKind::Redact);
            if (!streamable || b.origText.isEmpty()) { eligible = false; break; }
            if (b.text != b.origText)
                edits.push_back({ b.page, b.origText, b.text });
        }
    }
    if (eligible && edits.isEmpty())
        eligible = false;                           // nichts zu ersetzen
    // RAM-Schutz: sehr grosse Dateien nicht komplett laden.
    if (eligible) {
        constexpr qint64 kMaxContentEditBytes = 64LL * 1024 * 1024;
        if (QFileInfo(textSourcePath()).size() > kMaxContentEditBytes)
            eligible = false;
    }

    m_busy = true;
    emit busyChanged();
    const int gen = ++m_exportGen;

    if (eligible) {
        m_pool.start(new PdfContentEditTask(this, textSourcePath(), exportTargetPath(), edits, gen));
    } else {
        emit contentEditFellBack();
        m_cancel = std::make_shared<std::atomic<bool>>(false);
        m_pool.start(new PdfExportTask(this, renderSourcePath(), exportTargetPath(),
                                       exportBoxes(), gen, m_cancel, importRemovals(), {},
                                       redactionEdits(), redactionAreas()));
    }
}

void PdfEditController::contentEditTaskFinished(bool ok, const QString& target,
                                                int generation) {
    if (generation != m_exportGen)
        return;                                     // veralteter Lauf
    if (ok) {
        m_busy = false;
        emit busyChanged();
        emit exportFinished(true, target, QString());
        return;
    }
    // Content-Stream-Editing gescheitert - Raster-Export mit gleicher Generation.
    emit contentEditFellBack();
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    // Auch der Rueckfall bekommt die Schwaerzungs-Flaechen mit, sonst rastert er
    // alles, obwohl der geometrische Weg raeumen kann.
    m_pool.start(new PdfExportTask(this, renderSourcePath(), exportTargetPath(),
                                   exportBoxes(), m_exportGen, m_cancel, importRemovals(), {},
                                   redactionEdits(), redactionAreas()));
}

void PdfEditController::exportTaskFinished(bool ok, const QString& target,
                                           const QString& error, int generation) {
    if (generation != m_exportGen)
        return;                                     // veralteter Lauf
    m_busy = false;
    emit busyChanged();
    emit exportFinished(ok, target, error);
}

// Anders als das seitenweise OCR des Betrachters steht die Erkennung danach
// dauerhaft in der Datei.
bool PdfEditController::ocrAvailable() const { return mg::ocr::available(); }

bool PdfEditController::alreadySearchable() const {
    return !m_docPath.isEmpty() && mg::PdfOcrLayer::hasLayer(m_docPath);
}

void PdfEditController::makeSearchable() {
    if (m_docPath.isEmpty() || m_searchableBusy)
        return;
    if (!mg::ocr::available()) {
        emit searchableFinished(false, 0, 0, 0, QStringLiteral("noocr"));
        return;
    }
    // Schwebende Seitenoperationen zuerst in die Datei bringen.
    finishOpenSessions();
    finishDrawSession();

    m_searchableCancel = std::make_shared<std::atomic<bool>>(false);
    const int gen = ++m_searchableGen;
    m_searchableBusy = true;
    emit searchableBusyChanged();
    // In eine Nachbardatei schreiben und erst bei Erfolg tauschen.
    m_pool.start(new PdfSearchableTask(this, m_docPath,
                                       m_docPath + QStringLiteral(".mgocr.tmp"),
                                       gen, m_searchableCancel));
}

void PdfEditController::cancelSearchable() {
    if (m_searchableCancel)
        m_searchableCancel->store(true, std::memory_order_relaxed);
}

void PdfEditController::searchableTaskProgress(int done, int total, int generation) {
    if (generation != m_searchableGen)
        return;
    emit searchableProgress(done, total);
}

void PdfEditController::searchableTaskFinished(bool ok, int pages, int words,
                                               int skipped, const QString& error,
                                               int generation) {
    const QString tmp = m_docPath + QStringLiteral(".mgocr.tmp");
    if (generation != m_searchableGen) {            // veralteter Lauf
        QFile::remove(tmp);
        return;
    }
    m_searchableBusy = false;
    emit searchableBusyChanged();

    if (!ok) {
        QFile::remove(tmp);
        emit searchableFinished(false, 0, 0, 0, error);
        return;
    }
    // Tausch auf dem GUI-Faden: erst hier schreibt nichts mehr in die alte Datei.
    if (!QFile::remove(m_docPath) || !QFile::rename(tmp, m_docPath)) {
        QFile::remove(tmp);
        emit searchableFinished(false, 0, 0, 0, QStringLiteral("replace"));
        return;
    }
    m_textWorkValid = false;
    emit documentRewritten();
    emit searchableFinished(true, pages, words, skipped, QString());
}

void PdfEditController::exportTaskProgress(int done, int total, int generation) {
    if (generation != m_exportGen)
        return;
    emit exportProgress(done, total);
}


// Was der Editor kennt, wird zu einer Overlay-Box; was er nicht kennt, bleibt
// als Fremdannotation in der Datei stehen.
bool PdfEditController::boxFromAnnotation(const mg::PdfAnnotation& a, PdfEditBox* out) {
    PdfEditBox b;
    b.srcObjNum = a.objNum;
    b.page      = a.page;                       // QUELLseite; Umrechnung s. Aufrufer
    b.rect      = a.rect;
    b.lineWidth = qBound(0.0, a.borderWidth, 72.0);

    // Der Editor kennt keine eigene Deckkraft, nur Alpha in den Farben.
    const int alpha = qBound(0, int(qRound(a.opacity * 255.0)), 255);
    auto withAlpha = [alpha](QColor c, const QColor& fallback) {
        if (!c.isValid()) c = fallback;
        c.setAlpha(alpha);
        return c;
    };

    switch (a.kind) {
    case mg::PdfAnnotKind::Ink:
        b.kind   = PdfAnnKind::Freehand;
        b.stroke = withAlpha(a.color, QColor(230, 44, 44));
        for (const QVector<QPointF>& stroke : a.inkPaths)
            for (const QPointF& p : stroke)
                b.points.push_back(p);
        if (b.points.size() < 2)
            return false;
        break;
    case mg::PdfAnnotKind::Line:
        if (a.line.size() < 2)
            return false;
        b.kind   = PdfAnnKind::Arrow;
        b.stroke = withAlpha(a.color, QColor(230, 44, 44));
        b.points = a.line;
        break;
    case mg::PdfAnnotKind::Square:
        b.kind   = PdfAnnKind::Rect;
        b.stroke = withAlpha(a.color, QColor(230, 44, 44));
        b.fill   = a.interiorColor.isValid() ? withAlpha(a.interiorColor, Qt::white)
                                             : QColor(0, 0, 0, 0);
        break;
    case mg::PdfAnnotKind::Circle:
        b.kind   = PdfAnnKind::Ellipse;
        b.stroke = withAlpha(a.color, QColor(230, 44, 44));
        b.fill   = a.interiorColor.isValid() ? withAlpha(a.interiorColor, Qt::white)
                                             : QColor(0, 0, 0, 0);
        break;
    case mg::PdfAnnotKind::FreeText:
    case mg::PdfAnnotKind::Text: {
        b.kind       = PdfAnnKind::Text;
        b.text       = a.contents;
        b.fontSizePt = a.fontSizePt > 0.0 ? a.fontSizePt : 12.0;
        b.color      = a.textColor.isValid() ? a.textColor : QColor(0, 0, 0);
        // Sticky Notes tragen ihre Farbe in /C; beide mit /CA-Alpha.
        const QColor paper = a.interiorColor.isValid() ? a.interiorColor : a.color;
        b.highlight  = paper.isValid() ? withAlpha(paper, paper)
                                       : QColor(254, 243, 155, 232);
        b.stroke     = QColor(0, 0, 0, 0);
        // Eine Sticky Note ist nur ein Symbol, ihr Text steht im Popup - daher Mindestgroesse.
        if (a.kind == mg::PdfAnnotKind::Text)
            b.rect = QRectF(a.rect.topLeft(), QSizeF(qMax(a.rect.width(), 140.0),
                                                     qMax(a.rect.height(), 40.0)));
        break;
    }
    case mg::PdfAnnotKind::Highlight:
    case mg::PdfAnnotKind::Underline:
    case mg::PdfAnnotKind::StrikeOut: {
        if (a.quads.isEmpty())
            return false;                        // ohne Bereiche nichts zu zeigen
        b.kind        = PdfAnnKind::Markup;
        b.markupStyle = (a.kind == mg::PdfAnnotKind::Highlight) ? 0
                      : (a.kind == mg::PdfAnnotKind::Underline) ? 1 : 2;
        // Markieren ist durchscheinend, damit der Text lesbar bleibt; Striche sind deckend.
        const QColor base = a.color.isValid()
                                ? a.color
                                : (b.markupStyle == 0 ? QColor(255, 235, 0) : QColor(200, 0, 0));
        b.stroke = withAlpha(base, base);
        if (b.markupStyle == 0 && !a.color.isValid())
            b.stroke.setAlpha(qMin(b.stroke.alpha(), 140));
        for (const QRectF& q : a.quads) {
            b.points.push_back(q.topLeft());
            b.points.push_back(q.bottomRight());
        }
        b.rect = a.rect;
        break;
    }
    default:
        return false;                            // unbekannt: nie raten
    }
    if (b.isStroke())
        b.recomputeBounds();
    *out = b;
    return true;
}


bool PdfEditController::annotationFromBox(const PdfEditBox& b, mg::PdfAnnotation* out) {
    // Nicht abbildbar: Text ersetzen ist eine Deckflaeche (eine Annotation darueber
    // waere wegklickbar), verkettete Boxen bilden zusammen einen Textfluss.
    if (b.kind == PdfAnnKind::Replace || b.chainNext != 0)
        return false;

    mg::PdfAnnotation a;
    a.page = b.page;                         // ANSICHTS-Seite (s. exportBoxes)
    a.rect = b.rect;
    a.borderWidth = b.lineWidth;

    // Genommen wird die durchsichtigste beteiligte Farbe - sonst wirkte eine
    // halbtransparente Notiz im Ergebnis deckend.
    int alpha = 255;
    auto noteAlpha = [&alpha](const QColor& c) {
        if (c.isValid() && c.alpha() > 0) alpha = qMin(alpha, c.alpha());
    };
    auto opaque = [](QColor c) { c.setAlpha(255); return c; };

    switch (b.kind) {
    case PdfAnnKind::Freehand:
        a.kind = mg::PdfAnnotKind::Ink;
        noteAlpha(b.stroke);
        a.color = opaque(b.stroke);
        if (b.points.size() < 2)
            return false;
        a.inkPaths.push_back(b.points);
        break;
    case PdfAnnKind::Arrow:
        if (b.points.size() < 2)
            return false;
        a.kind = mg::PdfAnnotKind::Line;
        noteAlpha(b.stroke);
        a.color = opaque(b.stroke);
        a.line  = { b.points.first(), b.points.last() };
        break;
    case PdfAnnKind::Rect:
    case PdfAnnKind::Ellipse:
        a.kind = (b.kind == PdfAnnKind::Rect) ? mg::PdfAnnotKind::Square
                                              : mg::PdfAnnotKind::Circle;
        noteAlpha(b.stroke);
        a.color = opaque(b.stroke);
        if (b.fill.alpha() > 0) {
            noteAlpha(b.fill);
            a.interiorColor = opaque(b.fill);
        }
        break;
    case PdfAnnKind::Markup: {
        if (b.points.size() < 2)
            return false;
        a.kind = (b.markupStyle == 1) ? mg::PdfAnnotKind::Underline
               : (b.markupStyle == 2) ? mg::PdfAnnotKind::StrikeOut
                                      : mg::PdfAnnotKind::Highlight;
        noteAlpha(b.stroke);
        a.color = opaque(b.stroke);
        for (int i = 0; i + 1 < b.points.size(); i += 2)
            a.quads.push_back(QRectF(b.points.at(i), b.points.at(i + 1)).normalized());
        a.borderWidth = 0.0;
        break;
    }
    case PdfAnnKind::Text:
        a.kind       = mg::PdfAnnotKind::FreeText;
        a.contents   = b.text;
        a.fontSizePt = b.fontSizePt;
        a.textColor  = opaque(b.color);
        if (b.highlight.alpha() > 0) {
            noteAlpha(b.highlight);
            a.interiorColor = opaque(b.highlight);
        }
        a.color       = QColor();
        a.borderWidth = 0.0;
        break;
    default:
        return false;
    }
    a.opacity = qBound(0.0, alpha / 255.0, 1.0);
    *out = a;
    return true;
}

void PdfEditController::startAnnotRead() {
    const int gen = ++m_annotReadGen;
    if (m_docPath.isEmpty())
        return;
    m_pool.start(new PdfAnnotReadTask(this, pristinePath(), gen));
}

void PdfEditController::annotReadFinished(const QVector<mg::PdfAnnotation>& annots,
                                          int generation) {
    if (generation != m_annotReadGen)
        return;                                     // veralteter Lauf

    // Bereits im Modell liegende Uebernahmen duerfen nicht ein zweites Mal entstehen.
    QSet<int> known;
    const QVector<PdfEditBox> existing = m_model.boxes();
    for (const PdfEditBox& b : existing)
        if (b.srcObjNum > 0)
            known.insert(b.srcObjNum);

    m_importBaseline.clear();
    QVector<PdfEditBox> fresh;
    for (const mg::PdfAnnotation& a : annots) {
        if (a.objNum <= 0)
            continue;
        PdfEditBox b;
        if (!boxFromAnnotation(a, &b))
            continue;
        const int key = pageKeyForView(b.page);
        if (key < 0)
            continue;
        b.page = key;
        // Der Zustand aus der Datei ist die Vergleichsgrundlage - auch fuer Boxen aus
        // dem Sidecar, die laengst veraendert sein koennen.
        m_importBaseline.insert(a.objNum, b.toJson());
        if (!known.contains(a.objNum))
            fresh.push_back(b);
    }

    if (fresh.isEmpty())
        return;

    // Uebernahmen sind keine Nutzeraktion: kein Undo-Kommando, kein dirty.
    for (PdfEditBox& b : fresh) {
        b.id = m_nextId++;
        m_model.insertBoxAt(m_model.count(), b);
    }
}

bool PdfEditController::importChanged(const PdfEditBox& b) const {
    if (b.srcObjNum <= 0)
        return false;
    const auto it = m_importBaseline.constFind(b.srcObjNum);
    if (it == m_importBaseline.constEnd())
        return true;                                // Herkunft unbekannt -> sicherheitshalber
    return b.toJson() != it.value();
}

// Nur wenn die Einstellung es verlangt UND sich jede Notiz abbilden laesst -
// eine Mischung aus Annotation und gemalt waere die schlechteste Antwort.
QVector<mg::PdfAnnotation> PdfEditController::exportAnnotations() const {
    QVector<mg::PdfAnnotation> out;
    if (!m_settings.pdfExportAsAnnotations())
        return out;
    const QVector<PdfEditBox> boxes = exportBoxes();
    if (boxes.isEmpty())
        return out;
    out.reserve(boxes.size());
    for (const PdfEditBox& b : boxes) {
        mg::PdfAnnotation a;
        if (!annotationFromBox(b, &a))
            return {};                      // eine reicht -> alles malen
        out.push_back(a);
    }
    return out;
}

QVector<mg::PdfRedactArea> PdfEditController::redactionAreas() const {
    QVector<mg::PdfRedactArea> out;
    const QVector<PdfEditBox> boxes = exportBoxes();     // Seiten = Ansichts-Index
    for (const PdfEditBox& b : boxes)
        if (b.kind == PdfAnnKind::Redact && b.rect.isValid())
            out.push_back({ b.page, b.rect });
    return out;
}

QVector<mg::PdfTextEdit> PdfEditController::redactionEdits() const {
    QVector<mg::PdfTextEdit> out;
    const QVector<PdfEditBox> boxes = exportBoxes();     // Seiten = Ansichts-Index
    for (const PdfEditBox& b : boxes)
        if (b.kind == PdfAnnKind::Redact && !b.origText.isEmpty())
            out.push_back({ b.page, b.origText, QString() });
    return out;
}

void PdfEditController::redactionFellBack(int generation) {
    if (generation != m_exportGen)
        return;
    emit contentEditFellBack();
}

QVector<int> PdfEditController::importRemovals() const {
    QVector<int> out;
    if (m_importBaseline.isEmpty())
        return out;
    QHash<int, const PdfEditBox*> byObj;
    const QVector<PdfEditBox> boxes = m_model.boxes();
    for (const PdfEditBox& b : boxes)
        if (b.srcObjNum > 0)
            byObj.insert(b.srcObjNum, &b);
    for (auto it = m_importBaseline.constBegin(); it != m_importBaseline.constEnd(); ++it) {
        const PdfEditBox* b = byObj.value(it.key(), nullptr);
        if (!b || importChanged(*b))                // gelöscht ODER verändert
            out.push_back(it.key());
    }
    return out;
}

// Gelesen wird immer die pristine Datei; ihr page ist ein Quellseiten-Index,
// die Anzeige spricht dagegen Ansichts-Seiten.
void PdfEditController::startFormRead() {
    const int gen = ++m_formReadGen;
    if (m_docPath.isEmpty())
        return;
    m_pool.start(new PdfFormReadTask(this, pristinePath(), gen));
}

void PdfEditController::formReadFinished(const QVector<mg::PdfFormField>& fields,
                                         int generation) {
    if (generation != m_formReadGen)
        return;                                     // veralteter Lauf
    m_formFields = fields;
    // Werte fuer Felder, die es hier nicht mehr gibt, stammen aus einem fremden Sidecar.
    if (!m_formEdits.isEmpty()) {
        QSet<QString> known;
        known.reserve(m_formFields.size());
        for (const mg::PdfFormField& f : m_formFields)
            if (!f.readOnly)
                known.insert(f.name);
        for (auto it = m_formEdits.begin(); it != m_formEdits.end(); ) {
            if (known.contains(it.key()))
                ++it;
            else
                it = m_formEdits.erase(it);
        }
        setFormDirty(!m_formEdits.isEmpty());
    }
    ++m_formValueRev;
    emit formValueRevChanged();
    emit formFieldsChanged();
}

QVariantList PdfEditController::formFields() const {
    QVariantList out;
    if (m_formFields.isEmpty())
        return out;
    out.reserve(m_formFields.size());
    for (const mg::PdfFormField& f : m_formFields) {
        if (f.page < 0)
            continue;                               // nicht platziertes Widget
        // Pristine Seiten tragen key == src; eine entfernte Seite liefert -1.
        const int view = m_plan.isEmpty() ? f.page : viewOfKey(f.page);
        if (view < 0)
            continue;
        QVariantMap m;
        m.insert(QStringLiteral("name"),      f.name);
        m.insert(QStringLiteral("tooltip"),   f.tooltip);
        m.insert(QStringLiteral("type"),      static_cast<int>(f.type));
        m.insert(QStringLiteral("page"),      view);
        m.insert(QStringLiteral("rot"),       pageRotation(view));
        m.insert(QStringLiteral("xPt"),       f.rect.x());
        m.insert(QStringLiteral("yPt"),       f.rect.y());
        m.insert(QStringLiteral("wPt"),       f.rect.width());
        m.insert(QStringLiteral("hPt"),       f.rect.height());
        m.insert(QStringLiteral("value"),
                 m_formEdits.contains(f.name) ? m_formEdits.value(f.name) : f.value);
        m.insert(QStringLiteral("onState"),   f.onState);
        m.insert(QStringLiteral("options"),   f.options);
        m.insert(QStringLiteral("optionValues"), f.optionValues);
        m.insert(QStringLiteral("readOnly"),  f.readOnly);
        m.insert(QStringLiteral("required"),  f.required);
        m.insert(QStringLiteral("multiline"), f.multiline);
        m.insert(QStringLiteral("password"),  f.password);
        m.insert(QStringLiteral("combo"),     f.combo);
        m.insert(QStringLiteral("editable"),  f.editable);
        m.insert(QStringLiteral("maxLen"),    f.maxLen);
        out.push_back(m);
    }
    return out;
}

QString PdfEditController::formOriginalValue(const QString& name, bool* found,
                                             bool* readOnly) const {
    if (found)    *found = false;
    if (readOnly) *readOnly = false;
    for (const mg::PdfFormField& f : m_formFields) {
        if (f.name != name)
            continue;
        if (found)    *found = true;
        if (readOnly) *readOnly = f.readOnly;
        return f.value;
    }
    return {};
}

void PdfEditController::setFormDirty(bool v) {
    if (m_formDirty == v)
        return;
    m_formDirty = v;
    emit formDirtyChanged();
}

void PdfEditController::setFormValue(const QString& name, const QString& value) {
    if (m_docPath.isEmpty() || name.isEmpty())
        return;
    bool found = false, readOnly = false;
    const QString orig = formOriginalValue(name, &found, &readOnly);
    if (!found || readOnly)
        return;                                     // unbekannt/schreibgeschützt
    if (value == orig)
        m_formEdits.remove(name);                   // wieder im Ursprungszustand
    else
        m_formEdits.insert(name, value);
    setFormDirty(!m_formEdits.isEmpty());
    // Bewusst kein formFieldsChanged: nur die Werte aendern sich, nicht die Liste.
    ++m_formValueRev;
    emit formValueRevChanged();
}

QString PdfEditController::formValue(const QString& name) const {
    const auto it = m_formEdits.constFind(name);
    if (it != m_formEdits.constEnd())
        return it.value();
    bool found = false, readOnly = false;
    return formOriginalValue(name, &found, &readOnly);
}

QString PdfEditController::formTargetPath() const {
    if (m_docPath.isEmpty())
        return {};
    return uniqueSuffixPath(m_docPath, QStringLiteral("_ausgefuellt"));
}

void PdfEditController::saveFormValues() {
    if (m_busy || m_docPath.isEmpty() || m_formFields.isEmpty() || m_formEdits.isEmpty())
        return;
    const QString target = formTargetPath();
    if (target.isEmpty())
        return;
    m_busy = true;
    emit busyChanged();
    const int gen = ++m_formSaveGen;
    // Bei abweichender Seitenfolge zuerst ausfuellen, dann den Plan anwenden -
    // sonst truege die Kopie die alte Reihenfolge.
    const bool applyPlan = !planIsIdentity();
    const QString fillTo = applyPlan ? (m_docPath + QStringLiteral(".mgformfill.pdf"))
                                     : target;
    m_formPlanTarget = applyPlan ? target : QString();
    m_pool.start(new PdfFormSaveTask(this, pristinePath(), fillTo, m_formEdits,
                                     gen, applyPlan));
}

void PdfEditController::formSaveFinished(bool ok, const QString& target,
                                         const QString& error, int generation,
                                         bool applyPlan) {
    if (generation != m_formSaveGen)
        return;                                     // veralteter Lauf

    QString finalTarget = target;
    bool flattened = false;
    if (ok && applyPlan && !m_formPlanTarget.isEmpty()) {
        // Die Feldwerte stehen bereits im Seiteninhalt; die Kopie zeigt sie ueberall,
        // ist dafuer aber kein bedienbares Formular mehr.
        QString aerr;
        if (assemblePlanTo(m_formPlanTarget, target, &aerr)) {
            finalTarget = m_formPlanTarget;
            flattened   = true;
        } else {
            ok = false;
        }
        QFile::remove(target);                      // Zwischendatei aufräumen
        m_formPlanTarget.clear();
    }

    m_busy = false;
    emit busyChanged();
    // Die Werte bleiben im Puffer, nur der Zustand noch nirgends geschrieben faellt.
    if (ok)
        setFormDirty(false);
    emit formSaved(ok, finalTarget, error, flattened);
}

QStringList PdfEditController::standardFonts() const {
    // Fehlt eine Familie, substituiert Qt beim Rendern - identisch in Anzeige und
    // Export, da beide dieselbe Font-Aufloesung nutzen.
    return { QStringLiteral("Times New Roman"),
             QStringLiteral("Arial"),
             QStringLiteral("Calibri"),
             QStringLiteral("Helvetica"),
             QStringLiteral("Courier New") };
}

QString PdfEditController::resolvedFont(const QString& family) const {
    // Liefert die Familie, die die Font-Datenbank tatsaechlich aufloest.
    return QFontInfo(QFont(family)).family();
}
