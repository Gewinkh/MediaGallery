#include "core/SpellChecker.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QStringEncoder>

#ifdef MG_HAVE_HUNSPELL
#include <hunspell/hunspell.hxx>
#endif

namespace mg {

struct SpellChecker::Impl {
#ifdef MG_HAVE_HUNSPELL
    std::unique_ptr<Hunspell> hs;
    //  Hunspell rechnet in der Kodierung des Wörterbuchs, nicht in UTF-16.
    QStringEncoder enc;
    QStringDecoder dec;
    bool encodingOk = false;
#endif
    QSet<QString> ignored;
    //  Vorschlaege je Wort. Hunspell erzeugt und bewertet dafuer viele
    //  Kandidaten - GEMESSEN 6,4 ms fuer EIN Wort, gegen 0,0002 ms fuer
    //  `isCorrect`. Der Aufruf sitzt im GUI-Faden (Kontextmenue), und dasselbe
    //  falsch geschriebene Wort steht in einem Text meist mehrfach.
    //  Klein gehalten: ein Kontextmenue fragt EIN Wort, mehr als eine Handvoll
    //  verschiedene sieht ein Nutzer in einer Sitzung selten hintereinander.
    mutable QHash<QString, QStringList> sugCache;
    static constexpr int kSugCacheMax = 64;
};

SpellChecker::SpellChecker() : d(std::make_unique<Impl>()) {}
SpellChecker::~SpellChecker() = default;

bool SpellChecker::compiledIn() {
#ifdef MG_HAVE_HUNSPELL
    return true;
#else
    return false;
#endif
}

QStringList SpellChecker::searchPaths(const QString& extraDir) {
    QStringList out;
    if (!extraDir.isEmpty()) out << extraDir;
    //  Die üblichen Orte der drei Zielsysteme; nicht vorhandene schaden nicht.
    out << QStringLiteral("/usr/share/hunspell")
        << QStringLiteral("/usr/share/myspell/dicts")
        << QStringLiteral("/usr/local/share/hunspell")
        << QStringLiteral("/Library/Spelling");
    //  Eigenes Verzeichnis neben der Konfiguration - dorthin darf der Nutzer
    //  ein Wörterbuch legen, ohne Systemrechte zu brauchen.
    const QString cfg = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!cfg.isEmpty()) out << cfg + QStringLiteral("/dictionaries");
    return out;
}

QStringList SpellChecker::availableLanguages(const QString& extraDir) {
    QSet<QString> seen;
    const QStringList paths = searchPaths(extraDir);
    for (const QString& p : paths) {
        QDir dir(p);
        if (!dir.exists()) continue;
        const QFileInfoList affs =
            dir.entryInfoList({ QStringLiteral("*.aff") }, QDir::Files | QDir::Readable,
                              QDir::Name);
        for (const QFileInfo& fi : affs) {
            //  Nur mit passender `.dic` ist es ein brauchbares Wörterbuch.
            if (!QFileInfo::exists(fi.absolutePath() + QLatin1Char('/')
                                   + fi.completeBaseName() + QStringLiteral(".dic")))
                continue;
            seen.insert(fi.completeBaseName());
        }
    }
    QStringList out(seen.cbegin(), seen.cend());
    out.sort();
    return out;
}

bool SpellChecker::open(const QString& language, const QString& extraDir) {
    m_lang.clear();
    //  Anderes Woerterbuch = andere Vorschlaege.
    d->sugCache.clear();
#ifdef MG_HAVE_HUNSPELL
    d->hs.reset();
    d->encodingOk = false;
    if (language.isEmpty()) return false;

    //  Voller Pfad ODER Kürzel: beides ist erlaubt, damit ein Testtreiber sein
    //  eigenes Wörterbuch mitbringen kann, ohne es zu installieren.
    QString base;
    if (QFileInfo::exists(language + QStringLiteral(".aff"))
        && QFileInfo::exists(language + QStringLiteral(".dic"))) {
        base = language;
    } else {
        const QStringList paths = searchPaths(extraDir);
        for (const QString& p : paths) {
            const QString cand = p + QLatin1Char('/') + language;
            if (QFileInfo::exists(cand + QStringLiteral(".aff"))
                && QFileInfo::exists(cand + QStringLiteral(".dic"))) {
                base = cand;
                break;
            }
        }
    }
    if (base.isEmpty()) return false;

    const QByteArray aff = (base + QStringLiteral(".aff")).toLocal8Bit();
    const QByteArray dic = (base + QStringLiteral(".dic")).toLocal8Bit();
    d->hs = std::make_unique<Hunspell>(aff.constData(), dic.constData());
    const QByteArray encName(d->hs->get_dic_encoding());
    //  Unbekannte Kodierung -> lieber gar nicht prüfen als falsch anstreichen.
    auto encoding = QStringConverter::encodingForName(encName.constData());
    if (!encoding) {
        d->hs.reset();
        return false;
    }
    d->enc = QStringEncoder(*encoding);
    d->dec = QStringDecoder(*encoding);
    d->encodingOk = true;
    m_lang = QFileInfo(base).completeBaseName();
    return true;
#else
    Q_UNUSED(language) Q_UNUSED(extraDir)
    return false;
#endif
}

bool SpellChecker::available() const {
#ifdef MG_HAVE_HUNSPELL
    return d->hs != nullptr && d->encodingOk;
#else
    return false;
#endif
}

void SpellChecker::ignoreWord(const QString& word) {
    //  Ein ignoriertes Wort wird nicht mehr beanstandet - alte Vorschlaege
    //  dazu duerfen nicht stehen bleiben.
    d->sugCache.remove(word);
    if (!word.isEmpty()) d->ignored.insert(word);
}

bool SpellChecker::isCorrect(const QString& word) const {
    if (word.isEmpty()) return true;
    if (d->ignored.contains(word)) return true;
#ifdef MG_HAVE_HUNSPELL
    if (!available()) return true;         // ohne Wissen wird nichts angestrichen
    QStringEncoder& e = const_cast<QStringEncoder&>(d->enc);
    const QByteArray enc = e.encode(word);
    if (e.hasError()) { e.resetState(); return true; }   // nicht darstellbar -> durchlassen
    return d->hs->spell(std::string(enc.constData(), size_t(enc.size())));
#else
    return true;
#endif
}

QStringList SpellChecker::suggest(const QString& word) const {
#ifdef MG_HAVE_HUNSPELL
    if (!available() || word.isEmpty()) return {};
    const auto cached = d->sugCache.constFind(word);
    if (cached != d->sugCache.cend()) return cached.value();
    QStringEncoder& e = const_cast<QStringEncoder&>(d->enc);
    QStringDecoder& dd = const_cast<QStringDecoder&>(d->dec);
    const QByteArray enc = e.encode(word);
    if (e.hasError()) { e.resetState(); return {}; }
    const std::vector<std::string> sug =
        d->hs->suggest(std::string(enc.constData(), size_t(enc.size())));
    QStringList out;
    out.reserve(int(sug.size()));
    for (const std::string& s : sug) {
        const QString t = dd.decode(QByteArray::fromRawData(s.data(), qsizetype(s.size())));
        if (dd.hasError()) { dd.resetState(); continue; }
        if (!t.isEmpty()) out << t;
        if (out.size() >= 8) break;        // mehr passt in kein Kontextmenü
    }
    //  Voll? Dann von vorn - eine Verdraengung nach Alter waere hier teurer
    //  als der Treffer wert ist, und die Liste ist ohnehin klein.
    if (d->sugCache.size() >= Impl::kSugCacheMax) d->sugCache.clear();
    d->sugCache.insert(word, out);
    return out;
#else
    Q_UNUSED(word)
    return {};
#endif
}

std::vector<SpellRange> SpellChecker::checkText(const QString& text) const {
    std::vector<SpellRange> out;
    if (!available() || text.isEmpty()) return out;

    const int n = int(text.size());
    int i = 0;
    while (i < n) {
        //  Wortanfang suchen: nur Buchstaben beginnen ein Wort.
        if (!text.at(i).isLetter()) { ++i; continue; }
        int start = i;
        int end = i;
        bool hasDigit = false;
        while (end < n) {
            const QChar c = text.at(end);
            if (c.isLetter()) { ++end; continue; }
            if (c.isDigit())  { hasDigit = true; ++end; continue; }
            //  Apostroph/Bindestrich zählen nur MITTEN im Wort.
            if ((c == QLatin1Char('\'') || c == QChar(0x2019) || c == QLatin1Char('-'))
                && end + 1 < n && text.at(end + 1).isLetter()) {
                ++end;
                continue;
            }
            break;
        }
        const QString word = text.mid(start, end - start);
        i = end;
        //  Übersprungen wird, was keine Rechtschreibung hat: Wörter mit Ziffern
        //  (B2B, MP3) und reine Großschreibung (Abkürzungen wie DOCX) - sonst
        //  wäre die Anzeige voller roter Linien, die niemand abstellen kann.
        if (hasDigit || word.size() < 2) continue;
        //  Die Abkürzungs-Regel gilt NUR für Schriften, die überhaupt zwei
        //  Fälle kennen. In Arabisch, Hebräisch, Chinesisch oder Thai ist jedes
        //  Wort gleich seiner Großform - die Regel hätte dort ALLES übersprungen
        //  und die Prüfung damit still abgeschaltet, obwohl das Wörterbuch da
        //  ist und der Fehler erkannt wird.
        const bool hasCase = (word != word.toLower());
        if (hasCase && word == word.toUpper()) continue;
        if (isCorrect(word)) continue;
        out.push_back({ start, end - start });
    }
    return out;
}

}   // namespace mg
