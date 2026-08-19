#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfFormFields.h - AcroForm-Formularfelder LESEN und AUSFÜLLEN
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  Ein interaktives PDF-Formular besteht aus dem Katalog-Eintrag
//  `/Root -> /AcroForm -> /Fields` und je Feld einer oder mehreren
//  Widget-Annotationen, die auf einer Seite liegen. Bisher wurden diese Widgets
//  nur MITGERENDERT (PDFium zeichnet ihr Erscheinungsbild) - ausfüllbar waren
//  sie nicht. Diese Einheit liest die Felder aus und schreibt neue Werte
//  zurück.
//
//  ABGRENZUNG ZU PdfEditModel
//  ──────────────────────────
//  Formularfelder gehören dem DOKUMENT, nicht dem Editor. Sie stehen deshalb
//  bewusst NICHT im Sidecar (`PdfEditModel` = eigene Notizen des Nutzers),
//  sondern werden aus der Datei gelesen und in die Datei geschrieben.
//
//  VERFAHREN BEIM SCHREIBEN: INKREMENTELLES UPDATE (append-only)
//  ────────────────────────────────────────────────────────────
//  Wie `PdfVectorExport`/`PdfContentEditor`: die Originalbytes bleiben 1:1
//  erhalten, angehängt werden nur die geänderten Feld-/Widget-Objekte, die neu
//  erzeugten Erscheinungsbilder und eine kleine XRef-Sektion mit `/Prev`.
//  Ein Fehlschlag schreibt NICHTS (kein Fragment).
//
//  ERSCHEINUNGSBILD: `/AP` WIRD SELBST ERZEUGT - NICHT `/NeedAppearances`
//  ─────────────────────────────────────────────────────────────────────
//  Es gibt zwei Wege, einen neuen Feldwert sichtbar zu machen:
//   (a) `/NeedAppearances true` setzen - der Betrachter soll das Aussehen
//       selbst erzeugen. Ein Zweizeiler, aber nur eine BITTE: PDFium (und damit
//       unsere eigene Anzeige über Qt PDF), viele mobile Betrachter und fast
//       jeder Druckweg erzeugen nichts nach - der Wert stünde in der Datei und
//       wäre trotzdem unsichtbar.
//   (b) je Widget einen `/AP /N`-Formstrom schreiben - mehr Arbeit, aber das
//       Ergebnis ist in JEDEM Betrachter und im Druck sichtbar.
//  Gewählt ist (b) (§0-Priorität 1: Funktionalität & Korrektheit vor
//  Code-Komplexität, Prio 9). `/NeedAppearances true` wird NUR als Notnagel
//  gesetzt, wenn für mindestens ein Feld kein Erscheinungsbild erzeugt werden
//  konnte (Text in der Kodierung der Formularschrift nicht darstellbar) -
//  dann ist die Bitte an den Betrachter besser als sichtbar nichts.
//  Ankreuzfelder/Optionsfelder brauchen ohnehin keinen neuen Strom: ihre
//  Zustände liegen bereits als `/AP /N /<Name>` in der Datei, gesetzt wird nur
//  `/AS`.
//
//  BEWUSST BEGRENZT (Rückgabe false -> Aufrufer meldet, dass nicht geht)
//  ───────────────────────────────────────────────────────────────────
//   • unverschlüsselt (kein /Encrypt), klassische xref-Tabelle (kein
//     XRef-Stream) - dieselbe Zusage wie bei den Schwester-Einheiten,
//   • Feldtypen /Tx (Text), /Btn (Ankreuz-/Optionsfeld) und /Ch (Auswahl);
//     Druckknöpfe werden gelesen, aber nie beschrieben,
//   • Unterschriftenfelder (/Sig) werden ÜBERGANGEN - weder gelesen noch
//     angezeigt noch beschrieben: sie zu füllen ist Kryptografie (Zertifikat,
//     Byte-Range-Digest, CMS-Container), nicht Parser-Arbeit. Ein leeres Feld
//     anzuzeigen, das man nie ausfüllen kann, hilft niemandem.
//
//  ABHÄNGIGKEITEN: Qt6::Core + ZLIB. Kein Q_OBJECT/moc; isoliert testbar.
// ══════════════════════════════════════════════════════════════════════════════

#include <QHash>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mg {

//  Feldart. `Push` (Druckknopf) ist nur der Vollständigkeit halber dabei -
//  er trägt keinen Wert und wird beim Schreiben übersprungen.
enum class PdfFieldType { Unknown = 0, Text, Checkbox, Radio, Choice, Push };

//  EIN ausfüllbares Feld, bereits auf EINE Widget-Annotation heruntergebrochen:
//  Ein Optionsfeld (Radio) mit drei Knöpfen liefert drei Einträge mit demselben
//  `name`, aber je eigenem `rect`/`page`/`onState`. Das passt zur Anzeige (je
//  Knopf eine Fläche) und macht die Zuordnung beim Schreiben eindeutig.
struct PdfFormField {
    QString      name;                 // vollständiger Name ("Adresse.Ort")
    QString      tooltip;              // /TU (Kurzhilfe), sonst leer
    PdfFieldType type = PdfFieldType::Unknown;

    int    page = -1;                  // Seitenindex des Widgets (−1 = nicht platziert)
    QRectF rect;                       // PDF-Punkte, Ursprung OBEN-LINKS der
                                       // ANGEZEIGTEN Seite (wie der ganze Editor)

    QString     value;                 // aktueller Wert: Text bzw. Zustandsname
                                       // ("Off" = nicht angekreuzt)
    QString     onState;               // Checkbox/Radio: der Name DIESES Knopfes
                                       // ("/Off" ausgenommen), sonst leer
    QStringList options;               // Auswahl: Anzeigetexte
    QStringList optionValues;          // Auswahl: zugehörige Exportwerte

    bool readOnly  = false;            // /Ff Bit 1
    bool required  = false;            // /Ff Bit 2
    bool multiline = false;            // /Ff Bit 13  (nur /Tx)
    bool password  = false;            // /Ff Bit 14  (nur /Tx)
    bool combo     = false;            // /Ff Bit 18  (nur /Ch)
    bool editable  = false;            // /Ff Bit 19  (nur /Ch: freie Eingabe)
    int  maxLen    = -1;               // /MaxLen (−1 = unbegrenzt)

    //  Innerei - nur für das Zurückschreiben interessant.
    int fieldObj  = -1;                // Objektnummer des Feld-Dicts
    int widgetObj = -1;                // Objektnummer der Widget-Annotation
                                       // (== fieldObj bei verschmolzenen Feldern)
};

class PdfFormFields {
public:
    //  Liest alle ausfüllbaren Felder von `path`. Liefert false, wenn die Datei
    //  nicht lesbar/kein PDF/verschlüsselt ist. Ein PDF OHNE Formular ist KEIN
    //  Fehler: Rückgabe true mit leerer Liste (`err` bleibt leer).
    static bool read(const QString& path, QVector<PdfFormField>* out,
                     QString* err = nullptr);

    //  Schreibt `outputPath` (atomar) mit den neuen Werten; `inputPath` bleibt
    //  unangetastet. Beide Pfade dürfen NICHT gleich sein.
    //
    //  `values` bildet den vollständigen Feldnamen auf den neuen Wert ab:
    //   • Text/Auswahl -> der Text selbst (bei Auswahl der EXPORTWERT),
    //   • Ankreuz-/Optionsfeld -> der Zustandsname ohne Schrägstrich, also der
    //     `onState` des gewünschten Knopfes bzw. "Off" zum Abwählen.
    //  Namen, die es im Dokument nicht gibt, werden ignoriert; schreibgeschützte
    //  Felder (`readOnly`) werden NICHT verändert.
    //
    //  Liefert false, wenn eine Vorbedingung nicht sicher erfüllt ist - dann
    //  bleibt `outputPath` ungeschrieben. `err` erhält einen kurzen Grund.
    static bool fillAndSave(const QString& inputPath, const QString& outputPath,
                            const QHash<QString, QString>& values,
                            QString* err = nullptr);

    //  FESTSCHREIBEN: schreibt `outputPath` (atomar), in dem jedes Widget als
    //  Teil des SEITENINHALTS steht statt als Annotation - aus dem Formular
    //  wird ein gewöhnliches Dokument. Nötig, sobald die Datei anschließend
    //  umgebaut wird (Seiten sortieren/löschen/drehen): `PdfAssembler` baut
    //  einen neuen Katalog und kann `/AcroForm` nicht mitnehmen; ohne diesen
    //  Schritt behielte die Kopie zwar die Widgets, aber kein Formular - und
    //  unsere eigene Anzeige (Qt PDF zeichnet Widgets grundsätzlich nicht)
    //  zeigte eine leere Seite. Gezeichnet wird der bereits vorhandene
    //  `/AP /N`-Strom, also genau das, was der Nutzer vorher gesehen hat.
    //  Versteckte Widgets (`/F` Bit 2/6) verschwinden ersatzlos.
    //  Liefert false, wenn die Datei kein Formular hat oder nicht sicher
    //  fortschreibbar ist - dann bleibt `outputPath` ungeschrieben.
    static bool flatten(const QString& inputPath, const QString& outputPath,
                        QString* err = nullptr);
};

} // namespace mg
