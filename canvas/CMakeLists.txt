add_library(glacier_canvas STATIC
    include/glacier/canvas/BoardScene.h
    include/glacier/canvas/BoardView.h
    include/glacier/canvas/ImageCache.h
    include/glacier/canvas/ImageItem.h
    src/BoardScene.cpp
    src/BoardView.cpp
    src/ImageCache.cpp
    src/ImageItem.cpp)
add_library(glacier::canvas ALIAS glacier_canvas)

glacier_configure_target(glacier_canvas)
target_include_directories(glacier_canvas PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(glacier_canvas PUBLIC glacier::io Qt6::Widgets)
