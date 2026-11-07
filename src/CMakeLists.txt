# ---------------------------------------------------------------------------
# mothion_core: plain C++20, NO Qt. Input samples, pressure curve, smoothing.
# Anything here must stay usable from a future engine / tests without Qt.
# ---------------------------------------------------------------------------
add_library(mothion_core STATIC
    core/PenSample.h
    core/PressureCurve.h core/PressureCurve.cpp
    core/StrokeSmoother.h core/StrokeSmoother.cpp
    core/RateMeter.h core/RateMeter.cpp

    core/brush/Random.h
    core/brush/Effector.h core/brush/Effector.cpp
    core/brush/BrushStyle.h
    core/brush/DabGenerator.h core/brush/DabGenerator.cpp
    core/brush/Hardness.h
    core/brush/StrokeShaper.h core/brush/StrokeShaper.cpp
    core/brush/VelocityMeter.h core/brush/VelocityMeter.cpp

    core/fill/FloodFill.h core/fill/FloodFill.cpp
    core/tween/Flow.h core/tween/Flow.cpp
    core/tween/Morph.h core/tween/Morph.cpp
    core/perspective/PerspectiveGrid.h core/perspective/PerspectiveGrid.cpp

    core/puppet/Geometry.h
    core/puppet/Contour.h core/puppet/Contour.cpp
    core/puppet/Triangulation.h core/puppet/Triangulation.cpp
    core/puppet/ArapDeformer.h core/puppet/ArapDeformer.cpp
    core/puppet/PuppetRig.h core/puppet/PuppetRig.cpp
    core/puppet/WarpRaster.h core/puppet/WarpRaster.cpp

    core/document/Tile.h
    core/document/TiledImage.h core/document/TiledImage.cpp
    core/document/Ids.h
    core/document/Drawing.h
    core/document/Timing.h core/document/Timing.cpp
    core/document/Cel.h core/document/Cel.cpp
    core/document/Document.h core/document/Document.cpp
    core/document/Command.h
    core/document/Audio.h
    core/document/History.h core/document/History.cpp
    core/document/commands/GroupCommand.h core/document/commands/GroupCommand.cpp
    core/document/commands/PaintCommand.h core/document/commands/PaintCommand.cpp
    core/document/commands/DrawingCommands.h core/document/commands/DrawingCommands.cpp
    core/document/commands/TimingCommands.h core/document/commands/TimingCommands.cpp
    core/document/commands/CelCommands.h core/document/commands/CelCommands.cpp
    core/document/commands/LayerCommands.h core/document/commands/LayerCommands.cpp
    core/document/commands/AudioCommands.h core/document/commands/AudioCommands.cpp
    core/document/commands/CompositingCommands.h core/document/commands/CompositingCommands.cpp
    core/document/Compositing.h
)
set_target_properties(mothion_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(mothion_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mothion_core PUBLIC cxx_std_20) # our headers need C++20
target_link_libraries(mothion_core PRIVATE Eigen3::Eigen) # puppet warp solver (.cpp only)
mothion_set_warnings(mothion_core)

# ---------------------------------------------------------------------------
# mothion_gui: everything Qt (input adapter, canvas, tools state, UI shell).
# A static library so tests and tools can link it without main().
# ---------------------------------------------------------------------------
qt_add_library(mothion_gui STATIC
    document/DocumentController.h document/DocumentController.cpp
    document/Tween.h document/Tween.cpp
    document/Tween.h document/Tween.cpp

    io/ProjectFile.h io/ProjectFile.cpp
    io/ProjectThumbnail.h
    io/Autosave.h io/Autosave.cpp
    io/FrameExport.h io/FrameExport.cpp
    io/VideoExport.h io/VideoExport.cpp

    tools/ToolState.h tools/ToolState.cpp
    tools/Preferences.h tools/Preferences.cpp
    tools/BrushDefinition.h
    tools/BrushPreset.h tools/BrushPreset.cpp
    tools/BrushLibrary.h tools/BrushLibrary.cpp
    tools/ColorSetLibrary.h tools/ColorSetLibrary.cpp
    tools/SutImporter.h tools/SutImporter.cpp

    input/TabletInputAdapter.h input/TabletInputAdapter.cpp
    input/ProximityFilter.h input/ProximityFilter.cpp
    input/TabletBackend.h input/TabletBackend.cpp

    canvas/DebugOverlay.h canvas/DebugOverlay.cpp
    canvas/DabStamper.h canvas/DabStamper.cpp
    canvas/TileQt.h canvas/TileQt.cpp
    canvas/StrokeSession.h canvas/StrokeSession.cpp
    canvas/FrameRenderer.h canvas/FrameRenderer.cpp
    canvas/PerspectiveGizmo.h canvas/PerspectiveGizmo.cpp
    canvas/ViewTransform.h canvas/ViewTransform.cpp
    canvas/NavigationGesture.h canvas/NavigationGesture.cpp
    canvas/CanvasTool.h
    canvas/MoveTool.h canvas/MoveTool.cpp
    canvas/EyedropperTool.h canvas/EyedropperTool.cpp
    canvas/FillTool.h canvas/FillTool.cpp
    canvas/ShapeTool.h canvas/ShapeTool.cpp
    canvas/PerspectiveSnap.h canvas/PerspectiveSnap.cpp
    canvas/EffectBrushTools.h canvas/EffectBrushTools.cpp
    canvas/TransformTool.h canvas/TransformTool.cpp
    canvas/SelectionMask.h canvas/SelectionMask.cpp
    canvas/SelectionTool.h canvas/SelectionTool.cpp
    canvas/PuppetTool.h canvas/PuppetTool.cpp
    canvas/CanvasView.h canvas/CanvasView.cpp

    ui/MainWindow.h ui/MainWindow.cpp
    ui/DockLayout.h ui/DockLayout.cpp
    ui/ExportDialog.h ui/ExportDialog.cpp
    ui/VideoExportDialog.h ui/VideoExportDialog.cpp
    ui/PanelDock.h ui/PanelDock.cpp
    ui/SplashScreen.h ui/SplashScreen.cpp
    ui/SelectionBar.h ui/SelectionBar.cpp
    ui/DiscordPresence.h ui/DiscordPresence.cpp
    ui/TransformBar.h ui/TransformBar.cpp
    ui/ShapeBar.h ui/ShapeBar.cpp
    ui/PuppetBar.h ui/PuppetBar.cpp
    ui/AutoplayWindow.h ui/AutoplayWindow.cpp
    compositing/NodeType.h compositing/NodeType.cpp
    compositing/Graph.h compositing/Graph.cpp
    compositing/Evaluator.h compositing/Evaluator.cpp
    compositing/nodes/FrameNode.h compositing/nodes/FrameNode.cpp
    compositing/nodes/BlurNode.h compositing/nodes/BlurNode.cpp
    compositing/nodes/MergeNode.h compositing/nodes/MergeNode.cpp
    compositing/nodes/ColorNode.h compositing/nodes/ColorNode.cpp
    compositing/nodes/OutputNode.h compositing/nodes/OutputNode.cpp
    compositing/nodes/ChromaticAberrationNode.h compositing/nodes/ChromaticAberrationNode.cpp
    compositing/nodes/SensorNoiseNode.h compositing/nodes/SensorNoiseNode.cpp
    compositing/nodes/GlowNode.h compositing/nodes/GlowNode.cpp
    compositing/nodes/MaskNode.h compositing/nodes/MaskNode.cpp
    compositing/MaskData.h compositing/MaskData.cpp
    ui/compositing/NodeGraphView.h ui/compositing/NodeGraphView.cpp
    ui/compositing/CompositingWindow.h ui/compositing/CompositingWindow.cpp
    ui/compositing/ImagePreview.h ui/compositing/ImagePreview.cpp
    ui/compositing/RangeSlider.h ui/compositing/RangeSlider.cpp
    diagnostics/CrashHandler.h diagnostics/CrashHandler.cpp
    ui/AdvancedToolSettings.h ui/AdvancedToolSettings.cpp
    ui/IconFactory.h ui/IconFactory.cpp
    audio/AudioAnalyzer.h audio/AudioAnalyzer.cpp
    audio/AudioPlayer.h audio/AudioPlayer.cpp
    ui/Shortcuts.h ui/Shortcuts.cpp
    ui/SettingsDialog.h ui/SettingsDialog.cpp
    ui/ImportSequenceDialog.h ui/ImportSequenceDialog.cpp
    ui/Theme.h ui/Theme.cpp
    ui/panels/ToolPalettePanel.h ui/panels/ToolPalettePanel.cpp
    ui/panels/ToolPropertiesPanel.h ui/panels/ToolPropertiesPanel.cpp
    ui/panels/SliderField.h ui/panels/SliderField.cpp
    ui/panels/PressureCurveEditor.h ui/panels/PressureCurveEditor.cpp
    ui/panels/SubToolPanel.h ui/panels/SubToolPanel.cpp
    ui/panels/VerticalValueSlider.h ui/panels/VerticalValueSlider.cpp
    ui/panels/BrushSlidersPanel.h ui/panels/BrushSlidersPanel.cpp
    ui/panels/BrushSizePanel.h ui/panels/BrushSizePanel.cpp
    ui/panels/ColorWheel.h ui/panels/ColorWheel.cpp
    ui/panels/ColorWheelPanel.h ui/panels/ColorWheelPanel.cpp
    ui/panels/ColorSetPanel.h ui/panels/ColorSetPanel.cpp
    ui/panels/NavigatorPanel.h ui/panels/NavigatorPanel.cpp
    ui/panels/BlendModes.h
    ui/panels/LayersPanel.h ui/panels/LayersPanel.cpp
    ui/panels/timeline/TimelineMetrics.h
    ui/panels/timeline/TimelinePlayer.h ui/panels/timeline/TimelinePlayer.cpp
    ui/panels/timeline/TimelineView.h ui/panels/timeline/TimelineView.cpp
    ui/panels/timeline/TimelinePanel.h ui/panels/timeline/TimelinePanel.cpp
)
qt_add_resources(mothion_gui "mothion_resources"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES
        "${PROJECT_SOURCE_DIR}/resources/theme/dark.qss"
        "${PROJECT_SOURCE_DIR}/resources/icon/mothion.png"
        "${PROJECT_SOURCE_DIR}/resources/fonts/Montserrat-Bold.ttf"
)
# Tablet backend selection: Wintab needs Qt's private GUI API, Windows only.
if(WIN32 AND MOTHION_ENABLE_WINTAB)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
    target_sources(mothion_gui PRIVATE input/TabletBackend_wintab.cpp)
    target_link_libraries(mothion_gui PRIVATE Qt6::GuiPrivate)
else()
    target_sources(mothion_gui PRIVATE input/TabletBackend_nowintab.cpp)
endif()

target_include_directories(mothion_gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Qt6 REQUIRED COMPONENTS Sql) # .sut brushes and .moth projects are SQLite databases
find_package(Qt6 REQUIRED COMPONENTS Network) # the local socket of Discord's Rich Presence
target_link_libraries(mothion_gui PUBLIC mothion_core Qt6::Widgets PRIVATE Qt6::Sql Qt6::Network Qt6::Multimedia)
mothion_set_warnings(mothion_gui)

# ---------------------------------------------------------------------------
# mothion: the executable, just main().
# ---------------------------------------------------------------------------
qt_add_executable(mothion WIN32 MACOSX_BUNDLE app/main.cpp)
target_compile_definitions(mothion PRIVATE MOTHION_VERSION="${PROJECT_VERSION}")
target_link_libraries(mothion PRIVATE mothion_gui)
if(WIN32)
    target_link_libraries(mothion_gui PRIVATE dbghelp) # the minidump a crash writes
else()
    set_target_properties(mothion PROPERTIES ENABLE_EXPORTS ON) # function names in a crash report's stack
endif()
if(WIN32)
    target_sources(mothion PRIVATE "${PROJECT_SOURCE_DIR}/resources/icon/mothion.rc")
endif()
mothion_set_warnings(mothion)

# ---------------------------------------------------------------------------
# mothion_thumbnails: Windows Explorer thumbnails of .moth projects, a COM
# DLL built next to mothion.exe (register it with regsvr32). Plain Win32,
# without Qt or the C++ runtime DLLs: Explorer loads it on its own.
# ---------------------------------------------------------------------------
if(WIN32)
    add_library(mothion_thumbnails SHARED
        shell/ThumbnailProvider.cpp shell/mothion_thumbnails.def io/ProjectThumbnail.h)
    target_include_directories(mothion_thumbnails PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(mothion_thumbnails PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(mothion_thumbnails PRIVATE ole32 shlwapi shell32 windowscodecs uuid)
    set_target_properties(mothion_thumbnails PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    mothion_set_warnings(mothion_thumbnails)
endif()

# ---------------------------------------------------------------------------
# Developer tools, off by default.
# mothion_brushsheet: renders test strokes of brushes (.sut too) to a PNG.
# ---------------------------------------------------------------------------
if(MOTHION_BUILD_DEVTOOLS)
    qt_add_executable(mothion_brushsheet devtools/brushsheet.cpp)
    target_link_libraries(mothion_brushsheet PRIVATE mothion_gui)
    mothion_set_warnings(mothion_brushsheet)
endif()
