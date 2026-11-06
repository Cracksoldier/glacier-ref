configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/GlacierVersion.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/glacier/core/Version.h"
    @ONLY)

add_library(glacier_core STATIC
    include/glacier/core/AppInfo.h
    include/glacier/core/Id.h
    src/AppInfo.cpp
    src/Id.cpp)
add_library(glacier::core ALIAS glacier_core)

glacier_configure_target(glacier_core)
target_include_directories(glacier_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(glacier_core PUBLIC Qt6::Core)
