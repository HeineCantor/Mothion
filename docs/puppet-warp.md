# How the puppet warp works

The puppet warp (key **P**) deforms a raster drawing as if it were a rubber sheet: you place
*pins* on the drawing, drag one, and the rest of the shape follows while staying as rigid as
possible. It was added in `d3bcb90` (*Add a puppet warp tool*) and refined in `6c89fb0`
(*Simplify puppet outlines within the mesh margin*).

All the geometry lives in `src/core/puppet/` (plain C++, no Qt, linear algebra with Eigen);
`src/canvas/PuppetTool.cpp` connects it to the document and the canvas.

## The pipeline at a glance

```
drawing pixels ─▶ mask ─▶ dilation ─▶ outlines ─▶ simplification
  (alpha > 0)    Bitmask  (expand px)   (Moore)    (Douglas–Peucker)
                                                         │
                                                         ▼
warped pixels ◀── per-triangle warp ◀── ARAP on pins ◀── triangle mesh
 (WarpRaster)     (affine + bilinear)   (ArapDeformer)   (constrained Delaunay + Ruppert)
```

## 1. From drawing to outline (`Contour`)

1. **Mask.** On the first click, `PuppetTool::start()` reads the pixels of the drawing at the
   current layer and frame (only the selected part, if there is a selection) and builds a
   `Bitmask`: 1 where alpha is non-zero.
2. **Dilation.** `dilated()` grows the shape by `expand` pixels (the *Expand* setting, at least 1)
   with a round brush, so the mesh has some margin around the strokes.
3. **Outlines.** `outerContours()` traces one closed outline per connected part (8-connectivity)
   with *Moore neighbour tracing*. **Holes are not traced**: the mesh covers them, since there is
   nothing to draw there anyway.
4. **Simplification.** `simplifyContour()` runs Douglas–Peucker on the closed outline. The
   tolerance starts at `expand − 1`, so the simplified outline may cut into the margin but not into
   the drawing. `PuppetRig::buildMeshes()` checks that the corners of every edge pixel
   (`edgeCorners`) stay inside; if any falls outside, it halves the tolerance and tries again.
   Without this step the staircase outline of the pixels would produce a swarm of tiny triangles
   along the edges.

## 2. The mesh (`Triangulation`)

`triangulatePolygon()` triangulates the inside of each outline:

- **Constrained Delaunay**: incremental insertion with Lawson flips; the outline's edges are
  recovered with Sloan's flips.
- **Ruppert-style refinement**: circumcenters of triangles that are too thin (smallest angle 20°)
  or too large are inserted, splitting the boundary edges they encroach.
- The largest triangle size depends on *Density*: 2%, 0.4% or 0.2% of the part's area (about 50,
  250 or 500 triangles).
- **Pin positions** are passed as extra points, so every pin is exactly one mesh vertex.

## 3. Pins and deformation (`PuppetRig`, `ArapDeformer`)

`PuppetRig` holds one mesh per separate part of the shape. **Parts are independent**: a pin only
moves its own mesh.

- **Adding or removing a pin** rebuilds that part's mesh with the pins as vertices (`remesh`).
  The deformation so far is kept: each new vertex is moved where the old mesh took it, using the
  barycentric coordinates of the triangle that contains it.
- **Dragging a pin** solves *As-Rigid-As-Possible* (Sorkine & Alexa, 2007) with all of the mesh's
  pins held as constraints. Each iteration alternates two steps:
  - **local**: for every vertex, the rotation that best maps its rest neighbourhood onto the
    deformed one, in closed form: `θ = atan2(M10 − M01, M00 + M11)`;
  - **global**: the positions that best fit those rotations, a sparse linear system `L p' = b`
    where `L` is the cotangent-weighted Laplacian (weights clamped positive, so obtuse triangles
    cannot make the system indefinite).

  The matrix depends only on *which* vertices are pins, not on *where* they are: the factorization
  (`SimplicialLDLT`) is redone only when a pin is added or removed. While dragging, solving with a
  new right-hand side is enough.
- **Iterations**: 8 per pen move, starting from the previous positions (so the shape follows the
  pen smoothly), then 40 when the pen lifts, to let the shape settle.
- Changing *Density* or *Expand* while the warp is open rebuilds everything (`setSettings`),
  keeping pins and deformation.

## 4. From vertices to pixels (`WarpRaster`)

`warp()` draws every triangle from the undeformed source to its deformed place:

- each triangle has its own affine transform, applied **backwards**: for every target pixel the
  matching point in the source is computed and sampled **bilinearly** (premultiplied ARGB pixels);
- triangles are composited source-over, one after another: where the shape folds over itself,
  later triangles end up on top;
- the work is split into horizontal bands computed in parallel (up to 16 threads); within a band
  the triangle order is kept.

## 5. The session in the document (`PuppetTool`)

- The warp stays open across clicks. **Enter**, switching tools, an undo, or a frame or layer
  change applies it as **a single undo step** (`endPaint(before)`); **Esc** drops it.
- With a selection, `splitByMask` separates the part to warp (`source`) from the part that stays
  put (`rest`); at every render the warp's result is composited over `rest`.
- **Alt+click** or **Delete** removes a pin.

## Current limitations

- Holes are filled by the mesh: their empty area follows the deformation like everything else,
  so the two sides of a hole cannot move independently.
- A part with no pins has no unique solution and stays where it is.
- The warp works on a single drawing (one layer, one frame); pins are not kept once the warp is
  applied.

The tests are in `tests/tst_puppet.cpp`.
