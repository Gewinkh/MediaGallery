// ICONS_H
#ifndef MEDIAGALLERY_ICONS_H
#define MEDIAGALLERY_ICONS_H
#include <QIcon>
#include <QIconEngine>
#include <QPixmap>
#include <QPainter>
#include <QSvgRenderer>
#include <QByteArray>

// ─────────────────────────────────────────────────────────────────────────────
//  Icons  –  Lazy-cached SVG icons, created once and reused.
//
//  Usage:
//    button->setIcon(Icons::trash());
//    button->setIconSize(QSize(16, 16));
//
//  All icons are rendered from inline SVG paths into QPixmap/QIcon on first
//  call and then stored in a static local — zero heap overhead afterwards.
// ─────────────────────────────────────────────────────────────────────────────
class Icons {
public:
    // ── Standard UI icons ───────────────────────────────────────────────────
    static QIcon calendar();      // 📅  date edit button
    static QIcon trash();         // 🗑  delete
    static QIcon pencil();        // ✏  rename
    static QIcon folder();        // 📂  category / open folder
    static QIcon tag();           // 🏷  tag
    static QIcon palette();       // 🎨  change color
    static QIcon image();         // 🖼  image media type
    static QIcon play();          // ▶  play / video  (with built-in dark circle – for thumbnail overlay)
    static QIcon playBare();      // ▶  play triangle only – no circle (for buttons that already have a circular background)
    static QIcon pause();         // ⏸  pause
    static QIcon music();         // 🎵  audio
    static QIcon volumeOn();      // 🔊  sound on
    static QIcon volumeOff();     // 🔇  muted
    static QIcon lock();          // 🔒  locked overlay
    static QIcon warning();       // ⚠  load error
    static QIcon xMark();         // ✕  cancel / close
    static QIcon plusMark();      // ✚  new tag
    static QIcon circle();        // ⬤  color dot toggle
    static QIcon arrowUp();       // ↑  sort ascending
    static QIcon arrowDown();     // ↓  sort descending
    static QIcon arrowIndent();   // ↳  depth indent
    static QIcon shuffle();       // 🔀  random / shuffle
    static QIcon pdf();           // PDF document

    // ── Convenience: render SVG data to a QIcon at given pixel size ─────────
    static QIcon fromSvg(const QByteArray& svgData, int size = 32);

private:
    Icons() = delete;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Inline implementation (header-only, everything is inline / static-local)
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
//  SvgIconEngine – renders SVG at the exact requested pixel size every time,
//  so icons stay sharp whether displayed at 16 px or 64 px.
// ─────────────────────────────────────────────────────────────────────────────
namespace { // anonymous – prevents ODR violations across translation units
class SvgIconEngine : public QIconEngine {
public:
    explicit SvgIconEngine(const QByteArray& svgData) : m_svg(svgData) {}

    void paint(QPainter* painter, const QRect& rect,
               QIcon::Mode /*mode*/, QIcon::State /*state*/) override
    {
        QSvgRenderer renderer(m_svg);
        renderer.render(painter, rect);
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pix(size);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        paint(&painter, QRect(QPoint(0,0), size), mode, state);
        return pix;
    }

    QIconEngine* clone() const override { return new SvgIconEngine(m_svg); }

private:
    QByteArray m_svg;
};
} // anonymous namespace

inline QIcon Icons::fromSvg(const QByteArray& svgData, int /*size*/)
{
    // size-parameter is kept for API compatibility but ignored —
    // SvgIconEngine renders at the actual requested size instead.
    return QIcon(new SvgIconEngine(svgData));
}

// ── calendar ────────────────────────────────────────────────────────────────
inline QIcon Icons::calendar()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#c8ddd8" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <rect x="3" y="4" width="18" height="18" rx="2"/>
  <line x1="16" y1="2" x2="16" y2="6"/>
  <line x1="8"  y1="2" x2="8"  y2="6"/>
  <line x1="3"  y1="10" x2="21" y2="10"/>
</svg>)svg", 24);
    return icon;
}

// ── trash ────────────────────────────────────────────────────────────────────
inline QIcon Icons::trash()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#e07070" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polyline points="3 6 5 6 21 6"/>
  <path d="M19 6l-1 14H6L5 6"/>
  <path d="M10 11v6M14 11v6"/>
  <path d="M9 6V4h6v2"/>
</svg>)svg", 24);
    return icon;
}

// ── pencil ────────────────────────────────────────────────────────────────────
inline QIcon Icons::pencil()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#80a8ff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"/>
  <path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"/>
</svg>)svg", 24);
    return icon;
}

// ── folder ────────────────────────────────────────────────────────────────────
inline QIcon Icons::folder()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#00c8b4" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"/>
</svg>)svg", 24);
    return icon;
}

// ── tag ─────────────────────────────────────────────────────────────────────
inline QIcon Icons::tag()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#c8ddd8" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M20.59 13.41l-7.17 7.17a2 2 0 0 1-2.83 0L2 12V2h10l8.59 8.59a2 2 0 0 1 0 2.82z"/>
  <line x1="7" y1="7" x2="7.01" y2="7"/>
</svg>)svg", 24);
    return icon;
}

// ── palette ──────────────────────────────────────────────────────────────────
inline QIcon Icons::palette()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#c8a0ff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <circle cx="12" cy="12" r="10"/>
  <circle cx="8.5"  cy="8.5"  r="1.5" fill="#c8a0ff" stroke="none"/>
  <circle cx="15.5" cy="8.5"  r="1.5" fill="#c8a0ff" stroke="none"/>
  <circle cx="12"   cy="16"   r="1.5" fill="#c8a0ff" stroke="none"/>
</svg>)svg", 24);
    return icon;
}

// ── image ────────────────────────────────────────────────────────────────────
inline QIcon Icons::image()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#c8ddd8" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <rect x="3" y="3" width="18" height="18" rx="2"/>
  <circle cx="8.5" cy="8.5" r="1.5"/>
  <polyline points="21 15 16 10 5 21"/>
</svg>)svg", 24);
    return icon;
}

// ── play ─────────────────────────────────────────────────────────────────────
inline QIcon Icons::play()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="none">
  <circle cx="12" cy="12" r="11" fill="#000000" fill-opacity="0.5"/>
  <polygon points="9.5,7 18,12 9.5,17" fill="#ffffff" fill-opacity="0.80"/>
</svg>)svg", 24);
    return icon;
}

// ── playBare (no circle background – triangle only, for buttons with their own circle style) ──
inline QIcon Icons::playBare()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="none">
  <polygon points="8,5 20,12 8,19" fill="#ffffff" fill-opacity="0.90"/>
</svg>)svg", 24);
    return icon;
}

// ── pause ─────────────────────────────────────────────────────────────────────
inline QIcon Icons::pause()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"
     fill="none" stroke="none">
  <rect x="6"  y="4" width="4" height="16" fill="#ffffff" fill-opacity="0.82"/>
  <rect x="14" y="4" width="4" height="16" fill="#ffffff" fill-opacity="0.82"/>
</svg>)svg", 24);
    return icon;
}

// ── music ─────────────────────────────────────────────────────────────────────
inline QIcon Icons::music()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#c8ddd8" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M9 18V5l12-2v13"/>
  <circle cx="6"  cy="18" r="3"/>
  <circle cx="18" cy="16" r="3"/>
</svg>)svg", 24);
    return icon;
}

// ── volumeOn ─────────────────────────────────────────────────────────────────
inline QIcon Icons::volumeOn()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#00c8b4" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polygon points="11 5 6 9 2 9 2 15 6 15 11 19"/>
  <path d="M19.07 4.93a10 10 0 0 1 0 14.14"/>
  <path d="M15.54 8.46a5 5 0 0 1 0 7.07"/>
</svg>)svg", 24);
    return icon;
}

// ── volumeOff ─────────────────────────────────────────────────────────────────
inline QIcon Icons::volumeOff()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#e07070" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polygon points="11 5 6 9 2 9 2 15 6 15 11 19"/>
  <line x1="23" y1="9"  x2="17" y2="15"/>
  <line x1="17" y1="9"  x2="23" y2="15"/>
</svg>)svg", 24);
    return icon;
}

// ── lock ─────────────────────────────────────────────────────────────────────
inline QIcon Icons::lock()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#c8ddd8" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <rect x="3" y="11" width="18" height="11" rx="2"/>
  <path d="M7 11V7a5 5 0 0 1 10 0v4"/>
</svg>)svg", 24);
    return icon;
}

// ── warning ─────────────────────────────────────────────────────────────────
inline QIcon Icons::warning()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#f0c040" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"/>
  <line x1="12" y1="9" x2="12" y2="13"/>
  <line x1="12" y1="17" x2="12.01" y2="17"/>
</svg>)svg", 24);
    return icon;
}

// ── xMark ─────────────────────────────────────────────────────────────────
inline QIcon Icons::xMark()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#e07070" stroke-width="2.5" stroke-linecap="round">
  <line x1="18" y1="6"  x2="6"  y2="18"/>
  <line x1="6"  y1="6"  x2="18" y2="18"/>
</svg>)svg", 24);
    return icon;
}

// ── plusMark ─────────────────────────────────────────────────────────────────
inline QIcon Icons::plusMark()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#00c8b4" stroke-width="2.5" stroke-linecap="round">
  <line x1="12" y1="5" x2="12" y2="19"/>
  <line x1="5"  y1="12" x2="19" y2="12"/>
</svg>)svg", 24);
    return icon;
}

// ── circle (solid dot) ────────────────────────────────────────────────────────
inline QIcon Icons::circle()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">
  <circle cx="12" cy="12" r="9" fill="#c8ddd8" stroke="none"/>
</svg>)svg", 24);
    return icon;
}

// ── arrowUp ───────────────────────────────────────────────────────────────────
inline QIcon Icons::arrowUp()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#c8ddd8" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
  <line x1="12" y1="19" x2="12" y2="5"/>
  <polyline points="5 12 12 5 19 12"/>
</svg>)svg", 24);
    return icon;
}

// ── arrowDown ─────────────────────────────────────────────────────────────────
inline QIcon Icons::arrowDown()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#c8ddd8" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
  <line x1="12" y1="5" x2="12" y2="19"/>
  <polyline points="19 12 12 19 5 12"/>
</svg>)svg", 24);
    return icon;
}

// ── arrowIndent ───────────────────────────────────────────────────────────────
inline QIcon Icons::arrowIndent()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#c8ddd8" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M4 4v9h13"/>
  <polyline points="13 13 17 17 13 21"/>
</svg>)svg", 24);
    return icon;
}

// ── shuffle ───────────────────────────────────────────────────────────────────
inline QIcon Icons::shuffle()
{
    static const QIcon icon = fromSvg(R"svg(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none"
     stroke="#c8ddd8" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <polyline points="16 3 21 3 21 8"/>
  <line x1="4" y1="20" x2="21" y2="3"/>
  <polyline points="21 16 21 21 16 21"/>
  <line x1="15" y1="15" x2="21" y2="21"/>
  <line x1="4" y1="4" x2="9" y2="9"/>
</svg>)svg", 24);
    return icon;
}

// pdf icon
inline QIcon Icons::pdf() {
    static const char svg[] =
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
        "<rect x='3' y='1' width='14' height='19' rx='2' fill='#c0392b'/>"
        "<text x='5' y='16' font-size='6' fill='white' font-weight='bold'>PDF</text>"
        "</svg>";
    return fromSvg(QByteArray(svg));
}

#endif // MEDIAGALLERY_ICONS_H
