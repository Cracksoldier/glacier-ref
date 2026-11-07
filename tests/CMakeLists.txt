glacier_add_test(tst_locale_harness
    SOURCES tst_locale_harness.cpp
    LIBS Qt6::Core)

glacier_add_test(tst_appinfo
    SOURCES tst_appinfo.cpp
    LIBS glacier::core)

glacier_add_test(tst_id
    SOURCES tst_id.cpp
    LIBS glacier::core)

glacier_add_test(tst_imageplugins
    SOURCES tst_imageplugins.cpp
    LIBS Qt6::Gui Qt6::Svg)

glacier_add_test(tst_zip
    SOURCES tst_zip.cpp
    LIBS Qt6::Core glacier::miniz)

glacier_add_test(tst_platforminfo
    SOURCES tst_platforminfo.cpp
    LIBS glacier::platform)

glacier_add_test(tst_mainwindow
    SOURCES tst_mainwindow.cpp
    LIBS glacier::app glacier::canvas glacier::editing)

glacier_add_test(tst_links
    SOURCES tst_links.cpp
    LIBS glacier::app glacier::canvas glacier::editing glacier::persistence)

glacier_add_test(tst_affine2d
    SOURCES tst_affine2d.cpp
    LIBS glacier::core)

glacier_add_test(tst_boarddocument
    SOURCES tst_boarddocument.cpp
    LIBS glacier::core)

glacier_add_test(tst_manifest
    SOURCES tst_manifest.cpp
    LIBS glacier::persistence)

glacier_add_test(tst_boardserializer
    SOURCES tst_boardserializer.cpp
    LIBS glacier::persistence)

# Writes the compatibility fixtures (tests/fixtures/boards); not run by CTest.
qt_add_executable(glacier_test_boardfixtures helpers/boardfixtures.cpp)
glacier_configure_target(glacier_test_boardfixtures)
target_include_directories(glacier_test_boardfixtures PRIVATE "${PROJECT_SOURCE_DIR}/tests/common")
target_link_libraries(glacier_test_boardfixtures PRIVATE glacier::persistence Qt6::Gui)
set_target_properties(glacier_test_boardfixtures PROPERTIES MACOSX_BUNDLE OFF WIN32_EXECUTABLE OFF)

glacier_add_test(tst_compatibility
    SOURCES tst_compatibility.cpp
    LIBS glacier::persistence)
target_compile_definitions(tst_compatibility PRIVATE
    GLACIER_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/boards")

# Saves a board and stalls halfway, to be killed (tst_atomicsave).
qt_add_executable(glacier_test_slowsaver helpers/slowsaver.cpp)
glacier_configure_target(glacier_test_slowsaver)
target_include_directories(glacier_test_slowsaver PRIVATE "${PROJECT_SOURCE_DIR}/tests/common")
target_link_libraries(glacier_test_slowsaver PRIVATE glacier::persistence Qt6::Gui)
set_target_properties(glacier_test_slowsaver PROPERTIES MACOSX_BUNDLE OFF WIN32_EXECUTABLE OFF)

glacier_add_test(tst_atomicsave
    SOURCES tst_atomicsave.cpp
    LIBS glacier::persistence)
target_compile_definitions(tst_atomicsave PRIVATE
    GLACIER_SLOWSAVER_PATH="$<TARGET_FILE:glacier_test_slowsaver>")
add_dependencies(tst_atomicsave glacier_test_slowsaver)

glacier_add_test(tst_documentlock
    SOURCES tst_documentlock.cpp
    LIBS glacier::persistence)
target_compile_definitions(tst_documentlock PRIVATE
    GLACIER_LOCKHOLDER_PATH="$<TARGET_FILE:glacier_test_lockholder>")
add_dependencies(tst_documentlock glacier_test_lockholder)

# Writes recovery snapshots and waits to be killed (tst_recovery).
qt_add_executable(glacier_test_recoverywriter helpers/recoverywriter.cpp)
glacier_configure_target(glacier_test_recoverywriter)
target_include_directories(glacier_test_recoverywriter PRIVATE "${PROJECT_SOURCE_DIR}/tests/common")
target_link_libraries(glacier_test_recoverywriter PRIVATE glacier::persistence Qt6::Gui)
set_target_properties(glacier_test_recoverywriter PROPERTIES MACOSX_BUNDLE OFF WIN32_EXECUTABLE OFF)

glacier_add_test(tst_recovery
    SOURCES tst_recovery.cpp
    LIBS glacier::persistence)
target_compile_definitions(tst_recovery PRIVATE
    GLACIER_RECOVERYWRITER_PATH="$<TARGET_FILE:glacier_test_recoverywriter>")
add_dependencies(tst_recovery glacier_test_recoverywriter)

glacier_add_test(tst_linkpaths
    SOURCES tst_linkpaths.cpp
    LIBS glacier::core)

qt_add_executable(glacier_test_lockholder helpers/lockholder.cpp)
target_link_libraries(glacier_test_lockholder PRIVATE Qt6::Core)
set_target_properties(glacier_test_lockholder PROPERTIES MACOSX_BUNDLE OFF WIN32_EXECUTABLE OFF)

glacier_add_test(tst_sessionassetstore
    SOURCES tst_sessionassetstore.cpp
    LIBS glacier::core Qt6::Concurrent)
target_compile_definitions(tst_sessionassetstore PRIVATE
    GLACIER_LOCKHOLDER_PATH="$<TARGET_FILE:glacier_test_lockholder>")
add_dependencies(tst_sessionassetstore glacier_test_lockholder)

glacier_add_test(tst_imageprobe
    SOURCES tst_imageprobe.cpp
    LIBS glacier::io)

glacier_add_test(tst_svgvalidator
    SOURCES tst_svgvalidator.cpp
    LIBS glacier::io)

glacier_add_test(tst_batchlayout
    SOURCES tst_batchlayout.cpp
    LIBS glacier::core)

glacier_add_test(tst_mimerouter
    SOURCES tst_mimerouter.cpp
    LIBS glacier::io)

glacier_add_test(tst_importservice
    SOURCES tst_importservice.cpp
    LIBS glacier::io)

glacier_add_test(tst_imagecache
    SOURCES tst_imagecache.cpp
    LIBS glacier::canvas glacier::core)

glacier_add_test(tst_boardview
    SOURCES tst_boardview.cpp
    LIBS glacier::canvas glacier::editing glacier::core)

glacier_add_test(tst_documentchange
    SOURCES tst_documentchange.cpp
    LIBS glacier::core)

glacier_add_test(tst_operations
    SOURCES tst_operations.cpp
    LIBS glacier::editing)

glacier_add_test(tst_commandcontroller
    SOURCES tst_commandcontroller.cpp
    LIBS glacier::editing)

glacier_add_test(tst_clipboardfragment
    SOURCES tst_clipboardfragment.cpp
    LIBS glacier::editing)
