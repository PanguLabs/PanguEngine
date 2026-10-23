cmake_minimum_required(VERSION 4.0)

# Native host tool, not a Pangu Module or runtime library.
add_library(PanguAbiProfileCore STATIC
    Private/AbiProfile.cpp
    Private/ToolIo.cpp
)
set_target_properties(PanguAbiProfileCore PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Lib"
)
target_link_libraries(PanguAbiProfileCore PUBLIC Pangu::NlohmannJson Pangu::Foundation PRIVATE Pangu::BuildOptions)

add_executable(PanguAbiProfileTool
    Private/Main.cpp
)
target_link_libraries(PanguAbiProfileTool PRIVATE Pangu::BuildOptions PanguAbiProfileCore)
set_target_properties(PanguAbiProfileTool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
)
