#pragma once

#include <QIcon>

namespace mothion::ui {

enum class IconId {
    New, Open, Save, Undo, Redo,
    Brush, Eraser, Hand, Zoom, Move, RectSelect, LassoSelect, Puppet, Eyedropper, Fill, Transform, Blend, Liquify, Shape, Snap, Crosshair,
    Collapse, Expand, Float, Close,
    ZoomOut, ZoomFit, ZoomActual, RotateLeft, RotateRight, RotateReset,
    FlipHorizontal, FlipVertical, Eye, Lock, Add, Delete, ArrowUp, ArrowDown, Clear,
    Deselect, InvertSelection, EraseSelection, CutToLayer, CopyToLayer, MergeLayers, MergeDown,
    Folder, Frame, PreviousFrame, NextFrame, Wrench, Play, Stop, Pause, Speaker, SpeakerOff, Camera, MaskAdd, MaskDelete, ClipLayer, Trajectory, Flag, PreviousMarker, NextMarker, Keyframe, AddLayer, AddPerspective, AddVector,
    // What a layer is, in its row (raster, vector, perspective), and the rasterize button.
    KindRaster, KindVector, KindPerspective, Rasterize,
    // The timeline bar: a new frame (a frame with a plus) and the onion skin (an onion).
    NewFrame, Onion,
};

// Provisional icons, drawn in code as simple line art on a 24x24 grid.
// Everything that needs an icon asks here, so swapping in real (SVG) artwork
// later only changes this file.
QIcon icon(IconId id);
// Which color the glyph takes: Auto is the button text color, and the highlighted text over the highlight of a
// checked button; Plain the button text color whatever the state (a checked button with no fill of its own);
// Selected the highlighted text (over a selected row).
enum class IconTone { Auto, Plain, Selected };
QIcon icon(IconId id, IconTone tone);

} // namespace mothion::ui
