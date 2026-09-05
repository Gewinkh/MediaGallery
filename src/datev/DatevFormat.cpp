#include "datev/DatevFormat.h"

#include <QDateTime>

namespace mg::datev {

QList<KopfFeld> kopfFelder(int version) {
    Q_UNUSED(version);
    //  Feld 1 traegt die Kennung selbst ("EXTF"/"DTVF"), Feld 2 die Zahl aus
    //  dem Formatnamen (700), Feld 4 den Klartextnamen ("Buchungsstapel"),
    //  Feld 6 einen Zeitstempel, Feld 22 einen ISO-Waehrungscode.
    return {
        { 1,  StringKey::DatevFieldIdent },
        { 2,  StringKey::DatevFieldVersion },
        { 4,  StringKey::DatevFieldFormatName },
        { 6,  StringKey::DatevFieldCreated },
        { 22, StringKey::DatevFieldCurrency },
    };
}

QString erzeugtAmLesbar(const QString& roh) {
    if (roh.size() < 14) return {};
    for (QChar c : roh)
        if (!c.isDigit()) return {};
    const QDateTime dt = QDateTime::fromString(roh.left(14), QStringLiteral("yyyyMMddHHmmss"));
    if (!dt.isValid()) return {};
    return dt.toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"));
}

}  // namespace mg::datev
