# Internal utility library: logging, arena allocator, packet pack/unpack.
# Built as a PIC static library and linked privately into plctag2.

add_library(plctag2_utils STATIC
    debug.c
    arena.c
    bytes.c
)

target_include_directories(plctag2_utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src/include
)

set_target_properties(plctag2_utils PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
)

plctag_set_compiler_options(plctag2_utils)
