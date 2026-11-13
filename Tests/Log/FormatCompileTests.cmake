cmake_minimum_required(VERSION 4.0)
include("${FORMAT_SETTINGS}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${FORMAT_SOURCE}" -B "${FORMAT_BINARY}" -G Ninja
    "-DCMAKE_CXX_COMPILER=${FORMAT_COMPILER}" "-DCMAKE_CXX_FLAGS=${FORMAT_FLAGS}"
    "-DCMAKE_OSX_SYSROOT=${FORMAT_SYSROOT}" "-DCMAKE_OSX_ARCHITECTURES=${FORMAT_ARCH}"
    "-DPANGU_FORMAT_INCLUDES=${FORMAT_INCLUDES}" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "格式夹具配置失败：${output}\n${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${FORMAT_BINARY}" --target Positive
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Positive 未先通过：${output}\n${error}")
endif()
foreach(name IN ITEMS UnsupportedFormatter RuntimeFormat RuntimeFormatObject RuntimeText RawPointer LongDouble)
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${FORMAT_BINARY}" --target "${name}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result STREQUAL "0" OR NOT "${output}${error}" MATCHES "FormatRecord")
        message(FATAL_ERROR "${name} 未由前端编译拒绝：exit=${result}\n${output}\n${error}")
    endif()
endforeach()
