find_package(Python3 REQUIRED COMPONENTS Interpreter)

# The Python wrapper uses ctypes and ships as a pure-Python package.
# Running setup.py / pip install is left to the packaging step.
add_custom_target(plctag2_python ALL
    COMMAND ${Python3_EXECUTABLE} -m py_compile ${CMAKE_CURRENT_SOURCE_DIR}/plctag2/__init__.py
    COMMENT "Syntax-checking Python wrapper"
    DEPENDS plctag2
)
