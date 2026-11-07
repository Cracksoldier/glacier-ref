add_library(glacier_platform STATIC
    include/glacier/platform/PlatformInfo.h
    include/glacier/platform/PlatformWindowService.h
    src/PlatformInfo.cpp
    src/PlatformWindowService.cpp)
add_library(glacier::platform ALIAS glacier_platform)

glacier_configure_target(glacier_platform)
target_include_directories(glacier_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(glacier_platform PUBLIC Qt6::Gui PRIVATE glacier::core)
