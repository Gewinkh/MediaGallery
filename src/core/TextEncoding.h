#pragma once
//  TextEncoding - Bytes zu Text, wenn die Kodierung nicht mitgeliefert wird.
//  Erkennen statt annehmen: erst auf gueltiges UTF-8 pruefen, sonst CP1252.
#include <QByteArray>
#include <QString>

namespace mg {

enum class TextEncodingUsed { Utf8, Cp1252 };

//  CP1252, nicht Latin-1: die beiden gehen bei 0x80-0x9F auseinander, und genau
//  dort liegen Euro-Zeichen und typografische Anfuehrungszeichen.
QString decodeCp1252(const QByteArray& raw);

//  Ein fuehrendes UTF-8-BOM wird verworfen, nicht als Zeichen geliefert.
QString decodeUnknownText(const QByteArray& raw, TextEncodingUsed* used = nullptr);

}  // namespace mg
