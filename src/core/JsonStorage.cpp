#include "core/JsonStorage.h"

#include "core/MGStorage.h"
#include "core/PathUtils.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>

#include <unordered_map>
#include <QRandomGenerator>
#include <QSet>
#include <QDataStream>

#include <algorithm>

JsonStorage::JsonStorage(QObject* parent) : QObject(parent) {
    //  Sammelndes Speichern: Ein Null-Timer feuert am Ende des laufenden
    //  Ereignisdurchlaufs - alles, was darin anfällt (100 Dateien auf einen
    //  Tag ziehen), wird zu EINEM Schreibvorgang.
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(0);
    m_savePool.setMaxThreadCount(1);       // Reihenfolge der Schreibvorgaenge
    connect(&m_saveTimer, &QTimer::timeout, this, &JsonStorage::saveTimerFired);
    //  Beenden: was noch aussteht, muss auf die Platte. Der Destruktor allein
    //  genügt nicht - beim regulären Beenden räumt Qt die Ereignisschleife ab,
    //  bevor lange lebende Objekte fallen.
    if (QCoreApplication* app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this,
                &JsonStorage::flushPendingSave);
}

JsonStorage::~JsonStorage() { flushPendingSave(); }

QColor JsonStorage::randomTagColor() {
    static const QList<QColor> palette = {
        {220, 80,  80},  {80,  200, 120}, {80,  140, 220},
        {220, 160, 60},  {160, 80,  220}, {60,  200, 200},
        {220, 100, 160}, {140, 200, 60},  {80,  180, 200},
        {200, 140, 80},  {100, 120, 220}, {180, 80,  120},
        {60,  180, 140}, {200, 80,  60},  {120, 200, 160}
    };
    return palette[QRandomGenerator::global()->bounded(palette.size())];
}

QJsonObject JsonStorage::categoryToJson(const TagCategory& cat) {
    QJsonObject obj;
    obj["id"]           = cat.id;
    obj["name"]         = cat.name;
    obj["uniformColor"]          = cat.uniformColor;
    obj["color"]                 = cat.color.name();
    if (cat.inheritColorToChildren)
        obj["inheritColorToChildren"] = true;

    QJsonArray tags;
    for (const QString& t : cat.tags) tags.append(t);
    obj["tags"] = tags;

    QJsonArray files;
    for (const QString& f : cat.files) files.append(f);
    if (!files.isEmpty()) obj["files"] = files;

    if (!cat.children.isEmpty()) {
        QJsonArray children;
        for (const auto& ch : cat.children) children.append(categoryToJson(ch));
        obj["children"] = children;
    }

    return obj;
}

TagCategory JsonStorage::categoryFromJson(const QJsonObject& obj) {
    TagCategory cat;
    cat.id           = obj["id"].toString();
    cat.name         = obj["name"].toString();
    cat.uniformColor             = obj["uniformColor"].toBool(false);
    cat.color                    = QColor(obj["color"].toString("#00b4a0"));
    cat.inheritColorToChildren   = obj["inheritColorToChildren"].toBool(false);

    QJsonArray tags = obj["tags"].toArray();
    for (const auto& t : tags) cat.tags.append(t.toString());

    QJsonArray files = obj["files"].toArray();
    for (const auto& f : files) cat.files.append(f.toString());

    QJsonArray children = obj["children"].toArray();
    for (const auto& ch : children) cat.children.append(categoryFromJson(ch.toObject()));

    return cat;
}

// Dateizentrisch: { files: { name: { t: [tags], d: ISO8601 } }, tagColors, categories }.
// Nur nicht-leere Felder werden geschrieben. Ein altes "v"-Feld wird ignoriert.
void JsonStorage::loadNewFormat(const QJsonObject& root) {
    QJsonObject tagColors = root["tagColors"].toObject();
    for (auto it = tagColors.begin(); it != tagColors.end(); ++it)
        m_tagColors[it.key()] = QColor(it.value().toString("#64b4a0"));

    QJsonObject files = root["files"].toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        QJsonObject o = it.value().toObject();
        FileMeta& meta = m_fileMeta[it.key()];

        QJsonArray tagsArr = o["t"].toArray();
        for (const auto& tv : tagsArr) {
            QString tag = tv.toString();
            meta.tags.append(tag);
            ensureTagRegistered(tag);
        }

        //  „d" (eigenes Datum) und „o" (Merker) werden NICHT mehr gelesen: das
        //  Datum steht an der Datei. Alte Einträge verschwinden beim nächsten
        //  Speichern von selbst - der Sidecar wird immer ganz neu geschrieben.

        if (o.contains("c")) {
            // Only a colour Qt can parse is taken over; garbage stays "no choice"
            // so the export falls back to the global default instead of black-on-
            // black from an unusable value.
            const QColor c(o["c"].toString());
            if (c.isValid()) meta.textPdfColor = c;
        }
    }
}

void JsonStorage::loadFolder(const QString& folderPath) {
    //  Ein ausstehender Schreibvorgang gehört zum BISHERIGEN Ordner - er muss
    //  raus, bevor `m_folderPath` weiterzeigt, sonst landet er im falschen
    //  Ordner oder fällt ganz unter den Tisch.
    flushPendingSave();
    m_folderPath = folderPath;
    m_fileMeta.clear();
    m_tagColors.clear();
    m_categories.clear();
    m_jsonPath = sidecarPath(folderPath);

    //  Die eigene Ablage zuerst. Gibt es sie nicht, wird die alte JSON gelesen -
    //  und beim naechsten Speichern durch die neue ersetzt.
    QFile f(m_jsonPath);
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        const QByteArray roh = f.read(kMaxAblageBytes);
        f.close();
        noteDiskStamp(m_jsonPath);
        mg::storage::Ablage a;
        std::string fehler;
        if (mg::storage::lies(roh.constData(), std::size_t(roh.size()), a, &fehler)) {
            uebernimmAblage(a);
        } else {
            qWarning() << "JsonStorage:" << m_jsonPath << "nicht lesbar -"
                       << QString::fromStdString(fehler);
        }
        return;
    }

    //  Ordner umbenannt, Ablage nicht: sonst waere alle Verschlagwortung weg,
    //  obwohl die Datei danebenliegt. Bei mehreren wird nicht geraten.
    if (const QString verwaist = verwaisteAblage(folderPath); !verwaist.isEmpty()) {
        QFile alt(verwaist);
        if (alt.open(QIODevice::ReadOnly)) {
            const QByteArray roh = alt.read(kMaxAblageBytes);
            alt.close();
            mg::storage::Ablage a;
            if (mg::storage::lies(roh.constData(), std::size_t(roh.size()), a, nullptr)) {
                uebernimmAblage(a);
                //  Erst unter dem richtigen Namen schreiben, dann die alte weg.
                saveFolder(folderPath);
                if (QFileInfo::exists(m_jsonPath)) QFile::remove(verwaist);
                return;
            }
        }
    }

    leseAlteJson(altePath(folderPath));
    //  Still umziehen: eine Ablage, die nur gelesen und nie gespeichert wird,
    //  bliebe sonst fuer immer alt. Ein leerer Ordner bekommt keine Datei.
    if (!m_fileMeta.isEmpty() || !m_tagColors.isEmpty() || !m_categories.isEmpty())
        saveFolder(folderPath);
}

//  Der Leser fuer das ALTE Format. Er bleibt, damit vorhandene Ablagen
//  aufgehen; geschrieben wird nur noch die eigene Form.
void JsonStorage::leseAlteJson(const QString& pfad) {
    QFile f(pfad);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return;

    const QJsonObject root = doc.object();
    loadNewFormat(root);
    const QJsonArray cats = root["categories"].toArray();
    for (const auto& c : cats) m_categories.append(categoryFromJson(c.toObject()));
}

void JsonStorage::noteDiskStamp(const QString& path) {
    const QFileInfo fi(path);
    m_diskMTime = fi.exists() ? fi.lastModified() : QDateTime();
    m_diskSize  = fi.exists() ? fi.size() : -1;
}

bool JsonStorage::diskChangedSince(const QString& path) const {
    const QFileInfo fi(path);
    if (!fi.exists()) return false;                 // nichts, was wir verlieren könnten
    if (m_diskSize < 0) return true;                // wir haben nie gelesen
    return fi.size() != m_diskSize || fi.lastModified() != m_diskMTime;
}

// Die Datei hat sich seit unserem Lesen geändert - jemand anderes hat geschrieben. Übernommen wird, was wir nicht
// kennen; wo beide etwas wissen, gewinnt das HINZUFÜGEN: der Verlust fremder Verschlagwortung wöge schwerer.
void JsonStorage::mergeForeignChanges(const QString& path) {
    if (!diskChangedSince(path)) return;

    JsonStorage disk;
    disk.loadFolder(m_folderPath);

    for (auto it = disk.m_fileMeta.cbegin(); it != disk.m_fileMeta.cend(); ++it) {
        const FileMeta& theirs = it.value();
        if (!m_fileMeta.contains(it.key())) { m_fileMeta.insert(it.key(), theirs); continue; }
        FileMeta& ours = m_fileMeta[it.key()];
        for (const QString& t : theirs.tags)
            if (!ours.tags.contains(t)) ours.tags.append(t);
        if (!ours.textPdfColor.isValid() && theirs.textPdfColor.isValid())
            ours.textPdfColor = theirs.textPdfColor;
    }
    for (auto it = disk.m_tagColors.cbegin(); it != disk.m_tagColors.cend(); ++it)
        if (!m_tagColors.contains(it.key())) m_tagColors.insert(it.key(), it.value());
    //  Kategorien sind ein BAUM; ihn zu verschmelzen wäre Raten. Kennen wir
    //  keinen, übernehmen wir den fremden - sonst bleibt unserer stehen.
    if (m_categories.isEmpty()) m_categories = disk.m_categories;
}

QString JsonStorage::sidecarPath(const QString& folderPath) const {
    return folderPath + "/" + QFileInfo(folderPath).fileName() + kEndung;
}

//  Genau EINE `.mgstore`, die nicht zum Ordnernamen passt? Dann ist sie die
//  Ablage dieses Ordners unter altem Namen. Mehrere oder keine: leer.
QString JsonStorage::verwaisteAblage(const QString& folderPath) const {
    QDir d(folderPath);
    const QStringList treffer =
        d.entryList({ QStringLiteral("*") + QLatin1String(kEndung) }, QDir::Files);
    if (treffer.size() != 1) return {};
    const QString name = treffer.first();
    if (name == mg::folderSidecarName(folderPath)) return {};   // passt ohnehin
    return d.filePath(name);
}

QString JsonStorage::altePath(const QString& folderPath) const {
    return folderPath + "/" + QFileInfo(folderPath).fileName() + QStringLiteral(".json");
}

namespace {

std::uint32_t alsZahl(const QColor& c) {
    return c.isValid() ? ((std::uint32_t(c.red()) << 16) | (std::uint32_t(c.green()) << 8)
                          | std::uint32_t(c.blue()))
                       : mg::storage::kKeineFarbe;
}

QColor alsFarbe(std::uint32_t v) {
    return v == mg::storage::kKeineFarbe
               ? QColor()
               : QColor(int((v >> 16) & 0xFF), int((v >> 8) & 0xFF), int(v & 0xFF));
}

std::string utf8(const QString& s) { return s.toStdString(); }
QString ausUtf8(const std::string& s) { return QString::fromStdString(s); }

//  Den Kategorienbaum flach machen: `eltern` ist die Nummer der Mutter + 1.
void flachKategorien(const QList<TagCategory>& baum, std::uint32_t eltern,
                     const QHash<QString, std::uint32_t>& tagNr,
                     std::vector<mg::storage::Kategorie>& out) {
    for (const TagCategory& c : baum) {
        mg::storage::Kategorie k;
        k.id       = utf8(c.id);
        k.name     = utf8(c.name);
        k.farbe    = alsZahl(c.color.isValid() ? c.color : QColor(100, 180, 160));
        k.schalter = std::uint8_t((c.uniformColor ? 0x01 : 0)
                                  | (c.inheritColorToChildren ? 0x02 : 0));
        k.eltern   = eltern;
        for (const QString& t : c.tags)
            if (const auto it = tagNr.constFind(t); it != tagNr.cend()) k.tags.push_back(*it);
        out.push_back(std::move(k));
        const std::uint32_t meine = std::uint32_t(out.size());   // Nummer + 1
        flachKategorien(c.children, meine, tagNr, out);
    }
}

}  // namespace

//  Den Stand in die reine C++-Form bringen, aus der `mg::storage` die Bytes
//  macht. Zwei Dinge passieren dabei:
//   · Die Tag-KOMBINATIONEN werden gesammelt: gleiche Kombinationen teilen sich
//     einen Eintrag, die Datei nennt nur noch dessen Nummer.
//   · Die Kategorie-Zugehoerigkeit wandert von der Kategorie zur DATEI - im
//     Speicher bleibt sie, wo sie war, nur die Datei fuehrt sie andersherum.
mg::storage::Ablage JsonStorage::baueAblage(const QHash<QString, FileMeta>& files,
                                            const QHash<QString, QColor>& colors,
                                            const QList<TagCategory>& cats) {
    using namespace mg::storage;
    Ablage a;

    //  Tagtabelle - sortiert, damit dieselbe Ablage immer dieselben Bytes ergibt.
    QStringList tagNamen = colors.keys();
    std::sort(tagNamen.begin(), tagNamen.end());
    QHash<QString, std::uint32_t> tagNr;
    a.tags.reserve(tagNamen.size());
    for (const QString& t : std::as_const(tagNamen)) {
        tagNr.insert(t, std::uint32_t(a.tags.size()));
        a.tags.push_back({ utf8(t), alsZahl(colors.value(t, QColor(100, 180, 160))) });
    }

    flachKategorien(cats, 0, tagNr, a.kategorien);

    //  Kategorie-Zugehoerigkeit je DATEI einsammeln (der Baum in derselben
    //  Reihenfolge wie oben - `flachKategorien` laeuft ihn genauso ab).
    QHash<QString, std::vector<std::uint32_t>> katJeDatei;
    {
        std::uint32_t nr = 0;
        std::function<void(const QList<TagCategory>&)> lauf =
            [&](const QList<TagCategory>& baum) {
                for (const TagCategory& c : baum) {
                    const std::uint32_t meine = nr++;
                    for (const QString& f : c.files) katJeDatei[f].push_back(meine);
                    lauf(c.children);
                }
            };
        lauf(cats);
    }

    //  Gleiche Tag-Kombination = ein Eintrag, nachgeschlagen ueber die
    //  Nummernfolge SELBST: der frueher gebaute Textschluessel ("3,17,") kostete
    //  je Datei drei Zeichenketten und deren Hash.
    struct FolgeHash {
        std::size_t operator()(const std::vector<std::uint32_t>& v) const noexcept {
            std::size_t h = 1469598103934665603ULL;
            for (const std::uint32_t n : v) { h ^= n; h *= 1099511628211ULL; }
            return h;
        }
    };
    std::unordered_map<std::vector<std::uint32_t>, std::uint32_t, FolgeHash> satzNr;
    std::vector<std::uint32_t> nummern;
    const auto satzFuer = [&](const QStringList& tags) -> std::uint32_t {
        nummern.clear();
        nummern.reserve(std::size_t(tags.size()));
        for (const QString& t : tags)
            if (const auto it = tagNr.constFind(t); it != tagNr.cend()) nummern.push_back(*it);
        if (nummern.empty()) return kOhneSatz;
        //  NICHT sortieren: die Reihenfolge der Tags an einer Datei ist die, in
        //  der sie vergeben wurden, und genau so stehen die Chips darunter.
        const auto it = satzNr.find(nummern);
        if (it != satzNr.end()) return it->second;
        const std::uint32_t neu = std::uint32_t(a.saetze.size());
        satzNr.emplace(nummern, neu);
        a.saetze.push_back(nummern);
        return neu;
    };

    //  Jede Datei, die irgendetwas traegt: Tags, eigene Textfarbe oder eine
    //  Kategorie. Eine Datei ohne alles kommt nicht in die Ablage.
    //  Zuerst die Eintraege mit Tags oder Farbe, danach die, die NUR ueber eine
    //  Kategorie dabei sind. Frueher lief beides ueber ein `QSet` der Namen -
    //  das hiess je Datei ein Einfuegen und danach noch ein Nachschlagen; im
    //  Profil standen allein dafuer 2,8 % aller Befehle.
    const auto traegtEtwas = [](const FileMeta& m) {
        return !m.tags.isEmpty() || m.textPdfColor.isValid();
    };
    a.dateien.reserve(std::size_t(files.size()) + std::size_t(katJeDatei.size()));
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        if (!traegtEtwas(it.value())) continue;
        Datei d;
        d.name      = utf8(it.key());
        d.satz      = satzFuer(it.value().tags);
        d.textfarbe = alsZahl(it.value().textPdfColor);
        if (const auto k = katJeDatei.constFind(it.key()); k != katJeDatei.cend())
            d.kategorien = *k;
        a.dateien.push_back(std::move(d));
    }
    for (auto it = katJeDatei.cbegin(); it != katJeDatei.cend(); ++it) {
        const auto meta = files.constFind(it.key());
        if (meta != files.cend() && traegtEtwas(*meta)) continue;   // schon drin
        Datei d;
        d.name       = utf8(it.key());
        d.satz       = kOhneSatz;
        d.textfarbe  = kKeineFarbe;
        d.kategorien = it.value();
        a.dateien.push_back(std::move(d));
    }
    return a;
}

//  Umkehrung: den gelesenen Stand in die Behaelter der Klasse bringen.
void JsonStorage::uebernimmAblage(const mg::storage::Ablage& a) {
    m_fileMeta.clear();
    m_tagColors.clear();
    m_categories.clear();

    //  Eine QHash waechst durch Umhaengen ALLER Eintraege - bei 20.000 Dateien
    //  gut ein Dutzend Mal.
    m_fileMeta.reserve(int(a.dateien.size()));
    m_tagColors.reserve(int(a.tags.size()));

    QStringList tagNamen;
    tagNamen.reserve(int(a.tags.size()));
    for (const mg::storage::Tag& t : a.tags) {
        const QString name = ausUtf8(t.name);
        tagNamen.append(name);
        m_tagColors.insert(name, alsFarbe(t.farbe));
    }

    //  Die flache Liste wieder zum Baum machen. `eltern` ist die Nummer der
    //  Mutter + 1; sie steht immer VOR ihrem Kind (der Schreiber laeuft den
    //  Baum von oben ab), ein Zeiger nach hinten kann also nicht entstehen.
    std::vector<TagCategory> flach;
    flach.reserve(a.kategorien.size());
    for (const mg::storage::Kategorie& k : a.kategorien) {
        TagCategory c;
        c.id   = ausUtf8(k.id);
        c.name = ausUtf8(k.name);
        c.color = alsFarbe(k.farbe);
        c.uniformColor           = (k.schalter & 0x01) != 0;
        c.inheritColorToChildren = (k.schalter & 0x02) != 0;
        for (const std::uint32_t n : k.tags)
            if (n < std::uint32_t(tagNamen.size())) c.tags.append(tagNamen.at(int(n)));
        flach.push_back(std::move(c));
    }

    //  Dateien: Tags und Farbe an den Eintrag, Kategorie-Zugehoerigkeit an die
    //  Kategorie - dort, wo der Rest des Programms sie sucht.
    for (const mg::storage::Datei& d : a.dateien) {
        const QString name = ausUtf8(d.name);
        QStringList tags;
        if (d.satz != mg::storage::kOhneSatz && d.satz < a.saetze.size())
            for (const std::uint32_t n : a.saetze[d.satz])
                if (n < std::uint32_t(tagNamen.size())) tags.append(tagNamen.at(int(n)));
        const QColor farbe = alsFarbe(d.textfarbe);
        if (!tags.isEmpty() || farbe.isValid()) {
            FileMeta meta;
            meta.tags = std::move(tags);
            meta.textPdfColor = farbe;
            m_fileMeta.insert(name, std::move(meta));
        }
        for (const std::uint32_t k : d.kategorien)
            if (k < flach.size()) flach[k].files.append(name);
    }

    //  Von hinten nach vorn einhaengen: ein Kind wird an seine Mutter gegeben,
    //  bevor die selbst umzieht.
    for (std::size_t i = flach.size(); i-- > 0;) {
        const std::uint32_t eltern = a.kategorien[i].eltern;
        if (eltern == 0 || eltern > flach.size()) continue;
        flach[eltern - 1].children.prepend(flach[i]);
        flach[i].id.clear();                    // eingehaengt, nicht mehr Wurzel
    }
    for (std::size_t i = 0; i < flach.size(); ++i)
        if (a.kategorien[i].eltern == 0) m_categories.append(flach[i]);
}

//  Die Bytes der Ablage. Leer heisst: es gibt nichts zu speichern, die Datei
//  gehoert geloescht (sonst entstuende allein durch das Oeffnen eines Ordners
//  eine Ablage).
QByteArray JsonStorage::baueSidecar(const QHash<QString, FileMeta>& files,
                                    const QHash<QString, QColor>& colors,
                                    const QList<TagCategory>& cats) {
    const mg::storage::Ablage a = baueAblage(files, colors, cats);
    if (a.leer()) return {};
    const std::string bytes = mg::storage::schreibe(a);
    return QByteArray(bytes.data(), qsizetype(bytes.size()));
}

//  Die fertigen Bytes ablegen. Ohne Inhalt faellt die Datei weg.
//  ATOMAR (QSaveFile): diese Datei ist die EINZIGE Quelle aller Tags und Daten
//  eines Ordners. Mit `open(WriteOnly)` war sie zuerst auf 0 Bytes gekuerzt -
//  Totalverlust.
static bool schreibeSidecar(const QString& path, const QByteArray& bytes) {
    //  Die alte JSON-Ablage desselben Ordners - sie faellt weg, sobald die neue
    //  steht. Erst schreiben, dann loeschen: bricht das Schreiben ab, ist der
    //  alte Stand noch da.
    QString alt = path;
    if (alt.endsWith(QLatin1String(JsonStorage::kEndung)))
        alt.chop(int(qstrlen(JsonStorage::kEndung)));
    alt += QStringLiteral(".json");

    if (bytes.isEmpty()) {
        if (QFile::exists(path)) QFile::remove(path);
        if (QFile::exists(alt))  QFile::remove(alt);
        return true;
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "JsonStorage: kann" << path << "nicht schreiben";
        return false;
    }
    if (f.write(bytes) != bytes.size()) {
        f.cancelWriting();
        qWarning() << "JsonStorage: unvollstaendig geschrieben:" << path;
        return false;
    }
    if (!f.commit()) {
        qWarning() << "JsonStorage: konnte" << path << "nicht abschliessen";
        return false;
    }
    if (QFile::exists(alt)) QFile::remove(alt);
    return true;
}

void JsonStorage::saveFolder(const QString& folderPath) {
    if (folderPath == m_folderPath) {
        m_saveTimer.stop();
        m_savePending = false;
    }
    const QString path = sidecarPath(folderPath);
    //  Ein eigener Schreibvorgang ist unterwegs - dann ist die Datei auf der
    //  Platte nicht "fremd geaendert", sondern gleich unsere eigene.
    if (m_schreibtGerade == 0) mergeForeignChanges(path);

    schreibeSidecar(path, baueSidecar(m_fileMeta, m_tagColors, m_categories));
    noteDiskStamp(path);
    emit folderWritten(folderPath);
}

//  Derselbe Vorgang, aber Bauen und Schreiben laufen im Arbeitsfaden: bei
//  20.000 Dateien sind das 40 ms, die sonst zwischen der Geste und dem
//  naechsten Bild liegen. Der Faden bekommt KOPIEN der drei Behaelter -
//  implizit geteilt, das kostet beim Uebergeben nichts.
void JsonStorage::saveFolderAsync(const QString& folderPath) {
    if (folderPath.isEmpty()) return;
    const QString path = sidecarPath(folderPath);
    if (m_schreibtGerade == 0) mergeForeignChanges(path);

    const QHash<QString, FileMeta> files  = m_fileMeta;
    const QHash<QString, QColor>   colors = m_tagColors;
    const QList<TagCategory>       cats   = m_categories;
    ++m_schreibtGerade;

    JsonStorage* self = this;
    m_savePool.start([self, path, files, colors, cats] {
        schreibeSidecar(path, baueSidecar(files, colors, cats));
        QMetaObject::invokeMethod(self, [self, path] { self->schreibvorgangFertig(path); },
                                  Qt::QueuedConnection);
    });
}

void JsonStorage::schreibvorgangFertig(const QString& path) {
    if (m_schreibtGerade > 0) --m_schreibtGerade;
    noteDiskStamp(path);
    emit folderWritten(QFileInfo(path).absolutePath());
}

namespace {
constexpr quint32 kSnapMagic   = 0x4D47'5447;   // "MGTG"
constexpr quint16 kSnapVersion = 1;

void writeCat(QDataStream& ds, const TagCategory& c) {
    ds << c.id << c.name << c.uniformColor << c.color << c.inheritColorToChildren
       << c.tags << c.files << quint32(c.children.size());
    for (const TagCategory& ch : c.children) writeCat(ds, ch);
}

TagCategory readCat(QDataStream& ds, int depth) {
    TagCategory c;
    quint32 n = 0;
    ds >> c.id >> c.name >> c.uniformColor >> c.color >> c.inheritColorToChildren
       >> c.tags >> c.files >> n;
    //  Der Strom stammt aus dem eigenen Prozess; die Grenze steht trotzdem -
    //  ein beschaedigter Puffer soll den Stapel nicht sprengen.
    if (ds.status() != QDataStream::Ok || depth > 64) return c;
    for (quint32 i = 0; i < n; ++i) {
        if (ds.status() != QDataStream::Ok) break;
        c.children.append(readCat(ds, depth + 1));
    }
    return c;
}
}  // namespace

QByteArray JsonStorage::tagStateSnapshot() const {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setVersion(QDataStream::Qt_6_0);
    ds << kSnapMagic << kSnapVersion;

    ds << quint32(m_fileMeta.size());
    for (auto it = m_fileMeta.cbegin(); it != m_fileMeta.cend(); ++it)
        ds << it.key() << it.value().tags << it.value().textPdfColor;

    ds << quint32(m_tagColors.size());
    for (auto it = m_tagColors.cbegin(); it != m_tagColors.cend(); ++it)
        ds << it.key() << it.value();

    ds << quint32(m_categories.size());
    for (const TagCategory& c : m_categories) writeCat(ds, c);

    //  GEPACKT im Stapel liegen (RAM zuerst). Gemessen am selben Ordner: 578
    //  KB roh -> 78 KB, dafuer 1,9 ms. Stufe 1 und nicht 9 - hoehere Stufen
    //  kosteten deutlich mehr Zeit fuer wenige Prozent.
    return qCompress(out, 1);
}

void JsonStorage::restoreTagState(const QByteArray& snapshot) {
    //  Muell ergibt hier leer, und die Kennung unten faellt dann durch.
    const QByteArray raw = qUncompress(snapshot);
    QDataStream ds(raw);
    ds.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0; quint16 version = 0;
    ds >> magic >> version;
    if (magic != kSnapMagic || version != kSnapVersion) return;

    //  ERST vollstaendig lesen, DANN uebernehmen: bricht der Strom mittendrin
    //  ab, bleibt der bisherige Stand stehen statt halb ueberschrieben zu sein.
    QHash<QString, FileMeta> files;
    QHash<QString, QColor>   colors;
    QList<TagCategory>       cats;

    quint32 n = 0;
    ds >> n;
    for (quint32 i = 0; i < n && ds.status() == QDataStream::Ok; ++i) {
        QString name; FileMeta meta;
        ds >> name >> meta.tags >> meta.textPdfColor;
        files.insert(name, meta);
    }
    ds >> n;
    for (quint32 i = 0; i < n && ds.status() == QDataStream::Ok; ++i) {
        QString tag; QColor c;
        ds >> tag >> c;
        colors.insert(tag, c);
    }
    ds >> n;
    for (quint32 i = 0; i < n && ds.status() == QDataStream::Ok; ++i)
        cats.append(readCat(ds, 0));

    if (ds.status() != QDataStream::Ok) return;
    m_fileMeta   = std::move(files);
    m_tagColors  = std::move(colors);
    m_categories = std::move(cats);
}

void JsonStorage::saveCurrentFolder() {
    if (m_folderPath.isEmpty()) return;
    // Sammeln setzt eine laufende Ereignisschleife voraus - der Null-Timer feuert sonst nie. Ohne sie (Testtreiber,
    // Abbau beim Beenden) wird SOFORT geschrieben; das ist der sichere Fall, nicht der Ausnahmefall.
    if (!m_deferSaves || !QCoreApplication::instance()) {
        saveFolder(m_folderPath);
        return;
    }
    m_savePending = true;
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

//  Der Timer schreibt im Arbeitsfaden - wer wartet, ist niemand: die Geste ist
//  vorbei, das Bild kann gemalt werden. Ohne Ereignisschleife (Testtreiber,
//  Abbau) bleibt es beim sofortigen Schreiben.
void JsonStorage::saveTimerFired() {
    m_savePending = false;
    if (m_folderPath.isEmpty()) return;
    if (QCoreApplication::instance()) saveFolderAsync(m_folderPath);
    else                              saveFolder(m_folderPath);
}

void JsonStorage::flushPendingSave() {
    m_saveTimer.stop();
    if (m_savePending && !m_folderPath.isEmpty()) {
        m_savePending = false;          // VOR dem Schreiben zurücksetzen -
        saveFolder(m_folderPath);       // saveFolder darf nicht erneut anstoßen
    }
    m_savePending = false;
    //  Auf einen laufenden Schreibvorgang WARTEN: wer flusht, will die Datei
    //  gleich lesen (Ordnerwechsel, Beenden, fremder Leser).
    if (m_schreibtGerade > 0) {
        m_savePool.waitForDone();
        //  Der Rueckruf steht noch in der Warteschlange - der Zeitstempel
        //  gehoert aber JETZT gesetzt, sonst gilt die eigene Datei als fremd.
        m_schreibtGerade = 0;
        noteDiskStamp(sidecarPath(m_folderPath));
    }
}

QStringList JsonStorage::getTags(const QString& f) const {
    return m_fileMeta.value(f).tags;
}
void JsonStorage::setTags(const QString& f, const QStringList& tags) {
    m_fileMeta[f].tags = tags;
    for (const auto& t : tags) ensureTagRegistered(t);
}
QColor JsonStorage::textPdfColor(const QString& f) const {
    return m_fileMeta.value(f).textPdfColor;
}
void JsonStorage::setTextPdfColor(const QString& f, const QColor& color) {
    m_fileMeta[f].textPdfColor = color;
}
void JsonStorage::clearTextPdfColor(const QString& f) {
    m_fileMeta[f].textPdfColor = QColor();
}


QColor JsonStorage::tagColor(const QString& tag) const {
    return m_tagColors.value(tag, QColor(100, 180, 160));
}
void JsonStorage::setTagColor(const QString& tag, const QColor& c) {
    m_tagColors[tag] = c;
}
void JsonStorage::ensureTagRegistered(const QString& tag) {
    if (!m_tagColors.contains(tag))
        m_tagColors.insert(tag, randomTagColor());
}
QStringList JsonStorage::allTags() const {
    QStringList list;
    for (auto it = m_tagColors.cbegin(); it != m_tagColors.cend(); ++it)
        list.append(it.key());
    list.sort(Qt::CaseInsensitive);
    return list;
}
QStringList JsonStorage::filesWithTag(const QString& tag) const {
    QStringList out;
    if (tag.isEmpty()) return out;
    for (auto it = m_fileMeta.cbegin(); it != m_fileMeta.cend(); ++it)
        if (it.value().tags.contains(tag)) out.append(it.key());
    out.sort(Qt::CaseInsensitive);
    return out;
}

void JsonStorage::renameTag(const QString& oldName, const QString& newName) {
    if (oldName.isEmpty() || newName.isEmpty() || oldName == newName) return;

    const QColor c = m_tagColors.value(oldName, QColor(100, 180, 160));
    m_tagColors.remove(oldName);
    m_tagColors.insert(newName, c);

    for (auto it = m_fileMeta.begin(); it != m_fileMeta.end(); ++it) {
        const int i = it->tags.indexOf(oldName);
        if (i < 0) continue;
        if (it->tags.contains(newName)) it->tags.removeAt(i);   // kein Duplikat
        else                            it->tags[i] = newName;
    }
}

void JsonStorage::deleteTag(const QString& tag) {
    m_tagColors.remove(tag);
    for (auto it = m_fileMeta.begin(); it != m_fileMeta.end(); ++it)
        it->tags.removeAll(tag);
}

void JsonStorage::applyToItems(QVector<MediaItem>& items) const {
    // Ordner OHNE Sidecar sind der Normalfall - dann gibt es nichts zu übertragen, und `fileName()` wird gar nicht
    // erst gerufen: bei einem Ordner mit 300 Dateien 300 Allokationen weniger.
    if (m_fileMeta.isEmpty()) return;

    for (auto& item : items) {
        auto it = m_fileMeta.constFind(item.fileName());
        if (it == m_fileMeta.constEnd()) continue;
        const FileMeta& meta = *it;
        item.tags = meta.tags;
    }
}

void JsonStorage::renameFile(const QString& oldName, const QString& newName) {
    if (m_fileMeta.contains(oldName))
        m_fileMeta[newName] = m_fileMeta.take(oldName);
}

void JsonStorage::removeFile(const QString& fileName) {
    m_fileMeta.remove(fileName);
}
