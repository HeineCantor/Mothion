# Mothion

Frame-by-frame 2D animation software for solo animators and small studios.

[![CI](https://github.com/HeineCantor/Mothion/actions/workflows/ci.yml/badge.svg)](https://github.com/HeineCantor/Mothion/actions/workflows/ci.yml)

**Status: prototype.**

Mothion currently supports layers, drawings and Harmony-style exposure timing, with undo/redo, a timeline and a layers panel. It includes tablet input, a brush engine with Clip Studio Paint brush import, image import, move and selection tools, puppet warp, project saving/loading (`.moth`) and PNG/JPEG frame export.

This is still an homemade project, made with love and moths in our hearts, so enjoy and tell us how to improve this :D

## Build

### Requirements

- CMake ≥ 3.21
- C++20 compiler
- Qt ≥ 6.5 (Core, Gui, Widgets, Sql, Network, Multimedia; Test for the test suite)
- Eigen 3.4 (MPL-2.0, header-only)

Qt Multimedia, with its FFmpeg backend, decodes and plays the project's audio track. It is a separate package or module in most Qt distributions (`qt6-multimedia-dev` on Debian/Ubuntu, the *Qt Multimedia* module in the Qt Online Installer).

Exporting a video (File > Export video…) runs the `ffmpeg` program, which is not part of the build: install it (`sudo apt install ffmpeg`, `winget install ffmpeg`) and put it on the `PATH`, or next to the Mothion executable. Without it the rest of the application works as usual. The build scripts install it (with `apt`, `pacman` or `dnf`/`zypper`, and `winget` on Windows); on Fedora and openSUSE the distribution's own ffmpeg cannot write H.264, so for the video export take the one from RPM Fusion or Packman.

Qt's SQLite driver (`qsqlite`) is used to read `.sut` brush files. Qt must be dynamically linked, as required by the LGPL. CMake checks for static Qt builds and stops if one is found.

Eigen is used by the puppet warp implementation. CMake looks for a system installation first and downloads it during configuration if none is available.

## Continuous integration

GitHub Actions runs three jobs on every pull request and every push to `main` (and by hand from the Actions tab):

- **Ubuntu**: builds with Qt 6.8.3 and runs the CTest suite on the offscreen Qt platform (no display needed), then
  runs it again in the `C` locale, where Qt's locale-aware pieces behave differently.
- **Sanitizers**: the same suite built with the address and undefined-behaviour sanitizers.
- **Windows**: builds with MSVC and runs the suite.

The UI tests are one executable for each area (`tst_ui_panels`, `tst_ui_tools`, `tst_ui_drawing`, `tst_ui_puppet`,
`tst_ui_timeline`, `tst_ui_audio`, `tst_ui_compositing`, `tst_ui_window`), so `ctest --parallel` runs them side by side
and a change only rebuilds the area it touches.

To run the same checks locally:

```sh
cmake -S . -B build-ci -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DMOTHION_BUILD_TESTS=ON \
  -DMOTHION_ENABLE_WINTAB=OFF

cmake --build build-ci --parallel

QT_QPA_PLATFORM=offscreen ctest --test-dir build-ci --output-on-failure --parallel 4
LC_ALL=C QT_QPA_PLATFORM=offscreen ctest --test-dir build-ci --output-on-failure --parallel 4  # the C locale

# With the sanitizers (leaks are not checked: Qt leaves allocations at exit)
cmake -S . -B build-san -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMOTHION_ENABLE_WINTAB=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-san --parallel
ASAN_OPTIONS=detect_leaks=0 QT_QPA_PLATFORM=offscreen ctest --test-dir build-san --output-on-failure --parallel 4
```

## Building

### Linux

The easiest way to build on Linux is:

```sh
./build.sh
```

The script detects the package manager (apt, dnf, pacman or zypper), installs missing dependencies through `sudo`, builds the project and runs the tests.

Available options:

- `--run`: start Mothion after building.
- `--skip-tests`: skip the test suite.
- `--dev-tools`: build development tools.
- `--config Release`: select the build configuration.
- `--qt-dir <dir>`: use a custom Qt installation.
- `--no-install`: skip dependency installation.

See `scripts/build-linux.sh` for details.

To start Mothion, use:

```sh
./run.sh
```

This also rebuilds the project if the sources have changed. It accepts the same options as `build.sh`.

Alternatively, on Ubuntu/Debian, the project can be built manually:

```sh
sudo apt install build-essential cmake qt6-base-dev libqt6sql6-sqlite qt6-multimedia-dev qt6-wayland libeigen3-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
ctest --test-dir build
./build/src/mothion
```

To use the X11 backend from a Wayland session:

```sh
QT_QPA_PLATFORM=xcb ./build/src/mothion
```

### Windows (MSVC)

The build script handles dependency installation and deployment:

```bat
build.bat
```

It installs missing dependencies (CMake, Visual Studio 2022 C++ build tools and Qt) using `winget` and `aqtinstall`, builds the project, runs the tests and copies the required Qt DLLs next to the executable.

Available options:

- `-Run`: start Mothion after building.
- `-SkipTests`: skip the test suite.
- `-DevTools`: build development tools.
- `-Config Release`: select the build configuration.
- `-QtVersion`: select the Qt version.
- `-QtRoot`: Qt installation directory (default: `C:\Qt`).

See `scripts/build-windows.ps1` for details.

To start Mothion, use:

```bat
run.bat
```

It rebuilds the project if the sources have changed and accepts the same options as `build.bat`.

#### Manual build

1. Install Visual Studio 2022 with the *Desktop development with C++* workload.
2. Install Qt 6.x for **MSVC 2022 64-bit** using the Qt Online Installer, with the **Qt Multimedia** module selected under *Additional Libraries*.
3. Open an *x64 Native Tools Command Prompt for VS 2022* and run:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:\Qt\6.10.2\msvc2022_64

cmake --build build --config RelWithDebInfo
ctest --test-dir build -C RelWithDebInfo

C:\Qt\6.10.2\msvc2022_64\bin\windeployqt.exe build\RelWithDebInfo\mothion.exe

build\RelWithDebInfo\mothion.exe
```

`windeployqt` copies the required Qt DLLs and plugins (including the FFmpeg backend of Qt Multimedia) next to the executable.

### CMake options

| Option | Default | Description |
|---|---|---|
| `MOTHION_BUILD_TESTS` | ON | Build the QtTest suite. |
| `MOTHION_BUILD_DEVTOOLS` | OFF | Build `mothion_brushsheet`, a tool for rendering brush test strokes to PNG. Usage: `mothion_brushsheet out.png [brush.sut ...]`. |
| `MOTHION_ENABLE_WINTAB` | ON | Windows only. Enables `--wintab`, using the private `Qt6::GuiPrivate` API. Disable it if a Qt update breaks compatibility. |

### Command-line options

- `mothion [project.moth]`: open a project at startup.
- `--wintab` (Windows): use Wintab instead of Windows Ink. Requires the tablet driver's `wintab32.dll`.

## Architecture

The project is split into a Qt-independent core library, a Qt-based GUI library and the application executable.

```text
src/
├── core/       Core C++ code (no Qt dependency)
│   ├── brush/      Brush engine: dynamics, dab generation and stroke shaping
│   ├── puppet/     Puppet warp: contour extraction, triangulation and deformation
│   └── document/   Document model: tiled images, drawings, layers, timing and history
│
├── document/   DocumentController: manages the document, history and current frame/layer
├── io/         Project loading/saving (.moth) and PNG/JPEG frame export
├── input/      Tablet and mouse event handling
├── canvas/     Canvas view, rendering, navigation, painting and editing tools
├── tools/      Tool state, brush presets, brush definitions and brush library
├── ui/         Main window, dock layout, theme and panels
└── app/        Application entry point
```

### Core components

- **Brush engine:** pressure, velocity, tilt and other dynamics; configurable curves, spacing, tips, rotation, spray and colour variation. Stroke shaping includes smoothing, correction and start/end handling.
- **Puppet warp:** contour extraction, constrained Delaunay triangulation, mesh deformation using an as-rigid-as-possible solver, pinning and rasterization. Uses Eigen.
- **Document model:** tiled images (64×64 copy-on-write tiles), drawings, layers, Harmony-style exposure timing, commands and undo/redo history.
- **Canvas:** painting, layer composition, navigation, selection, move and puppet warp tools.
- **Brush library:** brush presets and definitions, including import of Clip Studio Paint `.sut` brushes.
- **Project I/O:** SQLite-based `.moth` project files, with versioning and compressed image tiles. Frames can be exported individually or as a numbered PNG/JPEG sequence.

The libraries are organized as follows:

```text
mothion_core (static, no Qt)
    └── mothion_gui (static, Qt Widgets)
            └── mothion (executable)
```

### Input handling

Tablet and mouse events are converted to `PenSample` objects in `input/`. The rest of the application works with this common input representation, without depending on Qt input events.

```text
QTabletEvent / QMouseEvent
          |
          v
    toPenSample()
          |
          v
      CanvasView
          |
          +── NavigationGesture
          |
          +── StrokeSmoother
                  |
                  v
              BrushInput
                  |
                  v
            DabGenerator
                  |
                  v
             StrokeSession
                  |
                  v
             TiledImage
                  |
                  v
             PaintCommand
                  |
                  v
                History
```

When a stroke ends, it is redrawn using the final points produced by `StrokeShaper`. Brushes that require it reuse the same random seed, keeping the final stroke consistent with the live preview.

### Dependency rules

- UI panels communicate through `ToolState` signals and slots. The Navigator is the exception: it uses the public `CanvasView` API.
- `DockLayout` handles panel arrangement and persistence.
- Tablet-related Qt private API usage is confined to `input/TabletBackend_wintab.cpp`.

## Crashes and logs

Mothion keeps a log of each run (the last three) and, if it dies, leaves what is needed to understand why:

- **Log**: `logs/mothion.log` (and `mothion.1.log`, `mothion.2.log` for the earlier runs) in the application data
  directory (`~/.local/share/Mothion/Mothion` on Linux, `%APPDATA%\Mothion\Mothion` on Windows). It holds the Qt
  messages with the time, the system and screens, and the projects opened and saved.
- **Crash report**: `crashes/crash-<time>.txt` with the reason and, on Linux, the stack of calls (offsets that
  `addr2line -e mothion -f -C -p <offset>` turns into places; build with RelWithDebInfo). On Windows a
  `crash-<time>.dmp` minidump is written next to it, to open in Visual Studio or WinDbg with the build's `.pdb`.
- **Backup**: after the report, the program tries to save a copy of the project as
  `crashes/<name>-backup-<time>.moth` (the stroke being drawn is not in it, and a badly damaged program may not
  manage it). The periodic autosave (below) covers what a crash cannot.

The next start tells the user about the crash, with buttons to open the folder and the copy. A tester who hits a
crash should send the report and the latest logs.

## Autosave

While a project has unsaved work, Mothion keeps a copy of it every few minutes (Settings > General > Autosave every;
5 by default, 0 turns it off), apart from the project's file, in `autosave/` of the application data directory. The
copy is made when the user is not drawing, and replaced at each autosave: there is one for each project. Saving the
project, or closing it on purpose, takes the copy away. If the program (or the machine) dies, the next start offers to
recover the work; a copy older than the project's file is dropped without asking.

## Controls (provisional)

| Action | Control |
|---|---|
| Brush / Eraser / Hand / Zoom | B / E / H / Z. Press E again to return to the previous tool. |
| Move / Rectangle selection / Lasso / Puppet warp | V / M / L / P |
| Move | Drag the drawing or selected area. Shift constrains movement to one axis. |
| Selection | Drag a rectangle (Shift for square) or use the lasso. Click to deselect. Ctrl+A selects all; Ctrl+D deselects. |
| Puppet warp | Click to add pins, drag to deform. Alt+click or Delete removes a pin. Enter applies; Esc cancels. Mesh options are in Tool properties. |
| Pan | Space or Tab + drag, middle mouse button, pen side button or Hand tool. |
| Zoom | Mouse wheel, Space+Ctrl+drag, Zoom tool, Ctrl++ / Ctrl+- / Ctrl+0 (fit). |
| Rotate | Shift+wheel, Space+Shift+drag, Ctrl+, / Ctrl+. / Ctrl+/ (reset). |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y. History is limited to 500 steps or 1 GB. |
| Input debug overlay | F12 |
| Timeline | Click or drag to select frame/layer. Arrow keys navigate; Home/End go to the first/last frame. Ctrl+wheel changes frame zoom. |
| Hold | Drag the right edge of an exposure block to extend it and shift later frames. |
| Drawings | Drawing on an empty frame creates a new drawing. Right-click the timeline for drawing/frame operations. |
| Layers | Add, delete and reorder using the timeline controls. Eye toggles visibility; padlock toggles locking; double-click to rename. |
| Navigator | Drag the thumbnail to move the view. Sliders control zoom and rotation; H/V flip the view. |
| Layout | Window → Reset default layout |
| Theme | Window → Theme → System / Light / Dark |
| Brushes | File → Import brush (.sut)… Imported brushes appear in Sub tool. |
| Images | File → Import image… Creates a new layer above the current one, with one drawing for the whole scene. |
| Projects | Ctrl+N (New), Ctrl+O (Open), Ctrl+S (Save), Ctrl+Shift+S (Save as). Projects use `.moth`; older `.mothion` files can also be opened. |
| Export | Ctrl+E: export the current frame, the whole scene or a frame range as PNG (with optional transparency) or JPEG. |

## Tablet support

Mothion uses Qt's tablet input support. The backend and available features depend on the operating system and tablet driver.

### Qt 6 behaviour

- **Windows:** Qt 6 uses Windows Ink (`WM_POINTER`) by default. Wintab can be enabled with `--wintab`, through `QWindowsApplication::setWinTabEnabled()` (a private Qt API). The older `-platform windows:nowmpointer` option is no longer available.
- **Tablet event compression:** `Qt::AA_CompressTabletEvents` is disabled by default and explicitly set in `main.cpp`. On Windows, Qt also provides historical samples through `GetPointerPenInfoHistory`; X11 does not compress tablet motion. Tablet events must be accepted, otherwise Qt may generate mouse events from them.
- **Proximity:** `TabletEnterProximity` and `TabletLeaveProximity` events are delivered to `QApplication`.
- **X11:** Qt distinguishes pen and eraser using the XInput device name (for example, `stylus`, `pen` or `eraser`). Some drivers expose a single device, making the eraser indistinguishable.
- **Wayland:** Qt uses the `tablet-unstable-v2` protocol. Eraser information comes from the compositor, and Qt does not compress tablet events. Position coordinates use 1/256-pixel steps. A compatible compositor is required (GNOME, KDE Plasma, Sway).

To see which devices Qt detects:

```sh
QT_LOGGING_RULES="qt.qpa.input*=true" ./mothion
```

### Testing tablet input

Press **F12** to open the input debug overlay. Check the following:

1. **Device:** the type should be `Stylus` and the tool should be `pen`. If it shows `Mouse`, the driver may be in mouse mode or Qt may not recognize the device.
2. **Proximity:** moving the pen close to the tablet should report `in`; moving it away should report `out`. The brush cursor should follow the pen while hovering.
3. **Position:** coordinates should include changing decimal values while moving the pen.
4. **Pressure:** values should range from approximately 0 to 1.000. The pressure curve indicator and stroke width should respond to pressure.
5. **Tilt:** X/Y values should change when tilting the pen, if supported.
6. **Rotation:** available only on pens that support it, such as the Wacom Art Pen.
7. **Eraser:** flipping the pen should switch to the eraser and erase strokes.
8. **Side buttons:** buttons should be reported in the overlay. They can also be used to navigate the canvas.
9. **Rate:** continuous strokes typically report 100–300 Hz, depending on the tablet and driver. Values around 60 Hz may indicate event compression or mouse fallback.

### Wacom

- **Windows:** use the official Wacom driver. For the default Windows Ink backend, enable *Use Windows Ink* under Wacom Tablet Properties → Mapping. You can also compare it with the `--wintab` backend.
- **Linux:** supported by the kernel's `wacom` driver. On X11, `xinput list` should show separate pen and eraser devices. On Wayland, configure the tablet through the desktop environment's tablet settings.

### Huion

- **Windows:** use the HuionTablet driver. Toggle Windows Ink according to the backend being tested. The `--wintab` option requires the Huion driver.
- **Linux:** many models work with the `hid-uclogic` kernel driver. Some pens do not have a physical eraser.

### XP-Pen

- **Windows:** use the Pentablet driver. If pressure does not work with Windows Ink, try `--wintab`.
- **Linux:** recent models are supported by `hid-uclogic`; others may require the XP-Pen driver. On X11, if the overlay reports `Mouse`, check the device name with `xinput list`.

## Licenses

Qt is used under LGPL v3 with dynamic linking. The project has no GPL dependencies.

Icons, names and graphics are original to Mothion. Icons are drawn in code in `IconFactory`.