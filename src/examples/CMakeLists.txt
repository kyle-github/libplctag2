function(add_example name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE plctag2::plctag2)
    plctag_set_compiler_options(${name})
endfunction()

# add_example(simple_read)
# add_example(simple_write)
# add_example(async_read)
