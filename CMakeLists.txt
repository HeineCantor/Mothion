cmake_minimum_required(VERSION 3.21)

# The version shown (About, the window); a release sets it from its tag (v1.2.0 -> 1.2.0, see .github/workflows/release.yml).
# Only its leading numbers go to project(): a tag like v1.2.0-beta1 is shown whole.
set(MOTHION_VERSION "0.1.0" CACHE STRING "The version of Mothion")
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" _mothion_numeric_version "${MOTHION_VERSION}")
if(NOT _mothion_numeric_version)
    message(FATAL_ERROR "MOTHION_VERSION must start with a number, e.g. 1.2.0 (it is \"${MOTHION_VERSION}\")")
endif()

project(Mothion
    VERSION ${_mothion_numeric_version}
    DESCRIPTION "2D frame-by-frame animation (prototype)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Linking the many test programs, each with the debug information, all at once can fill the memory: two at a time.
if(CMAKE_GENERATOR MATCHES "Ninja")
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS mothion_link=2)
    set(CMAKE_JOB_POOL_LINK mothion_link)
endif()

option(MOTHION_BUILD_TESTS "Build unit tests" ON)
option(MOTHION_BUILD_DEVTOOLS "Build developer tools (brush test sheet)" OFF)
# Wintab on Windows needs a *private* Qt API (QNativeInterface::Private::QWindowsApplication).
# It is isolated in src/input/TabletBackend_wintab.cpp: turn this off if a Qt update breaks it.
option(MOTHION_ENABLE_WINTAB "Allow switching the Windows tablet backend to Wintab (--wintab)" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Multimedia) # Multimedia: decoding and playing the audio track
qt_standard_project_setup()

# Qt is LGPL: it is linked dynamically, so the Qt DLLs handed out with Mothion can be replaced with another build.
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
