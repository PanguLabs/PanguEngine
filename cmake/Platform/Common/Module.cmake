include_guard(GLOBAL)

# Apply build policy to one actual STATIC or SHARED CMake Library Target.
# This is deliberately not a replacement for add_library().
function(pangu_configure_module targetName)
    if(NOT TARGET "${targetName}")
        message(FATAL_ERROR "pangu_configure_module: unknown target '${targetName}'.")
    endif()

    get_target_property(targetType "${targetName}" TYPE)
    if(NOT targetType STREQUAL "STATIC_LIBRARY" AND NOT targetType STREQUAL "SHARED_LIBRARY")
        message(FATAL_ERROR "Pangu Module '${targetName}' must be STATIC or SHARED.")
    endif()

    target_link_libraries("${targetName}" PUBLIC Pangu::BuildOptions)
    set_target_properties("${targetName}" PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Lib"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
    )
endfunction()
