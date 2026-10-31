# ---------------------------------------------------------------------------
# mothion_core: plain C++20, NO Qt. Input samples, pressure curve, smoothing.
# Anything here must stay usable from a future engine / tests without Qt.
# ---------------------------------------------------------------------------
add_library(mothion_core STATIC
    core/PenSample.h
    core/PressureCurve.h core/PressureCurve.cpp
    core/StrokeSmoother.h core/StrokeSmoother.cpp
    core/RateMeter.h core/RateMeter.cpp
)
set_target_properties(mothion_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(mothion_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(mothion_core PUBLIC cxx_std_20) # our headers need C++20
mothion_set_warnings(mothion_core)

# ---------------------------------------------------------------------------
# mothion_gui: everything Qt (input adapter, canvas, tools state, UI shell).
# A static library so tests and tools can link it without main().
# ---------------------------------------------------------------------------
qt_add_library(mothion_gui STATIC
    tools/ToolState.h tools/ToolState.cpp

    input/TabletInputAdapter.h input/TabletInputAdapter.cpp
    input/ProximityFilter.h input/ProximityFilter.cpp
    input/TabletBackend.h input/TabletBackend.cpp

    canvas/DebugOverlay.h canvas/DebugOverlay.cpp
    canvas/RasterSurface.h canvas/RasterSurface.cpp
    canvas/StrokeRenderer.h canvas/StrokeRenderer.cpp
    canvas/ViewTransform.h canvas/ViewTransform.cpp
    canvas/NavigationGesture.h canvas/NavigationGesture.cpp
    canvas/CanvasView.h canvas/CanvasView.cpp

    ui/MainWindow.h ui/MainWindow.cpp
    ui/DockLayout.h ui/DockLayout.cpp
    ui/PanelDock.h ui/PanelDock.cpp
    ui/IconFactory.h ui/IconFactory.cpp
    ui/Theme.h ui/Theme.cpp
    ui/panels/ToolPalettePanel.h ui/panels/ToolPalettePanel.cpp
    ui/panels/ToolPropertiesPanel.h ui/panels/ToolPropertiesPanel.cpp
    ui/panels/SliderField.h ui/panels/SliderField.cpp
    ui/panels/PressureCurveEditor.h ui/panels/PressureCurveEditor.cpp
    ui/panels/ColorPanel.h ui/panels/ColorPanel.cpp
    ui/panels/LayersPanel.h ui/panels/LayersPanel.cpp
    ui/panels/timeline/FakeTimelineData.h ui/panels/timeline/FakeTimelineData.cpp
    ui/panels/timeline/TimelineMetrics.h
    ui/panels/timeline/TimelineView.h ui/panels/timeline/TimelineView.cpp
    ui/panels/timeline/TimelinePanel.h ui/panels/timeline/TimelinePanel.cpp
)
qt_add_resources(mothion_gui "mothion_resources"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES "${PROJECT_SOURCE_DIR}/resources/theme/dark.qss"
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
target_link_libraries(mothion_gui PUBLIC mothion_core Qt6::Widgets)
mothion_set_warnings(mothion_gui)

# ---------------------------------------------------------------------------
# mothion: the executable, just main().
# ---------------------------------------------------------------------------
qt_add_executable(mothion WIN32 MACOSX_BUNDLE app/main.cpp)
target_compile_definitions(mothion PRIVATE MOTHION_VERSION="${PROJECT_VERSION}")
target_link_libraries(mothion PRIVATE mothion_gui)
mothion_set_warnings(mothion)
