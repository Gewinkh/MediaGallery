#include "html/HtmlView.h"

#include "core/PathUtils.h"
#include "core/Strings.h"
#include "editor/TextPdfExporter.h"
#include "html/HtmlPaint.h"
#include "html/HtmlParser.h"

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPointer>
#include <QPainter>
#include <QQuickWindow>
#include <QRunnable>
#include <QSGClipNode>
#include <QSGRectangleNode>
#include <QSGSimpleTextureNode>
#include <QStyleHints>
#include <QUrl>

#include <cmath>

namespace mg::html {

struct Seite {
    Layout l;
    Anzeigeliste a;
    Ordnung o;
    std::vector<uint32_t> kastenVon;    // je Dokumentknoten sein Kasten (Verankerung ueber neues Legen)
    std::vector<std::pair<uint32_t, float>> rollRand;    // Elemente mit scroll-margin-top, aufsteigend
    float breite = 0;
    mutable std::vector<QImage> kacheln;    // gerasterte Musterkacheln; nur der Malfaden (m_pool hat einen Faden)
};

namespace {

constexpr qint64 kMaxBytes = 64 << 20;
constexpr size_t kMaxTreffer = 100000;
//  Ein Treffer ueber mehrere Zeilen beginnt oberhalb des Fensters; so weit wird nach oben mitgeschaut (Dokument-px).
constexpr float kTrefferSpanne = 400;

//  Verankerung wie CSS scroll anchoring, aber je Zeile: das innerste Stueck am Zeilenanfang, das einen eigenen
//  Dokumentknoten hat (Text vor seinem Inline-Kasten; Text von ::before hat keinen). `oben`: Zeile ueber dem Fenster.
struct Anker {
    uint32_t knoten = kKein;
    int32_t versatz = 0;
    float oben = 0;
};

Anker ankerVon(const Seite& s, float y0, float y1) {
    const Stelle z = obersteZeile(s.l, s.o, y0, y1);
    if (!z.gueltig()) return {};
    //  Liegt der Zeilenanfang in keinem Stueck (Umbruchstelle vor einem Atom), gilt das naechste dahinter.
    const Stueck* beste = nullptr;
    uint32_t knoten = kKein;
    const auto rang = [&](const Stueck& t) { return std::pair(std::max(t.anfang, z.zeichen), t.laenge); };
    for (const Stueck& t : s.l.absaetze[z.absatz].stuecke) {
        if (z.zeichen >= t.anfang + std::max(t.laenge, 1) || (beste && rang(t) >= rang(*beste))) continue;
        const auto it = std::find(s.kastenVon.begin(), s.kastenVon.end(), t.kasten);
        if (it == s.kastenVon.end()) continue;
        beste = &t;
        knoten = uint32_t(it - s.kastenVon.begin());
    }
    if (!beste) return {};
    const int32_t versatz = std::max(0, z.zeichen - beste->anfang);
    return Anker{ knoten, versatz, zeilenOben(s.l, s.o, Stelle{ z.absatz, beste->anfang + versatz }) - y0 };
}

//  Oberkante der Zeile des Ankers im neuen Layout; NaN, wenn der Knoten keinen Kasten mehr hat oder ungemalt ist.
//  Innerstes Element unter dem Punkt: Eltern stehen vor ihren Kindern, also das letzte mit einem Kasten darunter.
//  `unter`: nur dieses Element und seine Nachfahren; `ohne`: deren Nachfahren nicht (verschobene Schichten).
uint32_t elementBei(const Seite& s, const Dokument& d, float x, float y, uint32_t unter = kKein,
                    const std::vector<uint32_t>& ohne = {}) {
    const auto liegtIn = [&](uint32_t n, uint32_t wurzel) {
        for (; n != kKein; n = d.knoten[n].eltern)
            if (n == wurzel) return true;
        return false;
    };
    const auto drin = [&](const Rechteck& r) { return x >= r.x && x < r.x + r.b && y >= r.y && y < r.y + r.h; };
    //  Inline-Kaesten zaehlen mit ihren Stuecken je Zeile: der Randkasten eines umbrochenen umfasst beide Zeilen ganz.
    std::vector<uint8_t> zeilig(s.l.lage.size(), 0);    // 1 hat Stuecke, 3 eines unter dem Punkt
    for (const Fragment& f : s.l.fragmente)
        if (f.kasten < zeilig.size()) zeilig[f.kasten] |= drin(f.r) ? 3 : 1;
    uint32_t aus = kKein;
    for (uint32_t n = 0; n < d.knoten.size() && n < s.kastenVon.size(); ++n) {
        const uint32_t k = s.kastenVon[n];
        if (k == kKein || d.knoten[n].art != Art::Element || k >= s.l.lage.size()) continue;
        if (!(zeilig[k] ? zeilig[k] == 3 : drin(s.l.lage[k]))) continue;
        if (unter != kKein && !liegtIn(n, unter)) continue;
        if (std::any_of(ohne.begin(), ohne.end(), [&](uint32_t w) { return liegtIn(n, w); })) continue;
        aus = n;
    }
    return aus;
}

//  Eine Stelle als Dokumentknoten und Versatz in seinem Text: haelt ueber neues Legen, auch wenn Absaetze dazukommen.
struct Griff {
    uint32_t knoten = kKein;
    int32_t versatz = 0;
};

Griff griffVon(const Seite& s, Stelle st) {
    if (!st.gueltig() || st.absatz >= s.l.absaetze.size()) return {};
    const auto& stuecke = s.l.absaetze[st.absatz].stuecke;
    const auto griff = [&](const Stueck& t, int32_t zeichen) -> Griff {
        const auto it = std::find(s.kastenVon.begin(), s.kastenVon.end(), t.kasten);
        if (it == s.kastenVon.end()) return {};
        //  Ein Kasten kann in mehreren Stuecken des Absatzes stehen: der Versatz zaehlt ueber alle davor.
        int32_t davor = 0;
        for (const Stueck& u : stuecke) {
            if (&u == &t) break;
            if (u.kasten == t.kasten) davor += u.laenge;
        }
        return Griff{ uint32_t(it - s.kastenVon.begin()), davor + std::clamp(zeichen - t.anfang, 0, t.laenge) };
    };
    for (const Stueck& t : stuecke)
        if (st.zeichen >= t.anfang && st.zeichen <= t.anfang + t.laenge)
            if (const Griff g = griff(t, st.zeichen); g.knoten != kKein) return g;
    //  Ohne eigenen Knoten (Listen- und details-Marke, ::before): das naechste Stueck dahinter, sonst das Ende davor.
    for (const Stueck& t : stuecke)
        if (t.anfang >= st.zeichen)
            if (const Griff g = griff(t, t.anfang); g.knoten != kKein) return g;
    for (auto t = stuecke.rbegin(); t != stuecke.rend(); ++t)
        if (const Griff g = griff(*t, t->anfang + t->laenge); g.knoten != kKein) return g;
    return {};
}

Stelle stelleVon(const Seite& s, Griff h) {
    if (h.knoten >= s.kastenVon.size() || s.kastenVon[h.knoten] == kKein) return {};
    const uint32_t k = s.kastenVon[h.knoten];
    for (uint32_t ai = 0; ai < s.l.absaetze.size(); ++ai) {
        int32_t rest = h.versatz;
        for (const Stueck& t : s.l.absaetze[ai].stuecke) {
            if (t.kasten != k) continue;
            if (rest <= t.laenge) return Stelle{ ai, t.anfang + rest };
            rest -= t.laenge;
        }
    }
    return {};
}

float ankerY(const Seite& s, const Anker& a) {
    if (a.knoten >= s.kastenVon.size() || s.kastenVon[a.knoten] == kKein) return NAN;
    const uint32_t k = s.kastenVon[a.knoten];
    for (uint32_t ai = 0; ai < s.l.absaetze.size(); ++ai)
        for (const Stueck& t : s.l.absaetze[ai].stuecke)
            if (t.kasten == k)
                return zeilenOben(s.l, s.o, Stelle{ ai, t.anfang + std::clamp(a.versatz, 0, std::max(0, t.laenge - 1)) });
    return NAN;
}

class LegeAufgabe : public QRunnable {
public:
    using Zurueck = std::function<void(std::shared_ptr<const Dokument>, std::shared_ptr<const Seite>, bool)>;
    LegeAufgabe(QObject* owner, QString pfad, std::shared_ptr<const Dokument> dok, std::vector<uint8_t> zustand,
                float breite, float hoehe, float faktor, std::shared_ptr<std::atomic<bool>> abbruch, Zurueck zurueck)
        : m_owner(owner), m_pfad(std::move(pfad)), m_dok(std::move(dok)), m_zustand(std::move(zustand)), m_breite(breite),
          m_hoehe(hoehe), m_faktor(faktor), m_abbruch(std::move(abbruch)), m_zurueck(std::move(zurueck)) {
        setAutoDelete(true);
    }

    void run() override {
        bool ok = true;
        if (!m_dok) {
            QFile f(m_pfad);
            ok = f.open(QIODevice::ReadOnly);
            const QByteArray roh = ok ? f.read(kMaxBytes) : QByteArray();
            Dokument d = zerlege(std::string_view(roh.constData(), size_t(roh.size())));
            d.basis = QFileInfo(m_pfad).absolutePath().toStdString();
            m_dok = std::make_shared<const Dokument>(std::move(d));
        }
        if (m_abbruch->load()) return;
        auto seite = std::make_shared<Seite>();
        SchriftWahl wahl;
        const Medium m{ int(std::lround(m_breite)), int(std::lround(m_hoehe)), false, m_faktor, &wahl };
        const Stilblatt b = stilblattVon(*m_dok);
        const Stile s = berechne(*m_dok, b, m, m_zustand);
        if (m_abbruch->load()) return;
        for (uint32_t n = 0; n < m_dok->knoten.size() && n < s.jeKnoten.size(); ++n)
            if (const int32_t r = s.stile[s.jeKnoten[n]].rollRand; r && m_dok->knoten[n].art == Art::Element)
                seite->rollRand.emplace_back(n, r / float(kQ));
        Kastenbaum k = baueKaesten(*m_dok, s, m_zustand);
        seite->l = lege(*m_dok, s, k, m, m_breite);
        seite->breite = m_breite;
        if (m_abbruch->load()) return;
        seite->a = baueAnzeige(*m_dok, s, k, seite->l);
        seite->o = ordne(seite->l, &seite->a, &k);
        seite->kastenVon = std::move(k.kastenVon);
        auto zurueck = m_zurueck;
        std::shared_ptr<const Dokument> dok = m_dok;
        std::shared_ptr<const Seite> fertig = std::move(seite);
        QMetaObject::invokeMethod(m_owner, [zurueck, dok, fertig, ok] { zurueck(dok, fertig, ok); },
                                  Qt::QueuedConnection);
    }

private:
    QObject* m_owner;
    QString m_pfad;
    std::shared_ptr<const Dokument> m_dok;
    std::vector<uint8_t> m_zustand;
    float m_breite, m_hoehe, m_faktor;
    std::shared_ptr<std::atomic<bool>> m_abbruch;
    Zurueck m_zurueck;
};

class MalAufgabe : public QRunnable {
public:
    //  `gewollt` leer: eine Schicht statt einer Kachel, `index` ist dann ihre Nummer.
    MalAufgabe(QObject* owner, std::shared_ptr<const Seite> seite, int index, int breite, qreal dpr, qreal zoom,
               std::shared_ptr<std::atomic<bool>> abbruch, std::shared_ptr<const HtmlView::Gewollt> gewollt,
               std::function<void(QImage)> zurueck)
        : m_owner(owner), m_seite(std::move(seite)), m_index(index), m_breite(breite), m_dpr(dpr), m_zoom(zoom),
          m_abbruch(std::move(abbruch)), m_gewollt(std::move(gewollt)), m_zurueck(std::move(zurueck)) {
        setAutoDelete(true);
    }

    void run() override {
        if (m_abbruch->load()) return;
        if (!m_gewollt) { schicht(); return; }
        //  Schon vorbeigerollt: ohne Bild zurueck, sonst malte der Faden beim schnellen Rollen erst alles Uebersprungene.
        if (m_index < m_gewollt->von.load() || m_index > m_gewollt->bis.load()) {
            auto zurueck = m_zurueck;
            QMetaObject::invokeMethod(m_owner, [zurueck] { zurueck(QImage()); }, Qt::QueuedConnection);
            return;
        }
        const int b = std::max(1, int(std::ceil(m_breite * m_dpr))), h = int(std::ceil(HtmlView::kKachel * m_dpr));
        QImage bild(b, h, QImage::Format_ARGB32_Premultiplied);
        {
            QPainter p(&bild);
            const qreal s = m_dpr * m_zoom;
            const float y0 = float(m_index * HtmlView::kKachel / m_zoom);
            p.scale(s, s);
            p.translate(0, -y0);
            male(p, m_seite->a, m_seite->l, Rechteck{ 0, y0, float(m_breite / m_zoom), float(HtmlView::kKachel / m_zoom) },
                 nullptr, &m_seite->kacheln, kOhneSchichten);
        }
        zurueck(std::move(bild));
    }

private:
    void schicht() {
        const Rechteck& r = m_seite->a.schichten[size_t(m_index)].r;
        const qreal s = m_dpr * m_zoom;
        QImage bild(std::max(1, int(std::ceil(r.b * s))), std::max(1, int(std::ceil(r.h * s))),
                    QImage::Format_ARGB32_Premultiplied);
        bild.fill(Qt::transparent);
        {
            QPainter p(&bild);
            p.scale(s, s);
            p.translate(-r.x, -r.y);
            male(p, m_seite->a, m_seite->l, r, nullptr, &m_seite->kacheln, m_index);
        }
        zurueck(std::move(bild));
    }

    void zurueck(QImage bild) {
        m_seite.reset();
        auto z = m_zurueck;
        QMetaObject::invokeMethod(m_owner, [z, bild = std::move(bild)]() mutable { z(std::move(bild)); }, Qt::QueuedConnection);
    }

    QObject* m_owner;
    std::shared_ptr<const Seite> m_seite;
    int m_index, m_breite;
    qreal m_dpr, m_zoom;
    std::shared_ptr<std::atomic<bool>> m_abbruch;
    std::shared_ptr<const HtmlView::Gewollt> m_gewollt;
    std::function<void(QImage)> m_zurueck;
};

class SuchAufgabe : public QRunnable {
public:
    using Zurueck = std::function<void(std::vector<Bereich>, bool)>;
    SuchAufgabe(QObject* owner, std::shared_ptr<const Seite> seite, QString muster, bool gross, bool woerter,
                std::shared_ptr<std::atomic<bool>> abbruch, Zurueck zurueck)
        : m_owner(owner), m_seite(std::move(seite)), m_muster(std::move(muster)), m_gross(gross), m_woerter(woerter),
          m_abbruch(std::move(abbruch)), m_zurueck(std::move(zurueck)) {
        setAutoDelete(true);
    }

    void run() override {
        if (m_abbruch->load()) return;
        bool ueber = false;
        std::vector<Bereich> t = suche(m_seite->l, m_seite->o, m_muster, m_gross, m_woerter, kMaxTreffer, &ueber,
                                       m_abbruch.get());
        m_seite.reset();
        if (m_abbruch->load()) return;
        auto zurueck = m_zurueck;
        QMetaObject::invokeMethod(m_owner, [zurueck, t = std::move(t), ueber]() mutable { zurueck(std::move(t), ueber); },
                                  Qt::QueuedConnection);
    }

private:
    QObject* m_owner;
    std::shared_ptr<const Seite> m_seite;
    QString m_muster;
    bool m_gross, m_woerter;
    std::shared_ptr<std::atomic<bool>> m_abbruch;
    Zurueck m_zurueck;
};

}   // namespace

HtmlView::HtmlView(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setCursor(Qt::IBeamCursor);
    setAcceptHoverEvents(true);
    m_pool.setMaxThreadCount(1);
    //  Der Faden darf nicht enden, solange Layouts seine Schriften halten.
    m_pool.setExpiryTimeout(-1);
    m_pdfPool.setMaxThreadCount(1);
    m_abbruch = std::make_shared<std::atomic<bool>>(false);
    m_suchAbbruch = std::make_shared<std::atomic<bool>>(false);
    m_rollTakt.setInterval(16);
    connect(&m_rollTakt, &QTimer::timeout, this, &HtmlView::rolle);
}

HtmlView::~HtmlView() {
    m_abbruch->store(true);
    m_suchAbbruch->store(true);
    m_pdfAbbruch->store(true);
    entsorge(std::exchange(m_seite, nullptr));
    m_pool.waitForDone();
    m_pdfPool.waitForDone();
}

void HtmlView::entsorge(std::shared_ptr<const Seite> seite) {
    if (!seite) return;
    m_pool.start(QRunnable::create([s = std::move(seite)]() mutable { s.reset(); }));
}

qreal HtmlView::faktor() const { return window() ? window()->effectiveDevicePixelRatio() : 1.0; }

void HtmlView::setSource(const QString& pathOrUrl) {
    QString pfad = pathOrUrl;
    if (pfad.startsWith(QLatin1String("file:"))) pfad = QUrl(pfad).toLocalFile();
    if (pfad == m_source) return;
    m_source = pfad;
    m_dok.reset();
    m_zustand.clear();
    ++m_vorschauGen;
    m_vorschau.leere();
    emit sourceChanged();
    starte(true);
}

QString HtmlView::pdfTarget() const { return TextPdf::targetPathFor(m_source); }

void HtmlView::countPdfPages(bool print, bool landscape) {
    PdfOptionen o;
    o.druck = print;
    o.quer = landscape;
    ++m_zaehlGen;
    if (!m_dok) { m_pdfWartet.emplace(PdfArt::Zaehlen, o); return; }
    runPdf(PdfArt::Zaehlen, QString(), o, m_zaehlGen);
}

void HtmlView::previewPdf(bool print, bool landscape) {
    PdfOptionen o;
    o.druck = print;
    o.quer = landscape;
    ++m_vorschauGen;
    if (!m_dok) { m_pdfWartet.emplace(PdfArt::Vorschau, o); return; }
    const QString ziel = m_vorschau.neu();
    if (ziel.isEmpty()) return;
    runPdf(PdfArt::Vorschau, ziel, o, m_vorschauGen);
}

void HtmlView::exportPdf(const QString& target, bool print, bool landscape, int firstPage, int lastPage,
                         const QList<int>& pages) {
    if (m_exporting || target.isEmpty() || !m_dok) {
        QMetaObject::invokeMethod(this, [this, target] {
            emit pdfExportFinished(false, target, Strings::get(StringKey::MarkdownPdfNotReady));
        }, Qt::QueuedConnection);
        return;
    }
    m_exporting = true;
    emit exportingChanged();
    PdfOptionen o;
    o.druck = print;
    o.quer = landscape;
    o.von = firstPage;
    o.bis = lastPage;
    o.seiten = pages;
    runPdf(PdfArt::Schreiben, mg::toLocalPath(target), o, 0);
}

//  Zaehlen, Vorschau und Schreiben teilen sich den Faden: alle legen neu, zwei auf einmal kosteten nur RAM.
void HtmlView::runPdf(PdfArt art, const QString& ziel, const PdfOptionen& o, int gen) {
    if (!m_dok) return;
    QPointer<HtmlView> self(this);
    PdfOptionen mit = o;
    mit.quelle = m_source;
    auto* t = QRunnable::create([self, art, dok = m_dok, zustand = m_zustand, o = std::move(mit), ziel, gen,
                                 abbruch = m_pdfAbbruch] {
        QString fehler;
        const int seiten = schreibePdf(*dok, zustand, o, ziel, &fehler, abbruch.get());
        if (abbruch->load()) return;
        QMetaObject::invokeMethod(self, [self, art, seiten, fehler, ziel, gen] {
            if (!self) return;
            switch (art) {
            case PdfArt::Zaehlen:
                //  Nur die juengste Zaehlung zaehlt - der Dialog fragt bei jedem Umschalten neu.
                if (gen == self->m_zaehlGen) emit self->pdfPagesCounted(seiten);
                return;
            case PdfArt::Vorschau:
                if (gen != self->m_vorschauGen || seiten <= 0) {
                    self->m_vorschau.verwerfe(ziel);
                    return;
                }
                self->m_vorschau.uebernehme(ziel);
                emit self->pdfPreviewReady(ziel, seiten);
                return;
            case PdfArt::Schreiben:
                self->m_exporting = false;
                emit self->exportingChanged();
                emit self->pdfExportFinished(seiten > 0, ziel, fehler);
                return;
            }
        }, Qt::QueuedConnection);
    });
    t->setAutoDelete(true);
    m_pdfPool.start(t);
}

void HtmlView::setContentY(qreal y) {
    if (y == m_contentY) return;
    m_richtung = y > m_contentY ? 1 : -1;
    m_contentY = y;
    emit contentYChanged();
    polish();
    update();
}

void HtmlView::setZoom(qreal z) {
    z = std::clamp(z, 0.25, 8.0);
    if (z == m_zoom) return;
    m_zoom = z;
    emit zoomChanged();
    emit layoutChanged();
    starte(false);
}

void HtmlView::starte(bool lesen) {
    m_abbruch->store(true);
    m_abbruch = std::make_shared<std::atomic<bool>>(false);
    const int gen = ++m_layoutGen;
    m_inArbeit.clear();
    m_nachlegen = false;
    if (lesen) m_sprung = kKein;
    if (m_source.isEmpty() || width() <= 0) {
        entsorge(std::exchange(m_seite, nullptr));
        m_kacheln.clear();
        m_wartend.clear();
        m_schichten.clear();
        m_schichtBilder.clear();
        m_schichtWartend.clear();
        m_schichtGen = -1;
        m_schichtNeu = true;
        m_ohneHoehe = false;
        m_hoehe = 0;
        m_busy = false;
        emit layoutChanged();
        emit stateChanged();
        update();
        return;
    }
    if (height() <= 0) {
        m_ohneHoehe = true;
        if (!m_busy) {
            m_busy = true;
            emit stateChanged();
        }
        return;
    }
    m_ohneHoehe = false;
    m_gelegtBreite = float(width() / m_zoom);
    m_gelegtFaktor = faktor() * m_zoom;
    if (!m_busy) {
        m_busy = true;
        emit stateChanged();
    }
    auto* self = this;
    m_pool.start(new LegeAufgabe(this, m_source, lesen ? nullptr : m_dok, m_zustand, m_gelegtBreite, float(height() / m_zoom),
                                 float(m_gelegtFaktor), m_abbruch,
                                 [self, gen](std::shared_ptr<const Dokument> d, std::shared_ptr<const Seite> s, bool ok) {
                                     if (gen != self->m_layoutGen) { self->entsorge(std::move(s)); return; }
                                     self->annehmen(gen, std::move(d), std::move(s), ok);
                                 }));
}

void HtmlView::annehmen(int, std::shared_ptr<const Dokument> dok, std::shared_ptr<const Seite> seite, bool ok) {
    //  Stellen gelten im Absatztext; nach neuem Legen derselben Datei nur, solange die Absaetze dieselben sind.
    const bool selbesDok = dok == m_dok;
    const bool gleich = selbesDok && m_seite && seite->l.absaetze.size() == m_seite->l.absaetze.size();
    //  Die oberste Zeile im Fenster bleibt stehen; sonst sprang der Inhalt beim Ziehen an der Trennlinie.
    const Anker anker = dok == m_dok && m_seite && m_contentY > 0
        ? ankerVon(*m_seite, float(m_contentY / m_zoom), float((m_contentY + height()) / m_zoom)) : Anker{};
    //  Die Markierung reist ueber Dokumentknoten mit: ein aufgeklapptes details verschiebt sonst ihre Absaetze.
    const Griff griffAnker = selbesDok && m_seite && !gleich ? griffVon(*m_seite, m_anker) : Griff{};
    const Griff griffFokus = selbesDok && m_seite && !gleich ? griffVon(*m_seite, m_fokus) : Griff{};
    m_dok = std::move(dok);
    entsorge(std::exchange(m_seite, std::move(seite)));
    if (!gleich && (m_anker.gueltig() || m_fokus.gueltig())) {
        m_anker = stelleVon(*m_seite, griffAnker);
        m_fokus = stelleVon(*m_seite, griffFokus);
        if (!m_anker.gueltig() || !m_fokus.gueltig()) m_anker = m_fokus = Stelle{};
        emit selectionChanged();
    }
    if (!m_suchText.isEmpty()) {
        if (!gleich) search(m_suchText, m_suchGross, m_suchWoerter, int(m_contentY));
        else for (size_t i = 0; i < m_treffer.size(); ++i) m_trefferY[i] = zeilenOben(m_seite->l, m_seite->o, m_treffer[i].von);
    }
    ++m_seiteGen;
    m_hoehe = m_seite->a.hoehe;
    m_leinwand = QColor::fromRgba(m_seite->a.leinwand);
    m_busy = m_nachlegen;
    m_failed = !ok;
    m_skript = m_dok->hatSkript;
    m_inArbeit.clear();
    m_wartend.clear();
    emit layoutChanged();
    emit stateChanged();
    if (const float y = ankerY(*m_seite, anker); !std::isnan(y)) {
        const qreal vorher = m_contentY;
        emit scrollRequested((y - anker.oben) * m_zoom);
        //  Die Ansicht haelt contentY: erst ihr Ergebnis zaehlt. Alte Kacheln ruecken mit, bis die neuen zugleich kommen.
        for (auto& [i, k] : m_kacheln) k.verschub += m_contentY - vorher;
    }
    if (m_sprung != kKein && selbesDok) springe(std::exchange(m_sprung, kKein));
    if (m_pdfWartet && m_dok) {
        const auto [art, o] = *std::exchange(m_pdfWartet, std::nullopt);
        if (art == PdfArt::Zaehlen) countPdfPages(o.druck, o.quer);
        else previewPdf(o.druck, o.quer);
    }
    polish();
    update();
}

void HtmlView::updatePolish() {
    rechneFlaechen();
    if (!m_seite || width() <= 0) return;
    const auto [sv, sb] = sicht();
    const int letzte = std::max(0, int(std::ceil(contentHeight() / kKachel)) - 1);
    //  In Rollrichtung ein halbes Fenster mehr: eine Radrastung rollt 0,45 Fensterhoehen, eine Kachel Vorrat reichte nicht.
    const int voraus = 1 + int(height() / 2 / kKachel);
    const int von = std::max(0, sv - (m_richtung < 0 ? voraus : 1)), bis = std::min(letzte, sb + (m_richtung > 0 ? voraus : 1));
    m_gewollt->von.store(von);
    m_gewollt->bis.store(bis);
    for (auto* m : { &m_kacheln, &m_wartend })
        for (auto it = m->begin(); it != m->end();) {
            const int i = int(std::floor((it->first * kKachel + it->second.verschub) / kKachel));
            it = (i < von - 1 || i > bis + 1) ? m->erase(it) : std::next(it);
        }
    const int gen = m_seiteGen;
    const qreal dpr = faktor();
    const auto bestelle = [&](int i) {
        const auto it = m_kacheln.find(i);
        if ((it != m_kacheln.end() && it->second.gen == gen) || m_wartend.count(i) || m_inArbeit.count(i)) return;
        m_inArbeit.insert(i);
        auto* self = this;
        //  In der Breite des Layouts, nicht der Ansicht: beim Ziehen kam sonst rechts Leinwand ins Bild, und der Rand streckte sie.
        m_pool.start(new MalAufgabe(this, m_seite, i, std::max(1, int(m_seite->breite * m_zoom)), dpr, m_zoom, m_abbruch,
                                    m_gewollt, [self, gen, i](QImage b) { self->kachelFertig(gen, i, std::move(b)); }));
    };
    //  Der Pool arbeitet der Reihe nach: das Sichtbare zuerst, der Vorrat danach, je Seite das Naechste zuerst.
    for (int i = sv; i <= sb; ++i) bestelle(i);
    //  Schichten einmal je Layout, gleich nach dem Sichtbaren: fixed und sticky stehen meist im Fenster.
    if (m_schichtGen != gen) {
        m_schichtGen = gen;
        const size_t n = m_seite->a.schichten.size();
        m_schichtWartend.assign(n, QImage());
        m_schichtOffen = int(n);
        if (n == 0) {
            m_schichten.clear();
            m_schichtBilder.clear();
            m_schichtNeu = true;
        }
        auto* self = this;
        for (size_t i = 0; i < n; ++i)
            m_pool.start(new MalAufgabe(this, m_seite, int(i), 0, dpr, m_zoom, m_abbruch, nullptr,
                                        [self, gen, i](QImage b) { self->schichtFertig(gen, int(i), std::move(b)); }));
    }
    for (int i = sb + 1; i <= bis; ++i) bestelle(i);
    for (int i = sv - 1; i >= von; --i) bestelle(i);
    if (m_nachlegen && !sichtOffen()) QMetaObject::invokeMethod(this, [this] { legeNach(); }, Qt::QueuedConnection);
}

std::pair<int, int> HtmlView::sicht() const {
    if (!m_seite || height() <= 0) return { 0, -1 };
    const int letzte = std::max(0, int(std::ceil(contentHeight() / kKachel)) - 1);
    return { std::max(0, int(std::floor(m_contentY / kKachel))),
             std::min(letzte, int(std::ceil((m_contentY + height()) / kKachel)) - 1) };
}

bool HtmlView::imFenster(int index, const Kachel& k) const {
    const qreal y = index * kKachel + k.verschub;
    return y < m_contentY + height() && y + kKachel > m_contentY;
}

bool HtmlView::sichtOffen() const {
    const auto [sv, sb] = sicht();
    for (int i = sv; i <= sb; ++i)
        if (m_inArbeit.count(i)) return true;
    return false;
}

void HtmlView::kachelFertig(int gen, int index, QImage bild) {
    if (gen != m_seiteGen) return;
    m_inArbeit.erase(index);
    //  Uebersprungen: wird sie inzwischen wieder gebraucht, bestellt das naechste Polieren sie neu.
    if (bild.isNull()) { polish(); return; }
    m_wartend[index] = Kachel{ std::move(bild), gen, true };
    zeigeKacheln();
    legeNach();
}

void HtmlView::schichtFertig(int gen, int index, QImage bild) {
    if (gen != m_seiteGen || size_t(index) >= m_schichtWartend.size()) return;
    m_schichtWartend[size_t(index)] = std::move(bild);
    if (--m_schichtOffen > 0) return;
    m_schichten = m_seite->a.schichten;
    m_schichtBilder = std::exchange(m_schichtWartend, {});
    m_schichtNeu = true;
    update();
}

//  Nach neuem Legen alle sichtbaren Kacheln zugleich tauschen: einzeln getauscht stand eine neue neben einer alten,
//  schmaleren, und am Rand blieb ein leeres Feld. Beim Rollen gibt es nichts Altes, dann sofort.
void HtmlView::zeigeKacheln() {
    bool alt = false;
    for (const auto& [i, k] : m_kacheln) alt |= k.gen != m_seiteGen && imFenster(i, k);
    if (alt && sichtOffen()) return;
    //  Verschobene alte Kacheln liegen nicht mehr an ihrem Platz und gingen sonst ueber die neuen.
    for (auto it = m_kacheln.begin(); it != m_kacheln.end();)
        it = it->second.verschub != 0 && it->second.gen != m_seiteGen ? m_kacheln.erase(it) : std::next(it);
    for (auto& [i, k] : m_wartend) m_kacheln[i] = std::move(k);
    m_wartend.clear();
    //  Ist das Dokument kuerzer geworden, zeigte eine alte Kachel hinter dem Ende sonst weiter den alten Inhalt.
    const int letzte = std::max(0, int(std::ceil(contentHeight() / kKachel)) - 1);
    for (auto it = m_kacheln.begin(); it != m_kacheln.end();)
        it = it->first > letzte ? m_kacheln.erase(it) : std::next(it);
    update();
}

//  Erst nach den sichtbaren Kacheln: sonst verwirft das naechste Layout sie, und beim Ziehen bleibt das alte Bild stehen.
void HtmlView::legeNach() {
    if (!m_nachlegen || sichtOffen()) return;
    m_nachlegen = false;
    if (width() > 0 && float(width() / m_zoom) != m_gelegtBreite) { starte(false); return; }
    m_busy = false;
    emit stateChanged();
}

void HtmlView::geometryChange(const QRectF& neu, const QRectF& alt) {
    QQuickItem::geometryChange(neu, alt);
    //  Waehrend des Ziehens nicht abbrechen: Japanisch (69 ms) wurde sonst bis zum Loslassen nie fertig.
    if (m_ohneHoehe && neu.height() > 0) starte(false);
    else if (neu.width() != alt.width() || (m_gelegtBreite < 0 && neu.width() > 0)) {
        if (m_busy && neu.width() > 0) m_nachlegen = true;
        else starte(false);
    }
    else if (neu.height() != alt.height()) { polish(); update(); }
}

void HtmlView::itemChange(ItemChange c, const ItemChangeData& d) {
    QQuickItem::itemChange(c, d);
    if ((c == ItemDevicePixelRatioHasChanged || c == ItemSceneChange) && window()
        && faktor() * m_zoom != m_gelegtFaktor && !m_source.isEmpty())
        starte(false);
}

void HtmlView::releaseResources() {
    m_knoten.clear();
    m_schichtKnoten.clear();
    m_grund = nullptr;
    for (auto& k : m_flaechenKnoten) k.clear();
    m_knotenVerloren = true;
}

void HtmlView::rechneFlaechen() {
    for (auto& f : m_flaechen) f.clear();
    m_flaechenNeu = true;
    if (!m_seite) return;
    const Layout& l = m_seite->l;
    const Ordnung& o = m_seite->o;
    const float y0 = float(m_contentY / m_zoom), y1 = float((m_contentY + height()) / m_zoom);
    std::vector<Rechteck> r;
    const auto uebertrage = [&](int ebene) {
        for (const Rechteck& x : r)
            m_flaechen[ebene].emplace_back(x.x * m_zoom, x.y * m_zoom - m_contentY, x.b * m_zoom, x.h * m_zoom);
        r.clear();
    };
    for (size_t i = 0; i < m_treffer.size(); ++i) {
        if (m_trefferY[i] > y1 || m_trefferY[i] < y0 - kTrefferSpanne) continue;
        flaechen(l, o, m_treffer[i], y0, y1, r);
        uebertrage(int(i) == m_trefferIndex ? 1 : 0);
    }
    if (hasSelection()) {
        flaechen(l, o, bereich(o, m_anker, m_fokus), y0, y1, r);
        uebertrage(2);
    }
}

Stelle HtmlView::stelleBei(QPointF p, bool glyphe) const {
    if (!m_seite) return {};
    return treffe(m_seite->l, m_seite->o, float(p.x() / m_zoom), float((p.y() + m_contentY) / m_zoom), glyphe);
}

void HtmlView::setzeFokus(const Stelle& s) {
    m_fokus = s;
    emit selectionChanged();
    polish();
    update();
}

void HtmlView::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !m_seite) { e->ignore(); return; }
    //  Sonst behielt eine TextEdit daneben den Fokus und nahm Strg+A/C vor den Kuerzeln der Flaeche.
    forceActiveFocus(Qt::MouseFocusReason);
    //  Dritter Druck kurz nach dem Doppelklick am selben Ort: ganzer Absatz wie im Browser.
    const QStyleHints* sh = QGuiApplication::styleHints();
    if (m_doppelUhr.isValid() && m_doppelUhr.elapsed() < sh->mouseDoubleClickInterval()
        && (e->position() - m_doppelOrt).manhattanLength() < sh->startDragDistance()) {
        m_doppelUhr.invalidate();
        const Bereich b = absatzUm(m_seite->l, stelleBei(e->position(), true));
        if (b.von.gueltig()) {
            m_anker = b.von;
            setzeFokus(b.bis);
            return;
        }
    }
    m_doppelUhr.invalidate();
    const Stelle s = stelleBei(e->position());
    if (!(e->modifiers() & Qt::ShiftModifier) || !m_anker.gueltig()) m_anker = s;
    m_zieht = true;
    m_maus = e->position();
    setKeepMouseGrab(true);
    setzeFokus(s);
}

void HtmlView::mouseMoveEvent(QMouseEvent* e) {
    if (!m_zieht) return;
    m_maus = e->position();
    setzeFokus(stelleBei(m_maus));
    if (m_maus.y() < 0 || m_maus.y() > height()) { if (!m_rollTakt.isActive()) m_rollTakt.start(); }
    else m_rollTakt.stop();
}

void HtmlView::mouseReleaseEvent(QMouseEvent* e) {
    m_zieht = false;
    m_rollTakt.stop();
    setKeepMouseGrab(false);
    //  Ein Klick ohne Markierung folgt Verweisen und schaltet wie im Browser ohne Skript: details, Haken, Radioknoepfe, label.
    if (!hasSelection() && !(e->modifiers() & Qt::ShiftModifier) && m_seite && m_dok) {
        const uint32_t el = elementUnter(e->position());
        if (el != kKein) {
            if (const uint32_t a = verweisUm(*m_dok, el); a != kKein) folge(a);
            else if (aktiviere(*m_dok, m_zustand, el)) starte(false);
        }
    }
    //  X11/Wayland: Markiertes gehoert in die Primaerauswahl (mittlere Maustaste).
    QClipboard* c = QGuiApplication::clipboard();
    if (hasSelection() && c->supportsSelection()) c->setText(selectedText(), QClipboard::Selection);
}

void HtmlView::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !m_seite) { e->ignore(); return; }
    m_doppelUhr.start();
    m_doppelOrt = e->position();
    const Bereich w = wortUm(m_seite->l, stelleBei(e->position(), true));
    if (!w.von.gueltig()) return;
    m_anker = w.von;
    setzeFokus(w.bis);
}

uint32_t HtmlView::verweisBei(QPointF p) const {
    const uint32_t el = elementUnter(p);
    return el == kKein ? kKein : verweisUm(*m_dok, el);
}

//  Verschobene Schichten zuerst, an ihrer gezeigten Stelle; an ihrem Platz im Fluss trifft man sie dann nicht mehr.
uint32_t HtmlView::elementUnter(QPointF p) const {
    if (!m_seite || !m_dok) return kKein;
    const float x = float(p.x() / m_zoom), y = float((p.y() + m_contentY) / m_zoom);
    const float y0 = float(m_contentY / m_zoom), h = float(height() / m_zoom);
    const std::vector<Schicht>& sch = m_seite->a.schichten;
    std::vector<uint32_t> verschoben;
    for (size_t i = sch.size(); i-- > 0;) {
        const Schicht& s = sch[i];
        const float d = s.versatz(y0, h);
        if (d == 0) continue;
        verschoben.push_back(s.knoten);
        if (x < s.r.x || x >= s.r.x + s.r.b || y < s.r.y + d || y >= s.r.y + d + s.r.h) continue;
        if (const uint32_t el = elementBei(*m_seite, *m_dok, x, y - d, s.knoten); el != kKein) return el;
    }
    return elementBei(*m_seite, *m_dok, x, y, kKein, verschoben);
}

void HtmlView::hoverMoveEvent(QHoverEvent* e) {
    const bool verweis = !m_zieht && verweisBei(e->position()) != kKein;
    if (verweis != m_ueberVerweis) {
        m_ueberVerweis = verweis;
        setCursor(verweis ? Qt::PointingHandCursor : Qt::IBeamCursor);
    }
    QQuickItem::hoverMoveEvent(e);
}

//  Wie im Markdown: Web und Mail nach aussen, andere Dateien an den Viewer, Spruenge in derselben Datei rollen hier.
void HtmlView::folge(uint32_t verweis) {
    const VerweisZiel z = verweisZiel(*m_dok, verweis, m_source);
    if (!z.aussen.isEmpty()) {
        emit linkActivated(z.aussen.toString());
        return;
    }
    if (!z.datei.isEmpty()) {
        emit openFileRequested(z.datei);
        return;
    }
    if (z.knoten == kKein) return;
    //  Ein Ziel im geschlossenen details hat keinen Kasten: erst aufklappen, gesprungen wird nach dem Legen.
    if (z.knoten != 0 && oeffneUm(*m_dok, m_zustand, z.knoten)) {
        m_sprung = z.knoten;
        starte(false);
        return;
    }
    springe(z.knoten);
}

void HtmlView::springe(uint32_t ziel) {
    if (!m_seite) return;
    float y = 0;
    if (ziel != 0) {
        const uint32_t k = ziel < m_seite->kastenVon.size() ? m_seite->kastenVon[ziel] : kKein;
        if (k == kKein || k >= m_seite->l.lage.size()) return;
        const auto& rr = m_seite->rollRand;
        const auto it = std::lower_bound(rr.begin(), rr.end(), ziel, [](const auto& e, uint32_t n) { return e.first < n; });
        y = m_seite->l.lage[k].y - (it != rr.end() && it->first == ziel ? it->second : 0.f);
    }
    emit scrollRequested(std::max(0.f, y) * m_zoom);
}

//  Ziehen ueber den Rand rollt, je weiter draussen, desto schneller.
void HtmlView::rolle() {
    const qreal dy = m_maus.y() < 0 ? m_maus.y() : m_maus.y() - height();
    emit scrollRequested(std::max(0.0, m_contentY + std::clamp(dy / 2, -60.0, 60.0)));
    setzeFokus(stelleBei(m_maus));
}

QString HtmlView::selectedText() const {
    if (!hasSelection() || !m_seite) return {};
    return klartext(m_seite->l, m_seite->o, bereich(m_seite->o, m_anker, m_fokus));
}

void HtmlView::copy() {
    if (hasSelection()) QGuiApplication::clipboard()->setText(selectedText());
}

void HtmlView::selectAll() {
    if (!m_seite) return;
    const Bereich b = alles(m_seite->l, m_seite->o);
    m_anker = b.von;
    setzeFokus(b.bis);
}

void HtmlView::clearSelection() {
    m_anker = Stelle{};
    setzeFokus(Stelle{});
}

void HtmlView::search(const QString& text, bool caseSensitive, bool wholeWords, int fromRow) {
    m_suchAbbruch->store(true);
    m_suchAbbruch = std::make_shared<std::atomic<bool>>(false);
    m_suchText = text;
    m_suchGross = caseSensitive;
    m_suchWoerter = wholeWords;
    const int gen = ++m_suchGen;
    if (text.isEmpty() || !m_seite) {
        m_treffer.clear();
        m_trefferY.clear();
        m_trefferIndex = -1;
        m_trefferUeber = m_sucht = false;
        ++m_suchRevision;
        emit searchChanged();
        polish();
        update();
        return;
    }
    m_sucht = true;
    emit searchChanged();
    auto* self = this;
    m_pool.start(new SuchAufgabe(this, m_seite, text, caseSensitive, wholeWords, m_suchAbbruch,
                                 [self, gen, fromRow](std::vector<Bereich> t, bool ueber) {
                                     self->trefferAngenommen(gen, std::move(t), ueber, fromRow);
                                 }));
}

void HtmlView::trefferAngenommen(int gen, std::vector<Bereich> t, bool ueber, int abY) {
    if (gen != m_suchGen || !m_seite) return;
    m_treffer = std::move(t);
    m_trefferUeber = ueber;
    m_sucht = false;
    m_trefferY.resize(m_treffer.size());
    m_trefferIndex = m_treffer.empty() ? -1 : 0;
    for (size_t i = 0; i < m_treffer.size(); ++i) m_trefferY[i] = zeilenOben(m_seite->l, m_seite->o, m_treffer[i].von);
    for (size_t i = 0; i < m_treffer.size(); ++i)
        if (m_trefferY[i] * m_zoom >= abY) { m_trefferIndex = int(i); break; }
    ++m_suchRevision;
    emit searchChanged();
    zeigeTreffer();
    polish();
    update();
}

void HtmlView::stepMatch(int richtung) {
    const int n = int(m_treffer.size());
    if (n == 0) return;
    m_trefferIndex = ((m_trefferIndex + richtung) % n + n) % n;
    emit searchChanged();
    zeigeTreffer();
    polish();
    update();
}

void HtmlView::clearSearch() { search(QString(), m_suchGross, m_suchWoerter, 0); }

//  Nur rollen, wenn der Treffer nicht schon gut sichtbar ist; dann ins obere Drittel.
void HtmlView::zeigeTreffer() {
    if (m_trefferIndex < 0 || size_t(m_trefferIndex) >= m_trefferY.size()) return;
    const qreal y = m_trefferY[size_t(m_trefferIndex)] * m_zoom;
    if (y < m_contentY + height() * 0.1 || y > m_contentY + height() * 0.8)
        emit scrollRequested(std::max(0.0, y - height() / 3));
}

QSGNode* HtmlView::updatePaintNode(QSGNode* alt, UpdatePaintNodeData*) {
    //  Eigener Clip-Knoten: clip des Items (auch eines Elternteils) liess die Kacheln ueber den Rand ragen.
    auto* wurzel = static_cast<QSGClipNode*>(alt);
    if (!wurzel) {
        wurzel = new QSGClipNode;
        wurzel->setIsRectangular(true);
        m_knoten.clear();
        m_grund = nullptr;
        for (auto& k : m_flaechenKnoten) k.clear();
        m_flaechenNeu = true;
        //  Texturen sind mit dem Szenengraph weg, die Bilder schon frei: neu malen lassen.
        bool fehlt = m_knotenVerloren;
        m_schichtKnoten.clear();
        if (!m_schichten.empty() && !m_schichtNeu) {
            m_schichtGen = -1;
            fehlt = true;
        }
        for (auto it = m_kacheln.begin(); it != m_kacheln.end();) {
            if (it->second.bild.isNull()) { it = m_kacheln.erase(it); fehlt = true; }
            else { it->second.neu = true; ++it; }
        }
        m_knotenVerloren = false;
        if (fehlt) QMetaObject::invokeMethod(this, [this] { polish(); }, Qt::QueuedConnection);
    }
    if (!m_grund) {
        m_grund = window()->createRectangleNode();
        wurzel->prependChildNode(m_grund);
    }
    if (wurzel->clipRect() != boundingRect()) {
        wurzel->setClipRect(boundingRect());
        wurzel->markDirty(QSGNode::DirtyGeometry);
    }
    m_grund->setRect(boundingRect());
    m_grund->setColor(m_leinwand);
    const auto entferne = [wurzel](const Knoten& k) {
        for (QSGSimpleTextureNode* n : { k.bild, k.rand }) {
            wurzel->removeChildNode(n);
            delete n;
        }
    };
    for (auto it = m_knoten.begin(); it != m_knoten.end();) {
        if (m_kacheln.count(it->first)) { ++it; continue; }
        entferne(it->second);
        it = m_knoten.erase(it);
    }
    const qreal dpr = faktor();
    for (auto& [i, k] : m_kacheln) {
        //  Neuer Inhalt bekommt einen neuen Knoten: der alte nimmt seine Textur mit.
        if (k.neu && !k.bild.isNull()) {
            if (const auto it = m_knoten.find(i); it != m_knoten.end()) {
                entferne(it->second);
                m_knoten.erase(it);
            }
            auto* n = new QSGSimpleTextureNode;
            n->setOwnsTexture(true);
            n->setFiltering(QSGTexture::Linear);
            n->setTexture(window()->createTextureFromImage(k.bild));
            //  Streckt die letzte Spalte, solange die Ansicht breiter ist als die Kachel: sonst stand bis zum neuen Layout
            //  die Leinwand als heller Streifen neben dunklen, vollbreiten Abschnitten (Arabisch). Eigene Textur: teilte
            //  der Knoten die der Kachel, behielt eine Kachel nach dem Tausch ihr altes Bild (offscreen).
            auto* rand = new QSGSimpleTextureNode;
            rand->setOwnsTexture(true);
            rand->setFiltering(QSGTexture::Nearest);
            rand->setTexture(window()->createTextureFromImage(k.bild.copy(k.bild.width() - 1, 0, 1, k.bild.height())));
            wurzel->insertChildNodeAfter(n, m_grund);
            wurzel->insertChildNodeAfter(rand, n);
            m_knoten[i] = Knoten{ n, rand };
            k.bild = QImage();
            k.neu = false;
        }
        if (const auto it = m_knoten.find(i); it != m_knoten.end()) {
            const QSize t = it->second.bild->texture()->textureSize();
            const QRectF r(0, i * kKachel + k.verschub - m_contentY, t.width() / dpr, t.height() / dpr);
            it->second.bild->setRect(r);
            it->second.rand->setRect(QRectF(r.right(), r.y(), std::max(0.0, width() - r.right()), r.height()));
        }
    }
    //  Schichten ueber den Kacheln, unter Markierung und Treffern; beim Rollen nur verschoben.
    if (m_schichtNeu) {
        for (const auto& [i, n] : m_schichtKnoten) {
            wurzel->removeChildNode(n);
            delete n;
        }
        m_schichtKnoten.clear();
        std::set<QSGNode*> flaechen;
        for (const auto& k : m_flaechenKnoten) flaechen.insert(k.begin(), k.end());
        QSGNode* vor = wurzel->firstChild();
        while (vor && !flaechen.count(vor)) vor = vor->nextSibling();
        QSGNode* letzter = nullptr;
        for (size_t i = 0; i < m_schichtBilder.size(); ++i) {
            if (m_schichtBilder[i].isNull()) continue;
            auto* n = new QSGSimpleTextureNode;
            n->setOwnsTexture(true);
            n->setFiltering(QSGTexture::Linear);
            n->setTexture(window()->createTextureFromImage(m_schichtBilder[i]));
            if (letzter) wurzel->insertChildNodeAfter(n, letzter);
            else if (vor) wurzel->insertChildNodeBefore(n, vor);
            else wurzel->appendChildNode(n);
            letzter = n;
            m_schichtKnoten.emplace_back(i, n);
            m_schichtBilder[i] = QImage();
        }
        m_schichtNeu = false;
    }
    const float y0 = float(m_contentY / m_zoom), hDok = float(height() / m_zoom);
    for (const auto& [i, n] : m_schichtKnoten) {
        const Schicht& s = m_schichten[i];
        const QSize t = n->texture()->textureSize();
        n->setRect(QRectF(s.r.x * m_zoom, (s.r.y + s.versatz(y0, hDok)) * m_zoom - m_contentY, t.width() / dpr, t.height() / dpr));
    }
    //  Rechteck-Knoten am Ende, ueber den Kacheln. Unter diesem Clip-Knoten zeichnete Qt weder einen eigenen
    //  QSGGeometryNode noch Kinder einer Zwischen-QSGNode an der richtigen Stelle (offscreen, Qt 6.12).
    if (m_flaechenNeu) {
        const QColor farben[3] = { QColor(255, 214, 0, 110), QColor(255, 140, 0, 150), m_farbeAuswahl };
        for (int e = 0; e < 3; ++e) {
            auto& k = m_flaechenKnoten[e];
            for (; k.size() > m_flaechen[e].size(); k.pop_back()) {
                wurzel->removeChildNode(k.back());
                delete k.back();
            }
            while (k.size() < m_flaechen[e].size()) {
                k.push_back(window()->createRectangleNode());
                wurzel->appendChildNode(k.back());
            }
            for (size_t i = 0; i < k.size(); ++i) {
                k[i]->setRect(m_flaechen[e][i]);
                k[i]->setColor(farben[e]);
            }
        }
        m_flaechenNeu = false;
    }
    return wurzel;
}

}   // namespace mg::html
