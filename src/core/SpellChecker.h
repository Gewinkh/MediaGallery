#pragma once
// Rechtschreib-PRÜFUNG, nicht Autokorrektur: der Text wird nie von selbst verändert. Hunspell ist OPTIONAL wie
// ZLIB - fehlt Bibliothek oder Wörterbuch, meldet `available()` false und jedes Wort gilt als richtig.
// EINE Instanz gehört EINEM Thread; sie entsteht im Worker und stirbt dort.

#include <QString>
#include <QStringList>
#include <memory>

namespace mg {

//  Ein falsch geschriebenes Wort: Anfang und Länge IM ABSATZTEXT.
struct SpellRange {
    int start = 0;
    int length = 0;
};

class SpellChecker {
public:
    SpellChecker();
    ~SpellChecker();
    SpellChecker(const SpellChecker&) = delete;
    SpellChecker& operator=(const SpellChecker&) = delete;

    //  Ist die Bibliothek überhaupt einkompiliert? (Baut jemand ohne Hunspell,
    //  ist das hier false und alles Weitere ein No-op.)
    static bool compiledIn();
    //  Welche Wörterbücher liegen auf diesem Rechner? (Sprachkürzel wie
    //  „de_DE", sortiert.) Gesucht wird in den üblichen Verzeichnissen und in
    //  `extraDir`, falls gesetzt.
    static QStringList availableLanguages(const QString& extraDir = QString());
    //  Verzeichnisse, in denen gesucht wird - für Fehlermeldungen und Tests.
    static QStringList searchPaths(const QString& extraDir = QString());

    //  Wörterbuch laden. `language` ist ein Kürzel („de_DE") ODER der volle
    //  Pfad einer `.aff`/`.dic` OHNE Endung. false = nicht gefunden/unlesbar.
    bool open(const QString& language, const QString& extraDir = QString());
    bool available() const;
    QString language() const { return m_lang; }

    //  Ein einzelnes Wort. Unbekannt = false. Ohne Wörterbuch IMMER true -
    //  ohne Wissen wird nichts angestrichen.
    bool isCorrect(const QString& word) const;
    //  Verbesserungsvorschläge (leer, wenn keine oder kein Wörterbuch).
    QStringList suggest(const QString& word) const;
    //  Ein Wort für diese Sitzung als richtig annehmen („Ignorieren").
    void ignoreWord(const QString& word);

    // Die Wortzerlegung gehört hierher, damit Anzeige und Kontextmenü dieselbe haben: Buchstaben und Ziffern zählen
    // zum Wort, Apostroph und Bindestrich nur INNERHALB. Wörter mit Ziffern und reine Großschreibung entfallen.
    std::vector<SpellRange> checkText(const QString& text) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    QString m_lang;
};

}   // namespace mg
