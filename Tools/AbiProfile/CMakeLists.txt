cmake_minimum_required(VERSION 4.0)

# Native host tool, not a Pangu Module or runtime library.
add_library(PanguAbiProfileCore STATIC
    Private/AbiProfile.cpp
    Private/ToolIo.cpp
)
set_target_properties(PanguAbiProfileCore PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Lib"
)
target_link_libraries(PanguAbiProfileCore PUBLIC ThirdParty::NlohmannJson Pangu::Foundation PRIVATE PanguBuildOptions)

add_executable(PanguAbiProfileTool
    Private/Main.cpp
)
target_link_libraries(PanguAbiProfileTool PRIVATE PanguBuildOptions PanguAbiProfileCore)
set_target_properties(PanguAbiProfileTool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
)
