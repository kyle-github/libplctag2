# The .NET wrapper uses P/Invoke and does not need a native shim —
# it links directly against the plctag2 shared library.
# This directory holds the C# source; build it with dotnet build.
#
# The CMakeLists.txt here exists only to install the shared library
# alongside the NuGet package output.
install(TARGETS plctag2
    RUNTIME DESTINATION dotnet/runtimes/$<LOWER_CASE:${CMAKE_SYSTEM_NAME}>-x64/native
    LIBRARY DESTINATION dotnet/runtimes/$<LOWER_CASE:${CMAKE_SYSTEM_NAME}>-x64/native
)
