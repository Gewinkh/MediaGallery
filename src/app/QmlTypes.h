#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  QmlTypes.h - die QML-TYPEN an EINER Stelle registrieren.
//
//  Warum es diese Datei gibt: `main.cpp` und `tests/bench/bench_shell.cpp`
//  richten beide eine QML-Umgebung ein und brauchen dieselben Typen. Solange
//  jede Datei ihre eigene Liste fuehrte, fehlte im Pruefstand regelmaessig ein
//  gerade erst hinzugekommener Typ - der Lauf brach dann mit „X is not a type"
//  ab und zeigte eine LEERE Flaeche, was leicht als kaputtes Layout
//  missdeutet wird (dreimal passiert, 2026-09-02).
//
//  Die SINGLETONS koennen NICHT mit: sie brauchen die Instanzen, die `main.cpp`
//  besitzt (`qmlRegisterSingletonInstance`). Ein neuer Typ gehoert also nur
//  noch hierher; ein neues Singleton weiterhin in beide Dateien.
// ─────────────────────────────────────────────────────────────────────────────
namespace mg {

//  Alle `qmlRegisterType`-Aufrufe des Projekts. Mehrfaches Rufen ist
//  unschaedlich (Qt ueberschreibt die Registrierung).
void registerQmlTypes();

}  // namespace mg
