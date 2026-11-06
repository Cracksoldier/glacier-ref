qt_add_executable(glacier-fixturegen main.cpp)
glacier_configure_target(glacier-fixturegen)
target_link_libraries(glacier-fixturegen PRIVATE Qt6::Gui Qt6::Svg Qt6::Concurrent)
set_target_properties(glacier-fixturegen PROPERTIES MACOSX_BUNDLE OFF WIN32_EXECUTABLE OFF)
