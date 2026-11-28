# 3D model cels: posing a rigged model

A **3D model cel** shows a rigged glTF model, posed by dragging its joints on the canvas and animated with keys like
any other cel. The design follows a static reading of how CLIP STUDIO PAINT 5.0.4 poses its 3D models (CCD inverse
kinematics, swing/twist joint limits, CPU linear blend skinning). The algorithms are written from that description, not
ported from decompiled code, and where this implementation chooses differently it says so below.

The rig maths live in `src/core/rig3d/` (plain C++, no Qt). The document side is `src/core/document/ModelCel.h`, and
the canvas side is `src/canvas/ModelRenderer.*` and `src/canvas/ModelGizmo.*`. The dialogs are in
`src/ui/ModelDialogs.*`.

## Using it

- **New 3D model cel**: the cube button in the Timeline's cel bar opens a `.glb` file, or a `.gltf` that keeps its
  buffers inside (data URIs). The file is kept whole in the project, so the project opens anywhere.
- **Posing** (any tool, out of rig mode). Each drag goes into the cel's Deform key on the current frame, following the
  rules of its keys, and is one undo step:
  - drag a joint: its parent turns to point it at the pointer. Bones above it marked *Follow* share the turn. Dragging
    a root joint moves the model;
  - **Shift**+drag a joint: IK, with the chain from that joint up to the root (or to the first *Anchor*);
  - **Ctrl**+drag a joint: its bone twists round itself, one degree per pointer step;
  - drag on nothing: the camera orbits round the model. **Alt**+drag pans and **Ctrl**+drag dollies. This is off
    while Transform or Move is the tool, so they still move the cel as a flat picture.
- **Joint settings**: double-click a joint. The dialog sets anchor, follow, stiffness, soft limit zone, and twist and
  bend limits. In rig mode the chosen joint shows its limit frame: red along the bone, green and blue the two bend
  directions.
- **Blend shapes**: the cel's menu in the Timeline, *Blend shapes…*: one slider per morph target, keyed with the pose.

The cel's other channels (position, rotation, scale, opacity) work as on any cel: they move the rendered picture.

## Data path

```
glTF bytes ─▶ rig3d::Model ─▶ Pose (from the Deform key) ─▶ FK ─▶ morph + skinning ─▶ OpenGL / CPU raster ─▶ cel image
 GltfLoader      nodes, skins,      rotations, places,       globalMatrices   posedMeshes      ModelRenderer     FrameRenderer
                 meshes (CSR)       morphs, camera
```

### Reading glTF (`GltfLoader`)

The model is read with [cgltf](https://github.com/jkuhlmann/cgltf) (MIT, fetched at a fixed release by CMake).
The loader takes the default scene's nodes, triangle meshes (positions, normals, the first UV set, the first joints and
weights set), morph targets, skins and the base colour of materials (factor and texture). Weights go into compressed
rows per vertex (CSR, zeros left out) and are made to add up to 1, as glTF requires. CSP does not renormalise in its
kernel; a file that breaks the rule would otherwise shrink toward the origin. Animations, cameras and lights are
ignored.

JSON numbers are parsed with `std::from_chars`, not cgltf's default `atof`. Under a locale with a decimal comma
(Italian, German) `atof` reads "0.8" as 0.

### The pose (`Pose`)

A pose is, per node, a local rotation (unit quaternion) and translation (scale stays as at rest), plus a weight per
blend shape and an orbit camera (yaw, pitch, distance, target, field of view). It is saved as plain numbers, which are
the cel's Deform key:

```
camera: yaw pitch distance tx ty tz fov │ node 0: qw qx qy qz tx ty tz │ node 1: … │ morph weights…
```

The keys interpolate number by number, and a quaternion in between is normalised again (nlerp). The gizmo keeps each
quaternion on the same hemisphere as at the start of the drag, so neighbouring keys go the short way round. The rest
pose (the model as made, seen whole from the front) is kept as the cel's `Deformer::rest`, so the code that makes a
first key for a deformer works unchanged.

### Skinning (`Skinning`), as in CSP

Linear blend skinning on the CPU, in double, with the vertices spread over the cores (`parallelFor`, 2048 a task). For
bone *b*, `M_b = global(b) · inverseBind(b)` and `N_b` is the inverse transpose of its 3×3:

```
p' = Σ_k w_k · (M_{b_k} · p) / h_k        h_k: the homogeneous w of that product (1 for affine)
n' = unit( Σ_k w_k · unit(N_{b_k} · n) )
```

The division by *w* is per bone, before the sum. Each bone's normal is made unit length before the sum, and the sum
again after. Blend shapes are added before skinning (`pos += w · Δpos`). A mesh with no skin is carried rigidly by its
node. A vertex with no weight stays where it was bound (a guard of this implementation).

### Joint limits (`JointLimit`)

A bone's rotation from its rest is expressed in its **limit frame**: x along the bone (towards its first child, or the
node's +y for a leaf, Blender's bone axis), y and z across it. It is then split into a **swing** (where x goes: the
direction *v*) and a **twist** round x before it: `R = swing(v) · twist(θ)`.

- **Twist**: an interval `[min, max]` with wrap-around. Outside it, the twist goes to the nearer end, measured round
  whichever way is shorter.
- **Swing**: seen in the stereographic projection from the pole −x, a bend of angle *a* towards y lands at
  `tan(a/2)`. The region is either a **rectangle** `[tan(aMin/2), tan(aMax/2)] × [tan(bMin/2), tan(bMax/2)]`, or a
  **stadium**: the same rectangle with its short sides replaced by half circles, which cuts off the corners (a bone bent
  fully both ways at once). On the sphere every edge is a circle `n·v = d`:
  - a rectangle side `P_y ≥ tan(θ/2)` is the plane `−sin(θ/2)·v_x + cos(θ/2)·v_y ≥ sin(θ/2)` (through the pole);
  - a stadium end, the disk `|P − C| ≤ r`, is `(1 − t)·v_x + 2C_y·v_y + 2C_z·v_z ≥ 1 + t` with `t = |C|² − r²`,
    divided by `√((1 + t)² + 4r²)`.

  Bends are capped at ±(π − 0.001), away from the pole. A free bend makes the region a rectangle. Two locked bends (min
  = max) leave only the twist.
- **Clamping a swing outside the region**: the nearest point of a small circle to *v* is
  `normalize(v − n·(n·v − g·d))` with `g = √((1 − (n·v)²)/(1 − d²))` (skipped when `|n·v| > 0.9999995`). **This
  implementation's choice**: it takes the truly nearest point of the region, the best (largest `v·u`) among the
  projections onto every edge that land in the region and the corners. The research describes CSP trying the violated
  bands in a fixed order, which is not always the nearest. A test checks the nearest point against a dense sampling of
  the region for six shapes.

### Inverse kinematics (`Ik`)

**CCD**, as in CSP. Up to 24 sweeps; each goes over the chain from the tip to the root. For joint *j* at *P*, with
`a = tip − P` and `b = target − P`:

1. skip if `|a|·|b|` or `|a × b|` is tiny (`1e-4` of the model's size, CSP's `1e-4` for a model of size 1);
2. `θ = acos(a·b / |a||b|) · (1 − stiffness)`, axis `unit(a × b)`;
3. turn the joint, then apply the **soft limit**, then the **hard limit**;
4. if the joint is limited, apply CSP's rejection test: if what is left of the turn moves the tip about an axis whose
   cosine with the asked axis is below 0.1, the step is taken back.

The chain is the dragged joint's parent and up, to the root or to the first **anchor**, and never past a node that is
not a bone of a skin (an "Armature" node). The CSP driver's habit of solving every other mouse event, and its
multi-effector reconciliation, are not reproduced: here there is one effector, solved from the pose the drag started
with on every event, so the result depends only on where the pointer is.

**Soft limit** (this implementation's design: the research leaves CSP's details open). Within `softZone` degrees of a
limit, a step that moves toward it is scaled to `max(0.1, margin/zone)` of the way (slerp), then clamped again.

**Aim with followers** (§6.6 of the research). A plain drag turns the parent to point the joint at the pointer. The
bones above it marked *Follow* (each while the one below also does) share the turn: from the top down each takes `1/n`
of the angle still left, so the parent, last, points the joint exactly.

**Twist**, as CSP's twist tool: ±1° for each pointer event, by the sign of `dx + dy`, within the twist limit.

### Drawing (`ModelRenderer`)

On the application's thread the posed mesh is drawn by OpenGL into an offscreen framebuffer with 4 samples per pixel:
ambient 0.35 plus 0.65 Lambert from the camera, in linear colour, written out in sRGB and premultiplied. Opaque
materials are drawn first, alpha-blended ones after. Elsewhere (another thread, no OpenGL, or `MOTHION_SOFTWARE_3D=1`)
`rig3d::rasterize` draws the same on the CPU: z-buffer, 2×2 samples, perspective-correct UVs. `FrameRenderer` keeps
each model cel's picture until its model, settings, pose or the page size change. It crops the picture to what it
covers and moves it with the cel's own keys and trajectory.

## File format

Format 21 adds the table `model_cels (cel, file_name, model blob, joints text)`: the glTF bytes whole and the joint
settings as JSON (only what differs from the defaults). The poses are the cel's motion keys, saved as for any cel. A
model that can no longer be read stays a model cel that shows nothing, and its keys are kept.

## What is not done

- glTF files with external buffers (export a `.glb`); textures other than the base colour; animations in the file.
- CSP's PuruPuru spring physics (hair, cloth), its axis-reordering table, its solver for several effectors at once.
- Translation constrained to a gizmo axis (CSP's `FUN_143a23d90`). Targets here lie in the plane across the view
  through the joint.
- Validation against CSP itself: as the research says, it is static reading until a dynamic test confirms it.
