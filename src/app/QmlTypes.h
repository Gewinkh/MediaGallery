#pragma once

// Die QML-Typen an einer Stelle, weil main.cpp und bench_shell dieselben brauchen.
// Fuehrte jede Datei ihre eigene Liste, fehlte im Pruefstand ein neuer Typ und der Lauf
// zeigte eine leere Flaeche. Singletons koennen nicht mit - sie brauchen die Instanzen.
namespace mg {

//  Alle `qmlRegisterType`-Aufrufe des Projekts. Mehrfaches Rufen ist
//  unschaedlich (Qt ueberschreibt die Registrierung).
void registerQmlTypes();

}  // namespace mg
