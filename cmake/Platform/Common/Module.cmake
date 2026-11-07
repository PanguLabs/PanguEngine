include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../../PanguLinkMode.cmake")

# Apply build policy to one actual STATIC or SHARED CMake Library Target.
# This is deliberately not a replacement for add_library().
function(pangu_configure_module targetName)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "EXPORT_MACRO" "")
    if(NOT TARGET "${targetName}")
        message(FATAL_ERROR "pangu_configure_module: unknown target '${targetName}'.")
    endif()

    get_target_property(targetType "${targetName}" TYPE)
    get_target_property(isImported "${targetName}" IMPORTED)
    get_target_property(isAlias "${targetName}" ALIASED_TARGET)
    if(isImported OR isAlias)
        message(FATAL_ERROR "Pangu Module '${targetName}' must be an actual local Library Target.")
    endif()
    if(NOT targetType STREQUAL "STATIC_LIBRARY" AND NOT targetType STREQUAL "SHARED_LIBRARY")
        message(FATAL_ERROR "Pangu Module '${targetName}' must be STATIC or SHARED.")
    endif()

    pangu_module_library_type(expectedType)
    if(NOT targetType STREQUAL "${expectedType}_LIBRARY")
        message(FATAL_ERROR "does not match PANGU_LINK_MODE: '${targetName}' is ${targetType}, mode is ${PANGU_LINK_MODE}.")
    endif()
    if(NOT ARG_EXPORT_MACRO)
        string(TOUPPER "${targetName}_API" ARG_EXPORT_MACRO)
    endif()
    if(NOT ARG_EXPORT_MACRO MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR "Invalid EXPORT_MACRO '${ARG_EXPORT_MACRO}'.")
    endif()
    set(exportDirectory "${CMAKE_BINARY_DIR}/Generated")
    file(MAKE_DIRECTORY "${exportDirectory}")
    set(exportHeader "#pragma once\n\n")
    if(expectedType STREQUAL "SHARED")
        string(APPEND exportHeader "#ifdef _WIN32\n#  ifdef ${targetName}_EXPORTS\n#    define ${ARG_EXPORT_MACRO} __declspec(dllexport)\n#  else\n#    define ${ARG_EXPORT_MACRO} __declspec(dllimport)\n#  endif\n#else\n#  define ${ARG_EXPORT_MACRO} __attribute__((visibility(\"default\")))\n#endif\n")
    else()
        string(APPEND exportHeader "#define ${ARG_EXPORT_MACRO}\n")
    endif()
    file(CONFIGURE OUTPUT "${exportDirectory}/${targetName}Export.h" CONTENT "${exportHeader}" @ONLY)
    target_include_directories("${targetName}" PUBLIC "$<BUILD_INTERFACE:${exportDirectory}>")
    set_property(TARGET "${targetName}" PROPERTY PANGU_EXPORT_MACRO "${ARG_EXPORT_MACRO}")

    # Warnings and no-exception policy apply to this Module, not to its consumers.
    target_link_libraries("${targetName}" PRIVATE PanguBuildOptions)
    set_target_properties("${targetName}" PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
        DEFINE_SYMBOL "${targetName}_EXPORTS"
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Lib"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
    )
endfunction()

function(pangu_export_module_query)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "TARGET;QUERY;HEADER" "")
    if(NOT ARG_TARGET OR NOT ARG_QUERY OR NOT ARG_HEADER OR ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "pangu_export_module_query requires TARGET, QUERY and HEADER.")
    endif()
    get_target_property(exportMacro "${ARG_TARGET}" PANGU_EXPORT_MACRO)
    if(NOT exportMacro)
        message(FATAL_ERROR "Configure Module '${ARG_TARGET}' before exporting its Query.")
    endif()
    pangu_module_library_type(moduleType)
    if(moduleType STREQUAL "STATIC")
        return()
    endif()
    get_target_property(hasQuery "${ARG_TARGET}" PANGU_HAS_QUERY)
    if(hasQuery)
        message(FATAL_ERROR "Module '${ARG_TARGET}' already has a native Query wrapper.")
    endif()
    set_property(TARGET "${ARG_TARGET}" PROPERTY PANGU_HAS_QUERY ON)
    set(querySource "${CMAKE_CURRENT_BINARY_DIR}/Generated/${ARG_TARGET}/ModuleQuery.cpp")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../ModuleQuery.cpp.in" "${querySource}" @ONLY)
    target_sources("${ARG_TARGET}" PRIVATE "${querySource}")
endfunction()
