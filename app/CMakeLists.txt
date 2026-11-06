# Application library: window classes, testable without the executable.
add_library(glacier_app STATIC
    MainWindow.h
    MainWindow.cpp)
add_library(glacier::app ALIAS glacier_app)

glacier_configure_target(glacier_app)
target_include_directories(glacier_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(glacier_app
    PUBLIC Qt6::Widgets
    PRIVATE glacier::core glacier::canvas glacier::platform)

qt_add_executable(glacier-ref main.cpp)
glacier_configure_target(glacier-ref)
target_link_libraries(glacier-ref PRIVATE glacier::app glacier::core glacier::platform)

qt_add_resources(glacier-ref app_resources
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES "${PROJECT_SOURCE_DIR}/resources/icons/glacier-ref.svg")

set_target_properties(glacier-ref PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "Glacier Ref"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.cracksoldier.GlacierRef"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

include(GNUInstallDirs)
install(TARGETS glacier-ref
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(UNIX AND NOT APPLE)
    install(FILES "${PROJECT_SOURCE_DIR}/resources/linux/io.github.cracksoldier.GlacierRef.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES "${PROJECT_SOURCE_DIR}/resources/icons/glacier-ref.svg"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME io.github.cracksoldier.GlacierRef.svg)
endif()
