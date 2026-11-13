# How vector layers work

A vector layer keeps what the brush did instead of only the pixels it left. Every stroke is stored
as its points plus the brush, size, opacity, colors and random seed it was drawn with; the layer's
pixels are a cache of what those strokes look like. Because the strokes can be drawn again at any
scale or place, a vector layer stays sharp when it is transformed, and its strokes can be cut,
erased and moved as lines rather than as pixels.

The model follows Clip Studio Paint's vector layers: a layer is raster *or* vector (never both), and
there is one brush engine with two destinations, pixels or stored strokes. The Layers panel has a
button for each kind of layer (drawing, perspective, vector).

## The model

| Piece | Where |
|---|---|
| `LayerKind::Vector` and `Layer::strokes` | [`Drawing.h`](../src/core/document/Drawing.h) |
| `VectorStroke`: points (`BrushInput`), brush *name*, size, opacity, colors, seed, erase, corner tips | [`VectorStroke.h`](../src/core/document/VectorStroke.h) |
| Commands: add a stroke, replace all strokes, rasterize | [`StrokeCommands.h`](../src/core/document/commands/StrokeCommands.h) |
| Saved as JSON in the `vector_strokes` table (format 16) | [`ProjectFile.cpp`](../src/io/ProjectFile.cpp) |

- The layer's `image` is the raster cache, so the renderer, the exports and the thumbnails need
  nothing special.
- A stroke names its brush; it is looked up again when the stroke is drawn again (the brush
  library, imported `.sut` brushes included). If the name is not found, the round brush is used.
- A stroke is stored with the points as the pen gave them, before the final correction pass.
- Color mixing and the watercolor edge read the pixels under the brush, so they cannot be drawn
  again from points: they are off on vector layers.

## Drawing it again

[`VectorRender.cpp`](../src/canvas/VectorRender.cpp) holds the two functions everything else uses:

- `drawStrokes()` draws a list of strokes onto an image, each as it was drawn (the live pen and the
  redraw give the same pixels, within a rounding of 2 in alpha).
- `transformedStroke()` moves a stroke by a transform: the points go where it takes them, the brush
  size follows the square root of the area scale, the direction of travel goes through the linear
  part (so a flip mirrors it).
- `splitStrokeByMask()` cuts a stroke along a selection into the runs inside and outside, putting a
  point on the outline where the line crosses it.

## What each tool does on a vector layer

| Tool | Behavior |
|---|---|
| Brush, eraser | The stroke is kept with its pixels, in one undo step. Inside a selection the stroke is cut to it. |
| Shapes | The outline is kept as strokes (round brush). A vector layer has no fill. |
| Move, Transform, Puppet warp | The pixels are stretched while the pen drags and the strokes are drawn again when it rests. With a selection only the strokes inside it move: they are cut along the outline. |
| Erase / cut / flip under a selection, copy to a new layer | The strokes are cut along the selection. Copy and cut make a vector layer. |
| Clear layer | Clears the strokes too. |
| Merge, merge down | Only among vector layers (Normal blend, not clipped); the strokes are joined. |
| Rasterize layer (Layers panel menu) | The layer becomes a raster layer; its strokes are let go. |
| Fill, gradient, blend, liquify | Not on a vector layer (as in Clip Studio); rasterize the layer first. |

## The vector eraser

The eraser's properties have four buttons for what it does on a vector layer
([`VectorErase.cpp`](../src/canvas/VectorErase.cpp), [`VectorEraseTool.cpp`](../src/canvas/VectorEraseTool.cpp)).
The last one chosen is remembered between sessions (`Preferences::vectorEraserMode`).

1. **Like a brush**: erases pixels, and the layer keeps the eraser's strokes.
2. **Touched part**: takes away what the eraser's circle covers of each stroke.
3. **Up to the intersection**: from where it touches to the nearest crossing with another stroke, on
   each side.
4. **Whole line**: the whole of every stroke it touches.

It works while the pen drags: each new stretch of the path is applied to what is left so far, and
only the area that changed is drawn again. A cut stroke ends on a point put on the line.

The intersection mode does not use the circle to say what is touched. A circle that reaches over a
crossing touches both lines there, and the erasing would run on past it. So a stroke is taken where
the pen comes down on its ink, or while it goes on where the path crosses its line; at a crossing of
two thick lines, the one the pen is on. The crossings are those with the other strokes as the layer
has them when the drag starts, and a stroke cut earlier ends exactly on its crossing, which still
counts as one.

## Puppet warp on a vector layer

The mesh is built from the layer's pixels, as for a raster layer. Once the pen rests, the strokes
are carried along by it ([`VectorWarp.cpp`](../src/canvas/VectorWarp.cpp)): each point goes where the
triangle that holds it was taken, long segments get points in between so a curve bends with the
warp, the brush grows or shrinks with the triangle, and the direction is found again from the new
path.

## Known limits

- A piece cut from the middle of a stroke starts again as a stroke does (a brush with a tapered start
  shows a thinner cut end).
- A non-uniform scale scales the round tip by the mean scale: it is not squashed.
- A vector layer is transformed, moved and warped with its strokes cut by a selection, but fills,
  gradients and the effect brushes need a rasterized layer.
- Out of the canvas: the strokes keep what lies outside it, the pixels are drawn inside the page only.
