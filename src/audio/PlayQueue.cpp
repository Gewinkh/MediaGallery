#include "audio/PlayQueue.h"

#include <QHash>
#include <QRandomGenerator>
#include <QSet>

#include <utility>

PlayQueue::PlayQueue(QObject* parent)
    : QObject(parent)
    , m_rng(QRandomGenerator::system()->generate64())
{}

PlayQueue::PlayQueue(uint64_t seed, QObject* parent)
    : QObject(parent)
    , m_rng(seed)
{}

QStringList PlayQueue::orderedItems() const {
    QStringList out;
    out.reserve(m_order.size());
    for (int idx : m_order)
        if (idx >= 0 && idx < m_items.size()) out.append(m_items.at(idx));
    return out;
}

QString PlayQueue::pathAtOrder(int orderPos) const {
    if (orderPos < 0 || orderPos >= m_order.size()) return QString();
    const int idx = m_order.at(orderPos);
    return (idx >= 0 && idx < m_items.size()) ? m_items.at(idx) : QString();
}

bool PlayQueue::startAtOrder(int orderPos) {
    if (orderPos < 0 || orderPos >= m_order.size()) return false;
    if (m_started && m_pos != orderPos) noteHistory();
    //  Kein Neumischen: der Rest bleibt, wie er angezeigt wird.
    m_pos = orderPos;
    m_started = true;
    emit currentChanged();
    return true;
}

QString PlayQueue::currentPath() const {
    const int i = currentItemIndex();
    return (i >= 0 && i < m_items.size()) ? m_items.at(i) : QString();
}

QVector<int> PlayQueue::grundfolge() const {
    const int n = int(m_items.size());
    QVector<int> aus;
    aus.reserve(n);
    if (m_eigene.isEmpty()) {
        for (int i = 0; i < n; ++i) aus.append(i);
        return aus;
    }
    //  Zuordnung statt `indexOf` je Eintrag - bei 5000 Titeln sonst 25 Mio. Vergleiche.
    QHash<QString, int> nummer;
    nummer.reserve(n);
    for (int i = 0; i < n; ++i) nummer.insert(m_items.at(i), i);
    for (const QString& p : m_eigene) {
        const auto it = nummer.constFind(p);
        if (it != nummer.cend()) aus.append(*it);
    }
    return aus;
}

bool PlayQueue::pflegeEigene() {
    if (m_eigene.isEmpty()) return false;
    //  Mengen statt `contains` je Eintrag - sonst waere jeder Ordnerwechsel quadratisch.
    const QSet<QString> da(m_items.cbegin(), m_items.cend());
    QSet<QString> drin;
    drin.reserve(m_eigene.size());
    QStringList neu;
    neu.reserve(m_items.size());
    for (const QString& p : std::as_const(m_eigene))
        if (da.contains(p) && !drin.contains(p)) { neu.append(p); drin.insert(p); }
    for (const QString& p : std::as_const(m_items))
        if (!drin.contains(p)) { neu.append(p); drin.insert(p); }
    if (neu == m_eigene) return false;
    m_eigene = std::move(neu);
    return true;
}

//  Aendert der Abgleich nichts, bleibt alles stehen - bei Zufall mischte jedes Neuordnen neu.
void PlayQueue::setCustomOrder(const QStringList& paths) {
    if (paths == m_eigene) return;
    const QStringList vorher = std::exchange(m_eigene, paths);
    pflegeEigene();
    if (m_eigene == vorher) return;
    rebuildOrder(m_started ? currentItemIndex() : -1);
    emit customOrderChanged();
    emit currentChanged();
}

void PlayQueue::clearCustomOrder() {
    if (m_eigene.isEmpty()) return;
    m_eigene.clear();
    rebuildOrder(m_started ? currentItemIndex() : -1);
    emit customOrderChanged();
    emit currentChanged();
}

bool PlayQueue::moveOrder(int von, int nach) {
    const int n = int(m_order.size());
    if (von < 0 || von >= n || nach < 0 || nach >= n || von == nach) return false;

    const int laufend = m_started ? currentItemIndex() : -1;
    m_order.move(von, nach);

    if (!m_shuffle) {
        QStringList neu;
        neu.reserve(n);
        for (const int i : std::as_const(m_order))
            if (i >= 0 && i < m_items.size()) neu.append(m_items.at(i));
        m_eigene = std::move(neu);
    }

    //  Die Stelle folgt dem TITEL, nicht der Nummer: wer den laufenden Titel
    //  verschiebt, will ihn weiterhoeren, nicht den, der nachgerueckt ist.
    if (laufend >= 0) m_pos = int(m_order.indexOf(laufend));
    emit customOrderChanged();
    emit currentChanged();
    return true;
}

// `keepItemIndex` ist der laufende Titel: er bleibt vorn und an seiner Stelle, alles danach wird bei Zufall neu
// gemischt. Ohne das risse jede Filteränderung den laufenden Titel weg.
void PlayQueue::rebuildOrder(int keepItemIndex) {
    m_order = grundfolge();
    const int n = int(m_order.size());

    if (m_shuffle && n > 1) {
        //  Fisher-Yates von hinten - jede Anordnung ist gleich wahrscheinlich.
        for (int i = n - 1; i > 0; --i) {
            const int j = int(m_rng.below(uint32_t(i + 1)));
            m_order.swapItemsAt(i, j);
        }
    }

    if (keepItemIndex >= 0 && m_order.contains(keepItemIndex)) {
        if (m_shuffle) {
            //  Gemischt: die Runde beginnt beim laufenden Titel.
            const int at = m_order.indexOf(keepItemIndex);
            if (at > 0) m_order.swapItemsAt(0, at);
            m_pos = 0;
        } else {
            //  Ungemischt: gesucht wird die STELLE des Titels - bei eigener
            //  Ordnung ist sie nicht seine Nummer in der Liste.
            m_pos = int(m_order.indexOf(keepItemIndex));
        }
    } else {
        m_pos = n > 0 ? 0 : -1;
    }
}

void PlayQueue::setItems(const QStringList& paths) {
    const QString playing = m_started ? currentPath() : QString();
    QHash<QString, int> neuNr;
    neuNr.reserve(paths.size());
    for (int i = 0; i < paths.size(); ++i) neuNr.insert(paths.at(i), i);
    auto neueNummer = [&](int alt) {
        return alt >= 0 && alt < m_items.size() ? neuNr.value(m_items.at(alt), -1) : -1;
    };
    //  Die Historie haelt Nummern der alten Liste - sonst fuehrte „zurueck" auf einen fremden Titel.
    QList<int> historie;
    for (const int h : std::as_const(m_history))
        if (const int n = neueNummer(h); n >= 0) historie.append(n);
    m_history = std::move(historie);

    //  Dieselben Titel in anderer Folge: die Mischung bleibt. Die Galerie des
    //  Player-Modus reicht die Playlist-Folge zurueck, ein Neumischen liefe endlos.
    if (m_shuffle && paths.size() == m_items.size() && neuNr.size() == paths.size()) {
        QVector<int> order;
        order.reserve(m_order.size());
        for (const int i : std::as_const(m_order)) {
            const int n = neueNummer(i);
            if (n < 0) break;
            order.append(n);
        }
        if (order.size() == m_order.size()) {
            m_order = std::move(order);
            m_items = paths;
            emit itemsChanged();
            return;
        }
    }

    m_items = paths;
    const int keep = playing.isEmpty() ? -1 : int(m_items.indexOf(playing));
    if (keep < 0) m_started = false;          // der laufende Titel ist heraus
    if (pflegeEigene()) emit customOrderChanged();
    rebuildOrder(keep);
    //  Stand der laufende Titel nicht mehr in der Liste, beginnt sie von vorn -
    //  aber sie SPIELT nicht von selbst weiter; das entscheidet die Engine.
    emit itemsChanged();
    emit currentChanged();
}

void PlayQueue::setShuffle(bool on) {
    if (m_shuffle == on) return;
    m_shuffle = on;
    //  Läuft noch nichts, darf die frische Mischung irgendwo beginnen.
    rebuildOrder(m_started ? currentItemIndex() : -1);
    emit shuffleChanged();
    emit currentChanged();
}

void PlayQueue::setRepeat(Repeat r) {
    if (m_repeat == r) return;
    m_repeat = r;
    emit repeatChanged();
}

bool PlayQueue::startAt(const QString& path) {
    const int idx = int(m_items.indexOf(path));
    if (idx < 0) return false;
    if (m_started && currentItemIndex() != idx) noteHistory();
    m_started = true;
    rebuildOrder(idx);
    emit currentChanged();
    return true;
}

void PlayQueue::noteHistory() {
    const int cur = currentItemIndex();
    if (cur < 0) return;
    if (!m_history.isEmpty() && m_history.last() == cur) return;   // kein Doppel
    m_history.append(cur);
    if (m_history.size() > kMaxHistory) m_history.removeFirst();
}

QString PlayQueue::advance(bool natural) {
    if (m_order.isEmpty() || m_pos < 0) return {};

    //  „Eine wiederholen" gilt NUR beim natürlichen Ende - wer weiterschaltet,
    //  will den nächsten.
    if (natural && m_repeat == Repeat::One)
        return currentPath();

    if (m_pos + 1 < m_order.size()) {
        noteHistory();
        ++m_pos;
        emit currentChanged();
        return currentPath();
    }

    if (m_repeat != Repeat::All) return {};        // hier ist Schluss
    noteHistory();
    if (m_shuffle) rebuildOrder(-1);               // neue Runde, neu gemischt
    m_pos = 0;
    emit currentChanged();
    return currentPath();
}

QString PlayQueue::peekNext(bool natural) const {
    if (m_order.isEmpty() || m_pos < 0) return {};
    if (natural && m_repeat == Repeat::One) return currentPath();
    if (m_pos + 1 < m_order.size()) return pathAtOrder(m_pos + 1);
    if (m_repeat != Repeat::All) return {};
    if (m_shuffle) return {};                  // die neue Runde wird erst gemischt
    return pathAtOrder(0);
}

QString PlayQueue::back() {
    if (m_order.isEmpty() || m_pos < 0) return {};

    //  Erst die Historie: sie weiß, was wirklich lief, auch wenn der Zufall
    //  zwischendurch umgeschaltet wurde.
    while (!m_history.isEmpty()) {
        const int item = m_history.takeLast();
        const int at = int(m_order.indexOf(item));
        if (at < 0) continue;                      // steht nicht mehr in der Liste
        m_pos = at;
        emit currentChanged();
        return currentPath();
    }

    //  Nichts gehört (frische Liste): dann die Ordnung rückwärts.
    if (m_pos > 0) {
        --m_pos;
    } else if (m_repeat == Repeat::All) {
        m_pos = m_order.size() - 1;                // am Anfang hinten weiter
    } else {
        return currentPath();                      // bleibt stehen
    }
    emit currentChanged();
    return currentPath();
}
