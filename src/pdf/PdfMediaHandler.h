#pragma once
// QPdfDocument bietet keine API fuer Annotationen oder eingebettete Dateien; hier wird
// der Rohstrom nach /Sound, /Screen, /Movie und /EmbeddedFile durchsucht. Extrahierte
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

private:
    QPdfDocument*            m_doc = nullptr;
    QVector<MediaAnnotation> m_annotations;
    QStringList              m_tempFiles;   // for cleanup
    QString                  m_pdfPath;

    void parseAnnotations(const QByteArray& data);
    void parseOneAnnotation(const QByteArray& data, qsizetype hitPos,
                            const QByteArray& subtypeTag);
    void parseLinkAnnotations(const QByteArray& data);
    bool extractEmbeddedStream(const QByteArray& data, qsizetype searchFrom,
                               MediaAnnotation& ann);
    void resolveRichMediaUrl(const QByteArray& data, qsizetype searchFrom,
                             MediaAnnotation& ann);

    static QRectF     parseNormalisedRect(const QByteArray& rectBytes,
                                          const QSizeF& pagePointSize);
    static QByteArray dictValue(const QByteArray& dict, const QByteArray& key);
    static QVector<qsizetype> findAll(const QByteArray& data,
                                      const QByteArray& pattern);
    static QString    guessMimeExt(const QByteArray& header);
    static MediaAnnotation::Type detectType(const QByteArray& subtype,
                                            const QString& ext);
    int resolvePageIndex(const QByteArray& data, const QByteArray& pageRef) const;
};
