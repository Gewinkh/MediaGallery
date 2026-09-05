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
    spaltenNeuRechnen();
    emit blockChanged();
    emit stateChanged();
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
    const int z = aktiv().daten + row;
    return z >= 0 && z < m_datei->zeilen.size() && m_datei->zeilen.at(z).isEmpty();
}

QString TableController::cell(int row, int column) const {
    if (!m_datei) return {};
    const int z = aktiv().daten + row;
    if (z < 0 || z >= m_datei->zeilen.size()) return {};
    return m_datei->zeilen.at(z).wert(column);
}

}  // namespace mg::table
