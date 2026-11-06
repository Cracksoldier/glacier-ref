add_library(glacier_io STATIC
    include/glacier/io/ImageDecoder.h
    include/glacier/io/ImageFormats.h
    include/glacier/io/SvgValidator.h
    src/ExifOrientation.cpp
    src/ExifOrientation.h
    src/ImageDecoder.cpp
    src/ImageFormats.cpp
    src/SvgValidator.cpp)
add_library(glacier::io ALIAS glacier_io)

glacier_configure_target(glacier_io)
target_include_directories(glacier_io PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(glacier_io
    PUBLIC glacier::core Qt6::Gui
    PRIVATE Qt6::Svg)
