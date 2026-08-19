#include "audio/PlayQueue.h"

#include <QRandomGenerator>

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
    //  Kein Neumischen: wer in der ANGEZEIGTEN Folge etwas anklickt, erwartet,
    //  dass der Rest so bleibt, wie er dasteht.
    m_pos = orderPos;
    m_started = true;
    emit currentChanged();
    return true;
}

QString PlayQueue::currentPath() const {
    const int i = currentItemIndex();
    return (i >= 0 && i < m_items.size()) ? m_items.at(i) : QString();
}

//  Die Reihenfolge neu aufbauen. `keepItemIndex` ist der Titel, der gerade
//  läuft: er bleibt vorn und an seiner Stelle, alles danach wird (bei Zufall)
//  neu gemischt. Ohne das würde jede Filteränderung den laufenden Titel
//  wegreißen.
void PlayQueue::rebuildOrder(int keepItemIndex) {
    const int n = int(m_items.size());
    m_order.resize(n);
    for (int i = 0; i < n; ++i) m_order[i] = i;

    if (m_shuffle && n > 1) {
        //  Fisher-Yates von hinten - jede Anordnung ist gleich wahrscheinlich.
        for (int i = n - 1; i > 0; --i) {
            const int j = int(m_rng.below(uint32_t(i + 1)));
            m_order.swapItemsAt(i, j);
        }
    }

    if (keepItemIndex >= 0 && keepItemIndex < n) {
        if (m_shuffle) {
            //  Gemischt: die Runde BEGINNT beim laufenden Titel, der Rest
            //  folgt in zufälliger Reihenfolge.
            const int at = m_order.indexOf(keepItemIndex);
            if (at > 0) m_order.swapItemsAt(0, at);
            m_pos = 0;
        } else {
            //  Ungemischt: die Liste bleibt, wie sie ist - „weiter" heißt hier
            //  der NÄCHSTE der Liste, nicht der zweite einer neuen Ordnung.
            m_pos = keepItemIndex;
        }
    } else {
        m_pos = n > 0 ? 0 : -1;
    }
}

void PlayQueue::setItems(const QStringList& paths) {
    const QString playing = m_started ? currentPath() : QString();
    m_items = paths;
    const int keep = playing.isEmpty() ? -1 : int(m_items.indexOf(playing));
    if (keep < 0) m_started = false;          // der laufende Titel ist heraus
    rebuildOrder(keep);
    //  Stand der laufende Titel nicht mehr in der Liste, beginnt sie von vorn -
    //  aber sie SPIELT nicht von selbst weiter; das entscheidet die Engine.
    emit itemsChanged();
    emit currentChanged();
}

void PlayQueue::setShuffle(bool on) {
    if (m_shuffle == on) return;
    m_shuffle = on;
    //  Ein-/Ausschalten ordnet neu, ohne den laufenden Titel zu unterbrechen.
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

//  Den bisherigen Titel in die Historie legen (vor jedem Wechsel).
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
    //  will den nächsten (Festlegung des Nutzers).
    if (natural && m_repeat == Repeat::One)
        return currentPath();

    if (m_pos + 1 < m_order.size()) {
        noteHistory();
        ++m_pos;
        emit currentChanged();
        return currentPath();
    }

    //  Am Ende der Liste.
    if (m_repeat != Repeat::All) return {};        // hier ist Schluss
    noteHistory();
    if (m_shuffle) rebuildOrder(-1);               // neue Runde, neu gemischt
    m_pos = 0;
    emit currentChanged();
    return currentPath();
}

QString PlayQueue::back() {
    if (m_order.isEmpty() || m_pos < 0) return {};

    //  Erst die Historie: sie weiß, was wirklich lief - auch wenn der Zufall
    //  zwischendurch an- oder ausgeschaltet wurde und die Ordnung eine andere
    //  ist als beim Hören.
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
