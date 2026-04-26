#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// PdfMediaHandler.h
// ══════════════════════════════════════════════════════════════════════════════
//
// Qt6's QPdfDocument has NO public API for annotations or embedded files.
// This class works around that hard limit by reading the raw PDF byte stream
// and parsing:
//
//   /Subtype /Sound   → audio annotation  (existing)
//   /Subtype /Screen  → video annotation  (RichMedia / Rendition action)
//   /Subtype /Movie   → legacy movie annotation
//   /EmbeddedFile     → embedded file streams (audio or video)
//
// Extracted streams are written to the OS temp directory and exposed as
// MediaAnnotation records, each with:
//   - page index
//   - normalised rect [0..1] in page-space (y=0 top)
//   - local file path to the extracted media
//   - media type (Audio / Video / Unknown)
//   - human-readable label
//
// PdfViewer owns one PdfMediaHandler and queries it after every document load.
// ══════════════════════════════════════════════════════════════════════════════

#include <QString>
#include <QRectF>
#include <QVector>
#include <QObject>
#include <QPdfDocument>

// ─────────────────────────────────────────────────────
// MediaAnnotation  –  one clickable media region
// ─────────────────────────────────────────────────────
struct MediaAnnotation {
    enum class Type { Audio, Video, Link, Unknown };

    int     page      = 0;          // 0-based
    QRectF  rect;                   // normalised [0..1], y=0 top
    QString sourcePath;             // local path to extracted temp file (may be empty)
    QString sourceUrl;              // URL from /A dict (for linked media)
    QString label;                  // /Contents or /NM
    Type    type      = Type::Unknown;

    bool hasMedia() const {
        return !sourcePath.isEmpty() || !sourceUrl.isEmpty();
    }

    // Returns the best playback URI
    QString resolvedUri() const {
        if (!sourcePath.isEmpty()) return sourcePath;
        return sourceUrl;
    }
};

// ─────────────────────────────────────────────────────
// PdfMediaHandler
// ─────────────────────────────────────────────────────
class PdfMediaHandler : public QObject {
    Q_OBJECT
public:
    explicit PdfMediaHandler(QPdfDocument* doc, QObject* parent = nullptr);

    // Main entry: scan all pages, populate internal annotation list.
    // Call once after the document is Ready.
    void scanDocument(const QString& pdfPath);

    // Per-page query (0-based)
    QVector<MediaAnnotation> annotationsForPage(int page) const;

    // All annotations across all pages
    const QVector<MediaAnnotation>& allAnnotations() const { return m_annotations; }

    // Cleans up temp files created during this session
    void cleanup();

    // All link annotations (Type::Link) across all pages
    QVector<MediaAnnotation> allLinks() const;

    // Returns true if any media annotation (audio/video) was found
    bool hasMedia() const;

    // Returns true if any link annotation was found
    bool hasLinks() const;

private:
    QPdfDocument*            m_doc = nullptr;
    QVector<MediaAnnotation> m_annotations;
    QStringList              m_tempFiles;   // for cleanup
    QString                  m_pdfPath;

    // ── Raw PDF parsing helpers ──────────────────────
    void parseAnnotations(const QByteArray& data);
    void parseOneAnnotation(const QByteArray& data, qsizetype hitPos,
                            const QByteArray& subtypeTag);
    void parseLinkAnnotations(const QByteArray& data);
    bool extractEmbeddedStream(const QByteArray& data, qsizetype searchFrom,
                               MediaAnnotation& ann);
    void resolveRichMediaUrl(const QByteArray& data, qsizetype searchFrom,
                             MediaAnnotation& ann);

    // ── Utilities ────────────────────────────────────
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
