find_program(GO_EXECUTABLE go REQUIRED)

add_custom_target(plctag2_go ALL
    COMMAND ${GO_EXECUTABLE} build ./...
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building Go wrapper"
    DEPENDS plctag2
)
