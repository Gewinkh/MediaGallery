#pragma once
// Werkzeug fuer die beiden Rohleser einer PDF (Audio, Medien/Links): Schluessel, Werte, Objekttabelle, Seitenbaum.
// Arbeitet auf dem rohen Bytestrom; Objekte in Objektstroemen und verschluesselte Dateien sieht es nicht.

#include <QByteArray>
#include <QHash>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <atomic>

namespace mg::pdfraw {

bool isWs(char c);
bool isDelim(char c);
void skipWs(const QByteArray& d, qsizetype& i);
long readUInt(const QByteArray& d, qsizetype& i);

// Position eines Schluessels, der mit einem Begrenzer endet - `/F` trifft nicht `/Filter`.
qsizetype keyPos(const QByteArray& d, const char* key, qsizetype from = 0);
QByteArray readDictAt(const QByteArray& d, qsizetype from);
long intDirect(const QByteArray& dict, const char* key, long def);
int firstRefForKey(const QByteArray& dict, const char* key);
int firstAnyRef(const QByteArray& d);
QByteArray nestedDictForKey(const QByteArray& dict, const char* key);
QByteArray bracketValue(const QByteArray& dict, const char* key);
QString stringValue(const QByteArray& dict, const char* key);
long lengthValue(const QByteArray& d, const QHash<int,qsizetype>& off, const QByteArray& dict);
bool isPageObject(const QByteArray& dict);
QSizeF mediaBoxSize(const QByteArray& pageDict);
QRectF parseNormalisedRect(const QByteArray& rectBytes, const QSizeF& ps);
QVector<qsizetype> findAll(const QByteArray& d, const char* pat);
// Objektnummer -> Versatz hinter `obj`. `cancel` darf null sein.
QHash<int,qsizetype> buildObjectOffsets(const QByteArray& d, const std::atomic<bool>* cancel = nullptr);
QByteArray enclosingObjDict(const QByteArray& d, qsizetype pos);
QVector<int> kidsRefs(const QByteArray& dict);
int findRootPagesObj(const QByteArray& d, const QHash<int,qsizetype>& off);
void flattenPages(const QByteArray& d, const QHash<int,qsizetype>& off, int num,
                  QVector<int>& out, QSet<int>& visited, int depth);

// Das Woerterbuch von Objekt `num`; leer, wenn das Objekt kein Woerterbuch ist.
QByteArray objectDict(const QByteArray& d, const QHash<int,qsizetype>& off, int num);
// Seitenobjekte in Lesereihenfolge ueber den Seitenbaum, ersatzweise in Dateireihenfolge.
QVector<int> pageObjects(const QByteArray& d, const QHash<int,qsizetype>& off);
// Erster Wert von `key`, der eine Zeichenkette ist - `/S /URI /URI (…)` liefert die Adresse, nicht den Namen.
// Loest Escapes, Oktalfolgen, Hex-Strings und UTF-16BE mit BOM auf.
QString stringForKey(const QByteArray& dict, const char* key);
// Inhalt eines Stroms: `/Length` (auch indirekt), wenn er bis `endstream` passt, sonst bis vor das Zeilenende.
// Leer, wenn `num` kein Strom ist.
QByteArray streamData(const QByteArray& d, const QHash<int,qsizetype>& off, int num);

}  // namespace mg::pdfraw
