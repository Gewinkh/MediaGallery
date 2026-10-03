#include "editor/MarkdownRender.h"

#include "editor/LanguageTable.h"
#include "editor/SyntaxScanner.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>
#include <QUrl>

namespace mg::editor::md {
namespace {

QColor mix(const QColor& a, const QColor& b, qreal t) {
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t),
                            float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

struct Ctx {
    int  indent = 0;        // Listentiefe im aktuellen Rahmen
    bool tight = false;
    bool muted = false;     // Zitat
    int  quoteDepth = 0;
};

class Builder {
public:
    Builder(QTextDocument* d, const Document& md, const RenderStyle& st, const std::atomic<bool>* abort);
    void run();

private:
    QTextDocument* m_doc;
    const Document& m_md;
    const RenderStyle& m_st;
    const std::atomic<bool>* m_abort;
    QTextCursor m_cur;
    // Der Block unter dem Cursor ist leer und noch unbenutzt: der erste eines Rahmens oder der hinter einem Rahmen.
    bool m_fresh = true;
    // Abstaende an Rahmen: Qt malt den Hintergrund eines Rahmens ueber dessen Aussenabstand, und leere Bloecke
    // sowie Blockabstaende direkt an einem Rahmen wirken nicht. Verlaesslich ist nur ein Block mit einem
    // Leerzeichen und fester Zeilenhoehe - der Abstandshalter.
    bool m_afterFrame = false;
    qreal m_gap = 0;
    qreal m_px;
    qreal m_indentPx;
    QColor m_text, m_muted, m_border, m_codeBg, m_link, m_heading, m_codeFg, m_mark;
    QHash<QString, int> m_slugs;
    qint64 m_imageBytes = 0;
    int m_imageNo = 0;

    bool aborted() const { return m_abort && m_abort->load(std::memory_order_relaxed); }
    QTextCharFormat charFormat(const Ctx& c) const;
    void beginBlock(const QTextBlockFormat& bf, const QTextCharFormat& cf);
    void afterFrame(QTextFrame* f, qreal gap);
    void beforeFrame();
    void spacer(qreal height);
    void setLastBottom(qreal px);

    void blocks(const QList<Block>& bl, const Ctx& c);
    void paragraph(const QString& text, const Ctx& c, QTextBlockFormat bf);
    void heading(const Block& b, const Ctx& c);
    void code(const Block& b, const Ctx& c);
    void quote(const Block& b, const Ctx& c);
    void list(const Block& b, const Ctx& c);
    void table(const Block& b, const Ctx& c);
    void rule(const Ctx& c, qreal top, qreal bottom);
    void details(const Block& b, const Ctx& c);
    void frontMatter(const Block& b);
    void footnotes();
    void inlines(const QList<Run>& runs, const QTextCharFormat& base);
    void image(const Run& r, const QTextCharFormat& base);
    struct Box {
        QTextFrame* innen = nullptr;
        QTextFrame* aussen = nullptr;
    };
    Box box(const Ctx& c, const QColor& bg, qreal pad);
    void closeBox(const Box& b, qreal gap);
};

Builder::Builder(QTextDocument* d, const Document& md, const RenderStyle& st, const std::atomic<bool>* abort)
    : m_doc(d), m_md(md), m_st(st), m_abort(abort), m_cur(d) {
    m_px = st.body.pixelSize() > 0 ? st.body.pixelSize() : 15;
    m_indentPx = qRound(m_px * 1.7);
    const SyntaxPalette& p = st.palette;
    m_text = p.text;
    m_muted = mix(p.text, p.background, 0.35);
    m_border = mix(p.text, p.background, 0.78);
    m_codeBg = mix(p.background, p.text, 0.055);
    m_link = p.colorFor(Tok::Link);
    m_heading = p.colorFor(Tok::Heading);
    m_codeFg = p.colorFor(Tok::CodeSpan);
    m_mark = p.colorFor(Tok::Number);
    m_mark.setAlphaF(0.3f);
}

QTextCharFormat Builder::charFormat(const Ctx& c) const {
    QTextCharFormat f;
    f.setFont(m_st.body);
    f.setForeground(c.muted ? m_muted : m_text);
    return f;
}

void Builder::spacer(qreal height) {
    QTextBlockFormat bf;
    bf.setLineHeight(qMax<qreal>(1, height), QTextBlockFormat::FixedHeight);
    QTextCharFormat cf;
    QFont winzig = m_st.body;
    winzig.setPixelSize(1);
    cf.setFont(winzig);
    m_cur.setBlockFormat(bf);
    m_cur.setBlockCharFormat(cf);
    m_cur.insertText(QStringLiteral(" "), cf);
    m_fresh = false;
    m_afterFrame = false;
    m_gap = 0;
}

void Builder::beginBlock(const QTextBlockFormat& bf, const QTextCharFormat& cf) {
    if (m_fresh && m_afterFrame) {
        spacer(m_gap - bf.topMargin());
        m_cur.insertBlock(bf, cf);
    } else if (m_fresh) {
        m_cur.setBlockFormat(bf);
        m_cur.setBlockCharFormat(cf);
    } else {
        m_cur.insertBlock(bf, cf);
    }
    m_fresh = false;
}

void Builder::afterFrame(QTextFrame* f, qreal gap) {
    m_cur = f->lastCursorPosition();
    m_cur.movePosition(QTextCursor::NextBlock);
    m_fresh = true;
    m_afterFrame = true;
    m_gap = gap;
}

// Vor jedem Rahmen ein Abstandshalter: der untere Abstand des Blocks davor wirkt dann, direkt am Rahmen nicht.
void Builder::beforeFrame() {
    if (!m_fresh) {
        m_cur.insertBlock();
        m_fresh = true;
    }
    spacer(m_afterFrame ? m_gap : 1);
}

void Builder::setLastBottom(qreal px) {
    if (m_fresh) {
        if (m_afterFrame) m_gap = qMax(m_gap, px);
        return;
    }
    QTextBlockFormat bf = m_cur.blockFormat();
    if (bf.bottomMargin() >= px) return;
    bf.setBottomMargin(px);
    m_cur.setBlockFormat(bf);
}

// Kein Rand: ein Rahmen mit `border` > 0, der keine Tabelle ist, bringt die QML-Textflaeche beim Zeichnen zum
// Absturz (QQuickTextNodeEngine, Qt 6.11). Eingerueckt wird ueber einen AEUSSEREN Rahmen ohne Hintergrund - den
// eigenen Aussenabstand wuerde der Hintergrund mit uebermalen.
Builder::Box Builder::box(const Ctx& c, const QColor& bg, qreal pad) {
    beforeFrame();
    Box b;
    if (c.indent > 0) {
        QTextFrameFormat of;
        of.setBorder(0);
        of.setPadding(0);
        of.setMargin(0);
        of.setLeftMargin(c.indent * m_indentPx);
        b.aussen = m_cur.insertFrame(of);
        m_fresh = true;
        m_afterFrame = false;
        spacer(0);
    }
    QTextFrameFormat ff;
    ff.setBackground(bg);
    ff.setBorder(0);
    ff.setPadding(pad);
    ff.setMargin(0);
    b.innen = m_cur.insertFrame(ff);
    if (!b.aussen) b.aussen = b.innen;
    m_fresh = true;
    m_afterFrame = false;
    return b;
}

// Der Pflichtblock hinter einem inneren Rahmen stuende sonst in voller Zeilenhoehe am Ende des Kastens.
void Builder::closeBox(const Box& b, qreal gap) {
    if (m_fresh && m_afterFrame) spacer(0);
    if (b.aussen != b.innen) {
        afterFrame(b.innen, 0);
        spacer(0);
    }
    afterFrame(b.aussen, gap);
}

void Builder::inlines(const QList<Run>& runs, const QTextCharFormat& base) {
    for (const Run& r : runs) {
        if (r.kind == Run::Break) {
            m_cur.insertText(QString(QChar::LineSeparator), base);
            continue;
        }
        if (r.kind == Run::Image) {
            image(r, base);
            continue;
        }
        QTextCharFormat f = base;
        if (r.flags & Bold) f.setFontWeight(QFont::Bold);
        if (r.flags & Italic) f.setFontItalic(true);
        if (r.flags & Strike) f.setFontStrikeOut(true);
        if (r.flags & Underline) f.setFontUnderline(true);
        if (r.flags & (Code | Kbd)) {
            f.setFontFixedPitch(true);
            QFont mono = m_st.mono;
            mono.setPixelSize(qMax(8, qRound(base.font().pixelSize() * 0.9)));
            f.setFont(mono, QTextCharFormat::FontPropertiesSpecifiedOnly);
            f.setFontWeight(r.flags & Bold ? QFont::Bold : QFont::Normal);
            f.setBackground(m_codeBg);
            if (!(r.flags & Kbd)) f.setForeground(m_codeFg);
        }
        if (r.flags & Sub) f.setVerticalAlignment(QTextCharFormat::AlignSubScript);
        if (r.flags & Sup) f.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
        if (r.flags & Mark) f.setBackground(m_mark);
        if (!r.href.isEmpty()) {
            f.setAnchor(true);
            f.setAnchorHref(r.href);
            f.setForeground(m_link);
            // Hochgestellt (Fussnote) laege der Strich auf der Grundlinie, unter nichts.
            f.setFontUnderline(!(r.flags & (Sup | Sub)));
        }
        m_cur.insertText(r.text, f);
    }
}

void Builder::image(const Run& r, const QTextCharFormat& base) {
    const QUrl url(r.src);
    QString pfad;
    if (url.isLocalFile()) pfad = url.toLocalFile();
    else if (url.scheme().isEmpty()) pfad = QDir(m_st.baseDir).filePath(QUrl::fromPercentEncoding(r.src.toUtf8()));

    QImage img;
    if (!pfad.isEmpty() && m_imageBytes < m_st.imageBudget) {
        QImageReader reader(pfad);
        reader.setAutoTransform(true);
        const QSize s = reader.size();
        if (s.isValid() && s.width() > m_st.maxImageWidth)
            reader.setScaledSize(s.scaled(m_st.maxImageWidth, s.height(), Qt::KeepAspectRatio));
        if (s.isValid() && qint64(s.width()) * s.height() <= 100'000'000) img = reader.read();
    }
    if (img.isNull() || m_imageBytes + img.sizeInBytes() > m_st.imageBudget) {
        // Nicht ladbar (fern, fehlt, zu gross): Alternativtext als Verweis auf die Quelle.
        QTextCharFormat f = base;
        f.setFontItalic(true);
        f.setForeground(m_link);
        f.setAnchor(true);
        f.setAnchorHref(r.src);
        m_cur.insertText(r.text.isEmpty() ? r.src : r.text, f);
        return;
    }
    m_imageBytes += img.sizeInBytes();
    const QString name = QStringLiteral("mg-img:%1").arg(m_imageNo++);
    m_doc->addResource(QTextDocument::ImageResource, QUrl(name), img);
    QTextImageFormat f;
    f.setName(name);
    f.setWidth(img.width());
    f.setHeight(img.height());
    f.setMaximumWidth(QTextLength(QTextLength::PercentageLength, 100));
    if (!r.href.isEmpty()) {
        f.setAnchor(true);
        f.setAnchorHref(r.href);
    }
    m_cur.insertImage(f);
}

void Builder::paragraph(const QString& text, const Ctx& c, QTextBlockFormat bf) {
    bf.setBottomMargin(c.tight ? m_px * 0.2 : m_px * 0.8);
    if (!bf.hasProperty(QTextFormat::ObjectIndex)) bf.setIndent(c.indent);
    const QTextCharFormat cf = charFormat(c);
    beginBlock(bf, cf);
    inlines(parseInlines(text, m_md.refs, m_md.footnotes), cf);
}

//  Sprungziel fuer `[text](#ziel)`: die Ansicht liest `kAnchorProperty`, der PDF-Schreiber nur `anchorNames` -
//  ohne sie zeigte ein Verweis im PDF auf ein Ziel, das es nicht gibt. Nur das erste Zeichen, ein Ziel je Name.
void sprungziel(const QTextBlock& block, const QString& name) {
    if (block.length() < 2) return;
    QTextCursor k(block);
    k.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    QTextCharFormat f;
    f.setAnchor(true);
    f.setAnchorNames({name});
    k.mergeCharFormat(f);
}

void Builder::heading(const Block& b, const Ctx& c) {
    static constexpr qreal kScale[] = {2.0, 1.55, 1.28, 1.1, 1.0, 0.9};
    const int lvl = qBound(1, b.level, 6);
    const QList<Run> runs = parseInlines(b.text, m_md.refs, m_md.footnotes);

    QString slug = slugify(plainText(runs));
    const int n = m_slugs.value(slug, 0);
    m_slugs.insert(slug, n + 1);
    if (n > 0) slug += QLatin1Char('-') + QString::number(n);

    QTextBlockFormat bf;
    bf.setHeadingLevel(lvl);
    bf.setTopMargin(m_doc->isEmpty() ? 0 : m_px * (lvl <= 2 ? 1.5 : 1.15));
    bf.setBottomMargin(m_px * (lvl <= 2 ? 0.3 : 0.5));
    bf.setIndent(c.indent);
    bf.setProperty(kAnchorProperty, slug);
    QTextCharFormat cf = charFormat(c);
    QFont f = m_st.body;
    f.setPixelSize(qRound(m_px * kScale[lvl - 1]));
    f.setBold(true);
    cf.setFont(f);
    if (!c.muted) cf.setForeground(lvl == 6 ? m_muted : m_heading);
    beginBlock(bf, cf);
    inlines(runs, cf);
    sprungziel(m_cur.block(), slug);
    if (lvl <= 2) {
        QTextBlockFormat knapp = m_cur.blockFormat();
        knapp.setBottomMargin(m_px * 0.25);
        m_cur.setBlockFormat(knapp);
        rule(c, m_px * 0.25, m_px * 0.7);
    }
}

// Eine Linie ist ein Rahmen von 1 px Hoehe mit Hintergrund. Qts Linien-Eigenschaft am Block zeichnet die
// QML-Textflaeche nicht.
void Builder::rule(const Ctx& c, qreal top, qreal bottom) {
    setLastBottom(top);
    beforeFrame();
    QTextFrameFormat ff;
    ff.setHeight(1);
    ff.setBackground(m_border);
    ff.setBorder(0);
    ff.setPadding(0);
    ff.setMargin(0);
    ff.setLeftMargin(c.indent * m_indentPx);
    QTextFrame* f = m_cur.insertFrame(ff);
    QTextCharFormat winzig;
    QFont klein = m_st.body;
    klein.setPixelSize(1);
    winzig.setFont(klein);
    QTextBlockFormat leer;
    leer.setLineHeight(1, QTextBlockFormat::FixedHeight);
    m_cur.setBlockFormat(leer);
    m_cur.setBlockCharFormat(winzig);
    afterFrame(f, bottom);
}

// Der Rahmen ist durchsichtig und haelt nur den Platz: runde Ecken, Kopfleiste und Kopier-Knopf kann die
// QML-Textflaeche nicht zeichnen, das tut die Oberflaeche dahinter (`MarkdownView::codeBlocks`).
void Builder::code(const Block& b, const Ctx& c) {
    beforeFrame();
    QTextFrameFormat ff;
    ff.setBorder(0);
    ff.setPadding(m_px * 0.75);
    ff.setMargin(0);
    ff.setTopMargin(kCodeHeader);
    ff.setLeftMargin(c.indent * m_indentPx);
    ff.setProperty(kCodeTextProperty, b.text);
    ff.setProperty(kCodeLangProperty, b.info);
    QTextFrame* f = m_cur.insertFrame(ff);
    m_fresh = true;
    m_afterFrame = false;
    const LanguageDef& def = languageForId(languageForFence(b.info));
    QTextCharFormat plain;
    QFont mono = m_st.mono;
    mono.setPixelSize(qMax(8, qRound(m_px * 0.88)));
    plain.setFont(mono);
    plain.setForeground(m_text);
    QTextCharFormat tokFmt[int(Tok::Count)];
    for (int t = 0; t < int(Tok::Count); ++t) {
        tokFmt[t] = plain;
        tokFmt[t].setForeground(m_st.palette.colorFor(Tok(t)));
        if (Tok(t) == Tok::Heading) tokFmt[t].setFontWeight(QFont::Bold);
    }
    const QTextBlockFormat bf;
    int state = 0;
    SpanList spans;
    const QStringList lines = b.text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        beginBlock(bf, plain);
        spans.clear();
        state = def.kind == ScannerKind::PlainText ? 0 : scanLine(line, def, state, spans);
        qsizetype pos = 0;
        for (const Span& s : spans) {
            if (s.start < pos || s.start + s.length > line.size()) continue;
            if (s.start > pos) m_cur.insertText(line.mid(pos, s.start - pos), plain);
            m_cur.insertText(line.mid(s.start, s.length), tokFmt[int(s.tok)]);
            pos = s.start + s.length;
        }
        if (pos < line.size()) m_cur.insertText(line.mid(pos), plain);
    }
    afterFrame(f, m_px * 0.8);
}

// Das Zitat ist ein getoenter Kasten. Ein Balken am linken Rand geht mit der QML-Textflaeche nicht: sie zeichnet
// keine einzelnen Zellkanten, Zellhintergruende nur so hoch wie der eigene Inhalt, und Rahmen mit Rand stuerzen ab.
void Builder::quote(const Block& b, const Ctx& c) {
    // Durchscheinend: ein Code-Kasten der Oberflaeche liegt darunter und bliebe sonst verdeckt.
    QColor toenung = m_st.palette.text;
    toenung.setAlphaF(float(0.04 * (c.quoteDepth + 1)));
    const Box kasten = box(c, toenung, m_px * 0.55);
    Ctx innen;
    innen.muted = true;
    innen.quoteDepth = c.quoteDepth + 1;
    blocks(b.children, innen);
    if (!m_fresh) {
        QTextBlockFormat letzter = m_cur.blockFormat();
        letzter.setBottomMargin(0);
        m_cur.setBlockFormat(letzter);
    }
    closeBox(kasten, m_px * 0.8);
}

void Builder::list(const Block& b, const Ctx& c) {
    QTextListFormat lf;
    if (b.ordered) {
        lf.setStyle(QTextListFormat::ListDecimal);
        lf.setStart(b.start);
    } else {
        static constexpr QTextListFormat::Style kStil[] = {
            QTextListFormat::ListDisc, QTextListFormat::ListCircle, QTextListFormat::ListSquare};
        lf.setStyle(kStil[c.indent % 3]);
    }
    lf.setIndent(c.indent + 1);
    QTextList* liste = nullptr;

    Ctx inner = c;
    inner.indent = c.indent + 1;
    inner.tight = b.tight;

    for (const Block& item : b.children) {
        if (aborted()) return;
        QTextBlockFormat bf;
        bf.setBottomMargin(b.tight ? m_px * 0.2 : m_px * 0.6);
        if (item.task >= 0)
            bf.setMarker(item.task ? QTextBlockFormat::MarkerType::Checked
                                   : QTextBlockFormat::MarkerType::Unchecked);
        const bool mitText = !item.children.isEmpty()
                          && (item.children.first().kind == Block::Paragraph
                              || item.children.first().kind == Block::Heading);
        if (mitText) paragraph(item.children.first().text, inner, bf);
        else beginBlock(bf, charFormat(inner));
        QTextBlockFormat eigen = m_cur.blockFormat();
        eigen.setBottomMargin(bf.bottomMargin());
        eigen.setIndent(0);
        m_cur.setBlockFormat(eigen);
        if (!liste) liste = m_cur.createList(lf);
        else liste->add(m_cur.block());

        const QList<Block> rest = mitText ? item.children.mid(1) : item.children;
        blocks(rest, inner);
    }
    if (c.indent == 0) setLastBottom(m_px * 0.8);
}

void Builder::table(const Block& b, const Ctx& c) {
    const int rows = int(b.rows.size());
    const int cols = int(b.aligns.size());
    if (rows == 0 || cols == 0) return;
    QTextTableFormat tf;
    // Ohne zusammenfallende Raender, dafuer Zellabstand -1: dieselbe Einzellinie, aber Qt spart die Randberechnung
    // je Zelle (gemessen an 527 Tabellenzeilen: Layout 187 -> 90 ms, erstes Bild 87 -> 70 ms).
    tf.setBorderCollapse(false);
    tf.setBorder(1);
    tf.setBorderBrush(m_border);
    tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    tf.setCellSpacing(-1);
    tf.setCellPadding(m_px * 0.4);
    tf.setHeaderRowCount(1);
    tf.setMargin(0);
    tf.setLeftMargin(c.indent * m_indentPx);
    beforeFrame();
    QTextTable* t = m_cur.insertTable(rows, cols, tf);

    Ctx inner = c;
    inner.indent = 0;
    for (int r = 0; r < rows; ++r) {
        if (aborted()) break;
        for (int col = 0; col < cols; ++col) {
            // Keine Zellhintergruende: die QML-Textflaeche malt sie nur hinter die Buchstaben.
            m_cur = t->cellAt(r, col).firstCursorPosition();
            QTextBlockFormat bf;
            switch (b.aligns[col]) {
            case Align::Center: bf.setAlignment(Qt::AlignHCenter); break;
            case Align::Right:  bf.setAlignment(Qt::AlignRight); break;
            default:            bf.setAlignment(Qt::AlignLeft); break;
            }
            QTextCharFormat cf = charFormat(inner);
            if (r == 0) cf.setFontWeight(QFont::Bold);
            m_cur.setBlockFormat(bf);
            m_cur.setBlockCharFormat(cf);
            inlines(parseInlines(b.rows[r][col], m_md.refs, m_md.footnotes), cf);
        }
    }
    afterFrame(t, m_px * 0.9);
}

// Die Summary steht in einem eigenen Rahmen: Blockhintergruende malt die QML-Textflaeche nur hinter den Text.
void Builder::details(const Block& b, const Ctx& c) {
    const bool offen = b.open != m_st.flipped.contains(b.id);
    const QString href = QString(kDetailsScheme) + QString::number(b.id);
    const Box kasten = box(c, m_codeBg, m_px * 0.35);
    QTextCharFormat cf = charFormat(c);
    beginBlock(QTextBlockFormat(), cf);
    QTextCharFormat lf = cf;
    lf.setFontWeight(QFont::Bold);
    lf.setAnchor(true);
    lf.setAnchorHref(href);
    lf.setForeground(m_link);
    QList<Run> runs = parseInlines(b.text.isEmpty() ? QStringLiteral("Details") : b.text, m_md.refs,
                                   m_md.footnotes);
    for (Run& r : runs) r.href = href;
    inlines(runs, lf);
    QTextCharFormat hint = cf;
    hint.setForeground(m_muted);
    hint.setAnchor(true);
    hint.setAnchorHref(href);
    m_cur.insertText(QStringLiteral("   ") + (offen ? m_st.hideLabel : m_st.showLabel), hint);
    closeBox(kasten, offen ? m_px * 0.5 : m_px * 0.8);
    if (offen) blocks(b.children, c);
}

// Die Frontmatter als Karte: der Rahmen haelt den Platz, Ecken und Akzentstrich zeichnet die Oberflaeche. Innen
// eine Tabelle ohne Linien; Listen und einzelne Woerter werden zu Etiketten.
void Builder::frontMatter(const Block& b) {
    beforeFrame();
    QTextFrameFormat ff;
    ff.setBorder(0);
    ff.setPadding(m_px * 0.7);
    ff.setMargin(0);
    ff.setProperty(kMetaProperty, true);
    QTextFrame* f = m_cur.insertFrame(ff);
    m_fresh = true;
    m_afterFrame = false;

    QTextTableFormat tf;
    tf.setBorder(0);
    tf.setCellSpacing(0);
    tf.setCellPadding(m_px * 0.2);
    tf.setMargin(0);
    tf.setLeftMargin(m_px * 0.7);
    beforeFrame();
    QTextTable* t = m_cur.insertTable(int(b.pairs.size()), 2, tf);

    QTextCharFormat schluessel;
    QFont klein = m_st.body;
    klein.setPixelSize(qMax(8, qRound(m_px * 0.72)));
    klein.setCapitalization(QFont::AllUppercase);
    klein.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
    klein.setWeight(QFont::DemiBold);
    schluessel.setFont(klein);
    schluessel.setForeground(m_muted);
    QTextCharFormat wert;
    QFont normal = m_st.body;
    normal.setPixelSize(qMax(8, qRound(m_px * 0.9)));
    wert.setFont(normal);
    wert.setForeground(m_text);
    QTextCharFormat etikett = wert;
    etikett.setBackground(mix(m_st.palette.background, m_st.palette.text, 0.11));
    QTextCharFormat luecke = wert;

    for (int r = 0; r < b.pairs.size(); ++r) {
        // Mittig: der kleinere Schluessel saesse sonst hoeher als der Wert daneben.
        for (int col = 0; col < 2; ++col) {
            QTextTableCell zelle = t->cellAt(r, col);
            QTextTableCellFormat zf = zelle.format().toTableCellFormat();
            zf.setVerticalAlignment(QTextCharFormat::AlignMiddle);
            zelle.setFormat(zf);
        }
        QTextCursor kc = t->cellAt(r, 0).firstCursorPosition();
        kc.insertText(b.pairs[r].first, schluessel);

        QTextCursor vc = t->cellAt(r, 1).firstCursorPosition();
        const QString v = b.pairs[r].second.trimmed();
        QStringList teile;
        if (v.size() >= 2 && v.front() == u'[' && v.back() == u']') {
            for (const QString& x : v.mid(1, v.size() - 2).split(u',', Qt::SkipEmptyParts))
                if (!x.trimmed().isEmpty()) teile << x.trimmed();
        } else if (!v.isEmpty() && !v.contains(u' ') && v.size() <= 32) {
            teile << v;
        }
        if (teile.isEmpty()) {
            vc.insertText(v, wert);
            continue;
        }
        // Schmale Leerzeichen innen geben dem Etikett Luft; Qt malt den Zeichenhintergrund nur hinter die Zeichen.
        for (int i = 0; i < teile.size(); ++i) {
            if (i) vc.insertText(QStringLiteral("  "), luecke);
            vc.insertText(QString(QChar(0x2009)) + teile[i] + QChar(0x2009), etikett);
        }
    }
    m_cur = t->lastCursorPosition();
    m_cur.movePosition(QTextCursor::NextBlock);
    spacer(0);
    afterFrame(f, m_px * 1.4);
}

void Builder::footnotes() {
    if (m_md.footnotes.isEmpty()) return;
    rule(Ctx{}, m_px * 0.6, m_px * 0.6);
    QTextListFormat lf;
    lf.setStyle(QTextListFormat::ListDecimal);
    lf.setIndent(1);
    QTextList* liste = nullptr;
    Ctx c;
    c.muted = true;
    c.tight = true;
    for (const Footnote& fn : m_md.footnotes) {
        QTextBlockFormat bf;
        bf.setProperty(kAnchorProperty, QStringLiteral("fn-") + fn.label);
        paragraph(fn.text, c, bf);
        sprungziel(m_cur.block(), QStringLiteral("fn-") + fn.label);
        QTextBlockFormat eigen = m_cur.blockFormat();
        eigen.setIndent(0);
        m_cur.setBlockFormat(eigen);
        if (!liste) liste = m_cur.createList(lf);
        else liste->add(m_cur.block());
    }
}

void Builder::blocks(const QList<Block>& bl, const Ctx& c) {
    for (const Block& b : bl) {
        if (aborted()) return;
        switch (b.kind) {
        case Block::Paragraph:   paragraph(b.text, c, QTextBlockFormat()); break;
        case Block::Heading:     heading(b, c); break;
        case Block::Code:        code(b, c); break;
        case Block::Quote:       quote(b, c); break;
        case Block::List:        list(b, c); break;
        case Block::Item:        blocks(b.children, c); break;
        case Block::Table:       table(b, c); break;
        case Block::Rule:        rule(c, m_px * 0.6, m_px * 1.2); break;
        case Block::Details:     details(b, c); break;
        case Block::FrontMatter: frontMatter(b); break;
        }
    }
}

void Builder::run() {
    QTextFrameFormat root = m_doc->rootFrame()->frameFormat();
    root.setMargin(0);
    m_doc->rootFrame()->setFrameFormat(root);
    m_doc->setDocumentMargin(0);
    m_doc->setIndentWidth(m_indentPx);
    m_doc->setDefaultFont(m_st.body);
    // Ein Bearbeitungsblock um den ganzen Aufbau: sonst berechnet jedes `insertText` die Cursorspalte neu und
    // zerlegt dafuer den wachsenden Absatz jedes Mal ganz (gemessen: 580-KB-Datei 869 -> 30 ms).
    QTextCursor klammer(m_doc);
    klammer.beginEditBlock();
    blocks(m_md.blocks, Ctx{});
    footnotes();
    klammer.endEditBlock();
}

}  // namespace

QString languageForFence(const QString& info) {
    const QString w = info.toLower();
    if (w.isEmpty() || w == u"text" || w == u"txt" || w == u"plain" || w == u"plaintext" || w == u"output")
        return {};
    static const QHash<QString, QString> alias = {
        {QStringLiteral("c"), QStringLiteral("cpp")},       {QStringLiteral("c++"), QStringLiteral("cpp")},
        {QStringLiteral("h"), QStringLiteral("cpp")},       {QStringLiteral("nasm"), QStringLiteral("asm")},
        {QStringLiteral("gas"), QStringLiteral("asm")},     {QStringLiteral("x86asm"), QStringLiteral("asm")},
        {QStringLiteral("assembly"), QStringLiteral("asm")}, {QStringLiteral("sh"), QStringLiteral("shell")},
        {QStringLiteral("bash"), QStringLiteral("shell")},  {QStringLiteral("zsh"), QStringLiteral("shell")},
        {QStringLiteral("console"), QStringLiteral("shell")}, {QStringLiteral("make"), QStringLiteral("shell")},
        {QStringLiteral("makefile"), QStringLiteral("shell")}, {QStringLiteral("py"), QStringLiteral("python")},
        {QStringLiteral("javascript"), QStringLiteral("js")}, {QStringLiteral("typescript"), QStringLiteral("js")},
        {QStringLiteral("ts"), QStringLiteral("js")},       {QStringLiteral("html"), QStringLiteral("xml")},
        {QStringLiteral("md"), QStringLiteral("markdown")}, {QStringLiteral("yml"), QStringLiteral("yaml")},
        {QStringLiteral("rs"), QStringLiteral("rust")},     {QStringLiteral("cs"), QStringLiteral("csharp")},
    };
    const QString id = alias.value(w, w);
    if (languageForId(id).kind != ScannerKind::PlainText) return id;
    const LanguageDef& perEndung = languageForPath(QStringLiteral("x.") + w);
    return perEndung.kind != ScannerKind::PlainText ? QString(perEndung.id) : QString();
}

QTextDocument* buildDocument(const Document& doc, const RenderStyle& style, const std::atomic<bool>* abort) {
    auto* d = new QTextDocument;
    d->setUndoRedoEnabled(false);
    Builder(d, doc, style, abort).run();
    return d;
}

}  // namespace mg::editor::md
