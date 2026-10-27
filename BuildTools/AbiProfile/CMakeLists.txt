cmake_minimum_required(VERSION 4.0)

# Native build tool, not a Pangu Module or runtime library.
add_library(PanguAbiProfile STATIC
    Private/AbiProfile.cpp
    Private/CollectionRequest.cpp
    Private/Collection.cpp
    Private/CollectionState.cpp
    Private/CompileCommands.cpp
    Private/HeaderTrace.cpp
    Private/ProbeProcess.cpp
    Private/Observation.cpp
    Private/Encoding.cpp
    Private/ToolIo.cpp
    Private/WindowsProfile.cpp
    Private/WindowsCollector.cpp
    Private/LinuxCollector.cpp
    Private/AppleCollector.cpp
    Private/AndroidCollector.cpp
    Private/DependencyGraph.cpp
    Private/TechnicalClosure.cpp
)
set_target_properties(PanguAbiProfile PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Lib"
)
target_link_libraries(PanguAbiProfile PUBLIC ThirdParty::NlohmannJson Pangu::Foundation PRIVATE PanguBuildOptions)
if(WIN32)
    target_sources(PanguAbiProfile PRIVATE Private/ProcessWindows.cpp)
else()
    target_sources(PanguAbiProfile PRIVATE Private/ProcessPosix.cpp)
endif()

add_executable(PanguAbiProfileTool
    Private/Main.cpp
)
target_link_libraries(PanguAbiProfileTool PRIVATE PanguBuildOptions PanguAbiProfile)
set_target_properties(PanguAbiProfileTool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
)
