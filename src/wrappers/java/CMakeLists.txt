find_package(JNI REQUIRED)

add_library(plctag2_jni SHARED
    # plctag2_jni.c
)

target_include_directories(plctag2_jni PRIVATE ${JNI_INCLUDE_DIRS})
target_link_libraries(plctag2_jni PRIVATE plctag2::plctag2 ${JNI_LIBRARIES})
plctag_set_compiler_options(plctag2_jni)
