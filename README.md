# Mothion

Frame-by-frame 2D animation software for solo animators and small studios.

**Status: prototype.** There is a document model (layers, drawings, Harmony-style
exposure timing) with undo, a timeline and a layers panel, tablet input, and a brush
engine that can import Clip Studio Paint brushes. There is no saving yet.

## Build

Requirements: CMake ≥ 3.21, a C++20 compiler, Qt ≥ 6.5 (Core, Gui, Widgets, Sql; Test for the tests).
Sql reads `.sut` brushes (SQLite databases, through the `qsqlite` driver shipped with Qt).
Qt must be linked **dynamically** (LGPL): CMake stops with an error if it finds a static Qt.

### Linux (Ubuntu / Debian)

```sh
sudo apt install build-essential cmake qt6-base-dev qt6-wayland
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build          # unit tests
./build/mothion
```

To try the X11 path from a Wayland session (through XWayland):
`QT_QPA_PLATFORM=xcb ./build/mothion`.

### Windows (MSVC)

1. Install Visual Studio 2022 (workload "Desktop development with C++") and, with the
   Qt Online Installer, Qt 6.x for **MSVC 2022 64-bit**.
2. From an "x64 Native Tools Command Prompt for VS 2022":

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:\Qt\6.10.2\msvc2022_64
cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo
C:\Qt\6.10.2\msvc2022_64\bin\windeployqt.exe build\RelWithDebInfo\mothion.exe
build\RelWithDebInfo\mothion.exe
```

`windeployqt` copies Qt's DLLs and plugins next to the executable (dynamic linking).

### CMake options

| Option | Default | Effect |
|---|---|---|
| `MOTHION_BUILD_TESTS` | ON | Builds the tests (QtTest). |
| `MOTHION_BUILD_DEVTOOLS` | OFF | Builds `mothion_brushsheet`: `mothion_brushsheet out.png [brush.sut ...]` renders test strokes of brushes to a PNG. |
| `MOTHION_ENABLE_WINTAB` | ON | Windows only: enables `--wintab`. Uses a **private** Qt API (`Qt6::GuiPrivate`); if a Qt update breaks it, set it to OFF. |

### Command line options

- `--wintab` (Windows): use Wintab instead of Windows Ink. Needs the tablet driver's `wintab32.dll`.

## Architecture

```
src/
├─ core/     plain C++, NO Qt dependency (enforced by CMake: mothion_core does not link Qt)
│            PenSample, PressureCurve, StrokeSmoother, RateMeter
│  ├─ brush/  brush engine: Effector (dynamics: pressure, velocity, tilt, random, quadratic
│  │          spline curves), BrushStyle (a "compiled" brush), DabGenerator (spacing recomputed
│  │          at every dab with the remainder carried across segments, tips, rotation, spray,
│  │          color variation; deterministic Lcg per stroke), StrokeShaper (stroke start/end,
│  │          anti-jitter filter, correction), VelocityMeter, hardness profile
│  └─ document/ project model: TiledImage (64×64 copy-on-write tiles), Drawing,
│              Timing (Harmony-style exposures), Layer, Document, Command + commands/, History
├─ document/ DocumentController: owns Document and History, current layer/frame,
│            runs commands and turns them into Qt signals (the UI's only access to the model)
├─ input/    the only place that knows QTabletEvent/QMouseEvent → PenSample
│            TabletInputAdapter, ProximityFilter, TabletBackend(_wintab|_nowintab)
├─ canvas/   CanvasView (widget), ViewTransform (zoom/pan/rotation/mirroring), NavigationGesture,
│            DabStamper (round, soft and image dabs, pre-colored, mipmapped),
│            StrokeSession (live stroke on a TiledImage: flow builds up, opacity is a ceiling,
│            watercolor mixing, ribbon, water edge),
│            FrameRenderer (composes the visible layers at a frame), TileQt (tiles↔QImage),
│            DebugOverlay (F12)
├─ tools/    ToolState: active tool, size, opacity, colors, curve, stabilizer;
│            BrushPreset: presets ("sub tools"); BrushDefinition: style + tip images;
│            BrushLibrary: imported brushes (copies in AppData/brushes, reloaded at startup);
│            SutImporter: reads Clip Studio Paint .sut files (parameters, dynamics, tips)
├─ ui/       MainWindow, DockLayout (arrangement and persistence), PanelDock, Theme, IconFactory
│  └─ panels/ Tools, Sub tool (presets with previews), Tool properties,
│             vertical Size/Opacity sliders, Color wheel, Color set,
│             Navigator, Layers; timeline/ = exposure timeline (Harmony-style)
└─ app/      main.cpp
```

Libraries: `mothion_core` (static, no Qt) ← `mothion_gui` (static, Qt Widgets) ← `mothion` (executable, `main` only).

**Input flow**

```
QTabletEvent / QMouseEvent ──input::toPenSample()──▶ PenSample
   │                                                    │
   │  (proximity: only to QApplication → ProximityFilter)│
   ▼                                                    ▼
CanvasView::handleSample ──▶ NavigationGesture (hand / zoom / rotate)
                         └─▶ StrokeSmoother → BrushInput (pressure curve, velocity, tilt)
                              → brush::DabGenerator → StrokeSession (DabStamper)
                              (into the drawing at the current layer/frame; when the pen lifts,
                               the stroke is redrawn from StrokeShaper's final points with the
                               same seed if the brush needs it, then PaintCommand → History
                               through DocumentController)
```

After `toPenSample()`, no part of the program touches Qt input events.

**Dependency rules**
- Panels do not know the canvas: they only talk to `ToolState` (signals/slots).
  The one exception is the Navigator, a view of the view, which uses `CanvasView`'s public API.
- `DockLayout` is the only place that knows how panels are arranged: redoing the UX
  means changing it there (and `Theme`/`dark.qss`/`IconFactory` for the look).
- Code using Qt private APIs is confined to `input/TabletBackend_wintab.cpp`.

## Controls (provisional)

| Action | Control |
|---|---|
| Brush / Eraser / Hand / Zoom | B / E / H / Z |
| Pan | Space + drag, middle button, pen side button, Hand tool |
| Zoom | wheel, Space+Ctrl+drag, Zoom tool, Ctrl + / Ctrl − / Ctrl 0 (fit) |
| Rotate | Shift + wheel, Space+Shift+drag, Ctrl , / Ctrl . / Ctrl / (reset) |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y (up to 500 steps or 1 GB) |
| Input debug overlay | F12 |
| Timeline | click/drag: current frame and layer; ← → Home End, ↑ ↓ layer; Ctrl+wheel = frame zoom |
| Hold | drag the right edge of a block (later frames shift) |
| Drawings | drawing on an empty frame creates a drawing; right-click the timeline: new drawing, clear frame, insert/delete frame, delete drawing |
| Layers | + / bin / arrows; eye = visibility, padlock = lock; double-click = rename |
| Navigator | click/drag the thumbnail to move the view; zoom/rotation sliders; flip H/V |
| Layout | Window → Reset default layout |
| Theme | Window → Theme → System / Light / Dark |
| Brushes | File → Import brush (.sut)…: it appears in Sub tool |

## Notes on Qt 6 and tablets (checked against Qt 6.10 docs and sources)

- **Windows**: by default Qt 6 uses **Windows Ink** (`WM_POINTER` messages), with no setup.
  Wintab is only enabled from code with `QWindowsApplication::setWinTabEnabled()` (private API),
  which we expose as `--wintab`. The `-platform windows:nowmpointer` option quoted in older
  documentation no longer exists.
- **Compression**: `Qt::AA_CompressTabletEvents` is false by default (we set it again in `main.cpp`):
  on Windows Qt also delivers historical samples (`GetPointerPenInfoHistory`), on X11 it does not
  compress tablet motion. Tablet events must always be **accepted**, otherwise Qt turns them into
  mouse events, which may be compressed.
- **Proximity**: `TabletEnterProximity`/`TabletLeaveProximity` only reach `QApplication`.
- **X11**: Qt tells pen from eraser by the XInput device **name** ("stylus", "pen", "eraser", ...).
  With drivers exposing a single device, the eraser may not be distinguishable.
- **Wayland**: `tablet-unstable-v2` protocol, the eraser type comes from the compositor; no
  compression on Qt's side, positions in 1/256 px steps. Needs a compositor that supports it
  (GNOME, KDE Plasma, Sway).

Useful logs: `QT_LOGGING_RULES="qt.qpa.input*=true" ./mothion` shows the devices Qt detects.

## Testing a tablet

Start Mothion, press **F12** and check:

1. **Device**: type `Stylus`, tool `pen`. If `Mouse` shows up while using the pen, the driver
   is in mouse mode or Qt does not recognize the device.
2. **Proximity**: bringing the pen close without touching → "in", moving it away → "out";
   the brush circle follows the pen while hovering.
3. **Subpixel position**: the position's decimals change continuously.
4. **Pressure**: from ~0 to 1.000; the orange dot moves along the pressure curve and the stroke
   width follows. Try editing the curve and drawing again.
5. **Tilt**: X/Y between −60° and +60° when tilting the pen (if the pen supports it).
6. **Rotation**: non-zero only with pens that support it (e.g. Wacom Art Pen).
7. **Eraser**: flipping the pen turns the tool into `eraser`, and the stroke erases even with the
   brush selected.
8. **Side buttons**: they show up under "buttons"; pressed without touching the surface and
   dragged, they move the view.
9. **Rate**: during a continuous stroke, typically 100–300 Hz depending on model and driver.
   Much lower values (e.g. ~60 Hz) mean compression or a fallback to the mouse.

### Wacom
- **Windows**: official Wacom driver. Default = Windows Ink: in "Wacom Tablet Properties" →
  Mapping, "Use Windows Ink" must be on, otherwise no pen events arrive.
  Also try `--wintab` and compare latency and rate.
- **Linux**: supported by the kernel (`wacom`). X11: `xinput list` should show separate devices
  "... Pen stylus" and "... Pen eraser". Wayland: set up in Settings → Wacom Tablet
  (GNOME) or Drawing Tablet (KDE).

### Huion
- **Windows**: HuionTablet driver; in the driver settings turn "Windows Ink" on or off to match
  the backend you are testing (`--wintab` needs the Huion driver installed).
- **Linux**: many models work with the `hid-uclogic` kernel driver without installing anything.
  Many Huion pens have no physical eraser: in that case step 7 does not apply.

### XP-Pen
- **Windows**: XP-Pen's "Pentablet" driver; it also has a Windows Ink option in the driver
  panel. If pressure does not arrive with Windows Ink, try `--wintab`.
- **Linux**: recent models are supported by `hid-uclogic`; otherwise the XP-Pen driver.
  On X11, if the overlay shows `Mouse`, check the device name with `xinput list`: Qt only
  recognizes it as a tablet from certain names (see the notes above).

## Licenses

Qt is used under LGPL v3 with dynamic linking. No GPL dependencies.
Icons, names and graphics are original to the project (icons are drawn in code in `IconFactory`).
