#include "editor/TextProblems.h"

#include <QTextBlock>
#include <QTextDocument>
#include <QTextCursor>
#include <QVariantMap>

namespace mg::editor {
namespace {

//  Lang genug, dass beim Tippen kein Durchgang laeuft, kurz genug, dass die
//  Marke nach einer Denkpause dasteht. Dieselbe Groesse wie beim Faltungsbalken.
constexpr int kSammelMs = 400;

}  // namespace

TextProblems::TextProblems(QObject* parent) : QObject(parent) {
    m_timer.setSingleShot(true);
    m_timer.setInterval(kSammelMs);
    connect(&m_timer, &QTimer::timeout, this, &TextProblems::neuErfassen);
}

QTextDocument* TextProblems::doc() const {
    return m_quickDoc ? m_quickDoc->textDocument() : nullptr;
}

void TextProblems::setDocument(QQuickTextDocument* d) {
    if (d == m_quickDoc) return;
    if (QTextDocument* alt = doc()) disconnect(alt, nullptr, this, nullptr);
    m_quickDoc = d;
    if (QTextDocument* neu = doc())
        connect(neu, &QTextDocument::contentsChange, this,
                [this](int pos, int removed, int added) {
                    //  Was geloescht wurde, weiss hier niemand mehr - also
                    //  immer erfassen. Eingefuegtes wird gepruaft.
                    if (removed > 0) { anstossen(); return; }
                    if (added <= 0) return;
                    QTextDocument* d = doc();
                    if (!d) return;
                    QTextCursor c(d);
                    c.setPosition(pos);
                    c.setPosition(qMin(pos + added, d->characterCount() - 1),
                                  QTextCursor::KeepAnchor);
                    if (touchesProblems(c.selectedText())) anstossen();
                });
    emit documentChanged();
    neuErfassen();
}

void TextProblems::setPath(const QString& p) {
    if (p == m_path) return;
    m_path = p;
    emit pathChanged();
    neuErfassen();
}

void TextProblems::setEnabled(bool v) {
    if (v == m_an) return;
    m_an = v;
    emit enabledChanged();
    neuErfassen();
}

void TextProblems::anstossen() {
    if (m_an) m_timer.start();
}

void TextProblems::neuErfassen() {
    m_timer.stop();
    QList<Problem> neu;
    if (m_an && doc()) neu = scanProblems(doc(), languageForPath(m_path), m_path);
    if (neu == m_probleme) return;

    m_probleme = std::move(neu);
    m_jeBlock.clear();
    for (const Problem& p : std::as_const(m_probleme)) m_jeBlock[p.block].append(p);
    emit problemsChanged();
}

const QList<Problem>* TextProblems::inBlock(int block) const {
    const auto it = m_jeBlock.constFind(block);
    return it == m_jeBlock.cend() ? nullptr : &*it;
}

QVariantList TextProblems::list() const {
    QVariantList aus;
    aus.reserve(m_probleme.size());
    for (const Problem& p : m_probleme) {
        QVariantMap m;
        m.insert(QStringLiteral("block"), p.block);
        m.insert(QStringLiteral("start"), p.start);
        m.insert(QStringLiteral("length"), p.length);
        m.insert(QStringLiteral("kind"), int(p.kind));
        m.insert(QStringLiteral("detail"), p.detail);
        aus.append(m);
    }
    return aus;
}

int TextProblems::firstBlock() const {
    return m_probleme.isEmpty() ? -1 : m_probleme.first().block;
}

}  // namespace mg::editor
