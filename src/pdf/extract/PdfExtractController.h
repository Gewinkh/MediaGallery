#pragma once
// QML-Singleton "PdfExtract": extrahiert Seiten in eine neue Datei, aus der offenen PDF und global aus allen
// PDFs des Ordners. Primär verlustfrei über PdfPageCopier; scheitert eine Quelle, rastert der Worker NUR DEREN
// Seiten als JPEG in dieselbe Ausgabe - bestmöglich, aber garantiert ein Ergebnis.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>
#include <QThreadPool>
#include <atomic>
#include <memory>

class PdfExtractController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit PdfExtractController(QObject* parent = nullptr);
    ~PdfExtractController() override;

    // Raster-Fallback: Parität zum Editor-Export (PdfEditController 150 dpi).
    static constexpr qreal kRasterDpi         = 150.0;
    static constexpr int   kRasterJpegQuality = 85;
    // Kantenschutz beim Fallback-Rendern extrem großer Seiten (RAM-Deckel).
    static constexpr int   kRasterMaxPx       = 8000;

    bool busy() const { return m_busy; }

    Q_INVOKABLE QString defaultSingleName(const QString& pathOrUrl,
                                          int pageIndex) const;
    Q_INVOKABLE QString defaultMultiName(const QString& pathOrUrl) const;

    Q_INVOKABLE void extractSingle(const QString& pathOrUrl, int page,
                                   const QString& baseName);
    Q_INVOKABLE void extractSelection(const QString& pathOrUrl,
                                      const QVariantList& pages,
                                      const QString& baseName);
    // Global: jobs = [{path, pages:[…]}, …] in Ordnerreihenfolge; Ziel-Ordner
    // explizit; baseName ist Pflicht (QML erzwingt, hier defensiv geprüft).
    Q_INVOKABLE void extractGlobal(const QVariantList& jobs,
                                   const QString& folderOrUrl,
                                   const QString& baseName);
    // `items` = [{path, page}] in EXAKT der Ausgabereihenfolge; aufeinanderfolgende Seiten derselben Quelle werden
    // zusammengefasst, Duplikate verworfen. Leerer `baseName` -> Default aus der ersten Quelle.
    Q_INVOKABLE void extractOrdered(const QVariantList& items,
                                    const QString& folderOrUrl,
                                    const QString& baseName);

    Q_INVOKABLE void scanFolder(const QString& folderOrUrl);

    void extractTaskFinished(bool ok, const QString& target,
                             const QString& error, int generation);
    void extractTaskProgress(int done, int total, int generation);
    void scanTaskFinished(const QVariantList& files, int generation);

signals:
    void busyChanged();
    // ok=true: targetPath = erzeugte Datei. QML aktualisiert danach die Galerie
    // (App.refreshCurrentFolder) und zeigt die Statusmeldung.
    void extractFinished(bool ok, const QString& targetPath,
                         const QString& errorText);
    void extractProgress(int done, int total);
    void folderPdfsReady(const QVariantList& files);

private:
    struct Job {
        QString      path;
        QVector<int> pages;   // 0-basiert, aufsteigend (Originalreihenfolge)
    };

    void    startExtract(QVector<Job> jobs, const QString& targetPath);
    void    setBusy(bool b);
    static QString makeTargetPath(const QString& folder, QString base,
                                  const QString& fallbackBase);
    static QVector<int> normalizePages(const QVariantList& pages);

    QThreadPool m_pool;                                   // 1 Worker (RAM-Deckel)
    std::shared_ptr<std::atomic<bool>> m_cancel;          // kooperativer Abbruch
    int  m_generation = 0;                                // veraltete Tasks filtern
    bool m_busy       = false;
};
