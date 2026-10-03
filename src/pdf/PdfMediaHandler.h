#pragma once
// QPdfDocument bietet keine API fuer Annotationen oder eingebettete Dateien; hier wird
// der Rohstrom nach /Sound, /Screen, /Movie und /Link durchsucht. Extrahierte
// Stroeme landen im Temp-Verzeichnis, je Treffer Seite, normiertes Rechteck und Pfad.

#include <QString>
#include <QRectF>
#include <QVector>
#include <QObject>
#include <QPdfDocument>

// MediaAnnotation  –  one clickable media region
struct MediaAnnotation {
    enum class Type { Audio, Video, Link, Unknown };

    int     page      = 0;          // 0-based
    QRectF  rect;                   // normalised [0..1], y=0 top
    QString sourcePath;             // local path to extracted temp file (may be empty)
    QString sourceUrl;              // URL from /A dict (for linked media)
    QString label;                  // /Contents or /NM
    Type    type      = Type::Unknown;
    int     targetPage = -1;        // Verweis innerhalb der Datei: Zielseite (0-basiert), sonst -1

    QString resolvedUri() const {
        if (!sourcePath.isEmpty()) return sourcePath;
        return sourceUrl;
    }
};

class PdfMediaHandler : public QObject {
    Q_OBJECT
public:
    explicit PdfMediaHandler(QPdfDocument* doc, QObject* parent = nullptr);

    // Main entry: scan all pages, populate internal annotation list.
    // Call once after the document is Ready.
    void scanDocument(const QString& pdfPath);

    const QVector<MediaAnnotation>& allAnnotations() const { return m_annotations; }

    // In scanDocument() angelegte Temp-Dateien (extrahierte Medienstreams).
    // Erlaubt dem Aufrufer das Aufraeumen, OHNE den Handler am Leben zu halten.
    const QStringList& tempFiles() const { return m_tempFiles; }

    // Endung zu den ersten Bytes eines Stroms; leer, wenn unbekannt oder gepackt.
    static QString guessMimeExt(const QByteArray& header);

private:
    struct Scan;
    void parseLink(const Scan& s, int num, const QByteArray& dict);
    void parseMedia(const Scan& s, int num, const QByteArray& dict, bool isVideo);
    bool extractStream(const QByteArray& bytes, MediaAnnotation& ann);
    QSizeF pageSize(const Scan& s, int page) const;

    QPdfDocument*            m_doc = nullptr;
    QVector<MediaAnnotation> m_annotations;
    QStringList              m_tempFiles;   // for cleanup
    QString                  m_pdfPath;
};
