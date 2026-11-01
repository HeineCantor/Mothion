#pragma once

#include "core/brush/BrushStyle.h"

#include <QImage>
#include <QList>
#include <QString>

#include <memory>

namespace mothion::tools {

// What a brush is, independent of the current tool settings: the engine
// style (dynamics, spacing, tips order, spray...) and the tip images it
// refers to. Size and opacity come from ToolState when a stroke starts.
// Shared and immutable: presets, ToolState and strokes hold the same one.
struct BrushDefinition
{
    QString name;
    brush::BrushStyle style;
    QList<QImage> tips; // coverage in the alpha channel; style.tipCount == tips.size()
};

using BrushDefinitionPtr = std::shared_ptr<const BrushDefinition>;

// Hard round brush whose radius follows pressure: the default for every tool.
BrushDefinitionPtr roundBrush();

} // namespace mothion::tools
