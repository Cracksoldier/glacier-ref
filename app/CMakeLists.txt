# Application library: window classes, testable without the executable.
add_library(glacier_app STATIC
    AppServices.h
    AppServices.cpp
    DiagnosticLog.h
    DiagnosticLog.cpp
    FramelessController.h
    FramelessController.cpp
    HelpDialog.h
    HelpDialog.cpp
    MainWindow.h
    MainWindow.cpp
    NoteStyleDialog.h
    NoteStyleDialog.cpp
    PreferencesDialog.h
    PreferencesDialog.cpp
    RecoveryDialog.h
    RecoveryDialog.cpp
    SettingsService.h
    SettingsService.cpp
    Theme.h
    Theme.cpp)
add_library(glacier::app ALIAS glacier_app)

glacier_configure_target(glacier_app)
target_include_directories(glacier_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# Offline help content (M4.01), in the library so tests see it too.
qt_add_resources(glacier_app app_help
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES "${PROJECT_SOURCE_DIR}/resources/help/basics.html")
target_link_libraries(glacier_app
    PUBLIC glacier::core glacier::io glacier::editing glacier::canvas glacier::persistence Qt6::Widgets Qt6::Concurrent
    PUBLIC glacier::platform)

qt_add_executable(glacier-ref main.cpp)
glacier_configure_target(glacier-ref)
target_link_libraries(glacier-ref PRIVATE glacier::app glacier::core glacier::io glacier::platform)

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
# Windows packages keep the executable at the top of the folder, next to the
# Qt runtime that windeployqt stages (packaging/windows/stage.ps1).
if(WIN32)
    set(GLACIER_INSTALL_BINDIR .)
else()
    set(GLACIER_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
endif()
install(TARGETS glacier-ref
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${GLACIER_INSTALL_BINDIR})

if(UNIX AND NOT APPLE)
    install(FILES "${PROJECT_SOURCE_DIR}/resources/linux/io.github.cracksoldier.GlacierRef.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES "${PROJECT_SOURCE_DIR}/resources/icons/glacier-ref.svg"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME io.github.cracksoldier.GlacierRef.svg)
endif()
