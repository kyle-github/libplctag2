find_program(CARGO_EXECUTABLE cargo REQUIRED)

add_custom_target(plctag2_rust ALL
    COMMAND ${CARGO_EXECUTABLE} build --release
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building Rust wrapper"
    DEPENDS plctag2
)
