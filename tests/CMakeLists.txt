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
    LIBS glacier::app glacier::canvas)

glacier_add_test(tst_affine2d
    SOURCES tst_affine2d.cpp
    LIBS glacier::core)

glacier_add_test(tst_boarddocument
    SOURCES tst_boarddocument.cpp
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
