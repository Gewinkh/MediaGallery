#include "datev/DatevController.h"

#include "core/AppSettings.h"
#include "core/PathUtils.h"
#include "core/Strings.h"
#include "datev/DatevFormat.h"

#include <QFile>
#include <QRunnable>
#include <QVariantMap>

namespace mg::datev {
namespace {

//  Eigener Deckel, unabhaengig vom 8-MB-Deckel des Texteditors: die Tabelle
//  haelt je Zeile 125 Zeichenketten, und darueber lohnt keine Anzeige mehr.
constexpr qint64 kMaxBytes = 32LL * 1024 * 1024;

//  So viele Zeilen sieht die Breitenmessung an.
constexpr int kProbeZeilen = 500;

class LeseTask : public QRunnable {
public:
    LeseTask(DatevController* owner, QString pfad,
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
            f.close();
            if (m_abbruch->load()) return;
            *d = parse(roh);
            if (f.size() > kMaxBytes) d->abgeschnitten = true;
            if (!d->ok) fehler = d->fehler;
        }
        if (m_abbruch->load()) return;
        auto zurueck = m_zurueck;
        QMetaObject::invokeMethod(m_owner, [zurueck, d, fehler] { zurueck(d, fehler); },
                                  Qt::QueuedConnection);
    }

private:
    DatevController* m_owner;
    QString m_pfad;
    std::shared_ptr<std::atomic<bool>> m_abbruch;
    std::function<void(std::shared_ptr<Datei>, QString)> m_zurueck;
};

}  // namespace

DatevController::DatevController(QObject* parent) : QObject(parent) {
    m_pool.setMaxThreadCount(1);
    //  Die Namen der Kopffelder kommen aus `Strings` - bei einem Sprachwechsel
    //  muss die Kopftabelle deshalb neu gelesen werden.
    connect(&AppSettings::instance(), &AppSettings::languageChanged,
            this, &DatevController::stateChanged);
}

DatevController::~DatevController() {
    if (m_abbruch) m_abbruch->store(true);
    if (m_suchAbbruch) m_suchAbbruch->store(true);
    m_pool.waitForDone();
}

void DatevController::setSource(const QString& pathOrUrl) {
    const QString pfad = mg::toLocalPath(pathOrUrl);
    if (pfad == m_source) return;
    if (m_abbruch) m_abbruch->store(true);

    if (m_suchAbbruch) m_suchAbbruch->store(true);
    m_suchText.clear();
    m_suchLaeuft = false;
    m_suche.leeren();

    //  Reihenfolge und Spaltenauswahl gehoeren zur DATEI, nicht zur Flaeche.
    clearSort();
    m_versteckt.clear();

    m_source = pfad;
    m_datei.reset();
    m_fehler.clear();
    m_spalten.clear();
    m_warnungen.clear();
    m_soll = m_haben = 0.0;
    m_busy = !pfad.isEmpty();
    emit sourceChanged();
    emit columnsChanged();
    emit stateChanged();
    emit searchChanged();
    if (pfad.isEmpty()) return;

    m_abbruch = std::make_shared<std::atomic<bool>>(false);
    auto* self = this;
    m_pool.start(new LeseTask(this, pfad, m_abbruch,
                              [self](std::shared_ptr<Datei> d, QString fehler) {
                                  self->ergebnisUebernehmen(std::move(d), fehler);
                              }));
}

void DatevController::ergebnisUebernehmen(std::shared_ptr<Datei> d, const QString& fehler) {
    m_busy = false;
    m_fehler = fehler;
    m_datei = d && d->ok ? std::move(d) : nullptr;

    m_soll = m_haben = 0.0;
    m_warnungen.clear();
    if (m_datei) {
        m_soll  = m_datei->soll;
        m_haben = m_datei->haben;
        for (const Warnung& w : std::as_const(m_datei->warnungen))
            m_warnungen.append(QStringLiteral("%1: %2").arg(w.zeile).arg(w.text));
    }
    spaltenNeuRechnen();
    emit stateChanged();
}

void DatevController::spaltenNeuRechnen() {
    m_spalten.clear();
    if (!m_datei) { emit columnsChanged(); return; }

    const int n = int(m_datei->spalten.size());
    m_spalten.reserve(n);

    for (int i = 0; i < n; ++i) {
        if (!m_alleSpalten && i < m_datei->spalteGefuellt.size()
            && !m_datei->spalteGefuellt.at(i)) continue;
        if (m_versteckt.contains(i)) continue;
        QVariantMap m;
        m.insert(QStringLiteral("index"), i);
        m.insert(QStringLiteral("title"), m_datei->spalten.at(i));
        //  Die Breite steht HIER, nicht in der Zelle: `columnChars` liest bis zu
        //  500 Zeilen, und je Zelle gerufen kostete das beim Rollen je neuer
        //  Zeile 20 x 500 Suchlaeufe.
        m.insert(QStringLiteral("chars"), columnChars(i));
        m_spalten.append(m);
    }
    emit columnsChanged();
}

void DatevController::setShowAllColumns(bool v) {
    if (v == m_alleSpalten) return;
    m_alleSpalten = v;
    //  Der Schalter ist der grosse Griff - was einzeln ausgeblendet war, gilt
    //  danach nicht mehr, sonst fehlten in "alle Spalten" weiter welche.
    m_versteckt.clear();
    if (m_sortSpalte >= 0) clearSort();
    spaltenNeuRechnen();
    //  Die Suche laeuft ueber die GEZEIGTEN Spalten - mit den ausgeblendeten
    //  kommen auch deren Treffer dazu.
    if (!m_suchText.isEmpty()) { m_suchAb = 0; sucheStarten(); }
}

void DatevController::search(const QString& text, bool caseSensitive,
                             bool wholeCell, int fromRow) {
    m_suchText = text;
    m_suchOpt.gross = caseSensitive;
    m_suchOpt.ganzeZelle = wholeCell;
    m_suchAb = qMax(0, fromRow);
    sucheStarten();
}

void DatevController::sucheStarten() {
    if (m_suchAbbruch) m_suchAbbruch->store(true);
    if (!m_datei || m_suchText.isEmpty()) {
        m_suchLaeuft = false;
        m_suche.leeren();
        emit searchChanged();
        return;
    }
    m_suchLaeuft = true;
    emit searchChanged();

    m_suchAbbruch = std::make_shared<std::atomic<bool>>(false);
    auto* self = this;
    m_pool.start(new mg::table::SuchTask(
        this, m_datei, &m_datei->buchungen, 0, int(m_datei->buchungen.size()),
        m_suchText, m_suchOpt, spaltenMaske(), {}, m_suchAbbruch,
        [self](QList<mg::table::Treffer> t, bool mehr, QList<int>) {
            self->suchErgebnis(std::move(t), mehr);
        },
        m_ordnung));
}

void DatevController::suchErgebnis(QList<mg::table::Treffer> treffer, bool mehr) {
    m_suchLaeuft = false;
    m_suche.setzeTreffer(std::move(treffer), mehr);
    m_suche.gehZuAb(m_suchAb);
    emit searchChanged();
}

void DatevController::stepMatch(int delta) {
    if (m_suche.anzahl() == 0) return;
    m_suche.schritt(delta);
    emit searchChanged();
}

void DatevController::clearSearch() {
    if (m_suchAbbruch) m_suchAbbruch->store(true);
    m_suchText.clear();
    m_suchLaeuft = false;
    m_suche.leeren();
    emit searchChanged();
}

QString DatevController::identifier() const {
    return (m_datei && !m_datei->kopf.isEmpty()) ? m_datei->kopf.at(0) : QString();
}

int DatevController::version() const {
    return (m_datei && m_datei->kopf.size() > 1) ? m_datei->kopf.at(1).toInt() : 0;
}

QString DatevController::formatName() const {
    return (m_datei && m_datei->kopf.size() > 3) ? m_datei->kopf.at(3) : QString();
}

QString DatevController::createdAt() const {
    return (m_datei && m_datei->kopf.size() > 5) ? erzeugtAmLesbar(m_datei->kopf.at(5)) : QString();
}

QVariantList DatevController::headerFields() const {
    QVariantList out;
    if (!m_datei) return out;
    const QList<KopfFeld> katalog = kopfFelder(version());
    out.reserve(m_datei->kopf.size());
    for (int i = 0; i < m_datei->kopf.size(); ++i) {
        QVariantMap m;
        m.insert(QStringLiteral("number"), i + 1);
        QString name;
        for (const KopfFeld& k : katalog)
            if (k.nummer == i + 1) { name = Strings::get(k.name); break; }
        m.insert(QStringLiteral("name"), name);
        m.insert(QStringLiteral("value"), m_datei->kopf.at(i));
        out.append(m);
    }
    return out;
}

int DatevController::columnChars(int column) const {
    if (!m_datei || column < 0 || column >= m_datei->spalten.size()) return 0;
    int n = int(m_datei->spalten.at(column).size());
    const int bis = int(qMin<qsizetype>(m_datei->buchungen.size(), kProbeZeilen));
    for (int i = 0; i < bis; ++i) {
        n = qMax(n, int(m_datei->buchungen.at(i).wert(column).size()));
    }
    return n;
}

int DatevController::rowCount() const {
    return m_datei ? int(m_datei->buchungen.size()) : 0;
}

int DatevController::columnCount() const {
    return m_datei ? int(m_datei->spalten.size()) : 0;
}

QVariantList DatevController::rowMatches(int row) const {
    QVariantList out;
    const QList<int> spalten = m_suche.spaltenIn(row);
    out.reserve(spalten.size());
    for (int s : spalten) out.append(s);
    return out;
}

QString DatevController::cell(int row, int column) const {
    if (!m_datei) return {};
    const int z = rohZeile(row);
    if (z < 0 || z >= m_datei->buchungen.size()) return {};
    return m_datei->buchungen.at(z).wert(column);
}

QString DatevController::rowText(int row) const {
    if (!m_datei) return {};
    QStringList felder;
    felder.reserve(m_spalten.size());
    for (const QVariant& v : m_spalten)
        felder.append(cell(row, v.toMap().value(QStringLiteral("index")).toInt()));
    return felder.join(QLatin1Char('\t'));
}

int DatevController::rohZeile(int anzeige) const {
    if (m_ordnung.isEmpty()) return anzeige;
    if (anzeige < 0 || anzeige >= m_ordnung.size()) return -1;
    return m_ordnung.at(anzeige);
}

QList<bool> DatevController::spaltenMaske() const {
    if (!m_datei) return {};
    const int n = int(m_datei->spalten.size());
    //  Nur die gezeigten Spalten: ein Treffer in einer ausgeblendeten waere ein
    //  Sprung auf eine Zelle, die niemand sieht.
    if (m_alleSpalten && m_versteckt.isEmpty()) return {};
    QList<bool> maske(n, true);
    if (!m_alleSpalten) maske = m_datei->spalteGefuellt;
    for (int s : m_versteckt)
        if (s >= 0 && s < maske.size()) maske[s] = false;
    return maske;
}

void DatevController::sortByColumn(int column) {
    if (!m_datei || column < 0 || column >= m_datei->spalten.size()) return;
    if (column != m_sortSpalte) m_sortRichtung = mg::table::SortRichtung::Auf;
    else if (m_sortRichtung == mg::table::SortRichtung::Auf)
        m_sortRichtung = mg::table::SortRichtung::Ab;
    else m_sortRichtung = mg::table::SortRichtung::Keine;
    m_sortSpalte = (m_sortRichtung == mg::table::SortRichtung::Keine) ? -1 : column;
    ordnungNeuBauen();
}

void DatevController::clearSort() {
    if (m_sortAbbruch) m_sortAbbruch->store(true);
    const bool hatte = m_sortSpalte >= 0 || !m_ordnung.isEmpty();
    m_sortSpalte   = -1;
    m_sortRichtung = mg::table::SortRichtung::Keine;
    m_sortLaeuft   = false;
    m_ordnung.clear();
    if (hatte) { ++m_inhaltRevision; emit sortChanged(); }
}

void DatevController::ordnungNeuBauen() {
    if (m_sortAbbruch) m_sortAbbruch->store(true);
    if (!m_datei || m_sortRichtung == mg::table::SortRichtung::Keine) {
        m_sortLaeuft = false;
        m_ordnung.clear();
        ++m_inhaltRevision;
        emit sortChanged();
        if (!m_suchText.isEmpty()) sucheStarten();
        return;
    }
    m_sortLaeuft = true;
    emit sortChanged();

    m_sortAbbruch = std::make_shared<std::atomic<bool>>(false);
    auto* self = this;
    m_pool.start(new mg::table::SortTask(
        this, m_datei, &m_datei->buchungen, 0, int(m_datei->buchungen.size()),
        m_sortSpalte, m_sortRichtung, m_sortAbbruch,
        [self](QList<int> ordnung) { self->sortErgebnis(std::move(ordnung)); }));
}

void DatevController::sortErgebnis(QList<int> ordnung) {
    m_sortLaeuft = false;
    m_ordnung = std::move(ordnung);
    ++m_inhaltRevision;
    emit sortChanged();
    //  Die Trefferzeilen zaehlen in der ANZEIGE - nach einem Wechsel der
    //  Reihenfolge zeigten die alten auf beliebige Buchungen.
    if (!m_suchText.isEmpty()) sucheStarten();
}

void DatevController::setColumnHidden(int column, bool hidden) {
    if (!m_datei || column < 0 || column >= m_datei->spalten.size()) return;
    //  Die LETZTE gezeigte Spalte bleibt stehen - eine Tabelle ohne Spalten ist
    //  eine leere Flaeche, aus der kein Weg zurueckfuehrt.
    if (hidden && m_spalten.size() <= 1) return;
    const bool war = m_versteckt.contains(column);
    if (war == hidden) return;
    if (hidden) m_versteckt.insert(column);
    else        m_versteckt.remove(column);
    if (hidden && column == m_sortSpalte) clearSort();
    spaltenNeuRechnen();
    ++m_inhaltRevision;
    emit sortChanged();
    if (!m_suchText.isEmpty()) sucheStarten();
}

void DatevController::showAllHiddenColumns() {
    if (m_versteckt.isEmpty()) return;
    m_versteckt.clear();
    spaltenNeuRechnen();
    ++m_inhaltRevision;
    emit sortChanged();
    if (!m_suchText.isEmpty()) sucheStarten();
}

}  // namespace mg::datev
