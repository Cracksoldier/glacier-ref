qt_add_executable(glacier-bench main.cpp)
glacier_configure_target(glacier-bench)
target_link_libraries(glacier-bench PRIVATE glacier::app glacier::canvas glacier::io glacier::platform Qt6::Widgets)
set_target_properties(glacier-bench PROPERTIES MACOSX_BUNDLE OFF WIN32_EXECUTABLE OFF)
