cmake_minimum_required(VERSION 3.15)

project(plctag2
    VERSION 2.0.0
    DESCRIPTION "Communication library for network PLCs"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(PLCTAG_BUILD_TESTS    "Build tests"             ON)
option(PLCTAG_BUILD_TOOLS    "Build tools"             ON)
option(PLCTAG_BUILD_EXAMPLES "Build examples"          ON)
option(PLCTAG_BUILD_WRAPPERS "Build language wrappers" OFF)
option(BUILD_SHARED_LIBS     "Build shared library"    ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(cmake/CompilerOptions.cmake)

add_subdirectory(src/utils)
add_subdirectory(src/lib)

if(PLCTAG_BUILD_TOOLS)
    add_subdirectory(src/tools)
endif()

if(PLCTAG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(src/tests)
endif()

if(PLCTAG_BUILD_EXAMPLES)
    add_subdirectory(src/examples)
endif()

if(PLCTAG_BUILD_WRAPPERS)
    add_subdirectory(src/wrappers)
endif()

install(TARGETS plctag2
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY src/include/ DESTINATION include)
