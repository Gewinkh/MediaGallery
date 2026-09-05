#pragma once
// Live-Transliteration Latein -> Arabisch (mit Harakat) oder Japanisch. Umgesetzt wird
// nur der Lauf direkt vor dem Cursor, links nach rechts mit laengstem Treffer; ein
// Treffer, der Praefix eines laengeren Keys ist, wartet auf mehr Eingabe.

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
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
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

    // Wendet das aktive Schema auf den Lauf vor `cursorPos` an. QML ersetzt gezielt `text[start..end)` statt den
    // Text neu zu setzen - so bleiben Undo-Stack und Tempo großer Dateien intakt.
    Q_INVOKABLE QVariantMap liveApply(const QString& text, int cursorPos) const;

    // Dasselbe, aber DIREKT im Dokument und in EINEM Undo-Schritt: `remove()` + `insert()` aus QML erzeugt zwei je
    // Tastendruck. `QTextCursor::beginEditBlock` fasst beides zusammen - erreichbar nur über das QQuickTextDocument.
    Q_INVOKABLE QVariantMap applyInDocument(QQuickTextDocument* doc, int cursorPos) const;

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
    struct SchemeData {
        QHash<QString, QString> map;       // Key (Latein) -> Ausgabe (Zielschrift)
        QSet<QString>           prefixes;  // alle ECHTEN Präfixe aller Keys
        QSet<QChar>             alphabet;  // Zeichenvorrat der Keys (Lauf-Grenze)
        QSet<QChar>             starts;    // erste Zeichen aller Keys (Sokuon)
        int                     maxKeyLen = 0;
    };

    // Artikel-Erkennung am Wortanfang (nur "ar"): `None` = kein Artikel, `Wait` = Lauf ist Präfix eines möglichen
    // Artikels und bleibt stehen, `Emit` = `consumed` Zeichen durch `out` ersetzen.
    struct ArticleMatch { enum State { None, Wait, Emit } state = None;
                          int consumed = 0; QString out; };

    static QHash<QString, QString> defaultMap(const QString& scheme);
    void rebuildDerived(SchemeData& sd);
    SchemeData& data(const QString& scheme);
    const SchemeData* dataConst(const QString& scheme) const;
    // Längster Key ab „at", dessen Ausgabe ein einzelner arabischer Konsonant ist.
    int          longestConsonantKeyLen(const SchemeData& sd, const QString& seg,
                                        int at) const;
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
