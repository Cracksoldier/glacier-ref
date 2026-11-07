cmake_minimum_required(VERSION 3.21)

project(GlacierRef
    VERSION 0.1.0
    DESCRIPTION "Offline desktop reference-board application"
    HOMEPAGE_URL "https://github.com/Cracksoldier/glacier-ref"
    LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GlacierCompilerSettings)

option(GLACIER_BUILD_TESTS "Build the Qt Test suite" ON)
option(GLACIER_BUILD_TOOLS "Build developer tools (dataset generator, benchmark)" ON)

# Minimum Qt: 6.11, the Qt of the Flatpak's KDE runtime (packaging/linux/
# flatpak), which CI builds and tests against; every other job uses the pin in
# .github/qt-version.env. Lower it only together with a CI job that builds
# against that lower version.
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets Svg Concurrent)
qt_standard_project_setup()

add_subdirectory(third_party/miniz)
add_subdirectory(core)
add_subdirectory(io)
add_subdirectory(persistence)
add_subdirectory(editing)
add_subdirectory(platform)
add_subdirectory(canvas)
add_subdirectory(app)
if(GLACIER_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(GLACIER_BUILD_TESTS)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Test)
    enable_testing()
    include(GlacierTesting)
    add_subdirectory(tests)
endif()
