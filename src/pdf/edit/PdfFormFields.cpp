#include "pdf/edit/PdfFormFields.h"
#include "pdf/edit/PdfObjects.h"
#include "pdf/edit/PdfEncodings.h"

#include <QFile>
#include <QPointF>
#include <QRegularExpression>
#include <QSet>
#include <QSizeF>

#include <algorithm>
#include <cmath>
#include <functional>

using namespace mg::pdfobj;

// Die Erläuterungen zum Gesamtverfahren stehen im Header.
namespace {

//  Der /AcroForm-Dict-Inhalt (leer, wenn das Dokument kein Formular hat).
//  `formObj` erhält die Objektnummer, falls /AcroForm eine Referenz ist,
//  sonst −1 (dann steht das Dict inline im Katalog).
QByteArray acroFormDict(const PdfDoc& doc, int* formObj) {
    *formObj = -1;
    const QByteArray root = doc.dictOf(doc.rootNum);
    const int ref = refValue(root, "AcroForm");
    if (ref >= 0) { *formObj = ref; return doc.dictOf(ref); }
    const QByteArray raw = rawValue(root, "AcroForm");
    return raw.startsWith("<<") ? dictOfObject(raw) : QByteArray();
}
// ── Feld-Einsammlung ────────────────────────────────────────────────────────
//  Vererbbare Angaben eines Feldknotens (RFC: /FT /Ff /V /DA /Q /MaxLen /Opt
//  werden vom übergeordneten Feld geerbt).
struct Inherited {
    QByteArray ft;
    qint64     ff = 0;
    QByteArray v;
    QByteArray opt;
    qint64     maxLen = -1;
};

mg::PdfFieldType typeFor(const QByteArray& ft, qint64 ff) {
    if (ft == "/Tx")  return mg::PdfFieldType::Text;
    if (ft == "/Ch")  return mg::PdfFieldType::Choice;
    if (ft == "/Btn") {
        if (ff & (qint64(1) << 16)) return mg::PdfFieldType::Push;    // Bit 17
        if (ff & (qint64(1) << 15)) return mg::PdfFieldType::Radio;   // Bit 16
        return mg::PdfFieldType::Checkbox;
    }
    return mg::PdfFieldType::Unknown;
}

//  Die „An"-Zustände eines Ankreuz-/Optionsknopfes: die Schlüssel von
//  /AP /N außer /Off. Leer, wenn das Widget kein Zustands-Wörterbuch hat.
QStringList onStatesOf(const PdfDoc& doc, const QByteArray& widgetDict) {
    const QByteArray ap = doc.resolved(widgetDict, "AP");
    if (!ap.startsWith("<<")) return {};
    const QByteArray apd = dictOfObject(ap);
    QByteArray n = rawValue(apd, "N");
    if (n.isEmpty()) return {};
    if (!n.startsWith("<<")) {                                  // Referenz?
        static const QRegularExpression re(QStringLiteral("^(\\d+)\\s+(\\d+)\\s+R$"));
        const auto m = re.match(QString::fromLatin1(n));
        if (!m.hasMatch()) return {};
        n = doc.bodyOf(m.captured(1).toInt()).trimmed();
    }
    if (!n.startsWith("<<")) return {};
    const QByteArray nd = dictOfObject(n);
    //  Ein Formstrom (also EIN Erscheinungsbild statt eines Zustands-Dicts)
    //  trägt immer eine /BBox - dann gibt es hier keine Zustände.
    if (findKey(nd, "BBox") >= 0) return {};
    QStringList states;
    for (const QByteArray& k : dictKeys(nd))
        if (k != "/Off") states << nameToString(k);
    return states;
}

//  /Opt einer Auswahlliste: Einträge sind Strings oder [Export Anzeige].
void parseOpt(const QByteArray& optArr, QStringList* display, QStringList* exported) {
    if (!optArr.startsWith('[')) return;
    const QByteArray inner = optArr.mid(1, optArr.size() - 2);
    qint64 i = 0;
    int guard = 0;
    while (i < inner.size() && ++guard < 20000) {
        while (i < inner.size() && isWs(inner[i])) ++i;
        if (i >= inner.size()) break;
        const qint64 e = skipValue(inner, i);
        if (e <= i) break;
        const QByteArray item = inner.mid(i, e - i).trimmed();
        if (item.startsWith('[')) {                             // [Export Anzeige]
            const QByteArray sub = item.mid(1, item.size() - 2);
            QByteArray a, b;
            qint64 j = 0;
            while (j < sub.size() && isWs(sub[j])) ++j;
            const qint64 je = skipValue(sub, j);
            readPdfStringBytes(sub, j, &a);
            qint64 k = je;
            while (k < sub.size() && isWs(sub[k])) ++k;
            if (!readPdfStringBytes(sub, k, &b)) b = a;
            *exported << pdfTextToString(a);
            *display  << pdfTextToString(b);
        } else {
            QByteArray a;
            if (readPdfStringBytes(item, 0, &a)) {
                *exported << pdfTextToString(a);
                *display  << pdfTextToString(a);
            }
        }
        i = e;
    }
}

//  Baut EINEN Eintrag aus Feld- und Widget-Dict.
mg::PdfFormField makeField(const PdfDoc& doc, int fieldObj, int widgetObj,
                           const QString& name, const Inherited& inh,
                           const QByteArray& fieldDict, const QByteArray& widgetDict) {
    mg::PdfFormField f;
    f.name      = name;
    f.fieldObj  = fieldObj;
    f.widgetObj = widgetObj;
    f.type      = typeFor(inh.ft, inh.ff);

    f.readOnly  =  inh.ff & 1;
    f.required  =  inh.ff & 2;
    f.multiline = (f.type == mg::PdfFieldType::Text)   && (inh.ff & (qint64(1) << 12));
    f.password  = (f.type == mg::PdfFieldType::Text)   && (inh.ff & (qint64(1) << 13));
    f.combo     = (f.type == mg::PdfFieldType::Choice) && (inh.ff & (qint64(1) << 17));
    f.editable  = (f.type == mg::PdfFieldType::Choice) && (inh.ff & (qint64(1) << 18));
    f.maxLen    = (inh.maxLen > 0) ? int(inh.maxLen) : -1;

    {   // Kurzhilfe /TU
        QByteArray raw;
        const qint64 p = findKey(fieldDict, "TU");
        if (p >= 0 && readPdfStringBytes(fieldDict, p, &raw))
            f.tooltip = pdfTextToString(raw);
    }

    //  Wert: bei Knöpfen ein Name, sonst ein Textstring (bei Mehrfachauswahl
    //  ein Array - dann zählt der erste Eintrag).
    if (f.type == mg::PdfFieldType::Checkbox || f.type == mg::PdfFieldType::Radio) {
        f.value = inh.v.startsWith('/') ? nameToString(inh.v) : QStringLiteral("Off");
        const QStringList st = onStatesOf(doc, widgetDict);
        if (!st.isEmpty()) f.onState = st.first();
        //  Ohne Zustands-Dict (z. B. /AP fehlt) bleibt der übliche Name „Yes".
        if (f.onState.isEmpty()) f.onState = QStringLiteral("Yes");
    } else if (f.type != mg::PdfFieldType::Push) {
        QByteArray v = inh.v;
        if (v.startsWith('[')) {                                 // Mehrfachauswahl
            const QByteArray sub = v.mid(1, v.size() - 2);
            qint64 j = 0; while (j < sub.size() && isWs(sub[j])) ++j;
            v = sub.mid(j);
        }
        QByteArray raw;
        if (readPdfStringBytes(v, 0, &raw))      f.value = pdfTextToString(raw);
        else if (v.startsWith('/'))              f.value = nameToString(v);
    }

    if (f.type == mg::PdfFieldType::Choice)
        parseOpt(inh.opt, &f.options, &f.optionValues);

    //  Lage: /Rect ist Benutzerraum der UNGEDREHTEN Seite.
    const auto pit = doc.annotPage.constFind(widgetObj);
    if (pit != doc.annotPage.constEnd()) f.page = pit.value();
    const QByteArray rect = rawValue(widgetDict, "Rect");
    if (f.page >= 0 && rect.startsWith('[')) {
        const QVector<double> v = numbersOfArray(rect);
        if (v.size() >= 4) {
            const int    pageObj = doc.pageObjs.at(f.page);
            const QSizeF box     = doc.pageBox(pageObj);
            const int    rot     = doc.pageRotate(pageObj);
            if (!box.isEmpty()) {
                const QPointF a = toDisplay(v[0], v[1], box, rot);
                const QPointF b = toDisplay(v[2], v[3], box, rot);
                f.rect = QRectF(QPointF(qMin(a.x(), b.x()), qMin(a.y(), b.y())),
                                QPointF(qMax(a.x(), b.x()), qMax(a.y(), b.y())));
            }
        }
    }
    return f;
}

//  Läuft den Feldbaum ab und erzeugt je WIDGET einen Eintrag.
void collectFields(const PdfDoc& doc, const QByteArray& formDict,
                   QVector<mg::PdfFormField>* out) {
    const QByteArray fields = doc.resolved(formDict, "Fields");
    if (!fields.startsWith('[')) return;

    QSet<int> seen;                                   // Zyklenschutz
    int guard = 0;

    std::function<void(int, const QString&, const Inherited&, int)> walk =
        [&](int objNum, const QString& parentName, const Inherited& parentInh, int depth) {
        if (depth > 32 || ++guard > 20000) return;
        if (seen.contains(objNum)) return;
        seen.insert(objNum);

        const QByteArray d = doc.dictOf(objNum);
        if (d.isEmpty()) return;

        QString name = parentName;
        {   // Teilname /T anhängen
            QByteArray raw;
            const qint64 p = findKey(d, "T");
            if (p >= 0 && readPdfStringBytes(d, p, &raw)) {
                const QString t = pdfTextToString(raw);
                name = parentName.isEmpty() ? t : parentName + QLatin1Char('.') + t;
            }
        }

        Inherited inh = parentInh;
        if (const QByteArray v = nameValue(d, "FT"); !v.isEmpty()) inh.ft = v;
        if (findKey(d, "Ff") >= 0) {
            bool ok = false;
            const qint64 x = rawValue(d, "Ff").toLongLong(&ok);
            if (ok) inh.ff = x;
        }
        if (findKey(d, "V")   >= 0) inh.v   = doc.resolved(d, "V");
        if (findKey(d, "Opt") >= 0) inh.opt = doc.resolved(d, "Opt");
        if (findKey(d, "MaxLen") >= 0) {
            bool ok = false;
            const qint64 x = rawValue(d, "MaxLen").toLongLong(&ok);
            if (ok) inh.maxLen = x;
        }

        //  /Kids: entweder Unterfelder (haben /T) oder die Widgets dieses Feldes.
        const QByteArray kids = doc.resolved(d, "Kids");
        QVector<int> kidObjs;
        if (kids.startsWith('[')) {
            static const QRegularExpression kre(QStringLiteral("(\\d+)\\s+(\\d+)\\s+R"));
            auto it = kre.globalMatch(QString::fromLatin1(kids));
            while (it.hasNext()) kidObjs.push_back(it.next().captured(1).toInt());
        }

        //  Unterschriftenfelder werden ÜBERGANGEN (s. Header): ausfüllen hieße
        //  signieren, und das ist Kryptografie. Ein Feld anzuzeigen, das man
        //  nie beschreiben kann, wäre ein Versprechen ohne Deckung.
        if (inh.ft == "/Sig") return;

        if (kidObjs.isEmpty()) {                       // verschmolzen: Feld == Widget
            out->push_back(makeField(doc, objNum, objNum, name, inh, d, d));
            return;
        }
        for (int k : std::as_const(kidObjs)) {
            const QByteArray kd = doc.dictOf(k);
            if (kd.isEmpty()) continue;
            if (findKey(kd, "T") >= 0) { walk(k, name, inh, depth + 1); continue; }
            out->push_back(makeField(doc, objNum, k, name, inh, d, kd));
        }
    };

    static const QRegularExpression fre(QStringLiteral("(\\d+)\\s+(\\d+)\\s+R"));
    auto it = fre.globalMatch(QString::fromLatin1(fields));
    while (it.hasNext())
        walk(it.next().captured(1).toInt(), QString(), Inherited{}, 0);
}

// ── Erscheinungsbild (/AP /N) für Text- und Auswahlfelder ───────────────────
//  Der /DA-String eines Feldes sieht typischerweise so aus: „/Helv 0 Tf 0 g".
//  Daraus brauchen wir Schriftname und -größe (0 = automatisch).
struct DaInfo {
    QByteArray da;                    // Originaltext
    QByteArray fontName = "Helv";     // ohne Schrägstrich
    qreal      size     = 0.0;
    bool       hasTf    = false;
    bool       hasColor = false;
};

DaInfo parseDa(const QByteArray& da) {
    DaInfo info; info.da = da;
    static const QRegularExpression re(
        QStringLiteral("/([^\\s/\\[\\]<>(){}]+)\\s+([0-9]*\\.?[0-9]+)\\s+Tf"));
    const auto m = re.match(QString::fromLatin1(da));
    if (m.hasMatch()) {
        info.fontName = m.captured(1).toLatin1();
        info.size     = m.captured(2).toDouble();
        info.hasTf    = true;
    }
    static const QRegularExpression cre(QStringLiteral("(^|\\s)(g|rg|k)(\\s|$)"));
    info.hasColor = cre.match(QString::fromLatin1(da)).hasMatch();
    return info;
}

//  Grobe mittlere Zeichenbreite der Standardschriften - sie genügt für Umbruch
//  und Auto-Größe. Sie liegt bewusst leicht ZU GROSS, damit nichts über den
//  Rand läuft (dieselbe Zusage wie in PdfVectorExport).
qreal avgCharWidth(const QByteArray& fontName, qreal sizePt) {
    const QByteArray f = fontName.toLower();
    if (f.startsWith("cour") || f.contains("mono")) return 0.600 * sizePt;
    if (f.startsWith("ti")   || f.contains("serif")) return 0.500 * sizePt;
    return 0.540 * sizePt;
}

QVector<QString> wrapText(const QString& text, qreal widthPt,
                          const QByteArray& fontName, qreal sizePt) {
    QVector<QString> lines;
    const qreal cw = qMax(0.1, avgCharWidth(fontName, sizePt));
    const int maxChars = qMax(1, int(widthPt / cw));
    for (const QString& para : text.split(QLatin1Char('\n'))) {
        if (para.isEmpty()) { lines.push_back(QString()); continue; }
        QString cur;
        for (const QString& word : para.split(QLatin1Char(' '))) {
            QString w = word;
            while (w.size() > maxChars) {
                if (!cur.isEmpty()) { lines.push_back(cur); cur.clear(); }
                lines.push_back(w.left(maxChars));
                w = w.mid(maxChars);
            }
            if (cur.isEmpty())                              cur = w;
            else if (cur.size() + 1 + w.size() <= maxChars) cur += QLatin1Char(' ') + w;
            else { lines.push_back(cur); cur = w; }
        }
        lines.push_back(cur);
    }
    return lines;
}

} // namespace

namespace mg {

bool PdfFormFields::read(const QString& path, QVector<PdfFormField>* out, QString* err) {
    if (!out) return false;
    out->clear();
    PdfDoc doc;
    if (!doc.load(path, err)) return false;

    int formObj = -1;
    const QByteArray form = acroFormDict(doc, &formObj);
    if (form.isEmpty()) return true;                   // Dokument ohne Formular
    collectFields(doc, form, out);
    return true;
}

bool PdfFormFields::fillAndSave(const QString& inputPath, const QString& outputPath,
                                const QHash<QString, QString>& values, QString* err) {
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    if (err) err->clear();

    if (inputPath == outputPath)
        return fail(QStringLiteral("Ziel darf nicht die Quelle sein"));

    PdfDoc doc;
    QString le;
    if (!doc.load(inputPath, &le)) return fail(le);

    int formObj = -1;
    QByteArray form = acroFormDict(doc, &formObj);
    if (form.isEmpty()) return fail(QStringLiteral("kein AcroForm-Formular"));

    QVector<PdfFormField> fields;
    collectFields(doc, form, &fields);
    if (fields.isEmpty()) return fail(QStringLiteral("keine Formularfelder"));

    //  Schrift des Formulars: der in /DA genannte Name im /DR-Ressourcen-Dict.
    //  Fehlt er, wird weiter unten eine eigene Helvetica angelegt - dann ist
    //  das Erscheinungsbild garantiert vollständig und hängt an nichts Fremdem.
    const QByteArray formDa = [&] {
        QByteArray raw;
        const qint64 p = findKey(form, "DA");
        if (p >= 0 && readPdfStringBytes(form, p, &raw)) return raw;
        return QByteArray("/Helv 0 Tf 0 g");
    }();
    const QByteArray drDict = [&] {
        const QByteArray dr = doc.resolved(form, "DR");
        return dr.startsWith("<<") ? dictOfObject(dr) : QByteArray();
    }();
    const QByteArray drFonts = [&] {
        if (drDict.isEmpty()) return QByteArray();
        const QByteArray f = doc.resolved(drDict, "Font");
        return f.startsWith("<<") ? dictOfObject(f) : QByteArray();
    }();

    // ── Änderungen sammeln ──────────────────────────────────────────────────
    struct ApJob {                                   // ein neu zu malendes Widget
        int        widgetObj = -1;
        qreal      w = 0, h = 0;
        QString    text;
        bool       multiline = false;
        int        quadding  = 0;                    // /Q: 0 links, 1 zentriert, 2 rechts
        QByteArray da;
    };

    QHash<int, QByteArray> newDicts;                 // Objektnummer -> neuer Dict-Inhalt
    QVector<ApJob>         apJobs;
    bool                   needAppearances = false;  // Notnagel (s. Header)
    bool                   touched = false;

    auto dictFor = [&](int objNum) -> QByteArray& {
        auto it = newDicts.find(objNum);
        if (it == newDicts.end()) it = newDicts.insert(objNum, doc.dictOf(objNum));
        return it.value();
    };

    //  Felder nach Namen bündeln - ein Optionsfeld hat mehrere Widgets.
    QHash<QString, QVector<int>> byName;             // Name -> Indizes in `fields`
    for (int i = 0; i < fields.size(); ++i) byName[fields.at(i).name].push_back(i);

    for (auto vit = values.constBegin(); vit != values.constEnd(); ++vit) {
        const auto nit = byName.constFind(vit.key());
        if (nit == byName.constEnd()) continue;      // Name gibt es nicht -> ignorieren
        const QVector<int>& idx = nit.value();
        const PdfFormField& first = fields.at(idx.first());
        if (first.readOnly) continue;                // schreibgeschützt -> unangetastet
        if (first.type == PdfFieldType::Push
            || first.type == PdfFieldType::Unknown) continue;

        if (first.type == PdfFieldType::Checkbox || first.type == PdfFieldType::Radio) {
            const QString state = vit.value();
            //  Nur „Off" oder ein tatsächlich vorhandener Zustand - ein
            //  erfundener Name würde das Feld unsichtbar kaputtmachen.
            bool known = (state == QLatin1String("Off"));
            for (int i : idx) if (fields.at(i).onState == state) known = true;
            if (!known)
                return fail(QStringLiteral("Zustand „%1\" gibt es im Feld „%2\" nicht")
                                .arg(state, first.name));
            dictFor(first.fieldObj) =
                setDictKey(dictFor(first.fieldObj), "V", toPdfName(state));
            for (int i : idx) {
                const PdfFormField& f = fields.at(i);
                const QByteArray as = (f.onState == state) ? toPdfName(state)
                                                           : QByteArray("/Off");
                dictFor(f.widgetObj) = setDictKey(dictFor(f.widgetObj), "AS", as);
            }
            touched = true;
            continue;
        }

        //  Text/Auswahl: /V als Textstring, Erscheinungsbild neu malen.
        QString text = vit.value();
        if (first.maxLen > 0 && text.size() > first.maxLen) text = text.left(first.maxLen);
        if (!first.multiline) text.replace(QLatin1Char('\n'), QLatin1Char(' '));

        dictFor(first.fieldObj) =
            setDictKey(dictFor(first.fieldObj), "V", toPdfTextString(text));
        //  Eine Auswahlliste merkt sich zusätzlich den Index - er wäre nach der
        //  Änderung falsch, also entfällt er (der Wert allein ist maßgeblich).
        if (first.type == PdfFieldType::Choice && findKey(dictFor(first.fieldObj), "I") >= 0)
            dictFor(first.fieldObj) = setDictKey(dictFor(first.fieldObj), "I", "[]");

        for (int i : idx) {
            const PdfFormField& f = fields.at(i);
            const QByteArray wd = doc.dictOf(f.widgetObj);
            const QVector<double> r = numbersOfArray(rawValue(wd, "Rect"));
            if (r.size() < 4) { needAppearances = true; continue; }
            ApJob job;
            job.widgetObj = f.widgetObj;
            job.w = qAbs(r[2] - r[0]);
            job.h = qAbs(r[3] - r[1]);
            job.text      = text;
            job.multiline = f.multiline;
            {   // /DA und /Q: eigenes vor geerbtem vor Formular-Vorgabe
                QByteArray raw;
                const qint64 p = findKey(wd, "DA");
                const QByteArray fd = doc.dictOf(f.fieldObj);
                const qint64 pf = findKey(fd, "DA");
                if (p >= 0 && readPdfStringBytes(wd, p, &raw))       job.da = raw;
                else if (pf >= 0 && readPdfStringBytes(fd, pf, &raw)) job.da = raw;
                else                                                  job.da = formDa;
                const QByteArray q = rawValue(fd, "Q");
                bool ok = false;
                const int qv = q.toInt(&ok);
                if (ok && qv >= 0 && qv <= 2) job.quadding = qv;
            }
            if (job.w < 1.0 || job.h < 1.0) { needAppearances = true; continue; }
            apJobs.push_back(job);
        }
        touched = true;
    }

    if (!touched) return fail(QStringLiteral("keine passenden Felder"));

    // ── Ausgabe zusammenbauen (inkrementelles Update, s. PdfObjects) ────────
    IncrementalUpdate up(doc);
    auto addObject = [&](int n, int gen, const QByteArray& body) {
        up.addObject(n, gen, body);
    };

    //  Ist die in /DA genannte Schrift nicht im /DR zu finden, legen wir eine
    //  eigene Helvetica an - nur dann. Sonst bliebe der Formstrom ohne Schrift
    //  und der Text unsichtbar.
    int  ownFontObj = -1;
    auto ensureOwnFont = [&]() -> int {
        if (ownFontObj < 0) {
            ownFontObj = up.reserveObjNum();
            addObject(ownFontObj, 0, "<< /Type /Font /Subtype /Type1 "
                                     "/BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
        }
        return ownFontObj;
    };

    for (const ApJob& job : apJobs) {
        //  Schrift auflösen: Name aus /DA im /DR nachschlagen.
        DaInfo da = parseDa(job.da);
        const QByteArray fontRef = rawValue(drFonts, da.fontName.constData());
        static const QRegularExpression rre(QStringLiteral("^(\\d+)\\s+(\\d+)\\s+R$"));
        const auto rm = rre.match(QString::fromLatin1(fontRef));

        QByteArray fontResName = da.fontName;
        QByteArray fontResRef;
        pdfenc::Encoding enc;
        bool encOk = true;
        if (rm.hasMatch()) {
            const int fo = rm.captured(1).toInt();
            fontResRef = QByteArray::number(fo) + " 0 R";
            const QByteArray fdict = doc.dictOf(fo);
            const QByteArray encVal = doc.resolved(fdict, "Encoding");
            enc = pdfenc::Encoding::fromEncodingValue(encVal, &encOk);
        } else {
            fontResName = "Helv";
            fontResRef  = QByteArray::number(ensureOwnFont()) + " 0 R";
            enc = pdfenc::Encoding::fromEncodingValue("/WinAnsiEncoding", &encOk);
            da.fontName = "Helv";
            da.hasTf    = false;                     // eigener Tf-Befehl unten
        }

        //  Größe: 0 (bzw. fehlend) heißt „automatisch".
        qreal size = da.size;
        if (size <= 0.0) {
            size = job.multiline ? 10.0 : qBound(4.0, job.h * 0.62, 12.0);
            if (!job.multiline && !job.text.isEmpty()) {
                const qreal need = job.text.size() * avgCharWidth(da.fontName, size);
                const qreal room = qMax(1.0, job.w - 4.0);
                if (need > room) size = qMax(4.0, size * room / need);
            }
        }

        //  Zeilen bestimmen und in Bytes der Formularschrift wandeln.
        const QVector<QString> lines = job.multiline
            ? wrapText(job.text, qMax(1.0, job.w - 4.0), da.fontName, size)
            : QVector<QString>{ job.text };

        QVector<QByteArray> encoded;
        bool allEncodable = true;
        for (const QString& l : lines) {
            QByteArray bytes;
            if (!encOk || !enc.encode(l, &bytes)) { allEncodable = false; break; }
            encoded.push_back(bytes);
        }
        if (!allEncodable) {
            //  Zeichen, die die Formularschrift nicht kennt (z. B. Kyrillisch
            //  in einer WinAnsi-Schrift). Der WERT steht korrekt in /V; für das
            //  Aussehen bleibt nur die Bitte an den Betrachter.
            needAppearances = true;
            continue;
        }

        //  Grafikzustand aus /DA übernehmen; Tf ggf. mit der berechneten Größe.
        QByteArray gs = da.da;
        if (da.hasTf) {
            static const QRegularExpression tre(
                QStringLiteral("/([^\\s/\\[\\]<>(){}]+)\\s+([0-9]*\\.?[0-9]+)\\s+Tf"));
            const QString repl = QStringLiteral("/%1 %2 Tf")
                                     .arg(QString::fromLatin1(fontResName),
                                          QString::fromLatin1(num(size)));
            QString s = QString::fromLatin1(gs);
            s.replace(tre, repl);
            gs = s.toLatin1();
        } else {
            gs += " /" + fontResName + " " + num(size) + " Tf";
        }
        if (!da.hasColor) gs += " 0 g";

        //  Inhalt: Beschneiden auf das Widget, dann Zeilen setzen.
        QByteArray ops;
        ops += "/Tx BMC\nq\n";
        ops += "1 1 " + num(qMax(0.0, job.w - 2.0)) + " " + num(qMax(0.0, job.h - 2.0))
             + " re\nW\nn\n";
        ops += "BT\n" + gs.trimmed() + "\n";
        const qreal lead = size * 1.16;
        //  Einzeilig wird senkrecht zentriert (so machen es Betrachter auch),
        //  mehrzeilig beginnt oben.
        qreal y = job.multiline ? (job.h - 2.0 - size * 0.85)
                                : (job.h / 2.0 - size * 0.28);
        for (int i = 0; i < encoded.size(); ++i) {
            if (y < -lead) break;                                // unten heraus
            const qreal tw = lines.at(i).size() * avgCharWidth(da.fontName, size);
            qreal x = 2.0;
            if (job.quadding == 1)      x = qMax(2.0, (job.w - tw) / 2.0);
            else if (job.quadding == 2) x = qMax(2.0, job.w - 2.0 - tw);
            ops += "1 0 0 1 " + num(x) + " " + num(y) + " Tm\n";
            ops += parenString(encoded.at(i)) + " Tj\n";
            y -= lead;
        }
        ops += "ET\nQ\nEMC\n";

        //  Der Formstrom bleibt UNKOMPRIMIERT - er ist wenige hundert Bytes
        //  groß, Deflate spart hier nichts und erschwert nur die Fehlersuche.
        const int apNum = up.reserveObjNum();
        up.addStream(apNum, 0,
                     "/Type /XObject /Subtype /Form /FormType 1"
                     " /BBox [0 0 " + num(job.w) + " " + num(job.h) + "]"
                     " /Resources << /Font << /" + fontResName + " " + fontResRef + " >> >>",
                     ops);

        //  /AP des Widgets komplett ersetzen: ein Textfeld hat kein sinnvolles
        //  /D oder /R, die verlören wir also nicht.
        dictFor(job.widgetObj) = setDictKey(dictFor(job.widgetObj), "AP",
                                            "<< /N " + QByteArray::number(apNum) + " 0 R >>");
    }

    //  Notnagel: nur wenn mindestens ein Aussehen NICHT erzeugt werden konnte.
    if (needAppearances) {
        if (formObj >= 0) {
            dictFor(formObj) = setDictKey(dictFor(formObj), "NeedAppearances", "true");
        } else {
            //  /AcroForm steht inline im Katalog -> den Katalog neu schreiben.
            QByteArray root = dictFor(doc.rootNum);
            QByteArray af   = rawValue(root, "AcroForm");
            if (af.startsWith("<<")) {
                QByteArray inner = dictOfObject(af);
                inner = setDictKey(inner, "NeedAppearances", "true");
                root  = setDictKey(root, "AcroForm", "<< " + inner + " >>");
                dictFor(doc.rootNum) = root;
            }
        }
    }

    //  Geänderte Objekte anhängen (die Objektnummer bleibt, nur der Inhalt ist
    //  neu - der Brute-Scan beim nächsten Lesen nimmt das LETZTE Vorkommen).
    for (auto it = newDicts.constBegin(); it != newDicts.constEnd(); ++it) {
        if (it.value().isEmpty()) return fail(QStringLiteral("Objekt-Dict leer"));
        addObject(it.key(), doc.genOf(it.key()), "<<" + it.value() + ">>");
    }

    QString ce;
    if (!up.commit(outputPath, &ce)) return fail(ce);
    return true;
}

// ── FESTSCHREIBEN ───────────────────────────────────────────────────────────
//  Erläuterung des Zwecks im Header. Gezeichnet wird je Widget sein eigener
//  `/AP /N`-Strom mit der Matrix aus PDF 32000-1, 12.5.5 („Algorithm:
//  appearance streams"): /BBox mit /Matrix abbilden, die Hülle davon auf
//  /Rect strecken. Das `Do` wendet /Matrix selbst noch einmal an - deshalb
//  wird NUR die Streckung als CTM gesetzt.
bool PdfFormFields::flatten(const QString& inputPath, const QString& outputPath,
                            QString* err) {
    auto fail = [&](const QString& m) { if (err) *err = m; return false; };
    if (err) err->clear();
    if (inputPath == outputPath)
        return fail(QStringLiteral("Ziel darf nicht die Quelle sein"));

    PdfDoc doc;
    QString le;
    if (!doc.load(inputPath, &le)) return fail(le);

    int formObj = -1;
    const QByteArray form = acroFormDict(doc, &formObj);
    if (form.isEmpty()) return fail(QStringLiteral("kein AcroForm-Formular"));

    IncrementalUpdate up(doc);
    static const QRegularExpression refRe(QStringLiteral("(\\d+)\\s+(\\d+)\\s+R"));

    for (int pi = 0; pi < doc.pageObjs.size(); ++pi) {
        const int       pageObj  = doc.pageObjs.at(pi);
        const QByteArray pageDict = doc.dictOf(pageObj);
        const QByteArray annots   = doc.resolved(pageDict, "Annots");
        if (!annots.startsWith('[')) continue;

        QByteArray  ops;                       // Zeichenbefehle dieser Seite
        QByteArray  xobjs;                     // neue /XObject-Einträge
        QSet<QByteArray> usedNames;            // schon vergebene Ressourcennamen
        QByteArray  kept;                      // Annotationen, die bleiben
        int         n = 0;                     // Zähler für die Ressourcennamen
        bool        removed = false;           // irgendein Widget entfernt?

        //  Die Ressourcen der Seite - ggf. aus dem Seitenbaum geerbt. Sie
        //  werden gleich materialisiert; hier zählen erst einmal die bereits
        //  vergebenen /XObject-Namen.
        QByteArray resInner;
        for (int walk = pageObj, guard = 0; walk >= 0 && guard < 32; ++guard) {
            const QByteArray wd = doc.dictOf(walk);
            if (wd.isEmpty()) break;
            const QByteArray res = doc.resolved(wd, "Resources");
            if (res.startsWith("<<")) { resInner = dictOfObject(res); break; }
            walk = refValue(wd, "Parent");
        }
        const QByteArray pageXObj = doc.resolved(resInner, "XObject");
        const QByteArray pageXObjInner =
            pageXObj.startsWith("<<") ? dictOfObject(pageXObj) : QByteArray();

        auto it = refRe.globalMatch(QString::fromLatin1(annots));
        while (it.hasNext()) {
            const auto      one     = it.next();
            const int       annNum  = one.captured(1).toInt();
            const QByteArray annDict = doc.dictOf(annNum);
            if (nameValue(annDict, "Subtype") != "/Widget") {
                kept += one.captured(0).toLatin1() + " ";
                continue;
            }
            removed = true;
            //  Ab hier verschwindet das Widget in jedem Fall aus /Annots -
            //  ein zurückbleibendes Widget ohne /AcroForm wäre genau der
            //  Zustand, den dieser Schritt vermeiden soll.
            const qint64 flags = intValue(annDict, "F");
            if (flags > 0 && (flags & 0x22))    // 2 = Hidden, 32 = NoView
                continue;

            //  /AP -> /N; ist /N ein Zustands-Dict, entscheidet /AS.
            const QByteArray ap = doc.resolved(annDict, "AP");
            if (!ap.startsWith("<<")) continue;
            const QByteArray apInner = dictOfObject(ap);
            int apNum = refValue(apInner, "N");
            if (apNum < 0) {
                const QByteArray nVal = doc.resolved(apInner, "N");
                const QString    as   = nameToString(nameValue(annDict, "AS"));
                if (!nVal.startsWith("<<") || as.isEmpty()) continue;
                apNum = refValue(dictOfObject(nVal), as.toLatin1().constData());
            }
            if (apNum < 0) continue;
            const QByteArray apDict = doc.dictOf(apNum);
            if (apDict.isEmpty()) continue;

            const QVector<double> r = numbersOfArray(doc.resolved(annDict, "Rect"));
            if (r.size() < 4) continue;
            const double rx0 = qMin(r[0], r[2]), rx1 = qMax(r[0], r[2]);
            const double ry0 = qMin(r[1], r[3]), ry1 = qMax(r[1], r[3]);
            if (rx1 - rx0 <= 0.0 || ry1 - ry0 <= 0.0) continue;

            //  /BBox über /Matrix abbilden und die Hülle nehmen.
            QVector<double> b = numbersOfArray(doc.resolved(apDict, "BBox"));
            if (b.size() < 4) b = { 0.0, 0.0, rx1 - rx0, ry1 - ry0 };
            QVector<double> m = numbersOfArray(doc.resolved(apDict, "Matrix"));
            if (m.size() < 6) m = { 1, 0, 0, 1, 0, 0 };
            double bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
            for (int c = 0; c < 4; ++c) {
                const double x = (c == 0 || c == 3) ? b[0] : b[2];
                const double y = (c < 2)            ? b[1] : b[3];
                const double tx = m[0] * x + m[2] * y + m[4];
                const double ty = m[1] * x + m[3] * y + m[5];
                if (c == 0) { bx0 = bx1 = tx; by0 = by1 = ty; }
                bx0 = qMin(bx0, tx); bx1 = qMax(bx1, tx);
                by0 = qMin(by0, ty); by1 = qMax(by1, ty);
            }
            const double bw = bx1 - bx0, bh = by1 - by0;
            const double sx = (bw > 1e-6) ? (rx1 - rx0) / bw : 1.0;
            const double sy = (bh > 1e-6) ? (ry1 - ry0) / bh : 1.0;

            //  Ressourcenname: der Name darf keinen bestehenden Eintrag der
            //  Seite überschreiben - sonst zeichnete die Seite plötzlich unser
            //  Widget an der Stelle ihres eigenen XObjects.
            if (usedNames.isEmpty()) {
                for (const QByteArray& k : dictKeys(pageXObjInner))
                    usedNames.insert(k);
            }
            QByteArray resName = "/MGFf" + QByteArray::number(n++);
            while (usedNames.contains(resName))
                resName = "/MGFf" + QByteArray::number(n++);
            usedNames.insert(resName);
            xobjs += " " + resName + " " + QByteArray::number(apNum) + " 0 R";
            ops += "q " + num(sx) + " 0 0 " + num(sy) + " "
                 + num(rx0 - bx0 * sx) + " " + num(ry0 - by0 * sy) + " cm "
                 + resName + " Do Q\n";
        }
        if (!removed) continue;                // keine Widgets auf dieser Seite

        QByteArray next = pageDict;

        //  (1) Ressourcen: die Seite bekommt ihre EIGENE Kopie samt der neuen
        //      XObjects. Ein von mehreren Seiten geteiltes Ressourcen-Objekt
        //      zu ändern wäre der Weg, auf dem eine Seite die Namen einer
        //      anderen erbt - und geerbte /Resources aus dem Seitenbaum
        //      werden hier ohnehin materialisiert.
        if (!xobjs.isEmpty()) {
            next = setDictKey(next, "Resources",
                              "<<" + setDictKey(resInner, "XObject",
                                                "<<" + pageXObjInner + xobjs + " >>")
                                  + " >>");

            //  (2) Inhalt: der bisherige wird in q/Q eingefasst, damit ein
            //      unbalancierter Grafikzustand nicht auf unsere Zeichnung
            //      durchschlägt; danach kommen die Widgets.
            const int preObj = up.reserveObjNum();
            up.addStream(preObj, 0, QByteArray(), "q\n");
            const int postObj = up.reserveObjNum();
            up.addStream(postObj, 0, QByteArray(), "Q\n" + ops);

            QByteArray refs;
            const QByteArray cont = rawValue(pageDict, "Contents");
            if (cont.startsWith('[')) {
                auto ci = refRe.globalMatch(QString::fromLatin1(cont));
                while (ci.hasNext()) refs += ci.next().captured(0).toLatin1() + " ";
            } else if (!cont.isEmpty() && !cont.startsWith("null")) {
                refs = cont.simplified() + " ";
            }
            next = setDictKey(next, "Contents",
                              "[" + QByteArray::number(preObj) + " 0 R " + refs
                                  + QByteArray::number(postObj) + " 0 R]");
        }

        //  (3) /Annots ohne die Widgets. Eine als REFERENZ abgelegte Liste
        //      wird ersetzt, sonst der Eintrag der Seite.
        const int arrayObj = refValue(pageDict, "Annots");
        if (arrayObj >= 0)
            up.addObject(arrayObj, doc.genOf(arrayObj), "[" + kept + "]");
        else
            next = setDictKey(next, "Annots", "[" + kept + "]");
        up.replaceDict(pageObj, next);
    }

    //  (4) Das Formular selbst auflösen: ohne /Fields ist es keines mehr.
    //      Die Feldobjekte bleiben als Leichen in der Datei - ein neuer
    //      Katalog (PdfAssembler) nimmt sie nicht mit.
    if (formObj >= 0) {
        up.replaceDict(formObj, QByteArray(" /Fields [] "));
    } else {
        QByteArray root = doc.dictOf(doc.rootNum);
        if (!root.isEmpty() && findKey(root, "AcroForm") >= 0)
            up.replaceDict(doc.rootNum,
                           setDictKey(root, "AcroForm", "<< /Fields [] >>"));
    }

    if (up.isEmpty()) return fail(QStringLiteral("nichts festzuschreiben"));
    QString ce;
    if (!up.commit(outputPath, &ce)) return fail(ce);
    return true;
}

} // namespace mg
