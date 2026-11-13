#include "canvas/VectorRender.h"

#include "canvas/DabStamper.h"
#include "canvas/StrokeSession.h"
#include "core/brush/DabGenerator.h"
#include "tools/ToolState.h"

#include <QColor>

#include <cmath>
#include <map>
#include <memory>
#include <numbers>

namespace mothion::canvas {

doc::VectorStroke transformedStroke(const doc::VectorStroke &stroke, const QTransform &transform)
{
    doc::VectorStroke out = stroke;
    const double scale = std::sqrt(std::abs(transform.determinant()));
    out.size = stroke.size * scale;
    for (brush::BrushInput &in : out.points) {
        const QPointF at = transform.map(QPointF(in.x, in.y));
        in.x = at.x();
        in.y = at.y();
        // The direction of travel goes through the linear part: it turns with a rotation and is mirrored by a flip.
        const double angle = in.direction * std::numbers::pi / 180.0;
        const double c = std::cos(angle), s = std::sin(angle);
        in.direction = std::atan2(transform.m12() * c + transform.m22() * s, transform.m11() * c + transform.m21() * s)
                       * 180.0 / std::numbers::pi;
    }
    return out;
}

SplitStroke splitStrokeByMask(const doc::VectorStroke &stroke, const SelectionMask &mask)
{
    SplitStroke out;
    if (stroke.points.empty())
        return out;
    auto in = [&](const brush::BrushInput &p) {
        return mask.at(int(std::floor(p.x)), int(std::floor(p.y))) >= 128;
    };
    std::vector<brush::BrushInput> run;
    bool runIn = in(stroke.points.front());
    auto flush = [&] {
        if (run.empty())
            return;
        doc::VectorStroke piece = stroke;
        piece.points = std::move(run);
        run.clear();
        (runIn ? out.inside : out.outside).push_back(std::move(piece));
    };
    run.push_back(stroke.points.front());
    for (std::size_t i = 1; i < stroke.points.size(); ++i) {
        const brush::BrushInput &a = stroke.points[i - 1], &b = stroke.points[i];
        const bool bIn = in(b);
        if (bIn != runIn) {
            // Where the segment crosses the outline: `lo` is still on a's side, `hi` on b's.
            double lo = 0.0, hi = 1.0;
            for (int step = 0; step < 12; ++step) {
                const double mid = (lo + hi) / 2.0;
                if (in(brush::DabGenerator::interpolate(a, b, mid)) == runIn)
                    lo = mid;
                else
                    hi = mid;
            }
            run.push_back(brush::DabGenerator::interpolate(a, b, lo));
            const brush::BrushInput start = brush::DabGenerator::interpolate(a, b, hi);
            flush();
            runIn = bIn;
            run.push_back(start);
        }
        run.push_back(b);
    }
    flush();
    return out;
}

QRect drawStrokes(doc::TiledImage &target, const std::vector<doc::VectorStroke> &strokes, const tools::ToolState &state,
                  const QRect &clip)
{
    QRect painted;
    std::map<std::string, tools::BrushDefinitionPtr> brushes; // looked up once each
    for (const doc::VectorStroke &s : strokes) {
        if (s.points.empty())
            continue;
        tools::BrushDefinitionPtr &definition = brushes[s.brush];
        if (!definition)
            definition = state.findBrush(QString::fromStdString(s.brush));
        brush::BrushStyle style = definition->style;
        style.size = s.size / 2.0;
        // As when it was drawn: the start rises live or once the stroke is done, not both.
        if (s.startCorrectedAfter)
            style.startTaper = 0.0;
        else
            style.startCorrection = 0;
        style.mix.on = false;
        style.waterEdge.on = false;
        auto stamper = std::make_shared<DabStamper>(QColor::fromRgba(s.color), style.hardness, definition->tips,
                                                    style.verticalThickness);
        stamper->setSecondaryColor(QColor::fromRgba(s.secondaryColor));
        StrokeSession session(target, std::move(stamper), s.opacity, s.erase, clip);
        painted |= session.paintStroke(style, s.seed, s.points, s.cornerTips);
    }
    return painted;
}

} // namespace mothion::canvas
