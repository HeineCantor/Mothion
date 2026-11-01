cmake_minimum_required(VERSION 3.21)

project(Mothion
    VERSION 0.1.0
    DESCRIPTION "2D frame-by-frame animation (prototype)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MOTHION_BUILD_TESTS "Build unit tests" ON)
option(MOTHION_BUILD_DEVTOOLS "Build developer tools (brush test sheet)" OFF)
# Wintab on Windows needs a *private* Qt API (QNativeInterface::Private::QWindowsApplication).
# It is isolated in src/input/TabletBackend_wintab.cpp: turn this off if a Qt update breaks it.
option(MOTHION_ENABLE_WINTAB "Allow switching the Windows tablet backend to Wintab (--wintab)" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

# Qt is LGPL: it must be linked dynamically so the application can stay closed source.
get_target_property(_mothion_qt_type Qt6::Core TYPE)
if(_mothion_qt_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "Mothion must link Qt dynamically (LGPL). Use a shared Qt build.")
endif()

include(cmake/CompilerWarnings.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(src)

if(MOTHION_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
