# Shared compiler settings for all Glacier Ref targets.
#
# Usage: glacier_configure_target(<target>)

include_guard(GLOBAL)

option(GLACIER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(GLACIER_SANITIZE "" CACHE STRING
    "Semicolon-separated sanitizers for GCC/Clang, e.g. address;undefined")

if(GLACIER_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "GLACIER_SANITIZE is only supported with GCC/Clang")
    endif()
    list(JOIN GLACIER_SANITIZE "," _glacier_sanitizers)
    add_compile_options(-fsanitize=${_glacier_sanitizers} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=${_glacier_sanitizers})
endif()

function(glacier_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
        if(GLACIER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast)
        if(GLACIER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()

    # Stricter Qt usage: explicit string encodings (Unicode paths), no implicit
    # QUrl-from-string, no deprecated APIs up to the minimum supported Qt.
    target_compile_definitions(${target} PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_URL_CAST_FROM_STRING
        QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
        QT_NO_FOREACH
        QT_DISABLE_DEPRECATED_UP_TO=0x060C00)
endfunction()
