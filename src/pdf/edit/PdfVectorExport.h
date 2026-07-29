#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  PdfVectorExport.h — VERLUSTFREIER Export des PDF-Editors
// ══════════════════════════════════════════════════════════════════════════════
//
//  ZWECK
//  ─────
//  Schreibt die Overlay-Anmerkungen (Notizen, Freihand, Pfeile, Rechtecke,
//  Ellipsen, „Text ersetzen"-Deckflächen) als ECHTE VEKTOR-Inhalte in die
//  Seiten-Content-Streams — statt jede Seite als 150-dpi-Bild neu zu rendern.
//
//  Der bisherige Weg (QPdfWriter + QPainter) rasterte AUSNAHMSLOS jede Seite,
//  auch völlig unberührte: das Dokument verlor dabei seine Textebene, die
//  eingebetteten Schriften und alle Vektorgrafik. Hier bleibt der
//  Originalinhalt jeder Seite BYTEGLEICH erhalten; angehängt wird nur, was der
//  Nutzer gezeichnet hat.
//
//  VERFAHREN: INKREMENTELLES UPDATE (append-only) — wie PdfContentEditor
//  ────────────────────────────────────────────────────────────────────
//  Originalbytes 1:1, danach angehängt: je betroffener Seite ein NEUER
//  Content-Stream mit den Zeichenbefehlen, ein aktualisiertes Seiten-Objekt
//  (`/Contents` wird zum Array [alt neu], `/Resources` um Schrift und
//  Transparenz-Zustand ergänzt) sowie eine kleine XRef-Sektion mit `/Prev`.
//  Seiten ohne Anmerkung werden ÜBERHAUPT NICHT angefasst.
//
//  KOORDINATEN: Die App rechnet in PDF-Punkten mit Ursprung OBEN-links (wie
//  die Anzeige), PDF selbst hat den Ursprung UNTEN-links. Umgerechnet wird
//  ausschließlich hier, an genau einer Stelle (`toPdfY`).
//
//  BEWUSST BEGRENZT (sonst false → Aufrufer nutzt den Raster-Export):
//   • unverschlüsselt (kein /Encrypt), klassische xref-Tabelle,
//   • je Seite EIN /Contents-Stream oder ein Array (beides wird unterstützt),
//   • Textnotizen nur in den 14 Standard-Schriften (Helvetica/Times/Courier
//     samt Fett/Kursiv) und nur mit Zeichen, die WinAnsiEncoding kennt —
//     eine fremde Schrift müsste sonst eingebettet werden,
//   • ein NICHT-identischer Seiten-Plan (eingefügte/entfernte Seiten) wird
//     hier NICHT abgebildet; dafür bleibt der Raster-Weg zuständig.
//  Ein Fehlschlag schreibt NICHTS (kein Fragment).
//
//  ABHÄNGIGKEITEN: Qt6::Core + Qt6::Gui (QColor/QRectF) + ZLIB.
//  Kein Q_OBJECT/moc; isoliert testbar.
// ══════════════════════════════════════════════════════════════════════════════

#include <QString>
#include <QVector>

#include "pdf/edit/PdfEditTypes.h"

namespace mg {

class PdfVectorExport {
public:
    //  Schreibt `outputPath` (atomar). `boxes` sind die Anmerkungen in
    //  PDF-Punkten mit Ursprung oben-links der ANGEZEIGTEN Seite; `page` zählt
    //  die Seiten von `inputPath`. Der Seiten-Plan des Editors ist in
    //  `inputPath` bereits enthalten (der Aufrufer übergibt die gebackene
    //  Arbeitsdatei) — hier wird nur noch angehängt. Gedrehte Seiten (/Rotate,
    //  auch geerbt) werden berücksichtigt.
    //  Liefert false, wenn irgendeine Vorbedingung nicht SICHER erfüllt ist —
    //  dann bleibt `outputPath` ungeschrieben und der Aufrufer weicht auf den
    //  Raster-Export aus. `err` erhält einen kurzen Grund.
    static bool exportAnnotations(const QString& inputPath, const QString& outputPath,
                                  const QVector<PdfEditBox>& boxes,
                                  QString* err = nullptr);
};

} // namespace mg
