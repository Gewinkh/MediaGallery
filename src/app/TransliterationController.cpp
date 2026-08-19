#include "app/TransliterationController.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QFile>
#include <QDir>
#include <QStandardPaths>

// ─────────────────────────────────────────────────────────────────────────────
//  Standard-Zuordnungen
// ─────────────────────────────────────────────────────────────────────────────
//  ARABISCH: Buckwalter-nahe Konsonanten (inkl. gebräuchlicher Digraphen wie
//  sh/kh/th) + Harakat als Kurzvokale - gemäß Wunsch: „a" = Fatha, „aa" = Alif,
//  „>aa" = Alif mit Hamza UND Fatha. Tanwīn über Großbuchstaben-N (aN/iN/uN),
//  damit „a"+„n" (Fatha + Nūn) eindeutig bleibt. Alles vom Nutzer editierbar.
//
//  JAPANISCH: Standard-Rōmaji -> Hiragana (inkl. Yōon, Varianten shi/si usw.,
//  x-Serie für kleine Kana, „nn" -> ん, „-" -> ー). Katakana wird aus derselben
//  Tabelle ABGELEITET (Codepoint-Versatz +0x60 für ぁ…ゖ; ー bleibt) - eine
//  Quelle, kein Duplikat. Sokuon (っ/ッ) ist eine Motor-Regel, kein Key.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Version der Standard-Zuordnungen. Bei JEDER Erweiterung/Änderung der Default-
// Maps (kArabic/kHiragana) hochzählen. load() gleicht eine ältere gespeicherte
// Datei dann EINMALIG mit den aktuellen Standards ab (fehlende Default-Keys werden
// ergänzt) und stempelt die neue Version zurück. So verdeckt eine alte
// transliteration.json nie neu hinzugekommene Keys wie „<"->ع.
//  v1 (implizit, kein Feld): vor Einführung dieser Versionierung.
//  v2: „ar" enthält jetzt „<"/„<a"/„<i"/„<u" (ع) sowie „gh" (statt früher „<g").
constexpr int kMapsVersion = 2;

struct Pair { const char* k; const char16_t* v; };

const Pair kArabic[] = {

    // =========================
    // Hamza-System
    // =========================
    { ">",    u"\u0621" },   // ء
    { ">a",   u"\u0623" },   // أ
    { ">i",   u"\u0625" },   // إ
    { ">u",   u"\u0623" },   // أ (Wortanfang -> أُ; symmetrisch zu >a/>i)
    { ">ia",  u"\u0625\u0650" }, // إِ (Alif mit Hamza unten + sichtbare Kasra)
    { ">aa",  u"\u0622" },   // آ
    { ">w",   u"\u0624" },   // ؤ
    { ">y",   u"\u0626" },   // ئ

    // =========================
    // Konsonanten
    // =========================

    { "b",  u"\u0628" },   // ب
    { "t",  u"\u062A" },   // ت
    { "th", u"\u062B" },   // ث
    { "j",  u"\u062C" },   // ج
    { "H",  u"\u062D" },   // ح
    { "kh", u"\u062E" },   // خ
    { "d",  u"\u062F" },   // د
    { "dh", u"\u0630" },   // ذ
    { "r",  u"\u0631" },   // ر
    { "z",  u"\u0632" },   // ز
    { "s",  u"\u0633" },   // س
    { "sh", u"\u0634" },   // ش
    { "s*",  u"\u0635" },   // ص
    { "d*",  u"\u0636" },   // ض
    { "t*",  u"\u0637" },   // ط
    { "z*",  u"\u0638" },   // ظ
    { "<",  u"\u0639" },   // ع
    { "<a", u"\u0639\u064E" },   // عَ
    { "<i", u"\u0639\u0650" },   // عِ
    { "<u", u"\u0639\u064F" },   // عُ
    { "gh", u"\u063A" },   // غ
    { "f",  u"\u0641" },   // ف
    { "q",  u"\u0642" },   // ق
    { "k",  u"\u0643" },   // ك
    { "l",  u"\u0644" },   // ل
    { "m",  u"\u0645" },   // م
    { "n",  u"\u0646" },   // ن
    { "h",  u"\u0647" },   // ه
    { "uu", u"\u0648" },   // و (lang)
    { "w", u"\u0648" },    // و (lang)
    { "y", u"\u064A" },   // ي (lang)
    { "\u00F6", u"\u0629" }, // ة  <- „ö" (die zwei Punkte spiegeln die Tā' marbūṭa)
    { "Y",  u"\u0649" },   // ى

    // =========================
    // Harakat
    // =========================
    { "a",  u"\u064E" },   // َ
    { "i",  u"\u0650" },   // ِ
    { "u",  u"\u064F" },   // ُ
    // Tanwīn über GROSSES N - so bleibt „min" (مِن, echtes Nūn) von „…uN"
    // (Tanwīn, KEIN Nūn-Buchstabe) eindeutig getrennt. Klein „an/in/un" ist
    // damit wieder Fatha/Kasra/Damma + Nūn.
    { "aN", u"\u064B" },   // ً
    { "iN", u"\u064D" },   // ٍ
    { "uN", u"\u064C" },   // ٌ
    { "o",  u"\u0652" },   // sukun ْ
    { "~",  u"\u0651" },   // shadda ّ (manuelle Variante A)

    // =========================
    // Langvokale (einheitlich)
    // =========================
    { "aa", u"\u0627" },   // ا  (langes ā; „uu"/„ii" stehen bereits oben)
    { "ii", u"\u064A" },   // ي
    // =========================
    // Ligaturen
    // =========================
    { "laa",  u"\u0644\u0627" }, // لا
    { "l>aa", u"\u0644\u0622" }, // لآ
    { "l>ia", u"\u0644\u0625" }, // لإ
};

const Pair kHiragana[] = {
    { "a", u"あ" }, { "i", u"い" }, { "u", u"う" }, { "e", u"え" }, { "o", u"お" },
    { "ka", u"か" }, { "ki", u"き" }, { "ku", u"く" }, { "ke", u"け" }, { "ko", u"こ" },
    { "ga", u"が" }, { "gi", u"ぎ" }, { "gu", u"ぐ" }, { "ge", u"げ" }, { "go", u"ご" },
    { "sa", u"さ" }, { "shi", u"し" }, { "si", u"し" }, { "su", u"す" }, { "se", u"せ" }, { "so", u"そ" },
    { "za", u"ざ" }, { "ji", u"じ" }, { "zi", u"じ" }, { "zu", u"ず" }, { "ze", u"ぜ" }, { "zo", u"ぞ" },
    { "ta", u"た" }, { "chi", u"ち" }, { "ti", u"ち" }, { "tsu", u"つ" }, { "tu", u"つ" },
    { "te", u"て" }, { "to", u"と" },
    { "da", u"だ" }, { "di", u"ぢ" }, { "du", u"づ" }, { "de", u"で" }, { "do", u"ど" },
    { "na", u"な" }, { "ni", u"に" }, { "nu", u"ぬ" }, { "ne", u"ね" }, { "no", u"の" },
    { "ha", u"は" }, { "hi", u"ひ" }, { "fu", u"ふ" }, { "hu", u"ふ" }, { "he", u"へ" }, { "ho", u"ほ" },
    { "ba", u"ば" }, { "bi", u"び" }, { "bu", u"ぶ" }, { "be", u"べ" }, { "bo", u"ぼ" },
    { "pa", u"ぱ" }, { "pi", u"ぴ" }, { "pu", u"ぷ" }, { "pe", u"ぺ" }, { "po", u"ぽ" },
    { "ma", u"ま" }, { "mi", u"み" }, { "mu", u"む" }, { "me", u"め" }, { "mo", u"も" },
    { "ya", u"や" }, { "yu", u"ゆ" }, { "yo", u"よ" },
    { "ra", u"ら" }, { "ri", u"り" }, { "ru", u"る" }, { "re", u"れ" }, { "ro", u"ろ" },
    { "wa", u"わ" }, { "wo", u"を" }, { "n", u"ん" }, { "nn", u"ん" },
    // Yōon
    { "kya", u"きゃ" }, { "kyu", u"きゅ" }, { "kyo", u"きょ" },
    { "gya", u"ぎゃ" }, { "gyu", u"ぎゅ" }, { "gyo", u"ぎょ" },
    { "sha", u"しゃ" }, { "shu", u"しゅ" }, { "sho", u"しょ" },
    { "sya", u"しゃ" }, { "syu", u"しゅ" }, { "syo", u"しょ" },
    { "ja", u"じゃ" }, { "ju", u"じゅ" }, { "jo", u"じょ" },
    { "jya", u"じゃ" }, { "jyu", u"じゅ" }, { "jyo", u"じょ" },
    { "zya", u"じゃ" }, { "zyu", u"じゅ" }, { "zyo", u"じょ" },
    { "cha", u"ちゃ" }, { "chu", u"ちゅ" }, { "cho", u"ちょ" },
    { "tya", u"ちゃ" }, { "tyu", u"ちゅ" }, { "tyo", u"ちょ" },
    { "nya", u"にゃ" }, { "nyu", u"にゅ" }, { "nyo", u"にょ" },
    { "hya", u"ひゃ" }, { "hyu", u"ひゅ" }, { "hyo", u"ひょ" },
    { "bya", u"びゃ" }, { "byu", u"びゅ" }, { "byo", u"びょ" },
    { "pya", u"ぴゃ" }, { "pyu", u"ぴゅ" }, { "pyo", u"ぴょ" },
    { "mya", u"みゃ" }, { "myu", u"みゅ" }, { "myo", u"みょ" },
    { "rya", u"りゃ" }, { "ryu", u"りゅ" }, { "ryo", u"りょ" },
    // Kleine Kana (x-Serie) + Chōonpu
    { "xa", u"ぁ" }, { "xi", u"ぃ" }, { "xu", u"ぅ" }, { "xe", u"ぇ" }, { "xo", u"ぉ" },
    { "xya", u"ゃ" }, { "xyu", u"ゅ" }, { "xyo", u"ょ" },
    { "xtsu", u"っ" }, { "xtu", u"っ" },
    { "-", u"ー" },
};

// Sonnenbuchstaben (الحُروف الشَّمْسِيَّة): nach dem Artikel „al-" bekommt DIESER
// Buchstabe eine Shadda und das Lām bleibt zeichenlos (الشَّمْس). Alle übrigen sind
// Mondbuchstaben (الحُروف القَمَرِيَّة) -> Sukun auf dem Lām (الْقَمَر).
bool isSunLetter(QChar c) {
    switch (c.unicode()) {
    case 0x062A: case 0x062B: case 0x062F: case 0x0630: // ت ث د ذ
    case 0x0631: case 0x0632: case 0x0633: case 0x0634: // ر ز س ش
    case 0x0635: case 0x0636: case 0x0637: case 0x0638: // ص ض ط ظ
    case 0x0644: case 0x0646:                           // ل ن
        return true;
    default:
        return false;
    }
}

// Buchstabe, auf dem eine Shadda sitzen darf (Verdopplungs-Merge B):
// arabischer Konsonant ب…ي - Alif (ا, Langvokal-Träger) ausgenommen.
bool isShaddaMergeable(QChar c) {
    const ushort u = c.unicode();
    return u >= 0x0628 && u <= 0x064A;   // ب … ي (0x0627 = ا liegt darunter)
}

// Gibt der Wert genau EINEN arabischen Konsonanten aus (Buchstabe, keine Harakat)?
bool isConsonantValue(const QString& v) {
    if (v.size() != 1) return false;
    const ushort u = v.at(0).unicode();
    return u >= 0x0621 && u <= 0x064A;   // ء … ي (Harakat beginnen ab 0x064B)
}

// Hiragana-Ausgabe -> Katakana (ぁ U+3041 … ゖ U+3096 -> +0x60; ー unverändert).
QString toKatakana(const QString& hira) {
    QString out;
    out.reserve(hira.size());
    for (const QChar c : hira) {
        const ushort u = c.unicode();
        out.append((u >= 0x3041 && u <= 0x3096) ? QChar(ushort(u + 0x60)) : c);
    }
    return out;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Aufbau / Persistenz
// ─────────────────────────────────────────────────────────────────────────────
TransliterationController::TransliterationController(QObject* parent)
    : QObject(parent) {
    // Standards vorbelegen; load() ersetzt einzelne Schemata aus der Datei.
    for (const QString& s : schemes()) {
        SchemeData sd;
        sd.map = defaultMap(s);
        rebuildDerived(sd);
        m_schemes.insert(s, sd);
    }
    load();
}

QHash<QString, QString> TransliterationController::defaultMap(const QString& scheme) {
    QHash<QString, QString> m;
    // Keys als UTF-8 (statt Latin-1) - nötig für Nicht-ASCII-Keys wie „ö";
    // ASCII bleibt unverändert, da UTF-8 dessen Obermenge ist.
    if (scheme == QLatin1String("ar")) {
        for (const Pair& p : kArabic)
            m.insert(QString::fromUtf8(p.k), QString::fromUtf16(p.v));
    } else if (scheme == QLatin1String("ja-hira")) {
        for (const Pair& p : kHiragana)
            m.insert(QString::fromUtf8(p.k), QString::fromUtf16(p.v));
    } else if (scheme == QLatin1String("ja-kata")) {
        for (const Pair& p : kHiragana)
            m.insert(QString::fromUtf8(p.k), toKatakana(QString::fromUtf16(p.v)));
    }
    return m;
}

void TransliterationController::rebuildDerived(SchemeData& sd) {
    sd.prefixes.clear();
    sd.alphabet.clear();
    sd.starts.clear();
    sd.maxKeyLen = 0;
    for (auto it = sd.map.cbegin(); it != sd.map.cend(); ++it) {
        const QString& k = it.key();
        sd.maxKeyLen = qMax(sd.maxKeyLen, int(k.size()));
        for (const QChar c : k)
            sd.alphabet.insert(c);
        if (!k.isEmpty())
            sd.starts.insert(k.at(0));
        for (int len = 1; len < k.size(); ++len)     // NUR echte Präfixe
            sd.prefixes.insert(k.left(len));
    }
    // Bindestrich gehört zum Lauf, obwohl er kein eigener Key ist: nur so bleibt
    // „al-shams" ein zusammenhängender Lauf für die Artikel-Erkennung. (Im
    // Japanisch-Schema ist „-" ohnehin Key -> hier redundant, aber unschädlich.)
    sd.alphabet.insert(QLatin1Char('-'));
}

QString TransliterationController::configFilePath() {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir + QStringLiteral("/transliteration.json");
}

void TransliterationController::load() {
    QFile f(configFilePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument jd = QJsonDocument::fromJson(f.readAll());
    if (!jd.isObject())
        return;
    const QJsonObject o = jd.object();
    m_enabled = o.value(QStringLiteral("enabled")).toBool(false);
    const QString sc = o.value(QStringLiteral("scheme")).toString();
    if (schemes().contains(sc))
        m_scheme = sc;
    // Versionsstand der Datei (fehlt bei v1-Dateien -> 0, also älter als kMapsVersion).
    const int fileVersion = o.value(QStringLiteral("mapsVersion")).toInt(0);
    const bool migrate    = (fileVersion < kMapsVersion);
    const QJsonObject maps = o.value(QStringLiteral("maps")).toObject();
    for (const QString& s : schemes()) {
        if (!maps.contains(s))
            continue;                                // Schema unverändert -> Standard
        QHash<QString, QString> m;
        const QJsonArray arr = maps.value(s).toArray();
        for (const QJsonValue& v : arr) {
            const QJsonObject e = v.toObject();
            const QString k = e.value(QStringLiteral("k")).toString();
            if (!k.isEmpty())
                m.insert(k, e.value(QStringLiteral("v")).toString());
        }
        // Migration (einmalig, solange fileVersion < kMapsVersion): neu hinzugekommene
        // Standard-Keys ergänzen, die in der alten Datei fehlen. Nutzer-Änderungen an
        // bestehenden Keys bleiben unangetastet (nur echte Lücken werden gefüllt) -
        // so erscheint „<"->ع wieder, ohne die persönliche Tabelle zu überschreiben.
        if (migrate) {
            const QHash<QString, QString> def = defaultMap(s);
            for (auto it = def.cbegin(); it != def.cend(); ++it)
                if (!m.contains(it.key()))
                    m.insert(it.key(), it.value());
        }
        SchemeData& sd = m_schemes[s];
        sd.map = m;
        rebuildDerived(sd);
    }
    // Migrierten Stand samt neuer Versionsnummer zurückschreiben -> der Abgleich
    // läuft genau einmal, nicht bei jedem Start.
    if (migrate)
        save();
}

void TransliterationController::save() const {
    QJsonObject maps;
    for (auto it = m_schemes.cbegin(); it != m_schemes.cend(); ++it) {
        QJsonArray arr;
        // Sortiert schreiben -> stabile, diff-freundliche Datei.
        QStringList keys = it.value().map.keys();
        keys.sort();
        for (const QString& k : keys) {
            QJsonObject e;
            e.insert(QStringLiteral("k"), k);
            e.insert(QStringLiteral("v"), it.value().map.value(k));
            arr.append(e);
        }
        maps.insert(it.key(), arr);
    }
    QJsonObject root;
    root.insert(QStringLiteral("enabled"),     m_enabled);
    root.insert(QStringLiteral("scheme"),      m_scheme);
    root.insert(QStringLiteral("mapsVersion"), kMapsVersion);
    root.insert(QStringLiteral("maps"),        maps);

    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    QSaveFile f(configFilePath());                   // atomar, wie Sidecar/Settings
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
        if (f.write(bytes) == bytes.size())
            f.commit();
        else
            f.cancelWriting();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Zustand
// ─────────────────────────────────────────────────────────────────────────────
void TransliterationController::setEnabled(bool on) {
    if (m_enabled == on)
        return;
    m_enabled = on;
    emit enabledChanged();
    save();
}

void TransliterationController::setScheme(const QString& s) {
    if (m_scheme == s || !schemes().contains(s))
        return;
    m_scheme = s;
    emit schemeChanged();
    save();
}

QStringList TransliterationController::schemes() const {
    return { QStringLiteral("ar"), QStringLiteral("ja-hira"), QStringLiteral("ja-kata") };
}

TransliterationController::SchemeData&
TransliterationController::data(const QString& scheme) {
    return m_schemes[scheme];
}

const TransliterationController::SchemeData*
TransliterationController::dataConst(const QString& scheme) const {
    const auto it = m_schemes.constFind(scheme);
    return it == m_schemes.cend() ? nullptr : &it.value();
}

void TransliterationController::bumpMappings() {
    ++m_mappingsRev;
    emit mappingsRevChanged();
    save();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Live-Motor
// ─────────────────────────────────────────────────────────────────────────────
QVariantMap TransliterationController::liveApply(const QString& text, int cursorPos) const {
    QVariantMap r;
    r.insert(QStringLiteral("changed"), false);
    const SchemeData* sd = dataConst(m_scheme);
    if (!m_enabled || !sd || sd->map.isEmpty()
        || cursorPos <= 0 || cursorPos > text.size())
        return r;

    // Lauf „mappbarer" Zeichen direkt vor dem Cursor (Deckel: Sicherheit +
    // konstante Kosten je Tastendruck; Zielschrift-Zeichen begrenzen natürlich).
    int start = cursorPos;
    const int minStart = qMax(0, cursorPos - 64);
    while (start > minStart && sd->alphabet.contains(text.at(start - 1)))
        --start;

    if (start == cursorPos) {
        // Direkt vor dem Cursor steht KEIN mappbares Zeichen - typischerweise ein
        // frisch getipptes Grenzzeichen (Leerzeichen/Zeilenumbruch/Satzzeichen).
        // Damit ist das vorige Wort abgeschlossen: den Lauf DAVOR „flushen", d. h.
        // noch wartende Präfix-Keys (z. B. schließendes „d" vor möglichem „dh",
        // oder „a"->Fatḥa vor möglichem „aa") jetzt festschreiben.
        const int bpos = cursorPos - 1;              // Position des Grenzzeichens
        if (bpos < 0 || sd->alphabet.contains(text.at(bpos)))
            return r;
        int rStart = bpos;
        const int rMin = qMax(0, bpos - 64);
        while (rStart > rMin && sd->alphabet.contains(text.at(rStart - 1)))
            --rStart;
        if (rStart == bpos)
            return r;                                // kein Lauf vor dem Grenzzeichen
        const bool ws = (rStart == 0) || text.at(rStart - 1).isSpace();
        const QString seg = text.mid(rStart, bpos - rStart);
        const QString out = convertRun(*sd, seg, ws, /*flush=*/true);
        if (out == seg)
            return r;                                // nichts festzuschreiben
        r.insert(QStringLiteral("changed"), true);
        r.insert(QStringLiteral("start"), rStart);
        r.insert(QStringLiteral("end"), bpos);
        r.insert(QStringLiteral("replacement"), out);
        // Cursor bleibt HINTER dem Grenzzeichen (die Zeichen ab bpos bleiben stehen).
        r.insert(QStringLiteral("cursor"),
                 rStart + int(out.size()) + (cursorPos - bpos));
        return r;
    }

    // Steht der Lauf am Wortanfang? (Nötig für die Artikel-Erkennung.) Wahr, wenn
    // er ganz vorn steht oder ein Leer-/Zeilenzeichen davor liegt. Bereits
    // umgesetzte arabische Zeichen zählen NICHT als Wortanfang (Wortmitte).
    const bool wordStart =
        (start == 0) || text.at(start - 1).isSpace();

    const QString seg = text.mid(start, cursorPos - start);
    QString out = convertRun(*sd, seg, wordStart, /*flush=*/false);

    // Verdopplungs-Merge (B, nur „ar"): Beginnt die Ausgabe mit demselben
    // Konsonanten, der UNMITTELBAR vor dem Lauf steht, verschmelzen beide zu einem
    // Buchstaben + Shadda („m" nach م -> مّ). Greift nicht bei bereits gesetzter
    // Shadda (kein Verdreifachen) und nicht bei Alif/Artikel-Ausgabe (beginnt mit ا).
    int repStart = start;
    if (m_scheme == QLatin1String("ar") && !out.isEmpty()
        && isShaddaMergeable(out.at(0)) && start > 0
        && text.at(start - 1) == out.at(0)
        && !(out.size() >= 2 && out.at(1) == QChar(0x0651))) {
        out = QString(text.at(start - 1)) + QChar(0x0651) + out.mid(1);  // <- Shadda
        repStart = start - 1;
    }

    if (out == seg && repStart == start)
        return r;

    r.insert(QStringLiteral("changed"), true);
    r.insert(QStringLiteral("start"), repStart);
    r.insert(QStringLiteral("end"), cursorPos);
    r.insert(QStringLiteral("replacement"), out);
    r.insert(QStringLiteral("cursor"), repStart + int(out.size()));
    return r;
}

int TransliterationController::longestConsonantKeyLen(const SchemeData& sd,
                                                     const QString& seg, int at) const {
    for (int len = qMin(sd.maxKeyLen, int(seg.size()) - at); len >= 1; --len) {
        const QString k = seg.mid(at, len);
        if (sd.map.contains(k) && isConsonantValue(sd.map.value(k)))
            return len;
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Kurzvokal am WORTANFANG -> Alif-Träger (+ ggf. Hamza) + sichtbarer Kurzvokal.
//  Ein bloßes Harakat (Fatḥa/Kasra/Ḍamma) hat am Wortanfang keinen Träger; ein
//  Alif springt ein. „>" verlangt zusätzlich eine Hamza (Hamzat al-qaṭʿ).
//   „a"->اَ  „i"->اِ  „u"->اُ   „>a"->أَ  „>i"->إِ  „>u"->أُ
//  (Langvokale „aa/ii/uu" und „>aa" tragen ihren Träger selbst -> hier NICHT
//   gelistet; sie laufen als normale Keys.)
// ─────────────────────────────────────────────────────────────────────────────
QString TransliterationController::wordInitialVowelCarrier(const QString& key) {
    static const QChar kAlef(0x0627), kAlefHamza(0x0623), kAlefHamzaU(0x0625);
    static const QChar kFatha(0x064E), kKasra(0x0650), kDamma(0x064F);
    if (key == QLatin1String("a"))  return QString(kAlef)      + kFatha;
    if (key == QLatin1String("i"))  return QString(kAlef)      + kKasra;
    if (key == QLatin1String("u"))  return QString(kAlef)      + kDamma;
    if (key == QLatin1String(">a")) return QString(kAlefHamza) + kFatha;
    if (key == QLatin1String(">i")) return QString(kAlefHamzaU)+ kKasra;
    if (key == QLatin1String(">u")) return QString(kAlefHamza) + kDamma;
    return QString();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Bestimmter Artikel „ال" am Wortanfang (nur Schema „ar").
//  Eingabeformen (alle -> identisches Ergebnis, bis auf die Hamza-Variante):
//    • literal    „al-<Kons>"       z. B. al-shams -> الشّ… , al-qamar -> الْق…
//    • assimiliert „a<Sonne>-<Sonne>" z. B. ash-shams / ar-rahman / an-nur
//    • mit „>" davor („>al-…"/„>as-s…") -> das Alif trägt eine Hamza: أل statt ال.
//  Regel: Sonnenbuchstabe -> Shadda auf ihm (Lām zeichenlos);
//         Mondbuchstabe   -> Sukun auf dem Lām.
//  Der Bindestrich ist reine Eingabesteuerung und erscheint nicht im Text.
//  flush=true (Wortende): unfertige Zwischenformen NICHT abwarten -> als „None"
//  zurückgeben (der Aufrufer verarbeitet den Lauf dann normal weiter).
// ─────────────────────────────────────────────────────────────────────────────
TransliterationController::ArticleMatch
TransliterationController::matchArticle(const SchemeData& sd, const QString& seg,
                                        bool flush) const {
    ArticleMatch r;
    const int n = seg.size();
    // Führendes „>" (optional) -> Hamza auf dem Artikel-Alif.
    int off = 0;
    QChar alef(0x0627);                              // ا
    if (n > 0 && seg.at(0) == QLatin1Char('>')) { off = 1; alef = QChar(0x0623); } // أ
    if (off >= n || seg.at(off) != QLatin1Char('a'))
        return r;                                    // None
    // „a" (bzw. „>a") allein - noch offen. Beim Flush kein Artikel.
    if (off + 1 >= n) { if (!flush) r.state = ArticleMatch::Wait; return r; }

    // C1: Konsonant direkt nach „a".
    const int aIdx = off;                            // Position des „a"
    const int c1len = longestConsonantKeyLen(sd, seg, aIdx + 1);
    const bool c1couldGrow = sd.prefixes.contains(seg.mid(aIdx + 1)); // Rest ist Präfix?
    if (c1len == 0) {                                // (noch) kein ganzer Konsonant
        if (c1couldGrow && !flush) { r.state = ArticleMatch::Wait; return r; }
        return r;                                    // None („aa", „ai" … kein Artikel)
    }
    if (aIdx + 1 + c1len == n && c1couldGrow && !flush) { // „as" könnte „ash" werden
        r.state = ArticleMatch::Wait; return r;
    }
    const QChar c1glyph = sd.map.value(seg.mid(aIdx + 1, c1len)).at(0);
    if (!isSunLetter(c1glyph))
        return r;                                    // None (nur Sonnen-/Lām-Vorsilbe)

    int p = aIdx + 1 + c1len;
    if (p >= n) { if (!flush) r.state = ArticleMatch::Wait; return r; } // „-" könnte folgen
    if (seg.at(p) != QLatin1Char('-'))
        return r;                                    // None (kein Bindestrich)
    ++p;                                             // „-" schlucken
    if (p >= n) { if (!flush) r.state = ArticleMatch::Wait; return r; } // Konsonant fehlt noch

    // C2: Konsonant nach dem Bindestrich.
    const int c2len = longestConsonantKeyLen(sd, seg, p);
    const bool c2couldGrow = sd.prefixes.contains(seg.mid(p));
    if (c2len == 0) {
        if (c2couldGrow && !flush) { r.state = ArticleMatch::Wait; return r; }
        return r;                                    // None
    }
    if (p + c2len == n && c2couldGrow && !flush) { r.state = ArticleMatch::Wait; return r; }

    const QChar c2glyph = sd.map.value(seg.mid(p, c2len)).at(0);

    static const QChar kLam(0x0644), kSukun(0x0652), kShadda(0x0651);
    QString out;
    out.reserve(4);
    out.append(alef);                                // ا bzw. أ (bei „>")
    out.append(kLam);
    // Literal „al-" + fremder Buchstabe: Lām ist real, Sonne->Shadda / Mond->Sukun.
    if (c1glyph == kLam && c2glyph != kLam) {
        if (isSunLetter(c2glyph)) { out.append(c2glyph); out.append(kShadda); }
        else                      { out.append(kSukun);  out.append(c2glyph); }
    } else {
        // Assimiliert („aš-š…") oder „al-l…": C1 wird geschluckt, C2 mit Shadda.
        // Nur gültig, wenn C1==C2 (gleiche Sonnen-Einheit); sonst kein Artikel.
        if (c1glyph != c2glyph)
            return ArticleMatch{};                   // None -> normal weiterverarbeiten
        out.append(c2glyph);
        out.append(kShadda);
    }
    r.state    = ArticleMatch::Emit;
    r.consumed = p + c2len;
    r.out      = out;
    return r;
}

QString TransliterationController::convertRun(const SchemeData& sd, const QString& seg,
                                             bool wordStart, bool flush) const {
    const bool ar   = (m_scheme == QLatin1String("ar"));
    const bool ja   = m_scheme.startsWith(QLatin1String("ja"));
    const bool kata = (m_scheme == QLatin1String("ja-kata"));
    QString out;
    out.reserve(seg.size());
    int i = 0;
    const int n = seg.size();

    // Wortanfang (nur „ar"): erst Artikel, sonst führender Kurzvokal -> Alif-Träger.
    if (ar && wordStart) {
        const ArticleMatch am = matchArticle(sd, seg, flush);
        if (am.state == ArticleMatch::Wait)
            return seg;                              // ganzer Lauf wartet unverändert
        if (am.state == ArticleMatch::Emit) {
            out += am.out;
            i = am.consumed;                         // Rest des Wortes läuft normal weiter
        } else {
            // Kein Artikel -> beginnt der Lauf mit einem Kurzvokal-Key (a/i/u bzw.
            // >a/>i/>u), fehlt ihm am Wortanfang der Träger -> Alif (+ ggf. Hamza)
            // voranstellen. Längeren Standard-Key (aa/>aa/ii/uu …) NICHT anfassen.
            int bestLen = 0;
            for (int len = qMin(sd.maxKeyLen, n); len >= 1; --len) {
                if (sd.map.contains(seg.left(len))) { bestLen = len; break; }
            }
            if (bestLen > 0) {
                const QString k = seg.left(bestLen);
                if (bestLen == n && sd.prefixes.contains(k) && !flush)
                    return seg;                      // könnte noch wachsen -> warten
                const QString carrier = wordInitialVowelCarrier(k);
                if (!carrier.isEmpty()) {
                    out += carrier;
                    i = bestLen;                     // Rest läuft normal weiter
                }
            }
        }
    }

    while (i < n) {
        // Längster exakter Key an Position i.
        int bestLen = 0;
        for (int len = qMin(sd.maxKeyLen, n - i); len >= 1; --len) {
            if (sd.map.contains(seg.mid(i, len))) { bestLen = len; break; }
        }
        if (bestLen > 0) {
            const QString k = seg.mid(i, bestLen);
            const bool reachesEnd = (i + bestLen == n);
            if (reachesEnd && sd.prefixes.contains(k) && !flush) {
                // Könnte noch länger werden („a" vor möglichem „aa") -> warten.
                // Beim Flush (Wortende) NICHT warten, sondern den Key festschreiben.
                out += seg.mid(i);
                i = n;
                continue;
            }
            // Verdopplung innerhalb des Laufs (B): derselbe Konsonant-Key folgt
            // direkt (auch Digraphe: „shsh" -> شّ). Langvokale aa/ii/uu sind KEINE
            // Konsonanten und bleiben unberührt.
            if (ar && isConsonantValue(sd.map.value(k))
                && i + 2 * bestLen <= n && seg.mid(i + bestLen, bestLen) == k) {
                out += sd.map.value(k);
                out += QChar(0x0651);                // Shadda
                i += 2 * bestLen;
                continue;
            }
            out += sd.map.value(k);
            i += bestLen;
            continue;
        }
        // Japanisch: Sokuon - verdoppelter Konsonant vor gültigem Key-Anfang
        // („kka" -> っか). „n" ausgenommen (Key „nn" -> ん übernimmt das).
        if (ja && i + 1 < n && seg.at(i) == seg.at(i + 1)
            && seg.at(i) != QLatin1Char('n') && sd.starts.contains(seg.at(i))) {
            out += kata ? QStringLiteral("\u30C3") : QStringLiteral("\u3063");
            ++i;
            continue;
        }
        // Rest ist Präfix eines Keys (z. B. „ky" vor „kya") -> warten (außer Flush).
        const QString tail = seg.mid(i);
        if (sd.prefixes.contains(tail) && !flush) {
            out += tail;
            i = n;
            continue;
        }
        // Kein Key, kein Präfix -> Zeichen unverändert übernehmen.
        out += seg.at(i);
        ++i;
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Zuordnungs-Verwaltung (Einstellungen)
// ─────────────────────────────────────────────────────────────────────────────
QVariantList TransliterationController::mappings(const QString& scheme) const {
    QVariantList list;
    const SchemeData* sd = dataConst(scheme);
    if (!sd)
        return list;
    QStringList keys = sd->map.keys();
    keys.sort();
    for (const QString& k : keys) {
        QVariantMap e;
        e.insert(QStringLiteral("key"),   k);
        e.insert(QStringLiteral("value"), sd->map.value(k));
        list.append(e);
    }
    return list;
}

bool TransliterationController::addMapping(const QString& scheme, const QString& key,
                                           const QString& value) {
    if (key.isEmpty() || value.isEmpty() || !m_schemes.contains(scheme))
        return false;
    SchemeData& sd = data(scheme);
    if (sd.map.contains(key))
        return false;                                // Duplikat: über update ändern
    sd.map.insert(key, value);
    rebuildDerived(sd);
    bumpMappings();
    return true;
}

bool TransliterationController::updateMapping(const QString& scheme, const QString& oldKey,
                                              const QString& key, const QString& value) {
    if (key.isEmpty() || value.isEmpty() || !m_schemes.contains(scheme))
        return false;
    SchemeData& sd = data(scheme);
    if (!sd.map.contains(oldKey))
        return false;
    if (key != oldKey && sd.map.contains(key))
        return false;                                // Kollision mit anderem Key
    sd.map.remove(oldKey);
    sd.map.insert(key, value);
    rebuildDerived(sd);
    bumpMappings();
    return true;
}

void TransliterationController::removeMapping(const QString& scheme, const QString& key) {
    if (!m_schemes.contains(scheme))
        return;
    SchemeData& sd = data(scheme);
    if (sd.map.remove(key) > 0) {
        rebuildDerived(sd);
        bumpMappings();
    }
}

void TransliterationController::resetScheme(const QString& scheme) {
    if (!m_schemes.contains(scheme))
        return;
    SchemeData& sd = data(scheme);
    sd.map = defaultMap(scheme);
    rebuildDerived(sd);
    bumpMappings();
}
