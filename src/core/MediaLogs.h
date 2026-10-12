#pragma once
//  MediaLogs - FFmpegs Redseligkeit stellen. QtMultimedia laedt FFmpeg als
//  Plugin mit RTLD_LOCAL; dessen Meldungen ("Input #0, mp3, from …") gehen am
//  Qt-Logging vorbei direkt auf stderr und stehen in der Ausgabe der Anwendung
//  als Fehler da, obwohl sie keine sind.
namespace mg::media {

//  Einmalig FFmpeg auf „nur Fehler" stellen. Traegt sich selbst nach, also
//  beliebig oft aufrufbar. ERST beim ersten Medium aufrufen: der dafuer noetige
//  dlopen kostet gemessen 2,3 MB RSS, und wer nie ein Medium oeffnet, soll ihn
//  nicht zahlen.
void beQuiet();

//  Linux: Software-Dekoder vorgeben statt Qts Vorab-Probe (573 ms im GUI-Faden); gesetzte Umgebung bleibt.
void limitHwProbe();

}  // namespace mg::media
