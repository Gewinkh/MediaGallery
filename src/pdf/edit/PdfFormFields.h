#pragma once
// Liest und füllt AcroForm-Felder (bisher wurden die Widgets nur mitgerendert); sie gehören dem
// DOKUMENT, nicht dem Editor, stehen also nicht im Sidecar. /AP /N wird selbst erzeugt -
// /NeedAppearances ist nur eine Bitte, die PDFium und fast jeder Druckweg ignorieren.

#include <QHash>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mg {

//  Feldart. `Push` (Druckknopf) ist nur der Vollständigkeit halber dabei -
//  er trägt keinen Wert und wird beim Schreiben übersprungen.
enum class PdfFieldType { Unknown = 0, Text, Checkbox, Radio, Choice, Push };

// EIN Feld, bereits auf EINE Widget-Annotation heruntergebrochen: ein Optionsfeld mit drei Knöpfen liefert drei
// Einträge mit demselben `name`, aber je eigenem `rect`/`page`/`onState`.
struct PdfFormField {
    QString      name;                 // vollständiger Name ("Adresse.Ort")
    QString      tooltip;              // /TU (Kurzhilfe), sonst leer
    PdfFieldType type = PdfFieldType::Unknown;

    int    page = -1;                  // Seitenindex des Widgets (−1 = nicht platziert)
    QRectF rect;                       // PDF-Punkte, Ursprung OBEN-LINKS der

    QString     value;                 // aktueller Wert: Text bzw. Zustandsname
    QString     onState;               // Checkbox/Radio: der Name DIESES Knopfes
    QStringList options;               // Auswahl: Anzeigetexte
    QStringList optionValues;          // Auswahl: zugehörige Exportwerte

    bool readOnly  = false;            // /Ff Bit 1
    bool required  = false;            // /Ff Bit 2
    bool multiline = false;            // /Ff Bit 13  (nur /Tx)
    bool password  = false;            // /Ff Bit 14  (nur /Tx)
    bool combo     = false;            // /Ff Bit 18  (nur /Ch)
    bool editable  = false;            // /Ff Bit 19  (nur /Ch: freie Eingabe)
    int  maxLen    = -1;               // /MaxLen (−1 = unbegrenzt)

    int fieldObj  = -1;                // Objektnummer des Feld-Dicts
    int widgetObj = -1;                // Objektnummer der Widget-Annotation
};

class PdfFormFields {
public:
    //  Liest alle ausfüllbaren Felder von `path`. Liefert false, wenn die Datei
    //  nicht lesbar/kein PDF/verschlüsselt ist. Ein PDF OHNE Formular ist KEIN
    //  Fehler: Rückgabe true mit leerer Liste (`err` bleibt leer).
    static bool read(const QString& path, QVector<PdfFormField>* out,
                     QString* err = nullptr);

    // `values`: Text/Auswahl -> der EXPORTWERT, Ankreuzfeld -> der `onState` bzw. "Off".
    // Unbekannte Namen und `readOnly`-Felder bleiben unberührt; false lässt `outputPath` ungeschrieben.
    static bool fillAndSave(const QString& inputPath, const QString& outputPath,
                            const QHash<QString, QString>& values,
                            QString* err = nullptr);

    // Schreibt jedes Widget in den Seiteninhalt (gezeichnet wird sein vorhandener /AP /N). Nötig vor
    // jedem Seitenumbau: PdfAssembler baut einen neuen Katalog und kann /AcroForm nicht mitnehmen - die
    // Kopie behielte Widgets ohne Formular, und Qt PDF zeichnet Widgets nie, die Seite bliebe leer.
    static bool flatten(const QString& inputPath, const QString& outputPath,
                        QString* err = nullptr);
};

} // namespace mg
