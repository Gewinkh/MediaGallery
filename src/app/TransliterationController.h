#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  TransliterationController.h - Live-Transliteration Latein -> Zielschrift.
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  QML-Singleton („Translit"): wandelt beim Tippen lateinische Eingaben live in
//  die Zielschrift um - Arabisch (mit Harakat) oder Japanisch (Hiragana bzw.
//  Katakana). Die Zuordnungen (z. B. „aa" -> ا, „a" -> ـَ, „>aa" -> أَ) sind je
//  Schema als editierbare Liste hinterlegt (Einstellungen) und werden als JSON
//  im App-Konfigurationsverzeichnis persistiert (transliteration.json, atomar
//  via QSaveFile) - zusammen mit enabled/scheme.
//
//  EINDEUTIGKEITS-REGEL („erst umsetzen, wenn zu 100 % klar")
//  ──────────────────────────────────────────────────────────
//  liveApply() betrachtet nur den zusammenhängenden Lauf „mappbarer" Zeichen
//  DIREKT VOR dem Cursor (Zeichen, die in irgendeinem Key vorkommen; bereits
//  umgesetzte Zielschrift-Zeichen begrenzen den Lauf natürlich) und wandelt
//  links->rechts mit LÄNGSTEM Treffer. Ein Treffer, der bis ans Lauf-Ende
//  reicht UND echter Präfix eines längeren Keys ist, bleibt UNVERÄNDERT
//  stehen (wartet auf mehr Eingabe); ebenso ein Rest, der Präfix irgendeines
//  Keys ist. Beispiel (Keys „a"->ـَ, „aa"->ا): „a" tippen -> wartet (könnte „aa"
//  werden); zweites „a" -> ا; stattdessen „b" -> ـَ + ب.
//
//  JAPANISCH-SONDERREGELN (nur Schemata „ja-*"; fest im Motor, da nicht über
//  einfache Key->Wert-Paare abbildbar):
//   • Sokuon: verdoppelter Konsonant vor gültigem Key-Anfang -> っ/ッ („kka").
//     „n" ist ausgenommen (dafür existiert der Key „nn" -> ん).
//
//  RAM: nur drei kleine QHash/QSet-Tabellen (wenige hundert Einträge); die
//  abgeleiteten Strukturen (Alphabet, Präfixmenge, max. Key-Länge) werden je
//  Schema EINMAL beim Laden/Ändern aufgebaut.
//
//  Registrierung: qmlRegisterSingletonInstance(…, "Translit", …) in main.cpp.
// ══════════════════════════════════════════════════════════════════════════════

#include <QObject>
#include <QQuickTextDocument>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <QHash>
#include <QSet>

class TransliterationController : public QObject {
    Q_OBJECT
    // Live-Umsetzung aktiv? (Toggle-Button oben rechts in den Editoren.)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // Aktives Schema: "ar" (Arabisch mit Harakat), "ja-hira", "ja-kata".
    Q_PROPERTY(QString scheme READ scheme WRITE setScheme NOTIFY schemeChanged)
    // Zähler: bumpt bei jeder Listen-Änderung -> die Settings-Liste liest
    // mappings() rev-getrieben neu (Muster wie selectionRev im PDF-Editor).
    Q_PROPERTY(int mappingsRev READ mappingsRev NOTIFY mappingsRevChanged)

public:
    explicit TransliterationController(QObject* parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    QString scheme() const { return m_scheme; }
    void setScheme(const QString& s);
    int mappingsRev() const { return m_mappingsRev; }

    // ── Live-Motor ────────────────────────────────────────────────────────────
    //  Wendet das aktive Schema auf den Lauf vor cursorPos an. Rückgabe:
    //  { changed, start, end, replacement, cursor } - QML ersetzt gezielt
    //  text[start..end) über TextEdit.remove()/insert() (kein Voll-Reset des
    //  Textes -> Undo-Stack und Performance großer Dateien bleiben intakt).
    Q_INVOKABLE QVariantMap liveApply(const QString& text, int cursorPos) const;

    //  Dasselbe, aber DIREKT im Dokument und in EINEM Undo-Schritt.
    //  Rueckgabe: { changed, cursor }.
    //  Warum es das braucht: der Weg ueber `remove()` + `insert()` aus QML
    //  erzeugt ZWEI Undo-Schritte je Tastendruck. Strg+Z lief dadurch durch
    //  halb umgesetzte Zwischenstaende („سَلam") statt einen Tastendruck
    //  zurueckzunehmen. `QTextCursor::beginEditBlock` fasst beides zu einem
    //  Schritt zusammen - erreichbar nur ueber das Dokument der TextArea, das
    //  QML als `QQuickTextDocument` herausgibt.
    Q_INVOKABLE QVariantMap applyInDocument(QQuickTextDocument* doc, int cursorPos) const;

    // ── Schemata & Zuordnungen (Einstellungen) ────────────────────────────────
    Q_INVOKABLE QStringList schemes() const;                  // feste IDs
    Q_INVOKABLE QVariantList mappings(const QString& scheme) const; // [{key,value}]
    Q_INVOKABLE bool addMapping(const QString& scheme, const QString& key,
                                const QString& value);
    Q_INVOKABLE bool updateMapping(const QString& scheme, const QString& oldKey,
                                   const QString& key, const QString& value);
    Q_INVOKABLE void removeMapping(const QString& scheme, const QString& key);
    Q_INVOKABLE void resetScheme(const QString& scheme);      // zurück auf Standard

signals:
    void enabledChanged();
    void schemeChanged();
    void mappingsRevChanged();

private:
    // Zuordnungstabelle EINES Schemas + abgeleitete Suchstrukturen.
    struct SchemeData {
        QHash<QString, QString> map;       // Key (Latein) -> Ausgabe (Zielschrift)
        QSet<QString>           prefixes;  // alle ECHTEN Präfixe aller Keys
        QSet<QChar>             alphabet;  // Zeichenvorrat der Keys (Lauf-Grenze)
        QSet<QChar>             starts;    // erste Zeichen aller Keys (Sokuon)
        int                     maxKeyLen = 0;
    };

    // Ergebnis der Artikel-Erkennung am Wortanfang (nur Schema „ar").
    //  None -> kein Artikel (normal weiterverarbeiten)
    //  Wait -> Lauf ist Präfix eines möglichen Artikels -> unverändert stehen lassen
    //  Emit -> Artikel erkannt: „consumed" Latein-Zeichen durch „out" ersetzen
    struct ArticleMatch { enum State { None, Wait, Emit } state = None;
                          int consumed = 0; QString out; };

    static QHash<QString, QString> defaultMap(const QString& scheme);
    void rebuildDerived(SchemeData& sd);
    SchemeData& data(const QString& scheme);
    const SchemeData* dataConst(const QString& scheme) const;
    // Längster Key ab „at", dessen Ausgabe ein einzelner arabischer Konsonant ist.
    int          longestConsonantKeyLen(const SchemeData& sd, const QString& seg,
                                        int at) const;
    // Artikel „al-…"/assimiliert „aš-…" am Wortanfang erkennen (Sonnen-/Mondregel).
    //  Optionales führendes „>" -> das Artikel-Alif trägt eine Hamza (أل statt ال).
    //  flush=true (Wortende erreicht): unfertige Formen NICHT mehr abwarten, sondern
    //  als „kein Artikel" behandeln.
    ArticleMatch matchArticle(const SchemeData& sd, const QString& seg, bool flush) const;
    QString convertRun(const SchemeData& sd, const QString& seg, bool wordStart,
                       bool flush) const;
    // Kurzvokal-Key am WORTANFANG ohne Trägerbuchstaben -> Alif-Träger (+ ggf.
    // Hamza) + sichtbarer Kurzvokal. Leerer String = kein solcher Key.
    //  „a"->اَ  „i"->اِ  „u"->اُ   „>a"->أَ  „>i"->إِ  „>u"->أُ
    static QString wordInitialVowelCarrier(const QString& key);
    void bumpMappings();
    void load();
    void save() const;
    static QString configFilePath();

    bool    m_enabled = false;
    QString m_scheme  = QStringLiteral("ar");
    int     m_mappingsRev = 0;
    QHash<QString, SchemeData> m_schemes;   // "ar" / "ja-hira" / "ja-kata"
};
