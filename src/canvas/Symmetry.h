#pragma once

#include "core/brush/BrushStyle.h"
#include "tools/ToolState.h"

#include <QLineF>
#include <QPointF>
#include <QSizeF>
#include <QTransform>

#include <vector>

namespace mothion::canvas {

// The symmetry of the brushes (tools::SymmetrySettings) worked out on the page: what each copy of a stroke is moved by,
// and the guides that show the axes.
//
// The axes go through `center` (page coordinates) and are turned by `angle` (degrees, clockwise as the page is
// drawn, y down): at 0 the mirror of Bilateral is upright, that of Vertical level, and the first copy of Rotate points
// up. Tile and HalfDrop go by the page's cells, which start at the center (it moves the grid) and are not turned.

// The transforms of the copies (page coordinates), the first one the identity (the stroke itself). Tile and HalfDrop
// need where the stroke starts (`start`): the copies go from its cell to each of the others.
std::vector<QTransform> symmetryTransforms(const tools::SymmetrySettings &settings, const QPointF &center,
                                           double angle, const QSizeF &page, const QPointF &start);

// Whether the angle means anything for `kind` (Tile and HalfDrop are not turned).
bool symmetryTurns(tools::SymmetrySettings::Kind kind);

// A point of the brush moved by a copy's transform: its place, and the ways it points (its direction of travel, the
// pen's angle), turned and mirrored as the transform does.
brush::BrushInput transformedInput(const brush::BrushInput &in, const QTransform &transform);

// The guides of the axes (page coordinates), long enough to cross `reach` round the center (Bilateral, Vertical,
// Both, Rotate and RotateMirror) or the page (Tile, HalfDrop).
std::vector<QLineF> symmetryGuides(const tools::SymmetrySettings &settings, const QPointF &center, double angle,
                                   const QSizeF &page, double reach);

} // namespace mothion::canvas
