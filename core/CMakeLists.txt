configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/GlacierVersion.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/glacier/core/Version.h"
    @ONLY)

add_library(glacier_core STATIC
    include/glacier/core/Affine2D.h
    include/glacier/core/AppInfo.h
    include/glacier/core/BatchLayout.h
    include/glacier/core/BoardDocument.h
    include/glacier/core/Id.h
    include/glacier/core/Limits.h
    include/glacier/core/SessionAssetStore.h
    src/Affine2D.cpp
    src/AppInfo.cpp
    src/BatchLayout.cpp
    src/BoardDocument.cpp
    src/Id.cpp
    src/SessionAssetStore.cpp)
add_library(glacier::core ALIAS glacier_core)

glacier_configure_target(glacier_core)
target_include_directories(glacier_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(glacier_core PUBLIC Qt6::Core)
