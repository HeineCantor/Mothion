#include "canvas/VectorRender.h"

#include "canvas/DabStamper.h"
#include "canvas/StrokeSession.h"
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
    const double turn = std::atan2(transform.m12(), transform.m11()) * 180.0 / std::numbers::pi;
    out.size = stroke.size * scale;
    for (brush::BrushInput &in : out.points) {
        const QPointF at = transform.map(QPointF(in.x, in.y));
        in.x = at.x();
        in.y = at.y();
        in.direction += turn;
    }
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
