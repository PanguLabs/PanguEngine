include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/PanguBuildTool.cmake")

function(pangu_discover_plugins)
    cmake_parse_arguments(PDP "" "ENGINE_ROOT;PROJECT_ROOT;OUTPUT_JSON" "" ${ARGN})
    if(PDP_UNPARSED_ARGUMENTS OR PDP_KEYWORDS_MISSING_VALUES OR
       NOT PDP_ENGINE_ROOT OR NOT PDP_OUTPUT_JSON)
        message(FATAL_ERROR "pangu_discover_plugins requires ENGINE_ROOT <dir> [PROJECT_ROOT <dir>] OUTPUT_JSON <variable>")
    endif()

    # 只追踪当前工具的固定源码输入；不遍历插件的源码文件。
    set(toolRoot "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../BuildTools/PanguBuildTool")
    set(thirdParty "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Source/ThirdParty/TomlPlusPlus")
    set(toolInputs
        "${toolRoot}/CMakeLists.txt"
        "${toolRoot}/Private/Main.cpp"
        "${toolRoot}/Private/PathEncoding.h"
        "${toolRoot}/Private/PluginDiscovery.cpp"
        "${toolRoot}/Private/PluginDiscovery.h"
        "${toolRoot}/Private/PluginJson.cpp"
        "${toolRoot}/Private/PluginJson.h"
        "${toolRoot}/Private/PluginPathInputs.cpp"
        "${toolRoot}/Private/PluginPathInputs.h"
        "${toolRoot}/Private/PluginManifest.cpp"
        "${toolRoot}/Private/PluginManifest.h"
        "${thirdParty}/CMakeLists.txt"
        "${thirdParty}/Repo/include/toml++/toml.h"
    )
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND toolInputs "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Platform/Linux/Toolchain.cmake")
    endif()
    # toml++ v3.4.0 的 header-only 实现文件为配置依赖；不建立自制时间戳索引。
    file(GLOB tomlHeaders LIST_DIRECTORIES false
        "${thirdParty}/Repo/include/toml++/*.hpp"
        "${thirdParty}/Repo/include/toml++/impl/*.hpp"
        "${thirdParty}/Repo/include/toml++/impl/*.inl"
    )
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${toolInputs} ${tomlHeaders})

    pangu_prepare_build_tool(tool)
    set(command "${tool}" discover --engine-root "${PDP_ENGINE_ROOT}")
    if(PDP_PROJECT_ROOT)
        list(APPEND command --project-root "${PDP_PROJECT_ROOT}")
    endif()
    execute_process(COMMAND ${command}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "PanguBuildTool plugin discovery failed (${status}):\n${error}")
    endif()
    if(NOT error STREQUAL "")
        message(FATAL_ERROR "PanguBuildTool wrote unexpected diagnostics on successful discovery:\n${error}")
    endif()

    string(JSON rootType ERROR_VARIABLE jsonError TYPE "${output}")
    if(jsonError OR NOT rootType STREQUAL "OBJECT")
        message(FATAL_ERROR "PanguBuildTool returned invalid JSON object: ${jsonError}")
    endif()
    string(JSON entriesType ERROR_VARIABLE jsonError TYPE "${output}" plugins)
    if(jsonError OR NOT entriesType STREQUAL "ARRAY")
        message(FATAL_ERROR "PanguBuildTool JSON is missing the plugins array: ${jsonError}")
    endif()
    string(JSON count LENGTH "${output}" plugins)
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(index RANGE 0 ${last})
            foreach(field IN ITEMS id name version source_dir manifest_path)
                string(JSON fieldType ERROR_VARIABLE jsonError TYPE "${output}" plugins ${index} ${field})
                if(jsonError OR NOT fieldType STREQUAL "STRING")
                    message(FATAL_ERROR "PanguBuildTool JSON plugins[${index}].${field} is not a string")
                endif()
            endforeach()
            string(JSON manifest GET "${output}" plugins ${index} manifest_path)
            string(JSON source GET "${output}" plugins ${index} source_dir)
            if(NOT IS_DIRECTORY "${source}" OR NOT EXISTS "${manifest}")
                message(FATAL_ERROR "PanguBuildTool JSON has an invalid source/manifest path: ${manifest}")
            endif()
            string(JSON inputsType ERROR_VARIABLE jsonError TYPE "${output}" plugins ${index} configure_inputs)
            if(jsonError OR NOT inputsType STREQUAL "ARRAY")
                message(FATAL_ERROR "PanguBuildTool JSON plugins[${index}].configure_inputs is not an array")
            endif()
            string(JSON inputCount LENGTH "${output}" plugins ${index} configure_inputs)
            if(inputCount LESS 2)
                message(FATAL_ERROR "PanguBuildTool JSON plugins[${index}] is missing required Configure inputs")
            endif()
            math(EXPR lastInput "${inputCount} - 1")
            foreach(inputIndex RANGE 0 ${lastInput})
                string(JSON inputType ERROR_VARIABLE jsonError TYPE "${output}" plugins ${index} configure_inputs ${inputIndex})
                if(jsonError OR NOT inputType STREQUAL "STRING")
                    message(FATAL_ERROR "PanguBuildTool Configure input is not a string")
                endif()
                string(JSON input GET "${output}" plugins ${index} configure_inputs ${inputIndex})
                if(NOT IS_ABSOLUTE "${input}" OR NOT EXISTS "${input}")
                    message(FATAL_ERROR "PanguBuildTool Configure input is invalid: ${input}")
                endif()
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
            endforeach()
        endforeach()
    endif()
    set("${PDP_OUTPUT_JSON}" "${output}" PARENT_SCOPE)
    message(STATUS "PanguBuildTool discovered ${count} source plugins")
endfunction()

function(pangu_add_enabled_plugins)
    cmake_parse_arguments(PAEP "" "ENGINE_ROOT;PROJECT_ROOT" "ENABLED" ${ARGN})
    if(PAEP_UNPARSED_ARGUMENTS OR NOT PAEP_ENGINE_ROOT)
        message(FATAL_ERROR "pangu_add_enabled_plugins requires ENGINE_ROOT <dir> [PROJECT_ROOT <dir>] [ENABLED <id>...]")
    endif()
    set(roots ENGINE_ROOT "${PAEP_ENGINE_ROOT}")
    if(PAEP_PROJECT_ROOT)
        list(APPEND roots PROJECT_ROOT "${PAEP_PROJECT_ROOT}")
    endif()
    pangu_discover_plugins(${roots} OUTPUT_JSON plugins)
    string(JSON count LENGTH "${plugins}" plugins)
    set(knownIds)
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(index RANGE 0 ${last})
            string(JSON id GET "${plugins}" plugins ${index} id)
            list(APPEND knownIds "${id}")
            string(JSON source GET "${plugins}" plugins ${index} source_dir)
            set("pluginSource_${id}" "${source}")
        endforeach()
    endif()
    set(enabled)
    foreach(id IN LISTS PAEP_ENABLED)
        if(id IN_LIST enabled)
            message(FATAL_ERROR "Duplicate enabled Plugin ID: ${id}")
        endif()
        if(NOT id IN_LIST knownIds)
            message(FATAL_ERROR "Unknown enabled Plugin ID: ${id}")
        endif()
        list(APPEND enabled "${id}")
    endforeach()
    list(SORT enabled)
    foreach(id IN LISTS enabled)
        add_subdirectory("${pluginSource_${id}}" "${CMAKE_BINARY_DIR}/Plugins/${id}" EXCLUDE_FROM_ALL)
    endforeach()
endfunction()
