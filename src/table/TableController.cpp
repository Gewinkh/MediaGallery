#include "table/TableController.h"

#include "core/PathUtils.h"
#include "core/Strings.h"
#include "editor/TextPdfExporter.h"
#include "table/TablePdf.h"
#include "table/TableWidths.h"
#include "table/TableWriter.h"

#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHashFunctions>
#include <QPointer>
#include <QRunnable>
#include <QSaveFile>
#include <QVariantMap>
#include <algorithm>
#include <functional>

namespace mg::table {
namespace {

//  Derselbe Deckel wie beim Buchungsstapel: die Tabelle haelt je Zeile ihre
//  Felder, und darueber lohnt keine Anzeige mehr.
constexpr qint64 kMaxBytes = 32LL * 1024 * 1024;

//  So viele Zeilen sieht die Breitenmessung an.
constexpr int kProbeZeilen = 500;

//  Obergrenze fuer „Bereich kopieren": darueber dauert der Aufbau im GUI-Faden
//  laenger, als das Ergebnis jemandem nuetzt.
constexpr qint64 kMaxBereichZellen = 500000;

//  Rueckgaengig: so viele Schritte und so viele Zeichen darin. Eine geloeschte
//  Spalte einer grossen Tabelle traegt jeden ihrer Werte.
constexpr size_t    kMaxSchritte = 1000;
constexpr qsizetype kMaxUndoZeichen = 16 * 1024 * 1024;

quint64 pruefsumme(const QByteArray& b) {
    return quint64(qHashBits(b.constData(), size_t(b.size()), 0));
}

qint64 aenderungszeit(const QString& pfad) {
    return QFileInfo(pfad).lastModified().toMSecsSinceEpoch();
}

using LeseZurueck = std::function<void(std::shared_ptr<Datei>, QString, quint64, qint64, qint64)>;

class LeseTask : public QRunnable {
public:
    LeseTask(TableController* owner, QString pfad,
             std::shared_ptr<std::atomic<bool>> abbruch, LeseZurueck zurueck)
        : m_owner(owner), m_pfad(std::move(pfad)),
          m_abbruch(std::move(abbruch)), m_zurueck(std::move(zurueck)) {
        setAutoDelete(true);
    }

    void run() override {
        QString fehler;
        auto d = std::make_shared<Datei>();
        quint64 summe = 0;
        qint64 groesse = -1;
        const qint64 zeit = aenderungszeit(m_pfad);
        QFile f(m_pfad);
        if (!f.open(QIODevice::ReadOnly)) {
            fehler = QStringLiteral("nicht lesbar");
        } else {
            const QByteArray roh = f.read(kMaxBytes);
            groesse = f.size();
            f.close();
            if (m_abbruch->load()) return;
            summe = pruefsumme(roh);
            //  Ohne Trennzeichen: `parse` raet es aus den ersten Zeilen.
            *d = parse(roh);
            if (groesse > kMaxBytes) d->abgeschnitten = true;
            if (!d->ok) fehler = d->fehler;
        }
        if (m_abbruch->load()) return;
        auto zurueck = m_zurueck;
        QMetaObject::invokeMethod(m_owner, [zurueck, d, fehler, summe, groesse, zeit] {
            zurueck(d, fehler, summe, groesse, zeit);
        }, Qt::QueuedConnection);
    }

private:
    TableController* m_owner;
    QString m_pfad;
    std::shared_ptr<std::atomic<bool>> m_abbruch;
    LeseZurueck m_zurueck;
};

//  Dasselbe Muster wie der DOCX-Editor: `<name>_edited.<endung>`, bei Bedarf
//  mit laufender Nummer - das Original bleibt, wie es ist.
QString kopiePfad(const QString& quelle) {
    const QFileInfo fi(quelle);
    const QString basis = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName()
                          + QStringLiteral("_edited");
    const QString endung = fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix();
    QString kandidat = basis + endung;
    for (int n = 2; QFileInfo::exists(kandidat); ++n)
        kandidat = basis + QStringLiteral(" (%1)").arg(n) + endung;
    return kandidat;
}

}  // namespace

struct TableController::SpeicherErgebnis {
    enum class Art { Ok, Konflikt, Kodierung, Schreibfehler };
    Art        art = Art::Ok;
    bool       kopie = false;
    bool       uebernommen = false;
    int        fehlerZeile = -1;
    QString    ziel;
    Grundlage  grundlage;
    QList<int> zeilenNr;
    QList<quint32> ids;
    quint32    rev = 0;
    quint32    ladeGen = 0;
};

TableController::TableController(QObject* parent) : QObject(parent) {
    m_pool.setMaxThreadCount(1);
    m_pdfPool.setMaxThreadCount(1);
    connect(this, &TableController::stateChanged, this, &TableController::rowsChanged);
}

TableController::~TableController() {
    //  Beim Zerstoeren haengt keine Oberflaeche mehr an den Signalen.
    blockSignals(true);
    flush();
    if (m_abbruch) m_abbruch->store(true);
    if (m_suchAbbruch) m_suchAbbruch->store(true);
    if (m_sortAbbruch) m_sortAbbruch->store(true);
    m_pdfAbbruch->store(true);
    m_pool.waitForDone();
    m_pdfPool.waitForDone();
}

QString TableController::pdfTarget() const { return TextPdf::targetPathFor(m_source); }

void TableController::countPdfPages(const QVariantMap& opt) {
    pdfStarten(PdfArt::Zaehlen, QString(), opt, ++m_pdfZaehlGen);
}

void TableController::previewPdf(const QVariantMap& opt) {
    const QString ziel = m_vorschau.neu();
    if (!ziel.isEmpty()) pdfStarten(PdfArt::Vorschau, ziel, opt, ++m_vorschauGen);
}

void TableController::exportPdf(const QString& target, const QVariantMap& opt) {
    if (m_pdfBusy || !m_datei || target.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, target] {
            emit pdfExportFinished(false, target, Strings::get(StringKey::MarkdownPdfNotReady));
        }, Qt::QueuedConnection);
        return;
    }
    m_pdfBusy = true;
    emit pdfBusyChanged();
    pdfStarten(PdfArt::Schreiben, mg::toLocalPath(target), opt, 0);
}

// Die Teile entstehen HIER im GUI-Faden und halten die Datei ueber den Anker: bearbeitet jemand waehrenddessen,
// schreibt der Controller in eine eigene Kopie (`schreibDatei`), der Export liest den Stand vom Start.
void TableController::pdfStarten(PdfArt art, const QString& ziel, const QVariantMap& opt, int gen) {
    QList<PdfTeil> teile;
    if (m_datei && opt.value(QStringLiteral("all")).toBool() && m_bereiche.size() > 1) {
        teile = teileAusDatei(m_datei, m_bereiche, m_werte, m_breiten, m_formate, m_gruppiert, m_dezimalKomma);
    } else if (m_datei) {
        const Bereich b = aktiv();
        PdfTeil t;
        if (m_bereiche.size() > 1 && m_block >= 0)
            t.titel = m_bloecke.value(m_block).toMap().value(QStringLiteral("title")).toString();
        QList<int> spalten;
        for (const QVariant& v : std::as_const(m_spalten)) {
            const QVariantMap m = v.toMap();
            const int i = m.value(QStringLiteral("index")).toInt();
            spalten.append(i);
            PdfSpalte sp;
            sp.titel = m.value(QStringLiteral("title")).toString();
            sp.px = m_breiten.value(i, 0);
            sp.zahl = spalteIstZahl(m_datei->zeilen, b.daten, b.bis, i, m_werte.get());
            sp.format = m_formate.value(i);
            t.spalten.append(sp);
        }
        t.zeilen = rowCount();
        t.zelle = [datei = std::shared_ptr<const Datei>(m_datei), werte = m_werte, spalten,
                   ordnung = m_ordnungAktiv ? m_ordnung : QList<int>(), sortiert = m_ordnungAktiv,
                   daten = b.daten, gruppiert = m_gruppiert, komma = m_dezimalKomma](int z, int s) {
            const int roh = sortiert ? ordnung.at(z) : daten + z;
            return zahlAnzeigen(gezeigterWert(datei->zeilen, roh, spalten.at(s), werte.get()), gruppiert, komma);
        };
        teile.append(std::move(t));
    }

    PdfOptionen o;
    o.druck = opt.value(QStringLiteral("print")).toBool();
    o.quer = opt.value(QStringLiteral("landscape")).toBool();
    o.gitter = opt.value(QStringLiteral("grid")).toBool();
    o.von = opt.value(QStringLiteral("first"), 1).toInt();
    o.bis = opt.value(QStringLiteral("last"), 0).toInt();
    for (const QVariant& v : opt.value(QStringLiteral("pages")).toList()) o.seiten.append(v.toInt());
    o.schrift = opt.value(QStringLiteral("font")).value<QFont>();
    o.grund = opt.value(QStringLiteral("background")).value<QColor>();
    o.text = opt.value(QStringLiteral("text")).value<QColor>();
    o.kopfGrund = opt.value(QStringLiteral("headerBackground")).value<QColor>();
    o.kopfText = opt.value(QStringLiteral("headerText")).value<QColor>();
    if (!o.text.isValid()) o.text = Qt::black;

    class PdfTask : public QRunnable {
    public:
        std::function<void()> arbeit;
        void run() override { arbeit(); }
    };
    auto* task = new PdfTask;
    task->setAutoDelete(true);
    QPointer<TableController> self(this);
    task->arbeit = [self, art, teile = std::move(teile), o, ziel, gen, abbruch = m_pdfAbbruch] {
        QString err;
        const int seiten = schreibePdf(teile, o, ziel, &err, abbruch.get());
        if (abbruch->load()) return;
        QMetaObject::invokeMethod(self, [self, art, seiten, err, ziel, gen] {
            if (!self) return;
            switch (art) {
            case PdfArt::Zaehlen:
                //  Nur die juengste Zaehlung - der Dialog fragt bei jedem Umschalten neu.
                if (gen == self->m_pdfZaehlGen) emit self->pdfPagesCounted(seiten);
                return;
            case PdfArt::Vorschau:
                if (gen != self->m_vorschauGen || seiten <= 0) {
                    self->m_vorschau.verwerfe(ziel);
                    return;
                }
                self->m_vorschau.uebernehme(ziel);
                emit self->pdfPreviewReady(ziel, seiten);
                return;
            case PdfArt::Schreiben:
                self->m_pdfBusy = false;
                emit self->pdfBusyChanged();
                emit self->pdfExportFinished(seiten > 0, ziel, err);
                return;
            }
        }, Qt::QueuedConnection);
    };
    m_pdfPool.start(task);
}

void TableController::setSource(const QString& pathOrUrl) {
    const QString pfad = mg::toLocalPath(pathOrUrl);
    if (pfad == m_source) return;
    //  Vor dem Wechsel sichern - danach kennt der Controller die alte Datei nicht mehr.
    flush();
    ++m_vorschauGen;
    m_vorschau.leere();
    m_source = pfad;
    emit sourceChanged();
    neuLesen();
}

void TableController::neuLesen() {
    if (m_abbruch) m_abbruch->store(true);
    ++m_ladeGen;
    clearSearch();
    m_filter = {};
    m_sortSpalte = -1;
    m_sortRichtung = SortRichtung::Keine;
    if (m_sortAbbruch) m_sortAbbruch->store(true);
    m_sortLaeuft = false;
    m_ordnung.clear();
    m_ordnungAktiv = false;
    ++m_inhaltRevision;
    m_versteckt.clear();
    m_datei.reset();
    m_werte.reset();
    m_formelZahl = 0;
    m_fehler.clear();
    m_spalten.clear();
    m_warnungen.clear();
    m_spaltenZahl = 0;
    m_info.clear();
    m_undo.clear();
    m_redo.clear();
    m_undoZeichen = 0;
    m_rev = m_gespeichertRev = m_basisRev = 0;
    m_speichert = false;
    m_nochmal = false;
    m_speicherFehler.clear();
    m_kopieName.clear();
    m_grundlage = {};
    m_basisZeilenNr.clear();
    m_busy = !m_source.isEmpty();
    emit sortChanged();
    emit editChanged();
    emit stateChanged();
    emit blockChanged();
    if (m_source.isEmpty()) return;

    m_abbruch = std::make_shared<std::atomic<bool>>(false);
    auto* self = this;
    m_pool.start(new LeseTask(this, m_source, m_abbruch,
                              [self](std::shared_ptr<Datei> d, QString fehler, quint64 summe,
                                     qint64 groesse, qint64 zeit) {
                                  self->ergebnisUebernehmen(std::move(d), fehler,
                                                            Grundlage{summe, groesse, zeit});
                              }));
}

void TableController::ergebnisUebernehmen(std::shared_ptr<Datei> d, const QString& fehler,
                                          Grundlage g) {
    m_busy = false;
    m_fehler = fehler;
    m_datei = d && d->ok ? std::move(d) : nullptr;
    m_grundlage = g;
    m_basisZeilenNr = m_datei ? m_datei->zeilenNr : QList<int>();

    m_warnungen.clear();
    m_bereiche.clear();
    m_bloecke.clear();
    m_block = -1;
    if (m_datei) {
        m_bereiche = findBlocks(*m_datei);
        //  Bei mehreren Tabellen wird die ERSTE gezeigt, nicht die flache
        //  Gesamtliste: die Bloecke haben verschiedene Spalten, und
        //  uebereinandergelegt ergaeben sie eine Aufzaehlung ohne Kopfzeile.
        if (m_bereiche.size() > 1) m_block = 0;
        bloeckeNeuBauen();
        for (const Warnung& w : std::as_const(m_datei->warnungen))
            m_warnungen.append(QStringLiteral("%1: %2").arg(w.zeile).arg(w.text));
    }
    formelnNeuRechnen();
    //  Breiten und Formate gehoeren zur DATEI, nicht zur Flaeche - sie kommen
    //  mit ihr.
    if (m_datei) mg::table::liesSpalten(m_source, m_breiten, m_formate);
    else { m_breiten.clear(); m_formate.clear(); }
    spaltenNeuRechnen();
    emit stateChanged();
    emit blockChanged();
}

void TableController::setColumnWidth(int column, int px) {
    if (column < 0) return;
    if (px <= 0) {
        if (m_breiten.remove(column) == 0) return;
    } else {
        const int neu = qBound(kMinBreite, px, kMaxBreite);
        if (m_breiten.value(column, 0) == neu) return;
        m_breiten.insert(column, neu);
    }
    spaltenAblegen();
    spaltenNeuRechnen();
    emit stateChanged();
}

void TableController::spaltenAblegen() {
    schreibeSpalten(m_source, m_breiten, m_formate);
}

//  Alle drei Formatgriffe laufen hier zusammen: aendern, ein leer gewordenes
//  Format wegwerfen, ablegen, Spalten neu bauen.
void TableController::formatAendern(int column,
                                    const std::function<void(SpaltenFormat&)>& aendere) {
    if (column < 0) return;
    SpaltenFormat f = m_formate.value(column);
    const SpaltenFormat vorher = f;
    aendere(f);
    if (f == vorher) return;
    if (f.leer()) m_formate.remove(column);
    else          m_formate.insert(column, f);
    spaltenAblegen();
    spaltenNeuRechnen();
    emit stateChanged();
}

void TableController::setColumnBold(int column, bool bold) {
    formatAendern(column, [bold](SpaltenFormat& f) { f.fett = bold; });
}

void TableController::setColumnColor(int column, const QString& color) {
    formatAendern(column, [&color](SpaltenFormat& f) { f.farbe = color; });
}

void TableController::setColumnBackground(int column, const QString& color) {
    formatAendern(column, [&color](SpaltenFormat& f) { f.hintergrund = color; });
}

void TableController::clearColumnFormat(int column) {
    formatAendern(column, [](SpaltenFormat& f) { f = SpaltenFormat(); });
}

//  Je Block ein eigener Bezugspunkt: A1 ist SEINE erste Datenzeile, auch wenn
//  gerade die flache Gesamtansicht gezeigt wird. Eine Formel darf ihren Wert
//  nicht mit der Ansicht wechseln.
void TableController::formelnNeuRechnen() {
    m_werte.reset();
    m_formelZahl = 0;
    if (!m_datei) return;

    QList<Bereich> bereiche = m_bereiche;
    if (bereiche.isEmpty()) bereiche.append(aktiv());

    QList<FormelBereich> auftrag;
    for (const Bereich& b : std::as_const(bereiche)) {
        FormelBereich f;
        f.daten = b.daten;
        f.bis = b.bis;
        f.zellen = sammleFormeln(m_datei->zeilen, b.daten, b.bis);
        m_formelZahl += int(f.zellen.size());
        if (!f.zellen.isEmpty()) auftrag.append(std::move(f));
    }
    if (auftrag.isEmpty()) return;
    m_dezimalKomma = dezimalKomma(m_datei->zeilen, 0, int(m_datei->zeilen.size()),
                                  m_datei->trenner);
    m_werte = rechne(m_datei->zeilen, auftrag, m_dezimalKomma);
}

Bereich TableController::aktiv() const {
    if (m_block >= 0 && m_block < m_bereiche.size()) return m_bereiche.at(m_block);
    //  Eine Datei ohne Leerzeilen hat GENAU EINEN Bereich - dann ist der auch
    //  der aktive, samt seiner erkannten Kopfzeile. Der flache Rueckfallweg
    //  gilt nur, wo es wirklich mehrere Bloecke gibt.
    if (m_bereiche.size() == 1) return m_bereiche.at(0);
    //  „Alles": die ganze Datei, so wie sie dasteht - Titel- und Kopfzeilen
    //  der Bloecke stehen dann als gewoehnliche Zeilen darin.
    Bereich ganz;
    ganz.von = 0;
    ganz.bis = m_datei ? int(m_datei->zeilen.size()) : 0;
    ganz.daten = 0;
    return ganz;
}

int TableController::aktiverBereich() const {
    if (m_block >= 0 && m_block < m_bereiche.size()) return m_block;
    return m_bereiche.size() == 1 ? 0 : -1;
}

void TableController::bloeckeNeuBauen() {
    m_bloecke.clear();
    if (m_bereiche.size() < 2) return;
    m_bloecke.reserve(m_bereiche.size());
    for (int i = 0; i < m_bereiche.size(); ++i) {
        const Bereich& b = m_bereiche.at(i);
        QString titel;
        if (b.titel >= 0)      titel = m_datei->zeilen.at(b.titel).wert(0).trimmed();
        else if (b.kopf >= 0)  titel = m_datei->zeilen.at(b.kopf).wert(0).trimmed();
        if (titel.isEmpty())
            titel = Strings::get(StringKey::TableBlock) + QLatin1Char(' ')
                    + QString::number(i + 1);
        QVariantMap m;
        m.insert(QStringLiteral("index"), i);
        m.insert(QStringLiteral("title"), titel);
        m.insert(QStringLiteral("rows"), b.bis - b.daten);
        m_bloecke.append(m);
    }
}

void TableController::setCurrentBlock(int i) {
    const int neu = (i >= 0 && i < m_bereiche.size()) ? i : -1;
    if (neu == m_block) return;
    m_block = neu;
    //  Spaltennummern bedeuten im naechsten Block etwas anderes - eine dort
    //  ausgeblendete, sortierte oder gefilterte Spalte waere eine willkuerlich andere.
    m_versteckt.clear();
    m_filter = {};
    clearSort();
    spaltenNeuRechnen();
    emit blockChanged();
    emit stateChanged();
    //  Die Treffer zaehlen ab der ersten Datenzeile DIESES Blocks - im neuen
    //  Block zeigten die alten Nummern auf beliebige Zeilen.
    if (!m_suchText.isEmpty()) { m_suchAb = 0; sucheStarten(); }
}

void TableController::search(const QString& text, bool caseSensitive,
                             bool wholeCell, int fromRow) {
    m_suchText = text;
    m_suchOpt.gross = caseSensitive;
    m_suchOpt.ganzeZelle = wholeCell;
    m_suchAb = qMax(0, fromRow);
    sucheStarten();
}

void TableController::sucheStarten() {
    if (m_suchAbbruch) m_suchAbbruch->store(true);
    m_andereBloecke = 0;
    //  Ein Filter ohne Treffer zeigt keine Zeile - dort kann auch nichts stehen.
    if (!m_datei || m_suchText.isEmpty() || (m_ordnungAktiv && m_ordnung.isEmpty())) {
        m_suchLaeuft = false;
        m_suche.leeren();
        emit searchChanged();
        return;
    }
    m_suchLaeuft = true;
    emit searchChanged();

    const Bereich b = aktiv();
    //  Je Block zaehlen, sobald es mehr als einen gibt.
    QList<BlockBereich> bloecke;
    if (m_bereiche.size() > 1) {
        bloecke.reserve(m_bereiche.size());
        for (const Bereich& x : std::as_const(m_bereiche))
            bloecke.append({ x.daten, x.bis });
    }
    m_suchAbbruch = std::make_shared<std::atomic<bool>>(false);
    auto* self = this;
    //  Die Datei haelt sich ueber den Anker am Leben, auch wenn inzwischen eine
    //  andere gelesen wird.
    m_pool.start(new SuchTask(this, m_datei, &m_datei->zeilen, b.daten, b.bis,
                              m_suchText, m_suchOpt, spaltenMaske(),
                              bloecke, m_suchAbbruch,
                              [self](QList<Treffer> t, bool mehr, QList<int> proBlock) {
                                  self->suchErgebnis(std::move(t), mehr, std::move(proBlock));
                              },
                              m_ordnungAktiv ? m_ordnung : QList<int>(), m_werte));
}

void TableController::suchErgebnis(QList<Treffer> treffer, bool mehr,
                                   QList<int> proBlock) {
    m_suchLaeuft = false;
    m_trefferProBlock = std::move(proBlock);
    //  „anderswo" heisst: alles ausser dem gerade gezeigten Block.
    m_andereBloecke = 0;
    for (int i = 0; i < m_trefferProBlock.size(); ++i)
        if (i != m_block) m_andereBloecke += m_trefferProBlock.at(i);
    m_suche.setzeTreffer(std::move(treffer), mehr);
    m_suche.gehZuAb(m_suchAb);
    emit searchChanged();
}

QVariantList TableController::otherBlocksWithMatches() const {
    QVariantList out;
    for (int i = 0; i < m_trefferProBlock.size() && i < m_bloecke.size(); ++i) {
        if (i == m_block || m_trefferProBlock.at(i) <= 0) continue;
        QVariantMap m = m_bloecke.at(i).toMap();
        m.insert(QStringLiteral("count"), m_trefferProBlock.at(i));
        out.append(m);
    }
    return out;
}

void TableController::jumpToBlock(int index) {
    if (index < 0 || index >= m_bereiche.size()) return;
    setCurrentBlock(index);
    //  Von vorn: der Sprung soll beim ERSTEN Treffer der Tabelle landen.
    m_suchAb = 0;
    sucheStarten();
}

void TableController::stepMatch(int delta) {
    if (m_suche.anzahl() == 0) return;
    m_suche.schritt(delta);
    emit searchChanged();
}

void TableController::clearSearch() {
    if (m_suchAbbruch) m_suchAbbruch->store(true);
    m_suchText.clear();
    m_suchLaeuft = false;
    m_andereBloecke = 0;
    m_suche.leeren();
    emit searchChanged();
}

bool TableController::headerRow() const {
    return aktiv().kopf >= 0;
}

void TableController::spaltenNeuRechnen() {
    m_spalten.clear();
    m_spaltenZahl = 0;
    if (!m_datei) return;

    const Bereich b = aktiv();
    //  Die breiteste Datenzeile bestimmt die Spaltenzahl - eine kurze Zeile ist
    //  kein Grund, eine vorhandene Spalte zu verschweigen.
    const int bis = qMin(b.bis, b.daten + kProbeZeilen);
    for (int i = b.daten; i < bis; ++i)
        m_spaltenZahl = qMax(m_spaltenZahl, m_datei->zeilen.at(i).felder());
    if (b.kopf >= 0)
        m_spaltenZahl = qMax(m_spaltenZahl, m_datei->zeilen.at(b.kopf).felder());

    const QStringList namen = b.kopf >= 0 ? m_datei->zeilen.at(b.kopf).alle() : QStringList();
    m_spalten.reserve(m_spaltenZahl);

    for (int i = 0; i < m_spaltenZahl; ++i) {
        if (m_versteckt.contains(i)) continue;
        //  Ohne Kopfzeile bleibt der Name LEER - die Nummer kommt aus der
        //  eigenen Leiste (Schalter in der oberen Leiste). Beides zugleich
        //  zeigte die Zahl doppelt.
        const QString titel = namen.value(i);

        int zeichen = int(titel.size());
        //  Die acht laengsten Zellen selbst: die Ansicht misst sie mit ihrer Schrift. Die laengste allein reicht
        //  nicht ("Zimmermann" ist breiter als "Christiansen"); geschaetzt ueber die mittlere Zeichenbreite war
        //  "Studiengang" doppelt so breit wie sein breitester Eintrag.
        QStringList laengste;
        for (int z = b.daten; z < bis; ++z) {
            const QString w = gezeigterWert(m_datei->zeilen, z, i, m_werte.get());
            if (laengste.size() == 8 && w.size() <= laengste.last().size()) continue;
            if (laengste.contains(w)) continue;
            auto pos = std::upper_bound(laengste.begin(), laengste.end(), w,
                                        [](const QString& a, const QString& b) { return a.size() > b.size(); });
            laengste.insert(pos, w);
            if (laengste.size() > 8) laengste.removeLast();
        }
        if (!laengste.isEmpty()) zeichen = qMax(zeichen, int(laengste.first().size()));
        for (QString& w : laengste) w = zahlAnzeigen(w, m_gruppiert, m_dezimalKomma);

        QVariantMap m;
        m.insert(QStringLiteral("index"), i);
        m.insert(QStringLiteral("title"), titel);
        //  Die Breite steht HIER, nicht in der Zelle: je Zelle gerechnet kostete
        //  das beim Rollen je neuer Zeile einen Lauf ueber die Probe mal Spalte.
        m.insert(QStringLiteral("chars"), zeichen);
        m.insert(QStringLiteral("widest"), laengste);
        m.insert(QStringLiteral("px"), m_breiten.value(i, 0));
        //  Die drei Formatfelder stehen nur da, wo etwas gesetzt ist - eine
        //  Zelle fragt sonst je Bild nach drei Werten, die nie belegt sind.
        const auto fm = m_formate.constFind(i);
        if (fm != m_formate.cend()) {
            if (fm->fett) m.insert(QStringLiteral("fett"), true);
            if (!fm->farbe.isEmpty()) m.insert(QStringLiteral("fg"), fm->farbe);
            if (!fm->hintergrund.isEmpty()) {
                m.insert(QStringLiteral("bg"), fm->hintergrund);
                //  Ohne eigene Textfarbe entscheidet der Hintergrund, nicht das
                //  Thema - sonst steht heller Text auf heller Flaeche.
                if (fm->farbe.isEmpty())
                    m.insert(QStringLiteral("fg"), lesbarAuf(fm->hintergrund));
            }
        }
        m_spalten.append(m);
    }
}

QString TableController::separator() const {
    return m_datei ? QString(m_datei->trenner) : QStringLiteral(";");
}

int TableController::rowCount() const {
    if (!m_datei) return 0;
    if (m_ordnungAktiv) return int(m_ordnung.size());
    const Bereich b = aktiv();
    return qMax(0, b.bis - b.daten);
}

int TableController::totalRows() const {
    if (!m_datei) return 0;
    const Bereich b = aktiv();
    return qMax(0, b.bis - b.daten);
}

bool TableController::rowEmpty(int row) const {
    if (!m_datei) return false;
    const int z = rohZeile(row);
    return z >= 0 && z < m_datei->zeilen.size() && m_datei->zeilen.at(z).isEmpty();
}

QVariantList TableController::rowMatches(int row) const {
    QVariantList out;
    const QList<int> spalten = m_suche.spaltenIn(row);
    out.reserve(spalten.size());
    for (int s : spalten) out.append(s);
    return out;
}

QString TableController::cell(int row, int column) const {
    if (!m_datei) return {};
    const int z = rohZeile(row);
    if (z < 0 || z >= m_datei->zeilen.size()) return {};
    const QString w = gezeigterWert(m_datei->zeilen, z, column, m_werte.get());
    //  Nur die ANZEIGE. Zwischenablage und Eingabefeld gehen andere Wege und
    //  bekommen weiter, was in der Datei steht.
    return zahlAnzeigen(w, m_gruppiert, m_dezimalKomma);
}

QString TableController::cellRaw(int row, int column) const {
    if (!m_datei) return {};
    const int z = rohZeile(row);
    if (z < 0 || z >= m_datei->zeilen.size()) return {};
    return m_datei->zeilen.at(z).wert(column);
}

bool TableController::cellIsFormula(int row, int column) const {
    if (!m_werte || m_werte->leer()) return false;
    const int z = rohZeile(row);
    return z >= 0 && m_werte->hatZeile(z) && m_werte->wert(z, column) != nullptr;
}

int TableController::rowNumber(int row) const {
    if (!m_filter.aktiv()) return row + 1;
    const int z = rohZeile(row);
    return z < 0 ? row + 1 : z - aktiv().daten + 1;
}

QString TableController::rowText(int row) const {
    if (!m_datei) return {};
    QStringList felder;
    felder.reserve(m_spalten.size());
    for (const QVariant& v : m_spalten)
        felder.append(cell(row, v.toMap().value(QStringLiteral("index")).toInt()));
    return felder.join(QLatin1Char('\t'));
}

QString TableController::rangeText(int row1, int col1, int row2, int col2) const {
    if (!m_datei) return {};
    const int za = qMax(0, qMin(row1, row2));
    const int ze = qMin(rowCount() - 1, qMax(row1, row2));
    const int sa = qMax(0, qMin(col1, col2));
    const int se = qMin(int(m_spalten.size()) - 1, qMax(col1, col2));
    if (za > ze || sa > se) return {};
    if (qint64(ze - za + 1) * qint64(se - sa + 1) > kMaxBereichZellen) return {};

    QStringList zeilen;
    zeilen.reserve(ze - za + 1);
    QStringList felder;
    felder.reserve(se - sa + 1);
    for (int z = za; z <= ze; ++z) {
        felder.clear();
        for (int i = sa; i <= se; ++i)
            felder.append(cell(z, m_spalten.at(i).toMap()
                                     .value(QStringLiteral("index")).toInt()));
        zeilen.append(felder.join(QLatin1Char('\t')));
    }
    return zeilen.join(QLatin1Char('\n'));
}

int TableController::rohZeile(int anzeige) const {
    if (!m_ordnungAktiv) return aktiv().daten + anzeige;
    if (anzeige < 0 || anzeige >= m_ordnung.size()) return -1;
    return m_ordnung.at(anzeige);
}

QList<bool> TableController::spaltenMaske() const {
    if (m_versteckt.isEmpty()) return {};
    QList<bool> maske(m_spaltenZahl, true);
    for (int s : m_versteckt)
        if (s >= 0 && s < maske.size()) maske[s] = false;
    return maske;
}

void TableController::setGroupDigits(bool v) {
    if (v == m_gruppiert) return;
    m_gruppiert = v;
    //  Die Zellen haengen ihre Bindung an `contentRevision` - ohne sie stuenden
    //  die sichtbaren Zeilen weiter in der alten Form da.
    ++m_inhaltRevision;
    emit sortChanged();
}

void TableController::setSlashDateMonthFirst(bool v) {
    if (v == m_monatZuerst) return;
    m_monatZuerst = v;
    if (m_filter.aktiv() || m_sortRichtung != SortRichtung::Keine) ordnungNeuBauen();
    else emit sortChanged();
}

void TableController::sortByColumn(int column) {
    if (column < 0 || column >= m_spaltenZahl) return;
    if (column != m_sortSpalte)              m_sortRichtung = SortRichtung::Auf;
    else if (m_sortRichtung == SortRichtung::Auf) m_sortRichtung = SortRichtung::Ab;
    else                                     m_sortRichtung = SortRichtung::Keine;
    m_sortSpalte = (m_sortRichtung == SortRichtung::Keine) ? -1 : column;
    ordnungNeuBauen();
}

void TableController::clearSort() {
    m_sortSpalte   = -1;
    m_sortRichtung = SortRichtung::Keine;
    //  Ein Filter bleibt stehen - dann gilt dessen Auswahl in Dateireihenfolge.
    ordnungNeuBauen();
}

void TableController::setFilter(int column, const QString& text, bool caseSensitive,
                                bool wholeCell) {
    FilterRegel neu;
    neu.spalte = (column >= 0 && column < m_spaltenZahl) ? column : -1;
    neu.text = text;
    neu.opt.gross = caseSensitive;
    neu.opt.ganzeZelle = wholeCell;
    if (neu.spalte == m_filter.spalte && neu.text == m_filter.text
        && neu.opt.gross == m_filter.opt.gross && neu.opt.ganzeZelle == m_filter.opt.ganzeZelle)
        return;
    m_filter = neu;
    ordnungNeuBauen();
}

void TableController::clearFilter() {
    if (!m_filter.aktiv()) return;
    m_filter = {};
    ordnungNeuBauen();
}

void TableController::ordnungNeuBauen() {
    if (m_sortAbbruch) m_sortAbbruch->store(true);
    if (!m_datei || (!m_filter.aktiv() && m_sortRichtung == SortRichtung::Keine)) {
        m_sortLaeuft = false;
        m_ordnung.clear();
        m_ordnungAktiv = false;
        ++m_inhaltRevision;
        emit sortChanged();
        emit rowsChanged();
        //  Die Trefferzeilen zaehlen in der ANZEIGE - nach einem Wechsel der
        //  Reihenfolge zeigten die alten auf beliebige Zeilen.
        if (!m_suchText.isEmpty()) sucheStarten();
        return;
    }
    m_sortLaeuft = true;
    emit sortChanged();

    const Bereich b = aktiv();
    Ordnungsauftrag a;
    a.von = b.daten;
    a.bis = b.bis;
    a.filter = m_filter;
    a.spalten = spaltenMaske();
    a.sortSpalte = m_sortSpalte;
    a.richtung = m_sortRichtung;
    a.monatZuerst = a.filter.monatZuerst = m_monatZuerst;
    m_sortAbbruch = std::make_shared<std::atomic<bool>>(false);
    auto* self = this;
    m_pool.start(new OrdnungTask(this, m_datei, &m_datei->zeilen, a, m_sortAbbruch,
                                 [self](QList<int> ordnung, bool aktiv, QVariant) {
                                     self->ordnungErgebnis(std::move(ordnung), aktiv);
                                 },
                                 {}, m_werte));
}

void TableController::ordnungErgebnis(QList<int> ordnung, bool aktiv) {
    m_sortLaeuft = false;
    m_ordnung = std::move(ordnung);
    m_ordnungAktiv = aktiv;
    ++m_inhaltRevision;
    emit sortChanged();
    emit rowsChanged();
    if (!m_suchText.isEmpty()) sucheStarten();
}

void TableController::setColumnHidden(int column, bool hidden) {
    if (column < 0 || column >= m_spaltenZahl) return;
    //  Die LETZTE Spalte bleibt stehen - eine Tabelle ohne Spalten ist eine
    //  leere Flaeche, aus der kein Weg zurueckfuehrt.
    if (hidden && m_spaltenZahl - m_versteckt.size() <= 1) return;
    const bool war = m_versteckt.contains(column);
    if (war == hidden) return;
    if (hidden) m_versteckt.insert(column);
    else        m_versteckt.remove(column);
    spaltenNeuRechnen();
    ++m_inhaltRevision;
    emit stateChanged();
    //  Eine ausgeblendete Spalte darf weder Reihenfolge noch Auswahl bestimmen.
    bool neuOrdnen = false;
    if (hidden && column == m_sortSpalte) {
        m_sortSpalte = -1;
        m_sortRichtung = SortRichtung::Keine;
        neuOrdnen = true;
    }
    if (hidden && column == m_filter.spalte) { m_filter = {}; neuOrdnen = true; }
    //  Ein Filter ueber alle Spalten sieht jetzt eine Spalte mehr oder weniger.
    if (m_filter.aktiv() && m_filter.spalte < 0) neuOrdnen = true;
    if (neuOrdnen) ordnungNeuBauen();
    else emit sortChanged();
    if (!m_suchText.isEmpty()) sucheStarten();
}

void TableController::showAllColumns() {
    if (m_versteckt.isEmpty()) return;
    m_versteckt.clear();
    spaltenNeuRechnen();
    ++m_inhaltRevision;
    emit stateChanged();
    if (m_filter.aktiv() && m_filter.spalte < 0) ordnungNeuBauen();
    else emit sortChanged();
    if (!m_suchText.isEmpty()) sucheStarten();
}

// Bearbeiten

bool TableController::editable() const {
    return ready() && !m_datei->abgeschnitten && !m_datei->gekappt;
}

bool TableController::structureEditable() const {
    return editable() && aktiverBereich() >= 0;
}

Datei& TableController::schreibDatei() {
    if (m_datei.use_count() > 1) m_datei = std::make_shared<Datei>(*m_datei);
    return *m_datei;
}

void TableController::infoAnlegen() {
    if (!m_info.isEmpty() || !m_datei) return;
    const int n = int(m_datei->zeilen.size());
    m_info.resize(n);
    //  Nummern werden nie wiederverwendet: ein zurueckgeholter Schritt traegt
    //  noch seine alte, und das Speichern ordnet ueber sie zu.
    for (int i = 0; i < n; ++i) m_info[i] = ZeilenInfo{m_naechsteId + quint32(i), i, 0};
    m_naechsteId += quint32(n);
}

void TableController::markiere(int zeile, bool umgebaut) {
    infoAnlegen();
    if (zeile < 0 || zeile >= m_info.size()) return;
    m_info[zeile].rev = m_rev;
    if (umgebaut) m_info[zeile].herkunft = -1;
}

void TableController::zeilenEinsetzen(int bereich, int pos, const QList<Zeile>& zeilen,
                                      const QList<ZeilenInfo>& infos, int anzeige) {
    Datei& d = schreibDatei();
    infoAnlegen();
    const int k = int(zeilen.size());
    for (int i = 0; i < k; ++i) {
        d.zeilen.insert(pos + i, zeilen.at(i));
        m_info.insert(pos + i, infos.at(i));
    }
    for (int i = 0; i < m_bereiche.size(); ++i) {
        Bereich& x = m_bereiche[i];
        if (i == bereich) { x.bis += k; continue; }
        if (x.von < pos) continue;
        x.von += k; x.bis += k; x.daten += k;
        if (x.titel >= 0) x.titel += k;
        if (x.kopf >= 0)  x.kopf += k;
    }
    if (!m_ordnungAktiv) return;
    for (int& o : m_ordnung)
        if (o >= pos) o += k;
    //  Eine neue Zeile steht dort, wo sie eingefuegt wurde - auch in einer
    //  gefilterten oder sortierten Ansicht, bis neu geordnet wird.
    if (anzeige >= 0 && bereich == aktiverBereich())
        for (int i = 0; i < k; ++i)
            m_ordnung.insert(qMin(anzeige + i, int(m_ordnung.size())), pos + i);
}

void TableController::zeilenEntfernen(int bereich, int pos, int anzahl) {
    Datei& d = schreibDatei();
    infoAnlegen();
    d.zeilen.remove(pos, anzahl);
    m_info.remove(pos, anzahl);
    for (int i = 0; i < m_bereiche.size(); ++i) {
        Bereich& x = m_bereiche[i];
        if (i == bereich) { x.bis -= anzahl; continue; }
        if (x.von <= pos) continue;
        x.von -= anzahl; x.bis -= anzahl; x.daten -= anzahl;
        if (x.titel >= 0) x.titel -= anzahl;
        if (x.kopf >= 0)  x.kopf -= anzahl;
    }
    if (!m_ordnungAktiv) return;
    QList<int> neu;
    neu.reserve(m_ordnung.size());
    for (int o : std::as_const(m_ordnung)) {
        if (o >= pos && o < pos + anzahl) continue;
        neu.append(o >= pos + anzahl ? o - anzahl : o);
    }
    m_ordnung = std::move(neu);
}

//  Liefert, ob an Breiten oder Formaten etwas zu ruecken war - nur dann muss
//  die Beidatei angefasst werden.
bool TableController::spaltenIndexVerschieben(int spalte, int delta, int* wegBreite,
                                              SpaltenFormat* wegFormat) {
    QSet<int> versteckt;
    for (int h : std::as_const(m_versteckt)) {
        if (delta > 0) versteckt.insert(h >= spalte ? h + 1 : h);
        else if (h != spalte) versteckt.insert(h > spalte ? h - 1 : h);
    }
    m_versteckt = std::move(versteckt);

    //  Breite und Format haengen an der SPALTENNUMMER. Wandern sie nicht mit,
    //  traegt nach dem Einfuegen einer Spalte die falsche die Farbe.
    auto rueckeAuf = [&](auto& bestand, auto* weg) {
        std::decay_t<decltype(bestand)> neu;
        neu.reserve(bestand.size());
        for (auto it = bestand.constBegin(); it != bestand.constEnd(); ++it) {
            const int s = it.key();
            if (delta > 0) { neu.insert(s >= spalte ? s + 1 : s, it.value()); continue; }
            if (s == spalte) { if (weg) *weg = it.value(); continue; }
            neu.insert(s > spalte ? s - 1 : s, it.value());
        }
        bestand = std::move(neu);
    };
    const bool etwasZuRuecken = !m_breiten.isEmpty() || !m_formate.isEmpty();
    rueckeAuf(m_breiten, wegBreite);
    rueckeAuf(m_formate, wegFormat);

    bool neuOrdnen = false;
    auto verschiebe = [&](int& wert, bool* weg) {
        if (wert < 0) return;
        if (delta > 0) { if (wert >= spalte) ++wert; return; }
        if (wert == spalte) { wert = -1; if (weg) *weg = true; }
        else if (wert > spalte) --wert;
    };
    bool sortWeg = false, filterWeg = false;
    verschiebe(m_sortSpalte, &sortWeg);
    verschiebe(m_filter.spalte, &filterWeg);
    if (sortWeg) { m_sortRichtung = SortRichtung::Keine; neuOrdnen = true; }
    //  Die gefilterte Spalte ist weg - „irgendeine Spalte" waere ein anderer Filter.
    if (filterWeg) { m_filter = {}; neuOrdnen = true; }
    if (neuOrdnen) ordnungNeuBauen();
    return etwasZuRuecken;
}

void TableController::schrittAusfuehren(Schritt& s, bool vorwaerts) {
    using Art = Schritt::Art;
    switch (s.art) {
    case Art::Zellen: {
        Datei& d = schreibDatei();
        if (vorwaerts) {
            for (const Zellwechsel& c : std::as_const(s.zellen)) {
                d.zeilen[c.zeile].setzeWert(c.spalte, c.neu);
                markiere(c.zeile, false);
            }
        } else {
            for (auto it = s.zellen.crbegin(); it != s.zellen.crend(); ++it) {
                Zeile& z = d.zeilen[it->zeile];
                z.setzeWert(it->spalte, it->alt);
                if (z.felder() > it->felderVorher) z.setzeFelderzahl(it->felderVorher);
                markiere(it->zeile, false);
            }
        }
        break;
    }
    case Art::ZeilenEin:
        if (vorwaerts) {
            if (s.neueInfos.isEmpty())
                for (int i = 0; i < s.neueZeilen.size(); ++i)
                    s.neueInfos.append(ZeilenInfo{m_naechsteId++, -1, 0});
            QList<ZeilenInfo> infos = s.neueInfos;
            for (ZeilenInfo& i : infos) i.rev = m_rev;
            zeilenEinsetzen(s.bereich, s.pos, s.neueZeilen, infos, s.anzeige);
        } else {
            zeilenEntfernen(s.bereich, s.pos, int(s.neueZeilen.size()));
        }
        break;
    case Art::ZeilenAus:
        if (vorwaerts) {
            for (auto it = s.weg.crbegin(); it != s.weg.crend(); ++it)
                zeilenEntfernen(s.bereich, it->zeile, 1);
        } else {
            QList<std::pair<int, int>> anzeigen;
            for (const Weg& w : std::as_const(s.weg)) {
                ZeilenInfo i = w.info;
                if (s.basisGen != m_basisGen) i.herkunft = -1;
                i.rev = m_rev;
                zeilenEinsetzen(s.bereich, w.zeile, {w.inhalt}, {i}, -1);
                if (w.anzeige >= 0) anzeigen.append({w.anzeige, w.zeile});
            }
            if (m_ordnungAktiv) {
                if (s.bereich == aktiverBereich()) {
                    std::sort(anzeigen.begin(), anzeigen.end());
                    for (const auto& [anzeige, zeile] : std::as_const(anzeigen))
                        m_ordnung.insert(qMin(anzeige, int(m_ordnung.size())), zeile);
                } else {
                    ordnungNeuBauen();
                }
            }
        }
        break;
    case Art::SpalteEin:
    case Art::SpalteAus: {
        Datei& d = schreibDatei();
        infoAnlegen();
        const bool einfuegen = (s.art == Art::SpalteEin) == vorwaerts;
        if (vorwaerts && s.betroffen.isEmpty()) {
            for (int r = s.von; r < s.bis; ++r) {
                const Zeile& z = d.zeilen.at(r);
                if (z.isEmpty()) continue;
                if (einfuegen ? z.felder() < s.spalte : z.felder() <= s.spalte) continue;
                s.betroffen.append({r, m_info.at(r)});
                if (!einfuegen) s.werte.append(z.wert(s.spalte));
            }
        }
        for (int i = 0; i < s.betroffen.size(); ++i) {
            const int r = s.betroffen.at(i).first;
            Zeile& z = d.zeilen[r];
            if (einfuegen) {
                z.spalteEinfuegen(s.spalte);
                if (s.art == Art::SpalteAus) z.setzeWert(s.spalte, s.werte.at(i));
            } else {
                z.spalteEntfernen(s.spalte);
            }
            if (vorwaerts) {
                markiere(r, true);
            } else {
                //  Zurueck zur alten Herkunft - aber nur, solange die Grundlage
                //  noch dieselbe ist.
                ZeilenInfo alt = s.betroffen.at(i).second;
                if (s.basisGen != m_basisGen) alt.herkunft = -1;
                alt.rev = m_rev;
                m_info[r] = alt;
            }
        }
        if (einfuegen) {
            const bool zurueck = spaltenIndexVerschieben(s.spalte, 1);
            //  Rueckgaengig einer geloeschten Spalte: ihre Breite und ihr
            //  Format kommen mit zurueck.
            if (s.wegBreite > 0) m_breiten.insert(s.spalte, s.wegBreite);
            if (!s.wegFormat.leer()) m_formate.insert(s.spalte, s.wegFormat);
            if (zurueck || s.wegBreite > 0 || !s.wegFormat.leer()) spaltenAblegen();
        } else {
            s.wegBreite = 0;
            s.wegFormat = SpaltenFormat();
            if (spaltenIndexVerschieben(s.spalte, -1, &s.wegBreite, &s.wegFormat))
                spaltenAblegen();
        }
        break;
    }
    case Art::Gruppe:
        if (vorwaerts)
            for (Schritt& t : s.teile) schrittAusfuehren(t, true);
        else
            for (auto it = s.teile.rbegin(); it != s.teile.rend(); ++it)
                schrittAusfuehren(*it, false);
        break;
    }
}

namespace {

qsizetype zeichenIn(const Zeile& z) {
    qsizetype n = 0;
    for (const auto& feld : z.belegte()) n += feld.second.size();
    return n;
}

}  // namespace

void TableController::schrittAblegen(Schritt s) {
    std::function<qsizetype(const Schritt&)> zaehle = [&](const Schritt& t) {
        qsizetype n = 0;
        for (const Zellwechsel& c : t.zellen) n += c.alt.size() + c.neu.size();
        for (const Zeile& z : t.neueZeilen) n += zeichenIn(z);
        for (const Weg& w : t.weg) n += zeichenIn(w.inhalt);
        for (const QString& w : t.werte) n += w.size();
        for (const Schritt& u : t.teile) n += zaehle(u);
        return n;
    };
    s.zeichen = zaehle(s);
    for (const Schritt& r : m_redo) m_undoZeichen -= r.zeichen;
    m_redo.clear();
    m_undoZeichen += s.zeichen;
    m_undo.push_back(std::move(s));
    while (m_undo.size() > 1
           && (m_undo.size() > kMaxSchritte || m_undoZeichen > kMaxUndoZeichen)) {
        m_undoZeichen -= m_undo.front().zeichen;
        m_undo.erase(m_undo.begin());
    }
}

void TableController::nachAenderung(bool struktur, bool spalten, bool formeln) {
    ++m_inhaltRevision;
    if (formeln) formelnNeuRechnen();
    if (spalten) spaltenNeuRechnen();
    if (struktur || m_bereiche.size() > 1) bloeckeNeuBauen();
    if (spalten)       emit stateChanged();
    else if (struktur) emit rowsChanged();
    emit sortChanged();
    emit editChanged();
    //  Ein laufender Ordnungslauf kennt die alten Zeilennummern.
    if (struktur && m_sortLaeuft) ordnungNeuBauen();
    if (!m_suchText.isEmpty()) sucheStarten();
}

bool TableController::setCell(int row, int column, const QString& text) {
    if (!editable() || column < 0 || column >= kMaxFelderZeile) return false;
    const int z = rohZeile(row);
    if (z < 0 || z >= m_datei->zeilen.size()) return false;
    const Zeile& zeile = m_datei->zeilen.at(z);
    //  Eine Leerzeile trennt Tabellen; beschrieben waere sie keine mehr.
    if (zeile.isEmpty()) return false;
    const QString alt = zeile.wert(column);
    if (alt == text) return true;

    Schritt s;
    s.art = Schritt::Art::Zellen;
    s.basisGen = m_basisGen;
    s.bereich = aktiverBereich();
    s.zellen.append({z, column, alt, text, zeile.felder()});
    ++m_rev;
    schrittAusfuehren(s, true);
    schrittAblegen(std::move(s));

    //  Waechst der Text ueber die gemessene Spaltenbreite, rechnet die Anzeige
    //  neu. Sobald irgendwo eine Formel steht, immer: ihr ERGEBNIS bestimmt die
    //  Breite, und es kann in einer ganz anderen Zelle stehen.
    bool breiter = column >= m_spaltenZahl || m_formelZahl > 0;
    for (const QVariant& v : std::as_const(m_spalten)) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("index")).toInt() == column)
            breiter = breiter || text.size() > m.value(QStringLiteral("chars")).toInt();
    }
    nachAenderung(false, breiter, formelBetroffen(alt, text));
    return true;
}

bool TableController::setColumnName(int column, const QString& name) {
    const Bereich b = aktiv();
    if (!editable() || b.kopf < 0 || column < 0 || column >= m_spaltenZahl) return false;
    const Zeile& kopf = m_datei->zeilen.at(b.kopf);
    const QString alt = kopf.wert(column);
    if (alt == name) return true;
    Schritt s;
    s.art = Schritt::Art::Zellen;
    s.basisGen = m_basisGen;
    s.bereich = aktiverBereich();
    s.zellen.append({b.kopf, column, alt, name, kopf.felder()});
    ++m_rev;
    schrittAusfuehren(s, true);
    schrittAblegen(std::move(s));
    nachAenderung(false, true, formelBetroffen(alt, name));
    return true;
}

bool TableController::insertRows(int row, int count) {
    if (!structureEditable() || count <= 0) return false;
    const Bereich b = aktiv();
    const int anzeige = qBound(0, row, rowCount());
    const int pos = anzeige >= rowCount() ? b.bis : rohZeile(anzeige);
    if (pos < b.daten || pos > b.bis) return false;

    Schritt s;
    s.art = Schritt::Art::ZeilenEin;
    s.basisGen = m_basisGen;
    s.bereich = aktiverBereich();
    s.pos = pos;
    s.anzeige = anzeige;
    Zeile leer;
    //  Mindestens ein Feld: eine Zeile ohne Felder waere eine Leerzeile und
    //  truege beim naechsten Lesen einen neuen Block.
    leer.ensureFelder(qMax(1, m_spaltenZahl));
    for (int i = 0; i < count; ++i) s.neueZeilen.append(leer);
    ++m_rev;
    schrittAusfuehren(s, true);
    schrittAblegen(std::move(s));
    nachAenderung(true, false, m_formelZahl > 0);
    return true;
}

bool TableController::removeRows(int row, int count) {
    if (!structureEditable() || count <= 0 || row < 0 || row + count > rowCount()) return false;
    QList<Weg> weg;
    for (int i = 0; i < count; ++i) {
        const int z = rohZeile(row + i);
        if (z < 0) return false;
        Weg w;
        w.zeile = z;
        w.anzeige = m_ordnungAktiv ? row + i : -1;
        w.inhalt = m_datei->zeilen.at(z);
        infoAnlegen();
        w.info = m_info.at(z);
        weg.append(w);
    }
    std::sort(weg.begin(), weg.end(), [](const Weg& a, const Weg& b) { return a.zeile < b.zeile; });

    //  Die Datei behaelt mindestens eine Zeile mit Inhalt - ohne sie liesse sie
    //  sich nicht mehr als Tabelle oeffnen.
    int belegt = 0;
    for (const Zeile& z : std::as_const(m_datei->zeilen)) if (!z.isEmpty()) ++belegt;
    for (const Weg& w : std::as_const(weg)) if (!w.inhalt.isEmpty()) --belegt;
    if (belegt <= 0) return false;

    Schritt s;
    s.art = Schritt::Art::ZeilenAus;
    s.basisGen = m_basisGen;
    s.bereich = aktiverBereich();
    s.weg = std::move(weg);
    ++m_rev;
    schrittAusfuehren(s, true);
    schrittAblegen(std::move(s));
    nachAenderung(true, false, m_formelZahl > 0);
    return true;
}

bool TableController::insertColumn(int column) {
    if (!structureEditable() || column < 0 || column > m_spaltenZahl
        || m_spaltenZahl >= kMaxFelderZeile)
        return false;
    const Bereich b = aktiv();
    Schritt s;
    s.art = Schritt::Art::SpalteEin;
    s.basisGen = m_basisGen;
    s.bereich = aktiverBereich();
    s.spalte = column;
    s.von = b.kopf >= 0 ? b.kopf : b.daten;
    s.bis = b.bis;
    ++m_rev;
    schrittAusfuehren(s, true);
    schrittAblegen(std::move(s));
    nachAenderung(false, true, m_formelZahl > 0);
    return true;
}

bool TableController::removeColumn(int column) {
    if (!structureEditable() || column < 0 || column >= m_spaltenZahl || m_spaltenZahl <= 1)
        return false;
    const Bereich b = aktiv();
    Schritt s;
    s.art = Schritt::Art::SpalteAus;
    s.basisGen = m_basisGen;
    s.bereich = aktiverBereich();
    s.spalte = column;
    s.von = b.kopf >= 0 ? b.kopf : b.daten;
    s.bis = b.bis;
    ++m_rev;
    schrittAusfuehren(s, true);
    schrittAblegen(std::move(s));
    nachAenderung(false, true, m_formelZahl > 0);
    return true;
}

int TableController::pasteText(int row, int column, const QString& text) {
    if (!editable() || row < 0 || text.isEmpty()) return 0;
    QStringList zeilen = text.split(QLatin1Char('\n'));
    for (QString& z : zeilen) if (z.endsWith(QLatin1Char('\r'))) z.chop(1);
    //  Eine Tabellenkalkulation schliesst die Ablage mit einem Umbruch ab.
    if (zeilen.size() > 1 && zeilen.last().isEmpty()) zeilen.removeLast();

    //  Nach rechts ueber die GEZEIGTEN Spalten - eine ausgeblendete bekaeme
    //  sonst unsichtbar einen Wert.
    QList<int> ziele;
    bool gefunden = false;
    for (const QVariant& v : std::as_const(m_spalten)) {
        const int idx = v.toMap().value(QStringLiteral("index")).toInt();
        if (idx == column) gefunden = true;
        if (gefunden) ziele.append(idx);
    }
    if (ziele.isEmpty()) return 0;

    Schritt gruppe;
    gruppe.art = Schritt::Art::Gruppe;
    gruppe.basisGen = m_basisGen;
    gruppe.bereich = aktiverBereich();
    ++m_rev;

    int fehlend = row + int(zeilen.size()) - rowCount();
    bool struktur = false;
    if (fehlend > 0 && structureEditable()) {
        Schritt ein;
        ein.art = Schritt::Art::ZeilenEin;
        ein.basisGen = m_basisGen;
        ein.bereich = aktiverBereich();
        ein.pos = aktiv().bis;
        ein.anzeige = rowCount();
        Zeile leer;
        leer.ensureFelder(qMax(1, m_spaltenZahl));
        for (int i = 0; i < fehlend; ++i) ein.neueZeilen.append(leer);
        schrittAusfuehren(ein, true);
        gruppe.teile.push_back(std::move(ein));
        struktur = true;
    }

    Schritt zellen;
    zellen.art = Schritt::Art::Zellen;
    zellen.basisGen = m_basisGen;
    zellen.bereich = aktiverBereich();
    int gesetzt = 0;
    for (int i = 0; i < zeilen.size() && row + i < rowCount(); ++i) {
        const int z = rohZeile(row + i);
        if (z < 0 || m_datei->zeilen.at(z).isEmpty()) continue;
        const QStringList felder = zeilen.at(i).split(QLatin1Char('\t'));
        int felderVorher = m_datei->zeilen.at(z).felder();
        for (int j = 0; j < felder.size() && j < ziele.size(); ++j) {
            const QString alt = m_datei->zeilen.at(z).wert(ziele.at(j));
            if (alt == felder.at(j)) continue;
            zellen.zellen.append({z, ziele.at(j), alt, felder.at(j), felderVorher});
            ++gesetzt;
        }
    }
    schrittAusfuehren(zellen, true);
    gruppe.teile.push_back(std::move(zellen));
    if (gesetzt == 0 && !struktur) {
        //  Nichts geaendert: auch keinen leeren Schritt ablegen.
        --m_rev;
        return 0;
    }
    schrittAblegen(std::move(gruppe));
    //  Grob, aber billig: ohne ein '=' im Eingefuegten kann keine Formel entstehen.
    nachAenderung(struktur, true, m_formelZahl > 0 || text.contains(QLatin1Char('=')));
    return gesetzt;
}

void TableController::undo() {
    if (m_undo.empty() || !editable()) return;
    Schritt s = std::move(m_undo.back());
    m_undo.pop_back();
    ++m_rev;
    schrittAusfuehren(s, false);
    const bool struktur = s.art != Schritt::Art::Zellen;
    m_redo.push_back(std::move(s));
    nachAenderung(struktur, true);
}

void TableController::redo() {
    if (m_redo.empty() || !editable()) return;
    Schritt s = std::move(m_redo.back());
    m_redo.pop_back();
    ++m_rev;
    schrittAusfuehren(s, true);
    const bool struktur = s.art != Schritt::Art::Zellen;
    m_undo.push_back(std::move(s));
    nachAenderung(struktur, true);
}

// Speichern

void TableController::save() {
    if (!m_datei || !modified() || !editable()) return;
    if (m_speichert) { m_nochmal = true; return; }
    speichernStarten(Modus::Normal);
}

void TableController::saveCopy() {
    if (!m_datei || m_speichert) return;
    speichernStarten(Modus::Kopie);
}

void TableController::flush() {
    //  Hoechstens ein zweiter Durchgang: waehrend des ersten Schreibens koennen
    //  weitere Aenderungen dazugekommen sein.
    for (int runde = 0; runde < 3; ++runde) {
        if (!m_speichert) {
            if (!m_datei || !modified() || !editable()) return;
            speichernStarten(Modus::Verlassen);
        }
        m_pool.waitForDone();
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
        if (!m_speicherFehler.isEmpty()) return;
    }
}

void TableController::reload() {
    if (m_speichert) {
        m_pool.waitForDone();
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    }
    neuLesen();
}

bool TableController::reloadIfChangedOnDisk() {
    if (!m_datei || modified() || m_speichert || m_source.isEmpty()) return false;
    const QFileInfo fi(m_source);
    if (fi.size() == m_grundlage.groesse
        && fi.lastModified().toMSecsSinceEpoch() == m_grundlage.zeit)
        return false;
    neuLesen();
    return true;
}

void TableController::speichernStarten(Modus modus) {
    infoAnlegen();
    m_speichert = true;
    m_nochmal = false;
    m_speicherFehler.clear();
    emit editChanged();

    auto e = std::make_shared<SpeicherErgebnis>();
    e->rev = m_rev;
    e->ladeGen = m_ladeGen;
    //  Eine flache Kopie: die Zeilenlisten teilen sich die Daten, und eine
    //  spaetere Aenderung im GUI-Faden legt sich ihre eigenen an.
    const std::shared_ptr<const Datei> stand = std::make_shared<const Datei>(*m_datei);
    const QList<ZeilenInfo> info = m_info;
    const QList<int> basisNr = m_basisZeilenNr;
    const quint32 basisRev = m_basisRev;
    const Grundlage grundlage = m_grundlage;
    const QString pfad = m_source;
    auto* self = this;

    m_pool.start(QRunnable::create([self, e, stand, info, basisNr, basisRev, grundlage, pfad, modus]() {
        using Art = SpeicherErgebnis::Art;
        const int n = int(stand->zeilen.size());
        e->ids.reserve(n);
        QList<SchreibZeile> plan;
        plan.reserve(n);

        QByteArray roh;
        bool konflikt = modus != Modus::Kopie;
        if (modus != Modus::Kopie) {
            QFile f(pfad);
            if (f.open(QIODevice::ReadOnly)) {
                roh = f.read(kMaxBytes + 1);
                konflikt = roh.size() != grundlage.groesse || pruefsumme(roh) != grundlage.hash;
            }
        }
        const bool kopie = modus == Modus::Kopie || (konflikt && modus == Modus::Verlassen);
        bool cp1252 = stand->cp1252;
        SchreibErgebnis erg;
        if (konflikt && !kopie) {
            e->art = Art::Konflikt;
        } else {
            for (int i = 0; i < n; ++i) {
                const ZeilenInfo z = i < info.size() ? info.at(i) : ZeilenInfo{0, i, 0};
                e->ids.append(z.id);
                if (kopie) plan.append({&stand->zeilen.at(i), -1, true});
                else       plan.append({&stand->zeilen.at(i), z.herkunft, z.rev > basisRev});
            }
            erg = kopie ? baueDatei({}, {}, stand->trenner, cp1252, plan)
                        : baueDatei(roh, basisNr, stand->trenner, cp1252, plan);
            //  Beim Verlassen geht nichts verloren: laesst sich ein Zeichen nicht
            //  in Windows-1252 schreiben, wird die Kopie UTF-8.
            if (!erg.ok && erg.fehlerZeile >= 0 && modus == Modus::Verlassen) {
                for (SchreibZeile& z : plan) { z.herkunft = -1; z.geaendert = true; }
                erg = baueDatei({}, {}, stand->trenner, false, plan);
                e->kopie = true;
            }
            if (!erg.ok) {
                e->art = erg.fehlerZeile >= 0 ? Art::Kodierung : Art::Schreibfehler;
                e->fehlerZeile = erg.fehlerZeile;
            } else {
                e->kopie = e->kopie || kopie;
                e->ziel = e->kopie ? kopiePfad(pfad) : pfad;
                QSaveFile f(e->ziel);
                if (!f.open(QIODevice::WriteOnly) || f.write(erg.bytes) != erg.bytes.size()
                    || !f.commit()) {
                    e->art = Art::Schreibfehler;
                } else {
                    e->grundlage = Grundlage{pruefsumme(erg.bytes), erg.bytes.size(),
                                             aenderungszeit(e->ziel)};
                    e->zeilenNr = std::move(erg.zeilenNr);
                    if (konflikt) e->art = Art::Konflikt;
                }
            }
        }
        //  An den Controller, nicht an die App: `flush` stellt genau diese
        //  Meldungen selbst zu, bevor es zurueckkehrt.
        QMetaObject::invokeMethod(self, [self, e] { self->speicherErgebnis(e); },
                                  Qt::QueuedConnection);
    }));
}

void TableController::speicherErgebnis(const std::shared_ptr<SpeicherErgebnis>& e) {
    if (e->uebernommen) return;
    e->uebernommen = true;
    using Art = SpeicherErgebnis::Art;
    m_speichert = false;
    if (e->ladeGen != m_ladeGen) { emit editChanged(); return; }

    const bool geschrieben = !e->ziel.isEmpty() && e->art != Art::Schreibfehler
                             && e->art != Art::Kodierung;
    if (e->art == Art::Konflikt)
        m_speicherFehler = Strings::get(StringKey::TableSaveConflict);
    else if (e->art == Art::Kodierung)
        m_speicherFehler = Strings::get(StringKey::TableSaveEncoding).arg(e->fehlerZeile + 1);
    else if (e->art == Art::Schreibfehler)
        m_speicherFehler = Strings::get(StringKey::TableSaveFailed);

    if (!geschrieben) {
        m_nochmal = false;
        emit editChanged();
        emit saved(false);
        return;
    }
    if (e->kopie) {
        //  Die Aenderungen stehen in der Kopie; die geoeffnete Datei bleibt die
        //  alte Grundlage.
        m_kopieName = QFileInfo(e->ziel).fileName();
        m_gespeichertRev = e->rev;
        m_nochmal = false;
        emit editChanged();
        emit saved(true);
        return;
    }

    m_grundlage = e->grundlage;
    m_basisZeilenNr = e->zeilenNr;
    ++m_basisGen;
    if (m_rev == e->rev) {
        //  Nichts dazwischen: die Datei ist jetzt genau das, was gezeigt wird.
        m_info.clear();
    } else {
        QHash<quint32, int> neuePos;
        neuePos.reserve(e->ids.size());
        for (int i = 0; i < e->ids.size(); ++i) neuePos.insert(e->ids.at(i), i);
        for (ZeilenInfo& z : m_info) {
            const auto it = neuePos.constFind(z.id);
            if (it == neuePos.cend()) { z.herkunft = -1; continue; }
            //  Nach dem Schreiben umgebaut: dann gibt es kein Gegenstueck.
            if (z.rev > e->rev && z.herkunft < 0) continue;
            z.herkunft = it.value();
        }
    }
    m_basisRev = e->rev;
    m_gespeichertRev = e->rev;
    m_speicherFehler.clear();
    m_kopieName.clear();
    emit editChanged();
    emit saved(true);
    if (m_nochmal) {
        m_nochmal = false;
        save();
    }
}

}  // namespace mg::table
