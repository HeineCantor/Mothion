#pragma once

#include "core/PressureCurve.h"
#include "core/document/Ids.h"
#include "tools/BrushDefinition.h"

#include <QColor>
#include <QObject>
#include <QString>

#include <array>

namespace mothion::tools {

// The tools in the tool palette. Not to be confused with mothion::InputTool,
// which is the *physical* end of the pen (tip / eraser / mouse).
enum class ToolId { Brush, Eraser, Hand, Zoom, Move, RectSelect, LassoSelect, Puppet, Eyedropper, Fill, Transform, Blend, Liquify, Shape };
inline constexpr int kToolCount = 14;

// Puppet warp options (see puppet::PuppetRig).
struct PuppetSettings
{
    int density = 1;      // mesh: 0 coarse, 1 normal, 2 fine
    int expand = 3;       // px of mesh around the drawing
    bool showMesh = true; // draw the mesh over the canvas
    // Local mode: no pins to place, everything is held; a drag moves the
    // point under the pen and bends what lies within `radius` of it.
    bool local = false;
    double radius = 80.0; // document px

    bool operator==(const PuppetSettings &) const = default;
};

// Paint bucket options.
struct FillSettings
{
    // Bucket: the area around the click. Lasso: the inside of an outline
    // drawn freehand.
    enum class Mode { Bucket, Lasso };
    Mode mode = Mode::Bucket;
    int tolerance = 10;    // 0..255: how far a color may be from the clicked one
    int expand = 2;        // px the fill grows under the lines around it
    bool allLayers = true; // the area is found in everything shown, else in this layer

    bool operator==(const FillSettings &) const = default;
};

// Blend tool: what the brush does to the pixels under it.
struct BlendSettings
{
    enum class Mode { Smudge, Blur };
    Mode mode = Mode::Smudge;

    bool operator==(const BlendSettings &) const = default;
};

// Liquify tool: how the brush moves the pixels under it.
struct LiquifySettings
{
    enum class Mode { Push, Expand, Shrink };
    Mode mode = Mode::Push;

    bool operator==(const LiquifySettings &) const = default;
};

// Shape tool: what a drag (or a series of clicks) draws, outlined, filled, or both.
// The outline is drawn in the main color with the tool's size as its
// thickness; the fill is the main color, or the secondary one under an outline.
struct ShapeSettings
{
    // A polygon and a curve are made by clicking their points one after the other.
    enum class Kind { Line, Rectangle, RoundedRectangle, Ellipse, Polygon, Curve };
    enum class Style { Outline, Filled, OutlineAndFill };
    Kind kind = Kind::Rectangle;
    Style style = Style::Outline; // an open shape (line, curve) is always an outline
    int cornerRadius = 16;        // px, of a rounded rectangle

    static bool isOpen(Kind kind) { return kind == Kind::Line || kind == Kind::Curve; }
    static bool isMultiPoint(Kind kind) { return kind == Kind::Polygon || kind == Kind::Curve; }

    bool operator==(const ShapeSettings &) const = default;
};

// Onion skin: the current layer's drawings of the frames around the
// current one, tinted and see-through over the canvas.
struct OnionSkinSettings
{
    bool enabled = false;
    int before = 2; // frames back
    int after = 2;  // frames on
    QColor beforeColor = QColor(230, 60, 60);
    QColor afterColor = QColor(40, 130, 230);
    double opacity = 0.35; // of the nearest ones; farther ones fade

    bool operator==(const OnionSkinSettings &) const = default;
};

struct BrushSettings
{
    double size = 12.0;   // diameter in document pixels at full pressure
    double opacity = 1.0; // 0..1, applied to the whole stroke
};

// Shared "what will the pen do" state.
//
// Panels edit it, the canvas reads it; neither knows about the other.
// This is the only coupling point between the tool UI and the canvas.
class ToolState : public QObject
{
    Q_OBJECT

public:
    explicit ToolState(QObject *parent = nullptr);

    static constexpr double kMinSize = 1.0;   // brush diameter limits, document px
    static constexpr double kMaxSize = 2000.0;

    static bool isPaintTool(ToolId tool) { return tool == ToolId::Brush || tool == ToolId::Eraser; }
    static QString displayName(ToolId tool);

    ToolId activeTool() const { return m_activeTool; }
    void setActiveTool(ToolId tool);
    // The tool active before the current one.
    ToolId previousTool() const { return m_previousTool; }
    // Selects `tool`, or, if it is already active, goes back to the previous
    // tool (the eraser shortcut: E to erase, E again to draw on).
    void toggleTool(ToolId tool);

    BrushSettings settings(ToolId tool) const { return m_settings[index(tool)]; }
    void setSize(ToolId tool, double size);
    void setOpacity(ToolId tool, double opacity);

    // The brush a paint tool draws with (never null; round by default).
    BrushDefinitionPtr brush(ToolId tool) const { return m_brushes[index(tool)]; }
    void setBrush(ToolId tool, BrushDefinitionPtr brush);

    // Two colors that keep their places, the main and the secondary, and one of them selected: the selected
    // color is the one the tools paint with and the wheel, the hex field and the eyedropper edit; the
    // other one is what a two-color brush uses besides it.
    QColor color() const { return m_secondarySelected ? m_secondaryColor : m_color; } // the selected one
    void setColor(const QColor &color);                                             // ... edited
    QColor mainColor() const { return m_color; }
    QColor secondaryColor() const { return m_secondaryColor; }
    void setSecondaryColor(const QColor &color);
    QColor inactiveColor() const { return m_secondarySelected ? m_color : m_secondaryColor; }
    bool secondarySelected() const { return m_secondarySelected; }
    void selectSecondary(bool secondary); // also leaves the transparent color
    void switchColor() { selectSecondary(!m_secondarySelected); } // X

    // The transparent "color", under the other two: while it is selected the
    // brush erases, with its own tip, size and opacity. It cannot be edited;
    // choosing any color (wheel, hex, eyedropper, swap) selects that one again.
    bool transparentSelected() const { return m_transparentSelected; }
    void setTransparentSelected(bool on);

    const PressureCurve &pressureCurve() const { return m_pressureCurve; }
    void setPressureCurve(const PressureCurve &curve);

    // 0 = raw input, towards 1 = heavier smoothing (more lag).
    double smoothing() const { return m_smoothing; }
    void setSmoothing(double amount);

    // Off: the pen's tilt is ignored (dynamics and pen direction), for pens
    // that report it badly or brushes that react to it too much.
    bool usesTilt() const { return m_useTilt; }
    void setUseTilt(bool use);

    // On: a brush's start correction is applied when the pen lifts, over its
    // first points (the stroke is drawn again), as Clip Studio does; off:
    // the start tapers live, by distance (BrushStyle::startTaper).
    bool correctsStartAfterStroke() const { return m_correctStartAfterStroke; }
    void setCorrectStartAfterStroke(bool on);

    // Sharp angles: where the stroke turns sharply it keeps a pointed corner
    // (the stabilizer and the correction after the stroke do not round it,
    // and the outer side gets a mitered point).
    bool sharpCorners() const { return m_sharpCorners; }
    void setSharpCorners(bool on);

    static constexpr double kMinPuppetRadius = 5.0;
    static constexpr double kMaxPuppetRadius = 1000.0;
    PuppetSettings puppetSettings() const { return m_puppet; }
    void setPuppetSettings(const PuppetSettings &settings);

    FillSettings fillSettings() const { return m_fill; }
    void setFillSettings(const FillSettings &settings);

    BlendSettings blendSettings() const { return m_blend; }
    void setBlendSettings(const BlendSettings &settings);
    LiquifySettings liquifySettings() const { return m_liquify; }
    void setLiquifySettings(const LiquifySettings &settings);

    // Snapping the pen to the axes of a perspective layer: while on, a stroke follows one of the
    // three axes of the layer snapped to (see canvas::PerspectiveSnap). There is none, or exactly one
    // (kNoId: none); the Layers panel's "Snap to" sets it.
    bool perspectiveSnap() const { return m_perspectiveSnap; }
    void setPerspectiveSnap(bool on);
    doc::LayerId perspectiveSnapLayer() const { return m_snapLayer; }
    void setPerspectiveSnapLayer(doc::LayerId layer);

    ShapeSettings shapeSettings() const { return m_shape; }
    void setShapeSettings(const ShapeSettings &settings);

    static constexpr int kMaxOnionFrames = 10;
    OnionSkinSettings onionSkin() const { return m_onion; }
    void setOnionSkin(const OnionSkinSettings &settings);

signals:
    void activeToolChanged(mothion::tools::ToolId tool);
    void toolSettingsChanged(mothion::tools::ToolId tool);
    void colorChanged(const QColor &color);          // the selected color changed, or another got selected
    void secondaryColorChanged(const QColor &color); // the color in the secondary place changed
    void selectedColorChanged(bool secondary);       // which of the two is selected
    void transparentSelectedChanged(bool on);
    void pressureCurveChanged();
    void smoothingChanged(double amount);
    void useTiltChanged(bool use);
    void correctStartAfterStrokeChanged(bool on);
    void sharpCornersChanged(bool on);
    void puppetSettingsChanged();
    void fillSettingsChanged();
    void perspectiveSnapChanged();
    void blendSettingsChanged();
    void liquifySettingsChanged();
    void shapeSettingsChanged();
    void onionSkinChanged();

private:
    static std::size_t index(ToolId tool) { return static_cast<std::size_t>(tool); }

    ToolId m_activeTool = ToolId::Brush;
    ToolId m_previousTool = ToolId::Brush;
    std::array<BrushSettings, kToolCount> m_settings;
    std::array<BrushDefinitionPtr, kToolCount> m_brushes;
    QColor m_color = QColor(0x20, 0x20, 0x24);
    QColor m_secondaryColor = Qt::white;
    bool m_secondarySelected = false;
    bool m_transparentSelected = false;
    PressureCurve m_pressureCurve;
    double m_smoothing = 0.3;
    bool m_useTilt = true;
    bool m_correctStartAfterStroke = false;
    bool m_sharpCorners = false;
    PuppetSettings m_puppet;
    FillSettings m_fill;
    bool m_perspectiveSnap = false;
    doc::LayerId m_snapLayer = doc::kNoId;
    BlendSettings m_blend;
    LiquifySettings m_liquify;
    ShapeSettings m_shape;
    OnionSkinSettings m_onion;
};

} // namespace mothion::tools
