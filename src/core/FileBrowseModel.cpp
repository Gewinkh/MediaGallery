#include "core/FileBrowseModel.h"

#include <QCollator>
#include <QDir>
#include "core/PathUtils.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QRunnable>
#include <QStandardPaths>

#include <algorithm>
#include <QRegularExpression>
#include <functional>

namespace {

//  Ein Verzeichnis lesen - im Worker. Liefert das Ergebnis über eine
//  QueuedConnection zurück in den GUI-Thread (Hausmuster).
class ScanTask : public QRunnable {
public:
    ScanTask(FileBrowseModel* owner, QString dir, QStringList globs, bool hidden, bool showAll,
             bool dirsOnly, quint64 gen, std::shared_ptr<std::atomic<bool>> cancel,
             std::function<void(std::vector<FileBrowseModel::Row>, quint64)> sink)
        : m_owner(owner), m_dir(std::move(dir)), m_globs(std::move(globs)),
          m_hidden(hidden), m_showAll(showAll), m_dirsOnly(dirsOnly), m_gen(gen),
          m_cancel(std::move(cancel)), m_sink(std::move(sink)) {
        setAutoDelete(true);
    }
    void run() override;

private:
    FileBrowseModel* m_owner;
    QString          m_dir;
    QStringList      m_globs;
    bool             m_hidden;
    bool             m_showAll;
    bool             m_dirsOnly;
    quint64          m_gen;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    std::function<void(std::vector<FileBrowseModel::Row>, quint64)> m_sink;
};

} // namespace

void ScanTask::run() {
    std::vector<FileBrowseModel::Row> out;
    QDir::Filters f = QDir::AllDirs | QDir::NoDotAndDotDot;
    if (!m_dirsOnly) f |= QDir::Files;
    if (m_hidden)    f |= QDir::Hidden;

    QDirIterator it(m_dir, f, QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        if (m_cancel && m_cancel->load()) return;      // Verzeichnis gewechselt
        it.next();
        const QFileInfo fi = it.fileInfo();
        FileBrowseModel::Row r;
        r.isDir = fi.isDir();
        //  Begleitdateien der App ausblenden - dieselbe Regel wie in der Galerie.
        if (!r.isDir && !m_showAll && mg::isCompanionFile(fi.fileName()))
            continue;
        //  Der Namensfilter gilt NUR für Dateien - ein Ordner muss immer
        //  betretbar bleiben, sonst käme man nirgendwo hin.
        if (!r.isDir && !m_globs.isEmpty()) {
            bool hit = false;
            for (const QString& g : m_globs) {
                const QRegularExpression re(
                    QRegularExpression::wildcardToRegularExpression(
                        g, QRegularExpression::UnanchoredWildcardConversion),
                    QRegularExpression::CaseInsensitiveOption);
                if (re.match(fi.fileName()).hasMatch()) { hit = true; break; }
            }
            if (!hit) continue;
        }
        r.name  = fi.fileName();
        r.size  = r.isDir ? 0 : fi.size();
        r.mtime = fi.lastModified();
        out.push_back(std::move(r));
    }
    if (m_cancel && m_cancel->load()) return;

    //  Sortiert wird im MODELL (`sortRows`) - dort steht die eingestellte
    //  Reihenfolge, und ein Umschalten der Spalte darf das Verzeichnis nicht
    //  erneut lesen müssen.

    FileBrowseModel* owner = m_owner;
    const quint64 gen = m_gen;
    auto sink = m_sink;
    auto rows = std::make_shared<std::vector<FileBrowseModel::Row>>(std::move(out));
    QMetaObject::invokeMethod(owner, [sink, rows, gen]() {
        sink(std::move(*rows), gen);
    }, Qt::QueuedConnection);
}


FileBrowseModel::FileBrowseModel(QObject* parent) : QAbstractListModel(parent) {
    m_pool.setMaxThreadCount(1);
    m_folder = QDir::homePath();
}

FileBrowseModel::~FileBrowseModel() {
    if (m_cancel) m_cancel->store(true);
    m_pool.waitForDone();
}

int FileBrowseModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant FileBrowseModel::data(const QModelIndex& index, int role) const {
    const int r = index.row();
    if (r < 0 || r >= int(m_rows.size())) return {};
    const Row& row = m_rows[size_t(r)];
    switch (role) {
    case NameRole:  return row.name;
    case IsDirRole: return row.isDir;
    case PathRole:  return join(m_folder, row.name);
    case SizeTextRole:
        return row.isDir ? QString()
                         : QLocale().formattedDataSize(row.size, 1,
                                                       QLocale::DataSizeTraditionalFormat);
    case DateTextRole:
        return row.mtime.isValid()
                   ? QLocale().toString(row.mtime, QLocale::ShortFormat) : QString();
    default: return {};
    }
}

QHash<int, QByteArray> FileBrowseModel::roleNames() const {
    return { { NameRole, "name" }, { IsDirRole, "isDir" }, { PathRole, "path" },
             { SizeTextRole, "sizeText" }, { DateTextRole, "dateText" } };
}

void FileBrowseModel::setFolder(const QString& path) {
    QString p = QDir::cleanPath(path.isEmpty() ? QDir::homePath() : path);
    if (!QFileInfo(p).isDir()) return;              // Unsinn nicht übernehmen
    if (p == m_folder && !m_rows.empty()) return;
    m_folder = p;
    emit folderChanged();
    startLoad();
}

void FileBrowseModel::setNameFilters(const QStringList& f) {
    if (f == m_filters) return;
    m_filters = f;
    emit nameFiltersChanged();
    startLoad();
}

void FileBrowseModel::setShowAllFiles(bool v) {
    if (m_showAllFiles == v)
        return;
    m_showAllFiles = v;
    emit showAllFilesChanged();
    startLoad();
}

void FileBrowseModel::setShowHidden(bool v) {
    if (v == m_showHidden) return;
    m_showHidden = v;
    emit showHiddenChanged();
    startLoad();
}

void FileBrowseModel::setDirsOnly(bool v) {
    if (v == m_dirsOnly) return;
    m_dirsOnly = v;
    emit dirsOnlyChanged();
    startLoad();
}

bool FileBrowseModel::canGoUp() const {
    QDir d(m_folder);
    return !d.isRoot();
}

void FileBrowseModel::cdUp() {
    QDir d(m_folder);
    if (d.isRoot() || !d.cdUp()) return;
    setFolder(d.absolutePath());
}

void FileBrowseModel::reload() { startLoad(); }

void FileBrowseModel::startLoad() {
    //  Laufenden Lauf abbestellen - sein Ergebnis wird über die Generation
    //  ohnehin verworfen, aber ein Netzverzeichnis soll nicht weiterlesen.
    if (m_cancel) m_cancel->store(true);
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    ++m_gen;
    if (!m_loading) { m_loading = true; emit loadingChanged(); }

    QStringList globs;
    for (const QString& f : m_filters) globs += globsOf(f);
    globs.removeAll(QStringLiteral("*"));           // „alles" = kein Filter

    auto sink = [this](std::vector<Row> rows, quint64 gen) {
        applyRows(std::move(rows), gen);
    };
    m_pool.start(new ScanTask(this, m_folder, globs, m_showHidden, m_showAllFiles, m_dirsOnly,
                              m_gen, m_cancel, sink));
}

void FileBrowseModel::applyRows(std::vector<Row> rows, quint64 gen) {
    if (gen != m_gen) return;                       // veralteter Lauf
    sortRows(rows);                                 // eingestellte Reihenfolge
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(rows.size());
    for (Row& r : rows)
        m_rows.push_back(std::move(r));
    endResetModel();
    emit countChanged();
    if (m_loading) { m_loading = false; emit loadingChanged(); }
}

QString FileBrowseModel::entryPath(int row) const {
    if (row < 0 || row >= int(m_rows.size())) return {};
    return join(m_folder, m_rows[size_t(row)].name);
}

bool FileBrowseModel::entryIsDir(int row) const {
    if (row < 0 || row >= int(m_rows.size())) return false;
    return m_rows[size_t(row)].isDir;
}

QVariantList FileBrowseModel::places() const {
    QVariantList out;
    auto add = [&out](const QString& name, const QString& path) {
        if (path.isEmpty() || !QFileInfo(path).isDir()) return;
        for (const QVariant& v : out)                       // keine Dubletten
            if (v.toMap().value(QStringLiteral("path")).toString() == path) return;
        out.append(QVariantMap{ { QStringLiteral("name"), name },
                                { QStringLiteral("path"), path } });
    };
    add(QDir(QDir::homePath()).dirName(), QDir::homePath());
    const struct { QStandardPaths::StandardLocation loc; } kPlaces[] = {
        { QStandardPaths::DesktopLocation },  { QStandardPaths::DocumentsLocation },
        { QStandardPaths::DownloadLocation }, { QStandardPaths::PicturesLocation },
        { QStandardPaths::MusicLocation },    { QStandardPaths::MoviesLocation },
    };
    for (const auto& p : kPlaces) {
        const QString path = QStandardPaths::writableLocation(p.loc);
        add(QStandardPaths::displayName(p.loc), path);
    }
    add(QDir::rootPath(), QDir::rootPath());
    return out;
}

QVariantList FileBrowseModel::crumbs() const {
    QVariantList out;
    QDir d(m_folder);
    QStringList parts;
    QString cur = d.absolutePath();
    while (true) {
        parts.prepend(cur);
        QDir up(cur);
        if (up.isRoot() || !up.cdUp()) break;
        cur = up.absolutePath();
    }
    for (const QString& p : parts) {
        const QDir dd(p);
        out.append(QVariantMap{
            { QStringLiteral("name"), dd.isRoot() ? p : dd.dirName() },
            { QStringLiteral("path"), p } });
    }
    return out;
}

QString FileBrowseModel::join(const QString& dir, const QString& name) const {
    if (name.isEmpty()) return dir;
    if (QDir::isAbsolutePath(name)) return QDir::cleanPath(name);
    return QDir::cleanPath(dir + QLatin1Char('/') + name);
}

QString FileBrowseModel::dirOf(const QString& path) const {
    return QFileInfo(path).absolutePath();
}

QString FileBrowseModel::baseName(const QString& path) const {
    return QFileInfo(path).fileName();
}

bool FileBrowseModel::fileExists(const QString& path) const {
    return QFileInfo::exists(path) && !QFileInfo(path).isDir();
}

bool FileBrowseModel::dirExists(const QString& path) const {
    return QFileInfo(path).isDir();
}

void FileBrowseModel::sortRows(std::vector<Row>& rows) const {
    //  `QCollator` numerisch: „Bild10" steht hinter „Bild9", Umlaute an ihrer
    //  Stelle. Ordner bleiben IMMER vorn - auch absteigend; sonst müsste man
    //  zum Hochgehen erst durch alle Dateien scrollen.
    QCollator coll;
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    const int  key  = m_sortKey;
    const bool desc = m_sortDesc;
    std::sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
        if (a.isDir != b.isDir) return a.isDir;
        int c = 0;
        if (key == SortSize)      c = (a.size < b.size) ? -1 : (a.size > b.size ? 1 : 0);
        else if (key == SortDate) c = (a.mtime < b.mtime) ? -1 : (a.mtime > b.mtime ? 1 : 0);
        if (c == 0) c = coll.compare(a.name, b.name);   // Gleichstand -> Name
        return desc ? c > 0 : c < 0;
    });
}

void FileBrowseModel::setSortKey(int k) {
    const int v = qBound(int(SortName), k, int(SortDate));
    if (v == m_sortKey) return;
    m_sortKey = v;
    beginResetModel();
    sortRows(m_rows);
    endResetModel();
    emit sortChanged();
}

void FileBrowseModel::setSortDescending(bool d) {
    if (d == m_sortDesc) return;
    m_sortDesc = d;
    beginResetModel();
    sortRows(m_rows);
    endResetModel();
    emit sortChanged();
}

int FileBrowseModel::createFolder(const QString& name) {
    const QString clean = name.trimmed();
    //  Nur ein NAME, kein Pfad: ein Trenner oder „.." würde aus dem aktuellen
    //  Verzeichnis herausführen - der Wähler soll aber genau dort anlegen.
    if (clean.isEmpty() || clean == QLatin1String(".") || clean == QLatin1String("..")
        || clean.contains(QLatin1Char('/')) || clean.contains(QLatin1Char('\\')))
        return 1;
    if (m_folder.isEmpty()) return 3;

    QDir dir(m_folder);
    //  `exists` deckt Datei UND Ordner ab: ein gleichnamiger Eintrag jeder Art
    //  verhindert das Anlegen, und ein „gibt es schon" ist die ehrlichere
    //  Meldung als ein Fehlschlag ohne Grund.
    if (QFileInfo::exists(dir.filePath(clean))) return 2;
    if (!dir.mkdir(clean)) return 3;

    reload();
    return 0;
}

QString FileBrowseModel::withSuffix(const QString& name, const QString& suffix) const {
    if (name.isEmpty() || suffix.isEmpty()) return name;
    if (!QFileInfo(name).suffix().isEmpty()) return name;
    return name + QLatin1Char('.') + suffix;
}

QUrl FileBrowseModel::toUrl(const QString& path) const {
    return QUrl::fromLocalFile(path);
}

QString FileBrowseModel::fromUrl(const QUrl& url) const {
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

//  "Bilder (*.png *.jpg)" -> ["*.png", "*.jpg"]. Ohne Klammern gilt der ganze
//  Text als EIN Muster (so schreiben es manche Aufrufstellen).
QStringList FileBrowseModel::globsOf(const QString& filterText) const {
    const int a = filterText.lastIndexOf(QLatin1Char('('));
    const int b = filterText.lastIndexOf(QLatin1Char(')'));
    const QString inner = (a >= 0 && b > a) ? filterText.mid(a + 1, b - a - 1)
                                            : filterText;
    QStringList out;
    for (const QString& g : inner.split(QRegularExpression(QStringLiteral("[\\s;]+")),
                                        Qt::SkipEmptyParts))
        out.append(g.trimmed());
    return out;
}
