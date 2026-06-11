# Internal utility library: logging, arena allocator, packet pack/unpack.
# Built as a PIC static library and linked privately into plctag2.

add_library(plctag2_utils STATIC
    debug.c
    arena.c
    bytes.c
    mutex.c
    clock.c
    thread.c
    random.c
    socket.c
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

if(WIN32)
    target_link_libraries(plctag2_utils PUBLIC ws2_32 bcrypt)
elseif(UNIX)
    target_link_libraries(plctag2_utils PUBLIC pthread)
endif()

plctag_set_compiler_options(plctag2_utils)
