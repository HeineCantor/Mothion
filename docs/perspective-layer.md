# How the perspective layer works

A perspective layer is a drawing guide: a flat plane with a square grid on it, seen through a
pinhole camera, that you turn and move in space to get the vanishing lines of the scene you
are drawing. It has no pixels, it is never exported or played, and it is edited entirely on
the canvas, with handles drawn at the center of the grid.

It came in three steps:

| Commit | What it added |
|---|---|
| `6f72602` *Perspective layer* | The layer kind, the grid model (turn / tilt / roll angles), the rendering, the project format, sliders in the Layers panel. |
| `f7150d1` *Perspective gizmo on the canvas* | A quaternion orientation and a 3D position instead of the angles, and the gizmo on the canvas (rings, arrows, scale, menu). The panel's controls were removed. |
| `c109625` *Various optimizations…* | Painting only what is needed of the lines, and a bounded area for the thumbnails. |

The geometry is plain C++ with no Qt (`src/core/perspective/`); the gizmo is
`src/canvas/PerspectiveGizmo.*`; the drawing is in `src/canvas/FrameRenderer.cpp`.

## 1. Where it sits in the document

`doc::Layer` got a `kind` (`Raster` or `Perspective`). A perspective layer lives in a drawing's
layer stack like any other: it has a name, visibility, lock, opacity, blend mode, it can be
moved, deleted and undone. Its grid is part of `LayerProperties` (`properties.perspective`, a
`perspective::Grid`), so every change to it goes through the same
`SetLayerPropertiesCommand` as a change of opacity, and undo and redo come for free.

What a perspective layer is **not**:

- **Not paintable.** It has no pixels. `DocumentController::workLayer()` refuses it
  ("is a perspective layer: it has no pixels to work on") and `beginPaintLayers()` skips it,
  so brush, fill, move, transform, puppet warp, copy and clear ignore it.
- **Not part of the picture.** `FrameRenderer::setGuidesVisible(false)` leaves perspective
  layers out; it is set for export, for autoplay, and for onion-skin ghosts
  (`composeDrawing` and `drawingBounds` have a `guides` flag for that).

It is created from the arrow next to **+** in the Layers panel (*Perspective Layer*):
`DocumentController::addLayer(onTop, kind)` builds it with `perspective::defaultGrid(w, h)`,
which centers the camera on the sheet and sizes a cell at 1/16 of the width.

## 2. The model: `perspective::Grid`

```cpp
struct Grid {
    Rotation orientation = fromAngles(30, 60, 0); // unit quaternion
    Vec3     position;                            // the plane's center, camera space
    double   fov = 60, frameWidth = 1920;         // lens, in degrees; sheet width, px
    double   originX = 960, originY = 540;        // where the camera looks, document px
    double   cell = 120;  int cells = 8;          // plane units; cells per side of the center
    uint32_t color;       bool horizon = true;
};
```

Two coordinate systems are involved.

**Camera space** (`Vec3`): x to the right, y down, z away from the eye, in document pixels.
The picture, i.e. the sheet, is the plane `z = 0`, where one unit is one document pixel.
The eye is on the camera's axis, at `z = -f`, with the focal length

```
f = (frameWidth / 2) / tan(fov / 2)
```

so that the horizontal field of view `fov` spans exactly `frameWidth` pixels. `frameWidth` is
the width of the sheet when the layer was made: it is stored in the grid so that the picture
does not change if the sheet is later resized.

**Plane space**: the grid's own frame. The plane is `z = 0`, the grid lines run along x and y,
the center is the origin. `orientation` and `position` take a plane point to the camera:

```
p_camera = R(q) · p_plane + position
```

With `orientation = identity` and `position = 0` the grid lies flat on the sheet, centered on
the camera's axis, and looks like an ordinary square grid.

## 3. The algebra

### 3.1 Projection

A camera-space point `v = (x, y, z)` is drawn at (`projectCamera`)

```
depth = f + z
k     = f / depth
X     = originX + x · k
Y     = originY + y · k
```

On the sheet (`z = 0`) `k = 1`; nearer to the eye `k > 1`, farther `k < 1`. A point with
`depth < 0.02 f` (behind the eye, or almost on it) has no projection (`std::nullopt`).

### 3.2 Rotations: quaternions

The orientation is a **unit quaternion** `q = (w, x, y, z)` rather than three angles:

- Dragging a ring rotates the plane *a little more* about one of its own axes, again and
  again. With Euler angles that means converting back and forth and meeting gimbal lock
  where two axes align; with a quaternion it is one multiplication.
- Composition is exact and cheap, and the interpolation of a drag has no seams.

Rotating a vector (`rotate`):

```
t  = 2 (q_vec × v)
v' = v + w·t + q_vec × t
```

Product (`a * b` = rotate by `b` first, then by `a`) is the Hamilton product. A rotation of
`θ` about the axis `n` is `axisAngle`: `(cos θ/2, n·sin θ/2)`.

The default pose, `fromAngles(turn, tilt, roll)`, is the grid spun `turn` degrees on its
normal, then tilted back `tilt` degrees (its far side goes away from the viewer), then rolled
about the line of sight:

```
q = Rz(roll) · Rx(-tilt) · Rz(turn)       defaults 30°, 60°, 0°
```

`normalized()` renormalizes after a drag to stop the drift of many multiplications, but
returns the quaternion **untouched if its length is already 1 within 1e-12**. That matters for
saving: renormalizing a quaternion that is already unit changes its last bits, and the
project would no longer read back bit for bit.

### 3.3 The grid lines and the near plane

`gridSegments` makes `2n + 1` lines in each direction, at `i · cell` for `i = -n … n`, each
running from `-n·cell` to `n·cell`. Every endpoint goes to camera space (`toCamera`), and the
line is **clipped against the near plane `depth = 0.02 f` in 3D, before projecting**:

```
t = (near - depth_a) / (depth_b - depth_a);   the end behind it moves to a + (b - a)·t
```

Clipping in 3D (where a segment stays a straight line parameterized linearly) and projecting
afterwards is what keeps lines that cross the eye's plane correct; clipping after the
projection would send them to infinity.

### 3.4 The horizon

All the directions of the plane vanish on one line in the picture. With `n = R(q) · (0,0,1)`
the plane's normal in camera space, a direction `d` of the plane satisfies `n · d = 0`. The
point at infinity of `d` is drawn at `(originX + f·dx/dz, originY + f·dy/dz)`; substituting `d`
gives the line

```
nx · (X - originX) + ny · (Y - originY) + nz · f = 0
```

`horizonLine` returns the segment of it centered on the point nearest to where the camera
looks, `reach` px each way (4 sheet widths): the nearest point is
`origin + (nx, ny) · (-nz · f / (nx² + ny²))`, the direction along it `(-ny, nx)`. When
`nx² + ny²` is nearly zero the plane faces the viewer and the horizon is at infinity: no line.

## 4. The gizmo on the canvas

`PerspectiveGizmo` is not a tool: it is a set of handles that belong to the canvas, shown
whenever the **current layer** is a visible perspective layer, whatever tool is active.

```
 ○ ○ ○   three rings (red, green, blue)  rotate about the plane's own x, y, z
 ↑ → ↗   three arrows                    move along those axes (also in depth)
  ■      the middle square               moves the plane parallel to the screen
  ■      a square on the grid            sizes the cells
  ⋯      the round button                menu: color, horizon, cells, lens, resets
```

The local axes are `X = (1,0,0)`, `Y = (0,-1,0)`, `Z = (0,0,-1)`: a right-handed frame whose
z comes out of the plane on the viewer's side.

### 4.1 Constant size on screen

Handles must keep their size at any zoom and at any depth. All the sizes are in screen
pixels (`kRingRadius` 70, `kAxisLength` 112, `kPick` 7) and are turned into plane units at
the **center's depth**:

```
units  = (f + z_center) / f            plane units per document px at that depth
radius = kRingRadius / zoom · units    (the same for the arrow length)
```

The ring of axis `a` is the circle centered at the plane's center, in the plane spanned by
`u, v` with `u × v = a`:

```
point(θ) = project( center + R(q) · (u cos θ + v sin θ) · radius )
```

sampled at 96 points; seen in perspective it is the ellipse the user sees. Hit testing is the
distance from the pointer to that polyline (and to the arrow segments, and the squares), the
nearest one within 7 px winning. Priority: menu button, middle square, arrows (outer 30% only,
the inner part is crowded by the rings), scale square, rings.

An arrow whose projection is shorter than 14 px (the axis points at the eye) is not drawn nor
grabbed.

### 4.2 What each drag computes

The drag never reads absolute positions; it applies the **increment since the last event** to
the grid as it was left by the previous event.

- **Middle square** — the pointer moves on the sheet, the plane at depth `z` follows it:
  `position.xy += Δpointer · units`.
- **Arrow** — the displacement of the pointer is projected on the arrow *as drawn now*:
  ```
  along    = (Δpointer · arrow) / (arrow · arrow)        arrow = tip - center, on screen
  position += direction_camera · (along · length)
  ```
  so dragging the pointer along the whole arrow moves the plane by one arrow length in 3D,
  whatever the foreshortening. A move that would put the center nearer than `0.1 f` to the
  eye is ignored, so the plane never goes behind the camera.
- **Ring** — the pointer's displacement along the ring's *screen tangent at the press* is an
  angle:
  ```
  φ = ((pointer - press) · T) / (T · T)          T = d(point)/dθ, screen px per radian
  q = q_press · axisAngle(local axis, φ)
  ```
  `T` is a central difference (±0.01 rad) of the projected ring at the grabbed point.
  Multiplying on the **right** rotates about the plane's own axis, so the ring follows the
  plane as it turns. `T` is never shorter than a quarter of the ring (`0.25 · kRingRadius`),
  or a ring seen edge on would spin the plane for a one pixel move.
- **Scale square** — it sits on the first diagonal crossing of the grid
  (`project(k·cell, k·cell)`) out of the rings, so it is near at hand. It follows the pointer
  along the direction center → handle:
  `cell = cell₀ · max(0.05, (pointer - center)·h / (h·h))`, with `h = handle - center`.

### 4.3 Preview, commit, undo

While dragging, `DocumentController::previewLayerProperties` puts the new grid on the layer
at once (it records nothing and signals a repaint of the whole drawing). On release,
`commitLayerProperties(ref, before, after)` puts `before` back and then executes **one**
`SetLayerPropertiesCommand` to `after`: a drag is a single undo step, however many events it
had. `cancel()` (a stroke interrupted by undo, a frame change…) restores `before`.
The choices of the menu are commands of their own, one undo step each.

### 4.4 How it fits with the tools

In `CanvasView::handleSample`, on a press the order is: navigation (Space, middle button,
pen barrel) → **gizmo** → color picking → stroke → the active `CanvasTool`. The gizmo takes
the press only if a handle is under the pointer, so with a perspective layer selected you can
still draw on another layer elsewhere. Details:

- The **right button is pan**, which is why the options are in a round **⋯** button and not
  in a context menu. A click (not a slip) on it opens the menu at the pointer.
- On a **locked** layer or cel the gizmo is drawn faded (opacity 0.35) and does nothing.
- The cursor changes over a handle (`updateGizmoCursor`), and `busy` includes a drag in
  progress so that a second button does not start another action.
- It is painted by `CanvasView::paintEvent` over the picture, after the tool overlays.

## 5. Drawing the grid (`FrameRenderer`)

`FrameRenderer::render` treats a perspective layer as one more layer of the drawing (with its
opacity and blend mode): `drawLayer` calls `paintPerspective(painter, grid)`:
antialiased lines of color `grid.color`, width `max(1, frameWidth / 960)` px (2 px on an HD
sheet), and the horizon dashed at 1.5× the width.

`drawingBounds` gives a perspective layer a bounding box (the bounds of its segments and
horizon, padded, limited to ±4 sheet widths around the origin, since the horizon has no end).

### Optimizations (`c109625`)

The first version was slow as soon as a perspective layer existed, even when drawing on
another layer:

- **Thumbnails.** The Layers panel recomposes the thumbnail of the frame after each change,
  composing the *whole* bounds of the frame. With the horizon those bounds could be 15360 ×
  3038 px (190 MB, 80 ms on a 1920 × 1080 sheet), every 250 ms. The thumbnail now
  composes only the sheet (`& QRect(0, 0, w, h)`): 6 ms.
- **Clipped lines.** Updating a small rectangle under the brush used to stroke every line,
  and Qt computes the dashes of the whole horizon even if a few pixels show. `paintPerspective`
  now clips each line (Liang–Barsky) to the part of the painter's clip, with a margin, and
  gives the horizon the **dash offset** of where the piece starts, so the dashes match from
  piece to piece. A 40 × 40 px update went from 54 µs to 6 µs.

## 6. Saving

Project format **3** (`kProjectFormatVersion`): the `layers` table has two more columns,
`kind` (`raster` / `perspective`) and `perspective` (the grid as compact JSON, an empty string,
not NULL, for a raster layer). A perspective layer has no tiles.

```json
{"orientation":[w,x,y,z], "position":[x,y,z], "fov":60, "frameWidth":1920,
 "originX":960, "originY":540, "cell":120, "cells":8, "color":"#ff3a7bd5", "horizon":true}
```

Reading is forgiving: missing or damaged values keep the defaults, and the early layers
(`turn` / `tilt` / `roll` angles, `centerX` / `centerY`) are still read, through `fromAngles`.
Formats 1 and 2 have no perspective layers; a project saved by a newer version is refused.

## 7. The Layers panel

- The **+** button has an arrow with *Layer* / *Perspective Layer*.
- The thumbnail of a perspective layer is its grid drawn small: `layerThumbnail` scales
  `originX/Y`, `position`, `frameWidth` and `cell` by the thumbnail's factor, so that the camera
  sees the same scene in miniature and the lines stay visible.
- It has no controls for the grid. A first version had sliders in the panel (and a widget with
  orbits was tried); both were dropped for the gizmo, since moving a grid from a panel while
  looking at the canvas was awkward.

## 8. Tests

| Test | What it checks |
|---|---|
| `tst_core` `perspectiveGridProjects` | Projection, rotations, translation, the horizon. |
| `tst_canvas` `perspectiveLayerDrawsItsGrid` | A line where it should be, none in a cell, bounds, no guides when they are off. |
| `tst_projectfile` | The grid (rotation, position, lens) comes back bit for bit. |
| `tst_ui` `perspectiveLayers` | The panel: adding a perspective layer, thumbnail, no controls. |
| `tst_ui` `perspectiveGizmo` | Real mouse drags on a `CanvasView`: move, arrows, rings, scale, menu, a locked layer, a raster layer. |

## 9. Things worth knowing

- Everything is in **document pixels**; the zoom and rotation of the view only matter to the
  gizmo, which divides its sizes by the zoom and works in screen px.
- The projection is a **pinhole**, not an orthographic or fisheye one: that is what makes the
  horizon and the vanishing points consistent with the lines. The lens presets of the menu are
  horizontal fields of view (20°–120°).
- A rotation never changes the position, and the position is in camera space: the plane turns
  about *its own center*, not about the camera.
- To add a handle: a case in `Handle`, its geometry in `geometry()`, a hit test in `handleAt()`,
  an increment in `move()`, and its painting in `paint()`. `handlePoint()` gives tests a point
  that grabs it.
