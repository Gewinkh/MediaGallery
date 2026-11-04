#include "app/DrawnIconData.h"

#include <QByteArray>
#include <QLatin1StringView>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numbers>
#include <utility>

namespace mg::icons {
namespace {

//  Tatsaechliche Farbe setzt das Item; "black" heisst nur: diese Seite des Pfads traegt die Symbolfarbe.
//  Ein `PathSvg` statt je Abschnitt ein Objekt; `A` laeuft in Qt durch dieselbe Bogenrechnung wie `PathArc`. `PathAngleArc`
//  bleibt: Sein Bogen entsteht anders und waere nicht mehr bildgleich.
#define MG_OUTLINE R"(ShapePath { strokeColor: "black"; strokeWidth: 2; fillColor: "transparent"; capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin)"

const char kEllipseCurve[] =
    MG_OUTLINE R"QML(
        PathAngleArc { centerX: 12; centerY: 12; radiusX: 8.5; radiusY: 6
                       startAngle: 0; sweepAngle: 360 }
    })QML";

const char kEyeCurve[] =
    MG_OUTLINE R"QML(
        PathSvg { path: "M2.5 12 C2.5 12 6 6 12 6 C18 6 21.5 12 21.5 12 C21.5 12 18 18 12 18 C6 18 2.5 12 2.5 12" }
    })QML";

const char kLoopCurve[] =
    MG_OUTLINE R"QML(
        PathAngleArc { centerX: 12; centerY: 12; radiusX: 7.5; radiusY: 7.5
                       startAngle: -50; sweepAngle: 305 }
    }
    ShapePath {
        strokeColor: "transparent"
        fillColor: "black"
        PathSvg { path: "M14.8 2.4 L19.4 5.2 L14.8 8.0 L14.8 2.4" }
    })QML";

const char kPenCurve[] =
    MG_OUTLINE R"QML(
        PathSvg { path: "M4.5 19.6 L5.9 14.7 L16.1 4.4 L19.5 7.8 L9.1 18.4 L4.5 19.6" }
    })QML";

const char kPlayCurve[] =
    R"QML(    ShapePath {
        strokeColor: "black"
        strokeWidth: 1.5
        fillColor: "black"
        joinStyle: ShapePath.RoundJoin
        PathSvg { path: "M7 4.5 L19 12 L7 19.5 L7 4.5" }
    })QML";

const char kRedoCurve[] =
    MG_OUTLINE R"QML(
        PathSvg { path: "M20 10 L9 10 A5 5 0 0 0 9 20 L16 20" }
    })QML";

const char kRotateLeftCurve[] =
    MG_OUTLINE R"QML(
        PathAngleArc { centerX: 12; centerY: 12; radiusX: 6.4; radiusY: 6.4
                       startAngle: -40; sweepAngle: 260 }
    }
    ShapePath {
        strokeColor: "transparent"
        fillColor: "black"
        PathSvg { path: "M13.6 0.8 L21.6 5.4 L13.6 10.0 L13.6 0.8" }
    })QML";

//  Luecke von 90 Grad statt 55 wie beim Wiederholungs-Ring: bei 18-20 px sieht man zuerst die offene Seite. Die Spitze
//  steht auf senkrechter Grundlinie, die den Bogen kreuzt; tangential gerechnet wirkte sie losgeloest.
const char kRotateRightCurve[] =
    MG_OUTLINE R"QML(
        PathAngleArc { centerX: 12; centerY: 12; radiusX: 6.4; radiusY: 6.4
                       startAngle: 220; sweepAngle: -260 }
    }
    ShapePath {
        strokeColor: "transparent"
        fillColor: "black"
        PathSvg { path: "M10.4 0.8 L2.4 5.4 L10.4 10.0 L10.4 0.8" }
    })QML";

const char kSaveCurve[] =
    MG_OUTLINE R"QML(
        PathSvg { path: "M5 4 L16 4 L19 7 L19 20 L5 20 L5 4" }
    })QML";

const char kSelectCurve[] =
    R"QML(    ShapePath {
        strokeColor: "black"
        strokeWidth: 1.5
        fillColor: "black"
        joinStyle: ShapePath.RoundJoin
        PathSvg { path: "M5 3 L19 11 L13 12.6 L10.4 18.6 L5 3" }
    })QML";

const char kSignatureCurve[] =
    MG_OUTLINE R"QML(
        PathSvg { path: "M3 17 C6 17 7 8 10 8 C13 8 12 15 14.5 15 C17 15 18 9 21 9" }
    })QML";

const char kTrashCurve[] =
    MG_OUTLINE R"QML(
        PathSvg { path: "M6.2 7.5 L7.2 20.5 L16.8 20.5 L17.8 7.5" }
    })QML";

const char kUnderlineCurve[] =
    MG_OUTLINE R"QML(
        PathSvg { path: "M6 4 L6 11 A6 6 0 0 0 18 11 L18 4" }
    })QML";

const char kUndoCurve[] =
    MG_OUTLINE R"QML(
        PathSvg { path: "M4 10 L15 10 A5 5 0 0 1 15 20 L8 20" }
    })QML";

const char kGearCurve[] = "";

constexpr Stroke s_alignCenter[] = {{4, 6, 20, 6, 2}, {7, 12, 17, 12, 2}, {6, 18, 18, 18, 2}};
constexpr Stroke s_alignLeft[] = {{4, 6, 20, 6, 2}, {4, 12, 14, 12, 2}, {4, 18, 17, 18, 2}};
constexpr Stroke s_alignRight[] = {{4, 6, 20, 6, 2}, {10, 12, 20, 12, 2}, {7, 18, 20, 18, 2}};
constexpr Stroke s_arrow[] = {{5, 19, 19, 5, 2}, {12, 5, 19, 5, 2}, {19, 5, 19, 12, 2}};
constexpr Stroke s_arrowDown[] = {{12, 5, 12, 19, 2}, {6.5, 13.5, 12, 19, 2}, {12, 19, 17.5, 13.5, 2}};
constexpr Stroke s_arrowLeft[] = {{5, 12, 19, 12, 2}, {10.5, 6.5, 5, 12, 2}, {5, 12, 10.5, 17.5, 2}};
constexpr Stroke s_arrowRight[] = {{5, 12, 19, 12, 2}, {13.5, 6.5, 19, 12, 2}, {19, 12, 13.5, 17.5, 2}};
constexpr Stroke s_arrowUp[] = {{12, 5, 12, 19, 2}, {6.5, 10.5, 12, 5, 2}, {12, 5, 17.5, 10.5, 2}};
constexpr Stroke s_audio[] = {{9, 18, 9, 6, 2}, {9, 6, 19, 4, 2}, {19, 4, 19, 16, 2}};
constexpr Ring r_audio[] = {{6.5, 18, 2.5}, {16.5, 16, 2.5}};
constexpr Stroke s_caret[] = {{9, 4, 15, 4, 2}, {9, 20, 15, 20, 2}, {12, 4, 12, 20, 2}};
constexpr Stroke s_check[] = {{4.5, 12.5, 9.5, 17.5, 2}, {9.5, 17.5, 19.5, 7, 2}};
constexpr Stroke s_chevronDown[] = {{5, 9, 12, 16, 2}, {12, 16, 19, 9, 2}};
constexpr Stroke s_chevronLeft[] = {{15, 5, 8, 12, 2}, {8, 12, 15, 19, 2}};
constexpr Stroke s_chevronRight[] = {{9, 5, 16, 12, 2}, {16, 12, 9, 19, 2}};
constexpr Stroke s_chevronUp[] = {{5, 15, 12, 8, 2}, {12, 8, 19, 15, 2}};
constexpr Stroke s_close[] = {{6, 6, 18, 18, 2}, {18, 6, 6, 18, 2}};
constexpr Stroke s_copy[] = {{16, 5, 5.5, 5, 2}, {4, 6.5, 4, 17, 2}};
constexpr Box b_copy[] = {{8, 8, 12, 12, 1.5}};
constexpr Stroke s_drag[] = {{7, 8, 17, 8, 2}, {7, 12, 17, 12, 2}, {7, 16, 17, 16, 2}};
constexpr Stroke s_expand[] = {{13, 11, 19, 5, 2}, {14, 5, 19, 5, 2}, {19, 5, 19, 10, 2}, {11, 13, 5, 19, 2}, {5, 14, 5, 19, 2}, {5, 19, 10, 19, 2}};
constexpr Ring r_eye[] = {{12, 12, 2.5}};
constexpr Stroke s_file[] = {{9, 9, 15, 9, 2}, {9, 13, 15, 13, 2}, {9, 17, 13, 17, 2}};
constexpr Box b_file[] = {{6, 3, 12, 18, 1}};
constexpr Stroke s_fitWindow[] = {{8, 9, 6, 9, 2}, {6, 9, 6, 7, 2}, {16, 9, 18, 9, 2}, {18, 9, 18, 7, 2}, {8, 15, 6, 15, 2}, {6, 15, 6, 17, 2}, {16, 15, 18, 15, 2}, {18, 15, 18, 17, 2}};
constexpr Box b_fitWindow[] = {{3.5, 4.5, 17, 15, 1}};
constexpr Fill f_folder[] = {{3, 5.5, 8.5, 3, 1, 1}, {3, 7, 18, 12.5, 1.5, 1}};
constexpr Fill f_folderOpen[] = {{3, 5.5, 8.5, 3, 1, 0.5}, {3, 7, 17, 7, 1.5, 0.5}, {4.5, 11, 18.5, 8.5, 1.5, 1}};
constexpr Ring r_gear[] = {{12, 12, 3.4}};
constexpr Stroke s_image[] = {{4, 17, 9, 12, 2}, {9, 12, 13, 16, 2}, {13, 16, 16, 14, 2}, {16, 14, 20, 17, 2}};
constexpr Box b_image[] = {{3.5, 5, 17, 14, 1.5}};
constexpr Ring r_image[] = {{8.5, 10, 1.6}};
constexpr Stroke s_list[] = {{9, 6, 20, 6, 2}, {9, 12, 20, 12, 2}, {9, 18, 20, 18, 2}};
constexpr Fill f_list[] = {{4, 5, 2, 2, 1, 1}, {4, 11, 2, 2, 1, 1}, {4, 17, 2, 2, 1, 1}};
constexpr Ring r_lock[] = {{12, 10, 3.2}};
constexpr Fill f_lock[] = {{4.5, 10, 15, 10, 2, 1}};
constexpr Stroke s_loopOne[] = {{9.6, 10.7, 11.0, 9.1, 2}};
constexpr Fill f_loopOne[] = {{10.9, 8.9, 2.5, 6.2, 1.2, 1}};
constexpr Stroke s_markupHighlight[] = {{3.5, 18, 20.5, 18, 3}};
constexpr Box b_markupHighlight[] = {{3.5, 6, 17, 8, 1}};
constexpr Stroke s_markupStrike[] = {{4, 12, 20, 12, 2.5}};
constexpr Stroke s_markupUnderline[] = {{5, 20, 19, 20, 2}};
constexpr Stroke s_minus[] = {{5, 12, 19, 12, 2}};
constexpr Fill f_pause[] = {{7, 5, 4, 14, 1, 1}, {13, 5, 4, 14, 1, 1}};
constexpr Stroke s_pen[] = {{13.24, 7.28, 16.59, 10.77, 2}};
constexpr Stroke s_plus[] = {{12, 5, 12, 19, 2}, {5, 12, 19, 12, 2}};
constexpr Box b_rect[] = {{3.5, 6, 17, 12, 1}};
constexpr Fill f_redact[] = {{3, 4, 18, 2, 1, 0.45}, {3, 16, 13, 2, 1, 0.45}, {2.5, 9, 19, 5.5, 1, 1}};
constexpr Stroke s_redo[] = {{16, 6, 20, 10, 2}, {20, 10, 16, 14, 2}};
constexpr Stroke s_replace[] = {{4, 9, 17, 9, 2}, {14, 6, 17, 9, 2}, {17, 9, 14, 12, 2}, {20, 15, 7, 15, 2}, {10, 12, 7, 15, 2}, {7, 15, 10, 18, 2}};
constexpr Stroke s_save[] = {{8, 4, 8, 10, 2}, {8, 10, 15, 10, 2}, {15, 10, 15, 4, 2}};
constexpr Box b_save[] = {{8, 13, 8, 7, 0}};
constexpr Stroke s_search[] = {{15, 15, 20, 20, 2}};
constexpr Ring r_search[] = {{10.5, 10.5, 6}};
constexpr Stroke s_signature[] = {{4, 21, 20, 21, 2}};
constexpr Stroke s_snap[] = {{4, 7, 20, 7, 2}, {4, 12, 20, 12, 2}, {4, 17, 20, 17, 2}};
constexpr Stroke s_table[] = {{3.5, 9.67, 20.5, 9.67, 2}, {3.5, 14.33, 20.5, 14.33, 2}, {9.17, 5, 9.17, 19, 2}, {14.83, 5, 14.83, 19, 2}};
constexpr Box b_table[] = {{3.5, 5, 17, 14, 1}};
constexpr Stroke s_toc[] = {{4, 6, 8, 6, 2}, {11, 6, 20, 6, 2}, {4, 12, 8, 12, 2}, {11, 12, 20, 12, 2}, {4, 18, 8, 18, 2}, {11, 18, 20, 18, 2}};
constexpr Stroke s_trash[] = {{3.5, 6.5, 20.5, 6.5, 2}, {9, 3.5, 15, 3.5, 2}, {9, 3.5, 9, 6.5, 2}, {15, 3.5, 15, 6.5, 2}, {10, 11, 10, 17.5, 2}, {14, 11, 14, 17.5, 2}};
constexpr Stroke s_undo[] = {{8, 6, 4, 10, 2}, {4, 10, 8, 14, 2}};
constexpr Stroke s_valignMiddle[] = {{4, 12, 20, 12, 2}, {12, 3, 12, 9, 2}, {9, 6, 12, 3, 2}, {12, 3, 15, 6, 2}, {12, 21, 12, 15, 2}, {9, 18, 12, 21, 2}, {12, 21, 15, 18, 2}};
constexpr Stroke s_valignTop[] = {{4, 4, 20, 4, 2}, {12, 20, 12, 9, 2}, {8, 13, 12, 9, 2}, {12, 9, 16, 13, 2}};
constexpr Stroke s_warn[] = {{12, 3.5, 21.5, 20, 2}, {21.5, 20, 2.5, 20, 2}, {2.5, 20, 12, 3.5, 2}, {12, 9.5, 12, 14.5, 2}};
constexpr Fill f_warn[] = {{11, 16.4, 2, 2, 1, 1}};

//  Nach Namen sortiert: `findSymbol` sucht binaer, die Tabelle liegt unveraenderlich im Programm statt im JS-Heap.
const Symbol kSymbols[] = {
    {"align-center", s_alignCenter, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"align-left", s_alignLeft, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"align-right", s_alignRight, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"arrow", s_arrow, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"arrow-down", s_arrowDown, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"arrow-left", s_arrowLeft, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"arrow-right", s_arrowRight, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"arrow-up", s_arrowUp, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"audio", s_audio, {}, r_audio, {}, nullptr, 0.8, 0, nullptr},
    {"caret", s_caret, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"check", s_check, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"chevron-down", s_chevronDown, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"chevron-left", s_chevronLeft, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"chevron-right", s_chevronRight, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"chevron-up", s_chevronUp, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"close", s_close, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"copy", s_copy, b_copy, {}, {}, nullptr, 0.8, 0, nullptr},
    {"drag", s_drag, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"ellipse", {}, {}, {}, {}, nullptr, 0.8, 0, kEllipseCurve},
    {"expand", s_expand, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"eye", {}, {}, r_eye, {}, nullptr, 0.8, 0, kEyeCurve},
    {"file", s_file, b_file, {}, {}, nullptr, 0.8, 0, nullptr},
    {"fit-window", s_fitWindow, b_fitWindow, {}, {}, nullptr, 0.8, 0, nullptr},
    {"folder", {}, {}, {}, f_folder, nullptr, 0.8, 0, nullptr},
    {"folder-open", {}, {}, {}, f_folderOpen, nullptr, 0.8, 0, nullptr},
    {"gear", {}, {}, r_gear, {}, nullptr, 0.8, 0, kGearCurve},
    {"image", s_image, b_image, r_image, {}, nullptr, 0.8, 0, nullptr},
    {"list", s_list, {}, {}, f_list, nullptr, 0.8, 0, nullptr},
    {"lock", {}, {}, r_lock, f_lock, nullptr, 0.8, 0, nullptr},
    {"loop", {}, {}, {}, {}, nullptr, 0.8, 0, kLoopCurve},
    {"loop-one", s_loopOne, {}, {}, f_loopOne, nullptr, 0.8, 0, kLoopCurve},
    {"markup-highlight", s_markupHighlight, b_markupHighlight, {}, {}, nullptr, 0.8, 0, nullptr},
    {"markup-strike", s_markupStrike, {}, {}, {}, "S", 0.8, -0.5, nullptr},
    {"markup-underline", s_markupUnderline, {}, {}, {}, nullptr, 0.8, 0, kUnderlineCurve},
    {"minus", s_minus, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"pause", {}, {}, {}, f_pause, nullptr, 0.8, 0, nullptr},
    {"pen", s_pen, {}, {}, {}, nullptr, 0.8, 0, kPenCurve},
    {"play", {}, {}, {}, {}, nullptr, 0.8, 0, kPlayCurve},
    {"plus", s_plus, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"rect", {}, b_rect, {}, {}, nullptr, 0.8, 0, nullptr},
    {"redact", {}, {}, {}, f_redact, nullptr, 0.8, 0, nullptr},
    {"redo", s_redo, {}, {}, {}, nullptr, 0.8, 0, kRedoCurve},
    {"replace", s_replace, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"rotate-left", {}, {}, {}, {}, nullptr, 0.8, 0, kRotateLeftCurve},
    {"rotate-right", {}, {}, {}, {}, nullptr, 0.8, 0, kRotateRightCurve},
    {"save", s_save, b_save, {}, {}, nullptr, 0.8, 0, kSaveCurve},
    {"search", s_search, {}, r_search, {}, nullptr, 0.8, 0, nullptr},
    {"select", {}, {}, {}, {}, nullptr, 0.8, 0, kSelectCurve},
    {"signature", s_signature, {}, {}, {}, nullptr, 0.8, 0, kSignatureCurve},
    {"snap", s_snap, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"table", s_table, b_table, {}, {}, nullptr, 0.8, 0, nullptr},
    {"toc", s_toc, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"trash", s_trash, {}, {}, {}, nullptr, 0.8, 0, kTrashCurve},
    {"undo", s_undo, {}, {}, {}, nullptr, 0.8, 0, kUndoCurve},
    {"valign-middle", s_valignMiddle, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"valign-top", s_valignTop, {}, {}, {}, nullptr, 0.8, 0, nullptr},
    {"warn", s_warn, {}, {}, f_warn, nullptr, 0.8, 0, nullptr},
};

QByteArray gearSource() {
    constexpr int teeth = 8;
    constexpr double rIn = 7.0, rOut = 10.2;
    const double step = 360.0 / teeth;
    const double halfOut = 13, halfIn = step / 2 - 5;
    const auto punkt = [](double r, double grad) {
        const double t = grad * std::numbers::pi / 180;
        return std::pair{12 + r * std::cos(t), 12 + r * std::sin(t)};
    };
    //  10 Stellen: Ab 7 weichen Kanten schon um eine Farbstufe ab, jede weitere Stelle kostet beim Einlesen je Instanz.
    QByteArray pfad;
    const auto zug = [&](double r, double grad) {
        const auto [x, y] = punkt(r, grad);
        pfad += (pfad.isEmpty() ? "M" : " L") + QByteArray::number(x, 'g', 10) + ' ' + QByteArray::number(y, 'g', 10);
    };
    for (int i = 0; i < teeth; ++i) {
        const double a = i * step;
        zug(rIn, a - halfIn);
        zug(rOut, a - halfOut);
        zug(rOut, a + halfOut);
        zug(rIn, a + halfIn);
    }
    zug(rIn, -halfIn);
    return QByteArray(MG_OUTLINE) + "\nPathSvg { path: \"" + pfad + "\" }\n}\n";
}

}  // namespace

const Symbol* findSymbol(QStringView name) {
    const auto* ende = std::end(kSymbols);
    const auto* it = std::lower_bound(std::begin(kSymbols), ende, name, [](const Symbol& s, QStringView n) {
        return QLatin1StringView(s.name).compare(n) < 0;
    });
    return it != ende && QLatin1StringView(it->name) == name ? it : nullptr;
}

std::span<const Symbol> allSymbols() { return kSymbols; }

QByteArray curveSource(const Symbol& symbol) {
    static const QByteArray kopf = "import QtQuick\nimport QtQuick.Shapes\nShape {\n"
                                   "    width: 24; height: 24; preferredRendererType: Shape.CurveRenderer\n";
    return kopf + (symbol.curve == kGearCurve ? gearSource() : QByteArray(symbol.curve)) + "\n}\n";
}

}  // namespace mg::icons
