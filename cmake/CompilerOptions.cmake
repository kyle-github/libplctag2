# Compiler detection helpers
if(WIN32)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU")
        set(PLCTAG_COMPILER_MINGW_GCC TRUE)
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(PLCTAG_COMPILER_MINGW_CLANG TRUE)
    endif()
endif()

function(plctag_set_compiler_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /WX
            /wd4996   # disable CRT deprecation warnings
        )
        target_compile_definitions(${target} PRIVATE
            _CRT_SECURE_NO_WARNINGS
            WIN32_LEAN_AND_MEAN
            NOMINMAX
        )
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
            -Werror
            -Wno-unused-parameter
        )
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            target_compile_options(${target} PRIVATE -Wno-gnu-zero-variadic-macro-arguments)
        endif()
    endif()

    if(WIN32)
        target_compile_definitions(${target} PRIVATE _WIN32_WINNT=0x0600)
    endif()
endfunction()
