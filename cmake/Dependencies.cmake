# Third-party libraries besides Qt.
#
# Eigen (MPL-2.0, header only): linear algebra for the puppet warp.
# A system copy is used when there is one (e.g. libeigen3-dev); otherwise
# the release archive is downloaded once at configure time.

find_package(Eigen3 3.4 QUIET NO_MODULE)
if(TARGET Eigen3::Eigen)
    message(STATUS "Eigen: system copy ${Eigen3_VERSION}")
else()
    include(FetchContent)
    FetchContent_Declare(eigen
        URL https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
        URL_HASH SHA256=8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        # Headers only: never run Eigen's own CMake project (tests, docs...).
        SOURCE_SUBDIR do-not-build)
    FetchContent_MakeAvailable(eigen)
    add_library(mothion_eigen INTERFACE)
    target_include_directories(mothion_eigen SYSTEM INTERFACE ${eigen_SOURCE_DIR})
    add_library(Eigen3::Eigen ALIAS mothion_eigen)
    message(STATUS "Eigen: downloaded 3.4.0")
endif()
