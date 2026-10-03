#include "core/PdfVorschau.h"

#include <QDir>
#include <QFile>
#include <QTemporaryFile>

namespace mg {

QList<int> seitenFolge(int gesamt, int von, int bis, const QList<int>& liste) {
    QList<int> out;
    if (gesamt <= 0) return out;
    if (!liste.isEmpty()) {
        //  Die Reihenfolge der Auswahl ist die des PDFs - wie in der Seitenauswahl der Extraktion.
        for (int s : liste)
            if (s >= 1 && s <= gesamt && !out.contains(s)) out.append(s);
        if (!out.isEmpty()) return out;
    }
    const int a = qBound(1, von, gesamt);
    const int b = bis <= 0 ? gesamt : qBound(a, bis, gesamt);
    for (int s = a; s <= b; ++s) out.append(s);
    return out;
}

PdfVorschau::~PdfVorschau() { leere(); }

//  QTemporaryFile statt eines eigenen Namens: eindeutig, nur fuer den Nutzer lesbar, nicht vorhersagbar.
QString PdfVorschau::neu() {
    QTemporaryFile f(QDir::tempPath() + QStringLiteral("/mediagallery-vorschau-XXXXXX.pdf"));
    f.setAutoRemove(false);
    if (!f.open()) return {};
    const QString pfad = f.fileName();
    f.close();
    m_offen.append(pfad);
    return pfad;
}

void PdfVorschau::uebernehme(const QString& pfad) {
    m_offen.removeAll(pfad);
    if (!m_aktuell.isEmpty() && m_aktuell != pfad) QFile::remove(m_aktuell);
    m_aktuell = pfad;
}

void PdfVorschau::verwerfe(const QString& pfad) {
    m_offen.removeAll(pfad);
    if (pfad != m_aktuell) QFile::remove(pfad);
}

void PdfVorschau::leere() {
    for (const QString& p : std::as_const(m_offen)) QFile::remove(p);
    m_offen.clear();
    if (!m_aktuell.isEmpty()) QFile::remove(m_aktuell);
    m_aktuell.clear();
}

}  // namespace mg
