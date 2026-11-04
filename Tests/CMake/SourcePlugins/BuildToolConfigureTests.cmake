cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED PANGU_ENGINE_SOURCE_DIR OR NOT DEFINED PANGU_TEST_ROOT)
    message(FATAL_ERROR "SourcePluginBuildTool test requires Engine source and isolated test root")
endif()

file(TO_CMAKE_PATH "${PANGU_ENGINE_SOURCE_DIR}" engineSource)
set(source "${PANGU_TEST_ROOT}/BuildToolFixture")
set(binary "${PANGU_TEST_ROOT}/BuildToolBinary")
file(MAKE_DIRECTORY "${source}/Engine/Plugins")
set(content [=[
cmake_minimum_required(VERSION 4.0)
project(SourcePluginBuildToolSmoke LANGUAGES NONE)
include("@ENGINE_SOURCE@/cmake/PanguPlugins.cmake")
pangu_discover_plugins(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Engine" OUTPUT_JSON result)
string(JSON count LENGTH "${result}" plugins)
if(NOT count EQUAL 0)
    message(FATAL_ERROR "Expected no plugins, got: ${result}")
endif()
]=])
string(REPLACE "@ENGINE_SOURCE@" "${engineSource}" content "${content}")
file(WRITE "${source}/CMakeLists.txt" "${content}")

execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${binary}" -G Ninja
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Empty-root configure failed: ${status}\n${output}\n${error}")
endif()

if(WIN32)
    set(suffix ".exe")
else()
    set(suffix "")
endif()
if(NOT EXISTS "${binary}/BuildTools/PanguBuildTool/PanguBuildTool${suffix}")
    message(FATAL_ERROR "First configure must build native PanguBuildTool in isolated BuildRoot")
endif()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    file(READ "${binary}/BuildTools/PanguBuildTool/compile_commands.json" toolCommands)
    if(NOT toolCommands MATCHES "-nostdinc[+][+]")
        message(FATAL_ERROR "Linux host build tool must use the selected LLVM libc++ headers")
    endif()
    execute_process(COMMAND ldd "${binary}/BuildTools/PanguBuildTool/PanguBuildTool"
        RESULT_VARIABLE linkedStatus OUTPUT_VARIABLE linkedLibraries ERROR_VARIABLE linkedError)
    if(NOT linkedStatus EQUAL 0 OR NOT linkedLibraries MATCHES "libc[+][+]" OR
       linkedLibraries MATCHES "libstdc[+][+]")
        message(FATAL_ERROR "Linux host build tool must link libc++, not libstdc++: ${linkedLibraries}; ${linkedError}")
    endif()
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" --build "${binary}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 0 OR NOT output MATCHES "no work to do")
    message(FATAL_ERROR "No-change Ninja should do no work: ${status}\n${output}\n${error}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${binary}" -G Ninja
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Warm configure failed: ${status}\n${output}\n${error}")
endif()
message(STATUS "SourcePluginBuildTool: empty-root configure, build, discovery, and warm configure passed")
