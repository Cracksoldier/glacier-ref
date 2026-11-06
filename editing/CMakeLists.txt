add_library(glacier_editing STATIC
    include/glacier/editing/ClipboardFragment.h
    include/glacier/editing/CommandController.h
    include/glacier/editing/Geometry.h
    include/glacier/editing/NoteLayout.h
    include/glacier/editing/Operations.h
    src/ClipboardFragment.cpp
    src/CommandController.cpp
    src/Geometry.cpp
    src/NoteLayout.cpp
    src/Operations.cpp)
add_library(glacier::editing ALIAS glacier_editing)

glacier_configure_target(glacier_editing)
target_include_directories(glacier_editing PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(glacier_editing PUBLIC glacier::core glacier::io Qt6::Gui)
