#pragma once
// PdfDocShare - die QPdfDocument-Instanz der Ansicht fuer alle Leser derselben Datei; laedt selbst nur im Rueckfall.

#include <QString>
#include <memory>

class QObject;
class QPdfDocument;

namespace PdfDocShare {

struct Entry;

// Haelt ein Dokument am Leben, geliehen oder selbst geladen. Beliebiger Faden; PDFium sperrt in QtPdf global.
class Lease {
public:
    Lease() = default;
    Lease(Lease&&) noexcept;
    Lease& operator=(Lease&&) noexcept;
    ~Lease();

    QPdfDocument* doc() const;
    bool borrowed() const { return m_entry != nullptr; }
    explicit operator bool() const { return doc() != nullptr; }
    void reset();

    // Ein selbst geladenes Dokument, z. B. nach moveToThread.
    static Lease adopt(std::unique_ptr<QPdfDocument> own);

private:
    friend Lease borrow(const QString&);
    std::shared_ptr<Entry>        m_entry;
    std::unique_ptr<QPdfDocument> m_own;
};

// GUI-Faden. Laedt das innere Dokument eines QML-PdfDocument aus `localPath` - ueber eine Sicht, in der Verweise auf
// Ton- und eingebettete Dateistroeme `null` sind - und bietet es an. `source` traegt nur einen Platzhalter, den
// PdfPageImage braucht. false, wenn die Datei nicht ladbar ist (Status dann Error).
bool load(QObject* quickDoc, const QString& localPath);

// GUI-Faden, statt destroy(): PdfPageImage liest im Bildfaden von Qt Quick ueber einen Traeger des QML-Dokuments, den
// Qt dort nicht festhaelt. Die Huelle bleibt bis zum Ende der Engine, das innere Dokument wird geschlossen (nach einer
// Probe durch denselben Faden, sobald keine Leihe mehr laeuft) und nicht mehr verliehen.
void retire(QObject* quickDoc);

// Leer, wenn keins angeboten ist oder die Datei sich seit dem Anbieten geaendert hat.
Lease borrow(const QString& localPath);

// Geliehen, sonst selbst geladen (Affinitaet: der aufrufende Faden); leer, wenn die Datei nicht ladbar ist.
Lease open(const QString& localPath);

} // namespace PdfDocShare
