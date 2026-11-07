add_library(glacier_persistence STATIC
    include/glacier/persistence/BoardSerializer.h
    include/glacier/persistence/DocumentLock.h
    include/glacier/persistence/Manifest.h
    include/glacier/persistence/ZipArchive.h
    src/BoardSerializer.cpp
    src/DocumentLock.cpp
    src/Manifest.cpp
    src/ZipArchive.cpp)
add_library(glacier::persistence ALIAS glacier_persistence)

glacier_configure_target(glacier_persistence)
target_include_directories(glacier_persistence PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(glacier_persistence
    PUBLIC glacier::core glacier::io Qt6::Core
    PRIVATE glacier::miniz)
