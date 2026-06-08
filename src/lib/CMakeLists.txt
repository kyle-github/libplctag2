file(GLOB_RECURSE PLCTAG_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_library(plctag2 ${PLCTAG_SOURCES})
add_library(plctag2::plctag2 ALIAS plctag2)

target_include_directories(plctag2
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(plctag2 PRIVATE PLCTAG_EXPORT)
else()
    target_compile_definitions(plctag2 PUBLIC PLCTAG_STATIC)
endif()

target_link_libraries(plctag2 PRIVATE plctag2_utils)

if(WIN32)
    target_link_libraries(plctag2 PRIVATE ws2_32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(plctag2 PRIVATE pthread)
endif()

plctag_set_compiler_options(plctag2)

set_target_properties(plctag2 PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    C_VISIBILITY_PRESET hidden
)
