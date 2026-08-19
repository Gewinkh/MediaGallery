#include "audio/AudioRing.h"

#include <algorithm>
#include <cstring>

void AudioRing::resize(size_t samples) {
    QMutexLocker lk(&m_mutex);
    m_buf.assign(samples, 0.0f);
    m_head = m_tail = m_fill = 0;
}

void AudioRing::clear() {
    QMutexLocker lk(&m_mutex);
    m_head = m_tail = m_fill = 0;
}

size_t AudioRing::capacity() const { QMutexLocker lk(&m_mutex); return m_buf.size(); }
size_t AudioRing::available() const { QMutexLocker lk(&m_mutex); return m_fill; }
size_t AudioRing::space() const {
    QMutexLocker lk(&m_mutex);
    return m_buf.size() - m_fill;
}

size_t AudioRing::write(const float* src, size_t count) {
    if (!src || count == 0) return 0;
    QMutexLocker lk(&m_mutex);
    const size_t cap = m_buf.size();
    if (cap == 0) return 0;
    const size_t n = std::min(count, cap - m_fill);
    //  Höchstens zwei Stücke: bis zum Ende des Feldes und wieder von vorn.
    const size_t first = std::min(n, cap - m_head);
    std::memcpy(m_buf.data() + m_head, src, first * sizeof(float));
    if (n > first)
        std::memcpy(m_buf.data(), src + first, (n - first) * sizeof(float));
    m_head = (m_head + n) % cap;
    m_fill += n;
    return n;
}

size_t AudioRing::read(float* dst, size_t count) {
    if (!dst || count == 0) return 0;
    QMutexLocker lk(&m_mutex);
    const size_t cap = m_buf.size();
    if (cap == 0) return 0;
    const size_t n = std::min(count, m_fill);
    const size_t first = std::min(n, cap - m_tail);
    std::memcpy(dst, m_buf.data() + m_tail, first * sizeof(float));
    if (n > first)
        std::memcpy(dst + first, m_buf.data(), (n - first) * sizeof(float));
    m_tail = (m_tail + n) % cap;
    m_fill -= n;
    return n;
}
