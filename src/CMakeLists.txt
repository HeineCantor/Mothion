# ---------------------------------------------------------------------------
# mothion_core: plain C++20, NO Qt. Input samples, pressure curve, smoothing.
# Anything here must stay usable from a future engine / tests without Qt.
# ---------------------------------------------------------------------------
add_library(mothion_core STATIC
    core/PenSample.h
)
set_target_properties(mothion_core PROPERTIES LINKER_LANGUAGE CXX AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(mothion_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
mothion_set_warnings(mothion_core)

# ---------------------------------------------------------------------------
# mothion: the application (UI shell, canvas, Qt input adapter).
# ---------------------------------------------------------------------------
qt_add_executable(mothion WIN32 MACOSX_BUNDLE
    app/main.cpp
    ui/MainWindow.h ui/MainWindow.cpp
    ui/Theme.h ui/Theme.cpp
)
qt_add_resources(mothion "mothion_resources"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES "${PROJECT_SOURCE_DIR}/resources/theme/dark.qss"
)
target_include_directories(mothion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(mothion PRIVATE MOTHION_VERSION="${PROJECT_VERSION}")
target_link_libraries(mothion PRIVATE mothion_core Qt6::Widgets)
mothion_set_warnings(mothion)
