# Qt Test helpers.
#
# glacier_add_test(<name> SOURCES <files...> [LIBS <targets...>])
#
# Builds a Qt Test executable and registers two CTest entries:
#   <name>        default locale
#   <name>.de_AT  GLACIER_TEST_LOCALE=de_AT (comma decimal separator), to catch
#                 locale-dependent formatting or parsing
# Both run with QT_QPA_PLATFORM=offscreen.

include_guard(GLOBAL)

function(glacier_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "glacier_add_test(${name}): SOURCES is required")
    endif()

    qt_add_executable(${name} ${ARG_SOURCES})
    glacier_configure_target(${name})
    target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}/tests/common")
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    set_target_properties(${name} PROPERTIES
        MACOSX_BUNDLE OFF
        WIN32_EXECUTABLE OFF)

    set(_env "QT_QPA_PLATFORM=offscreen")

    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${_env}")

    add_test(NAME ${name}.de_AT COMMAND ${name})
    set_tests_properties(${name}.de_AT PROPERTIES
        ENVIRONMENT "${_env};GLACIER_TEST_LOCALE=de_AT")
endfunction()
