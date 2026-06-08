option(PLCTAG_WRAPPER_JAVA   "Build Java/JNI wrapper"  OFF)
option(PLCTAG_WRAPPER_DOTNET "Build .NET/P-Invoke wrapper" OFF)
option(PLCTAG_WRAPPER_GO     "Build Go/cgo wrapper"    OFF)
option(PLCTAG_WRAPPER_PYTHON "Build Python wrapper"    OFF)
option(PLCTAG_WRAPPER_RUST   "Build Rust wrapper"      OFF)

if(PLCTAG_WRAPPER_JAVA)
    add_subdirectory(java)
endif()

if(PLCTAG_WRAPPER_DOTNET)
    add_subdirectory(dotnet)
endif()

if(PLCTAG_WRAPPER_GO)
    add_subdirectory(go)
endif()

if(PLCTAG_WRAPPER_PYTHON)
    add_subdirectory(python)
endif()

if(PLCTAG_WRAPPER_RUST)
    add_subdirectory(rust)
endif()
