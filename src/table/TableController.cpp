#include "table/TableController.h"

#include "core/PathUtils.h"
#include "core/Strings.h"

#include <QFile>
#include <QRunnable>
#include <QVariantMap>
#include <functional>

namespace mg::table {
namespace {

//  Derselbe Deckel wie beim Buchungsstapel: die Tabelle haelt je Zeile ihre
//  Felder, und darueber lohnt keine Anzeige mehr.
constexpr qint64 kMaxBytes = 32LL * 1024 * 1024;

//  So viele Zeilen sieht die Breitenmessung an.
constexpr int kProbeZeilen = 500;

class LeseTask : public QRunnable {
public:
    LeseTask(TableController* owner, QString pfad,
             std::shared_ptr<std::atomic<bool>> abbruch,
             std::function<void(std::shared_ptr<Datei>, QString)> zurueck)
        : m_owner(owner), m_pfad(std::move(pfad)),
          m_abbruch(std::move(abbruch)), m_zurueck(std::move(zurueck)) {
        setAutoDelete(true);
    }

    void run() override {
        QString fehler;
        auto d = std::make_shared<Datei>();
        QFile f(m_pfad);
        if (!f.open(QIODevice::ReadOnly)) {
            fehler = QStringLiteral("nicht lesbar");
        } else {
            const QByteArray roh = f.read(kMaxBytes);
            const qint64 groesse = f.size();
            f.close();
            if (m_abbruch->load()) return;
            //  Ohne Trennzeichen: `parse` raet es aus den ersten Zeilen.
            *d = parse(roh);
            if (groesse > kMaxBytes) d->abgeschnitten = true;
            if (!d->ok) fehler = d->fehler;
        }
        if (m_abbruch->load()) return;
        auto zurueck = m_zurueck;
        QMetaObject::invokeMethod(m_owner, [zurueck, d, fehler] { zurueck(d, fehler); },
                                  Qt::QueuedConnection);
    }

private:
    TableController* m_owner;
    QString m_pfad;
    std::shared_ptr<std::atomic<bool>> m_abbruch;
    std::function<void(std::shared_ptr<Datei>, QString)> m_zurueck;
};

}  // namespace

TableController::TableController(QObject* parent) : QObject(parent) {
    m_pool.setMaxThreadCount(1);
}

TableController::~TableController() {
    if (m_abbruch) m_abbruch->store(true);
    if (m_suchAbbruch) m_suchAbbruch->store(true);
    m_pool.waitForDone();
}

void TableController::setSource(const QString& pathOrUrl) {
    const QString pfad = mg::toLocalPath(pathOrUrl);
    if (pfad == m_source) return;
    m_source = pfad;
    emit sourceChanged();
    neuLesen();
}

void TableController::neuLesen() {
    if (m_abbruch) m_abbruch->store(true);
    clearSearch();
    clearSort();
    m_versteckt.clear();
    m_datei.reset();
    m_fehler.clear();
    m_spalten.clear();
    m_warnungen.clear();
    m_spaltenZahl = 0;
    m_busy = !m_source.isEmpty();
    emit stateChanged();
    if (m_source.isEmpty()) return;

    m_abbruch = std::make_shared<std::atomic<bool>>(false);
    auto* self = this;
    m_pool.start(new LeseTask(this, m_source, m_abbruch,
                              [self](std::shared_ptr<Datei> d, QString fehler) {
                                  self->ergebnisUebernehmen(std::move(d), fehler);
                              }));
}

void TableController::ergebnisUebernehmen(std::shared_ptr<Datei> d, const QString& fehler) {
    m_busy = false;
    m_fehler = fehler;
    m_datei = d && d->ok ? std::move(d) : nullptr;

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
    spaltenNeuRechnen();
    emit stateChanged();
    emit blockChanged();
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
    //  ausgeblendete oder sortierte Spalte waere eine willkuerlich andere.
    m_versteckt.clear();
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
    if (!m_datei || m_suchText.isEmpty()) {
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
        for (const Bereich& x : m_bereiche)
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
                              m_ordnung));
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
        for (int z = b.daten; z < bis; ++z)
            zeichen = qMax(zeichen, int(m_datei->zeilen.at(z).wert(i).size()));

        QVariantMap m;
        m.insert(QStringLiteral("index"), i);
        m.insert(QStringLiteral("title"), titel);
        //  Die Breite steht HIER, nicht in der Zelle: je Zelle gerechnet kostete
        //  das beim Rollen je neuer Zeile einen Lauf ueber die Probe mal Spalte.
        m.insert(QStringLiteral("chars"), zeichen);
        m_spalten.append(m);
    }
}

QString TableController::separator() const {
    return m_datei ? QString(m_datei->trenner) : QStringLiteral(";");
}

int TableController::rowCount() const {
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
    return m_datei->zeilen.at(z).wert(column);
}

QString TableController::rowText(int row) const {
    if (!m_datei) return {};
    QStringList felder;
    felder.reserve(m_spalten.size());
    for (const QVariant& v : m_spalten)
        felder.append(cell(row, v.toMap().value(QStringLiteral("index")).toInt()));
    return felder.join(QLatin1Char('\t'));
}

//  Anzeigezeile -> Zeile in der Datei. Ohne Sortierung ist das dieselbe
//  Rechnung wie zuvor; mit Sortierung steht die Antwort in `m_ordnung`.
int TableController::rohZeile(int anzeige) const {
    if (m_ordnung.isEmpty()) return aktiv().daten + anzeige;
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

void TableController::sortByColumn(int column) {
    if (column < 0 || column >= m_spaltenZahl) return;
    if (column != m_sortSpalte)              m_sortRichtung = SortRichtung::Auf;
    else if (m_sortRichtung == SortRichtung::Auf) m_sortRichtung = SortRichtung::Ab;
    else                                     m_sortRichtung = SortRichtung::Keine;
    m_sortSpalte = (m_sortRichtung == SortRichtung::Keine) ? -1 : column;
    ordnungNeuBauen();
}

void TableController::clearSort() {
    if (m_sortAbbruch) m_sortAbbruch->store(true);
    const bool hatte = m_sortSpalte >= 0 || !m_ordnung.isEmpty();
    m_sortSpalte   = -1;
    m_sortRichtung = SortRichtung::Keine;
    m_sortLaeuft   = false;
    m_ordnung.clear();
    if (hatte) { ++m_inhaltRevision; emit sortChanged(); }
}

void TableController::ordnungNeuBauen() {
    if (m_sortAbbruch) m_sortAbbruch->store(true);
    if (!m_datei || m_sortRichtung == SortRichtung::Keine) {
        m_sortLaeuft = false;
        m_ordnung.clear();
        ++m_inhaltRevision;
        emit sortChanged();
        //  Die Trefferzeilen zaehlen in der ANZEIGE - nach einem Wechsel der
        //  Reihenfolge zeigten die alten auf beliebige Zeilen.
        if (!m_suchText.isEmpty()) sucheStarten();
        return;
    }
    m_sortLaeuft = true;
    emit sortChanged();

    const Bereich b = aktiv();
    m_sortAbbruch = std::make_shared<std::atomic<bool>>(false);
    auto* self = this;
    m_pool.start(new SortTask(this, m_datei, &m_datei->zeilen, b.daten, b.bis,
                              m_sortSpalte, m_sortRichtung, m_sortAbbruch,
                              [self](QList<int> ordnung) {
                                  self->sortErgebnis(std::move(ordnung));
                              }));
}

void TableController::sortErgebnis(QList<int> ordnung) {
    m_sortLaeuft = false;
    m_ordnung = std::move(ordnung);
    ++m_inhaltRevision;
    emit sortChanged();
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
    //  Eine ausgeblendete Spalte darf die Reihenfolge nicht weiter bestimmen.
    if (hidden && column == m_sortSpalte) clearSort();
    spaltenNeuRechnen();
    ++m_inhaltRevision;
    emit stateChanged();
    emit sortChanged();
    if (!m_suchText.isEmpty()) sucheStarten();
}

void TableController::showAllColumns() {
    if (m_versteckt.isEmpty()) return;
    m_versteckt.clear();
    spaltenNeuRechnen();
    ++m_inhaltRevision;
    emit stateChanged();
    emit sortChanged();
    if (!m_suchText.isEmpty()) sucheStarten();
}

}  // namespace mg::table
