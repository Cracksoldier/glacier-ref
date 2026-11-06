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
