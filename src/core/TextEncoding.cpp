#include "core/TextEncoding.h"

#include <QStringDecoder>

namespace mg {
namespace {

//  0x80-0x9F von CP1252; darunter und darueber deckt sich die Seite mit
//  Latin-1. 0xFFFD steht fuer die fuenf in CP1252 unbelegten Bytes.
constexpr char16_t kHoch[32] = {
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
};

}  // namespace

QString decodeCp1252(const QByteArray& raw) {
    QString out;
    out.reserve(raw.size());
    for (char c : raw) {
        const unsigned char b = static_cast<unsigned char>(c);
        out.append(QChar(b >= 0x80 && b <= 0x9F ? kHoch[b - 0x80] : char16_t(b)));
    }
    return out;
}

QString decodeUnknownText(const QByteArray& raw, TextEncodingUsed* used) {
    QStringDecoder dec(QStringDecoder::Utf8, QStringDecoder::Flag::Default);
    QString text = dec.decode(raw);
    if (!dec.hasError()) {
        if (used) *used = TextEncodingUsed::Utf8;
        return text;
    }
    if (used) *used = TextEncodingUsed::Cp1252;
    return decodeCp1252(raw);
}

}  // namespace mg
