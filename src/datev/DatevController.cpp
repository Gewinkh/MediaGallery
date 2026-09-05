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
    m_pool.waitForDone();
}

void DatevController::setSource(const QString& pathOrUrl) {
    const QString pfad = mg::toLocalPath(pathOrUrl);
    if (pfad == m_source) return;
    if (m_abbruch) m_abbruch->store(true);

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
    spaltenNeuRechnen();
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

QString DatevController::cell(int row, int column) const {
    if (!m_datei || row < 0 || row >= m_datei->buchungen.size()) return {};
    return m_datei->buchungen.at(row).wert(column);
}

}  // namespace mg::datev
