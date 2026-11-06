# Vendored miniz 3.1.2 (MIT). See docs/DEPENDENCIES.md for provenance.
#
# Configuration:
#   MINIZ_NO_STDIO                   archives are read/written only through
#                                    callbacks (QIODevice/QSaveFile); no path-based I/O
#   MINIZ_NO_ZLIB_COMPATIBLE_NAMES   avoid zlib-named macros colliding with
#                                    Qt's or the system's zlib

add_library(glacier_miniz STATIC miniz.c miniz.h)
add_library(glacier::miniz ALIAS glacier_miniz)

target_include_directories(glacier_miniz SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(glacier_miniz PUBLIC
    MINIZ_NO_STDIO
    MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
set_target_properties(glacier_miniz PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON)

# Third-party code: do not apply project warnings.
if(MSVC)
    target_compile_options(glacier_miniz PRIVATE /W0)
else()
    target_compile_options(glacier_miniz PRIVATE -w)
endif()
