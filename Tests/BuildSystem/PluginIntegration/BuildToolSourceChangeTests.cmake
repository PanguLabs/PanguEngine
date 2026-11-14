cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED PANGU_ENGINE_SOURCE_DIR OR NOT DEFINED PANGU_TEST_ROOT)
    message(FATAL_ERROR "SourcePluginToolSourceChange requires Engine source and isolated test root")
endif()
set(mirror "${PANGU_TEST_ROOT}/MirroredTool")
set(binary "${PANGU_TEST_ROOT}/MirroredToolBuild")
file(MAKE_DIRECTORY "${mirror}/cmake" "${mirror}/BuildTools" "${mirror}/Source/ThirdParty/TomlPlusPlus/Repo")
file(COPY "${PANGU_ENGINE_SOURCE_DIR}/cmake/PanguPlugins.cmake"
          "${PANGU_ENGINE_SOURCE_DIR}/cmake/PanguBuildTool.cmake"
     DESTINATION "${mirror}/cmake")
file(COPY "${PANGU_ENGINE_SOURCE_DIR}/BuildTools/PanguBuildTool"
     DESTINATION "${mirror}/BuildTools")
file(COPY "${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/TomlPlusPlus/CMakeLists.txt"
     DESTINATION "${mirror}/Source/ThirdParty/TomlPlusPlus")
file(COPY "${PANGU_ENGINE_SOURCE_DIR}/Source/ThirdParty/TomlPlusPlus/Repo/include"
     DESTINATION "${mirror}/Source/ThirdParty/TomlPlusPlus/Repo")
file(MAKE_DIRECTORY "${mirror}/Engine")
set(sourceFile "${mirror}/BuildTools/PanguBuildTool/Private/PluginJson.cpp")
file(READ "${PANGU_ENGINE_SOURCE_DIR}/BuildTools/PanguBuildTool/Private/PluginJson.cpp" original)
file(WRITE "${sourceFile}" "${original}")
file(WRITE "${mirror}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 4.0)
project(PanguSourceToolChange LANGUAGES NONE)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PanguPlugins.cmake")
pangu_discover_plugins(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Engine" OUTPUT_JSON json)
]=])

execute_process(COMMAND "${CMAKE_COMMAND}" -S "${mirror}" -B "${binary}" -G Ninja
    RESULT_VARIABLE rc OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "Mirrored tool initial configure failed: ${rc}\n${output}\n${error}")
endif()
set(tool "${binary}/BuildTools/PanguBuildTool/PanguBuildTool.exe")
if(NOT EXISTS "${tool}")
    message(FATAL_ERROR "Mirrored PanguBuildTool binary missing")
endif()
file(TIMESTAMP "${tool}" oldTime "%s")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
file(APPEND "${sourceFile}" "\n// 仅用于隔离构建输入失效验证\n")
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${binary}" --parallel "${PANGU_TEST_BUILD_JOBS}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT rc EQUAL 0 OR NOT output MATCHES "Re-running CMake")
    message(FATAL_ERROR "Changed tool source must trigger reconfigure: ${rc}\n${output}\n${error}")
endif()
file(TIMESTAMP "${tool}" newTime "%s")
if(NOT newTime GREATER oldTime)
    message(FATAL_ERROR "Changed tool source must rebuild executable (${oldTime} vs ${newTime})")
endif()

file(APPEND "${sourceFile}" "\n#error intentional_host_tool_build_failure\n")
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${binary}" --parallel "${PANGU_TEST_BUILD_JOBS}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(rc EQUAL 0 OR NOT "${output}\n${error}" MATCHES "PanguBuildTool host build failed")
    message(FATAL_ERROR "Failed host tool must block configure: ${rc}\n${output}\n${error}")
endif()
file(WRITE "${sourceFile}" "${original}")
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${binary}" --parallel "${PANGU_TEST_BUILD_JOBS}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "Host tool recovery failed: ${rc}\n${output}\n${error}")
endif()
message(STATUS "SourcePluginBuildTool: tool source invalidation and compile-failure propagation passed")
