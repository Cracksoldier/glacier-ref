add_library(glacier_canvas STATIC
    include/glacier/canvas/BoardView.h
    src/BoardView.cpp)
add_library(glacier::canvas ALIAS glacier_canvas)

glacier_configure_target(glacier_canvas)
target_include_directories(glacier_canvas PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(glacier_canvas PUBLIC Qt6::Widgets)
