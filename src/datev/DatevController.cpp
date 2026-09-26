#include "datev/DatevController.h"

#include "core/AppSettings.h"
#include "core/PathUtils.h"
#include "core/Strings.h"
#include "datev/DatevFormat.h"
#include "table/TableWidths.h"

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

//  Obergrenze fuer „Bereich kopieren": darueber dauert der Aufbau im GUI-Faden
//  laenger, als das Ergebnis jemandem nuetzt.
constexpr qint64 kMaxBereichZellen = 500000;

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
    connect(this, &DatevController::stateChanged, this, &DatevController::rowsChanged);
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

    //  Reihenfolge, Filter und Spaltenauswahl gehoeren zur DATEI, nicht zur Flaeche.
    m_filter = {};
    m_sortSpalte = -1;
    m_sortRichtung = mg::table::SortRichtung::Keine;
    if (m_sortAbbruch) m_sortAbbruch->store(true);
    m_sortLaeuft = false;
    m_ordnung.clear();
    m_ordnungAktiv = false;
    ++m_inhaltRevision;
    emit sortChanged();
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

void DatevController::reload() {
    const QString pfad = m_source;
    m_source.clear();
    setSource(pfad);
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
    //  Breiten und Formate gehoeren zur DATEI, nicht zur Flaeche - sie kommen
    //  mit ihr.
    if (m_datei) mg::table::liesSpalten(m_source, m_breiten, m_formate);
    else { m_breiten.clear(); m_formate.clear(); }
    spaltenNeuRechnen();
    emit stateChanged();
}

void DatevController::setColumnWidth(int column, int px) {
    if (column < 0) return;
    if (px <= 0) {
        if (m_breiten.remove(column) == 0) return;
    } else {
        const int neu = qBound(mg::table::kMinBreite, px, mg::table::kMaxBreite);
        if (m_breiten.value(column, 0) == neu) return;
        m_breiten.insert(column, neu);
    }
    spaltenAblegen();
    spaltenNeuRechnen();
}

void DatevController::spaltenAblegen() {
    mg::table::schreibeSpalten(m_source, m_breiten, m_formate);
}

void DatevController::formatAendern(
    int column, const std::function<void(mg::table::SpaltenFormat&)>& aendere) {
    if (column < 0) return;
    mg::table::SpaltenFormat f = m_formate.value(column);
    const mg::table::SpaltenFormat vorher = f;
    aendere(f);
    if (f == vorher) return;
    if (f.leer()) m_formate.remove(column);
    else          m_formate.insert(column, f);
    spaltenAblegen();
    spaltenNeuRechnen();
}

void DatevController::setColumnBold(int column, bool bold) {
    formatAendern(column, [bold](mg::table::SpaltenFormat& f) { f.fett = bold; });
}

void DatevController::setColumnColor(int column, const QString& color) {
    formatAendern(column, [&color](mg::table::SpaltenFormat& f) { f.farbe = color; });
}

void DatevController::setColumnBackground(int column, const QString& color) {
    formatAendern(column, [&color](mg::table::SpaltenFormat& f) { f.hintergrund = color; });
}

void DatevController::clearColumnFormat(int column) {
    formatAendern(column, [](mg::table::SpaltenFormat& f) { f = mg::table::SpaltenFormat(); });
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
        m.insert(QStringLiteral("px"), m_breiten.value(i, 0));
        //  Nur gesetzte Felder - eine Zelle fragt sonst je Bild nach drei
        //  Werten, die nie belegt sind.
        const auto fm = m_formate.constFind(i);
        if (fm != m_formate.cend()) {
            if (fm->fett) m.insert(QStringLiteral("fett"), true);
            if (!fm->farbe.isEmpty()) m.insert(QStringLiteral("fg"), fm->farbe);
            if (!fm->hintergrund.isEmpty()) {
                m.insert(QStringLiteral("bg"), fm->hintergrund);
                //  Ohne eigene Textfarbe entscheidet der Hintergrund, nicht das
                //  Thema - sonst steht heller Text auf heller Flaeche.
                if (fm->farbe.isEmpty())
                    m.insert(QStringLiteral("fg"), mg::table::lesbarAuf(fm->hintergrund));
            }
        }
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
    spaltenNeuRechnen();
    const bool filterSpalteWeg = m_filter.spalte >= 0 && !spaltenMaske().value(m_filter.spalte, true);
    if (filterSpalteWeg) m_filter = {};
    if (m_sortSpalte >= 0) clearSort();
    else if (filterSpalteWeg || (m_filter.aktiv() && m_filter.spalte < 0)) ordnungNeuBauen();
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

void DatevController::setGroupDigits(bool v) {
    if (v == m_gruppiert) return;
    m_gruppiert = v;
    //  Die Zellen haengen ihre Bindung an `contentRevision`.
    ++m_inhaltRevision;
    emit sortChanged();
}

void DatevController::setSlashDateMonthFirst(bool v) {
    if (v == m_monatZuerst) return;
    m_monatZuerst = v;
    if (m_filter.aktiv() || m_sortRichtung != mg::table::SortRichtung::Keine) ordnungNeuBauen();
    else emit sortChanged();
}

void DatevController::sucheStarten() {
    if (m_suchAbbruch) m_suchAbbruch->store(true);
    //  Ein Filter ohne Treffer zeigt keine Buchung - dort kann nichts stehen.
    if (!m_datei || m_suchText.isEmpty() || (m_ordnungAktiv && m_ordnung.isEmpty())) {
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
        m_ordnungAktiv ? m_ordnung : QList<int>()));
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
    if (!m_datei) return 0;
    return m_ordnungAktiv ? int(m_ordnung.size()) : int(m_datei->buchungen.size());
}

int DatevController::rowNumber(int row) const {
    if (!m_filter.aktiv()) return row + 1;
    const int z = rohZeile(row);
    return z < 0 ? row + 1 : z + 1;
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
    //  Nur die ANZEIGE; die Zwischenablage geht weiter ueber den Rohwert.
    //  Ein Buchungsstapel schreibt seine Betraege immer mit Komma.
    return mg::table::zahlAnzeigen(m_datei->buchungen.at(z).wert(column), m_gruppiert, true);
}

QString DatevController::rowText(int row) const {
    if (!m_datei) return {};
    QStringList felder;
    felder.reserve(m_spalten.size());
    for (const QVariant& v : m_spalten)
        felder.append(cell(row, v.toMap().value(QStringLiteral("index")).toInt()));
    return felder.join(QLatin1Char('\t'));
}

QString DatevController::rangeText(int row1, int col1, int row2, int col2) const {
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

int DatevController::rohZeile(int anzeige) const {
    if (!m_ordnungAktiv) return anzeige;
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
    m_sortSpalte   = -1;
    m_sortRichtung = mg::table::SortRichtung::Keine;
    //  Ein Filter bleibt stehen - dann gilt dessen Auswahl in Dateireihenfolge.
    ordnungNeuBauen();
}

void DatevController::setFilter(int column, const QString& text, bool caseSensitive,
                                bool wholeCell) {
    mg::table::FilterRegel neu;
    neu.spalte = (m_datei && column >= 0 && column < m_datei->spalten.size()) ? column : -1;
    neu.text = text;
    neu.opt.gross = caseSensitive;
    neu.opt.ganzeZelle = wholeCell;
    if (neu.spalte == m_filter.spalte && neu.text == m_filter.text
        && neu.opt.gross == m_filter.opt.gross && neu.opt.ganzeZelle == m_filter.opt.ganzeZelle)
        return;
    m_filter = neu;
    ordnungNeuBauen();
}

void DatevController::clearFilter() {
    if (!m_filter.aktiv()) return;
    m_filter = {};
    ordnungNeuBauen();
}

void DatevController::ordnungNeuBauen() {
    if (m_sortAbbruch) m_sortAbbruch->store(true);
    if (!m_datei || (!m_filter.aktiv() && m_sortRichtung == mg::table::SortRichtung::Keine)) {
        m_sortLaeuft = false;
        m_ordnung.clear();
        m_ordnungAktiv = false;
        if (m_datei) { m_soll = m_datei->soll; m_haben = m_datei->haben; }
        ++m_inhaltRevision;
        emit sortChanged();
        emit rowsChanged();
        if (!m_suchText.isEmpty()) sucheStarten();
        return;
    }
    m_sortLaeuft = true;
    emit sortChanged();

    mg::table::Ordnungsauftrag a;
    a.von = 0;
    a.bis = int(m_datei->buchungen.size());
    a.filter = m_filter;
    a.spalten = spaltenMaske();
    a.sortSpalte = m_sortSpalte;
    a.richtung = m_sortRichtung;
    a.monatZuerst = a.filter.monatZuerst = m_monatZuerst;
    const Summenspalten ss = summenSpalten(m_datei->spalten);
    m_sortAbbruch = std::make_shared<std::atomic<bool>>(false);
    auto* self = this;
    m_pool.start(new mg::table::OrdnungTask(
        this, m_datei, &m_datei->buchungen, a, m_sortAbbruch,
        [self](QList<int> ordnung, bool aktiv, QVariant summen) {
            self->ordnungErgebnis(std::move(ordnung), aktiv, summen);
        },
        [ss](const QList<Zeile>& buchungen, const QList<int>* auswahl) -> QVariant {
            if (!auswahl) return {};
            double soll = 0.0, haben = 0.0;
            summiere(buchungen, ss, auswahl, &soll, &haben);
            return QVariantList{ soll, haben };
        }));
}

void DatevController::ordnungErgebnis(QList<int> ordnung, bool aktiv, const QVariant& summen) {
    m_sortLaeuft = false;
    m_ordnung = std::move(ordnung);
    m_ordnungAktiv = aktiv;
    const QVariantList s = summen.toList();
    if (s.size() == 2) { m_soll = s.at(0).toDouble(); m_haben = s.at(1).toDouble(); }
    else if (m_datei)  { m_soll = m_datei->soll;      m_haben = m_datei->haben; }
    ++m_inhaltRevision;
    emit sortChanged();
    emit rowsChanged();
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
    spaltenNeuRechnen();
    ++m_inhaltRevision;
    bool neuOrdnen = false;
    if (hidden && column == m_sortSpalte) {
        m_sortSpalte = -1;
        m_sortRichtung = mg::table::SortRichtung::Keine;
        neuOrdnen = true;
    }
    if (hidden && column == m_filter.spalte) { m_filter = {}; neuOrdnen = true; }
    if (m_filter.aktiv() && m_filter.spalte < 0) neuOrdnen = true;
    if (neuOrdnen) ordnungNeuBauen();
    else emit sortChanged();
    if (!m_suchText.isEmpty()) sucheStarten();
}

void DatevController::showAllHiddenColumns() {
    if (m_versteckt.isEmpty()) return;
    m_versteckt.clear();
    spaltenNeuRechnen();
    ++m_inhaltRevision;
    if (m_filter.aktiv() && m_filter.spalte < 0) ordnungNeuBauen();
    else emit sortChanged();
    if (!m_suchText.isEmpty()) sucheStarten();
}

}  // namespace mg::datev
