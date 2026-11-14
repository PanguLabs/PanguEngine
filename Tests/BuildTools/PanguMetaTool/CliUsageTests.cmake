cmake_minimum_required(VERSION 4.0)

if(PG_CLI_USAGE)
    # 退役命令不保留别名，避免旧术语继续进入实际构建调用。
    foreach(args IN ITEMS "" "--unknown" "--check-sdk" "generate;--request;request.json")
        execute_process(COMMAND "${PG_TOOL}" ${args} RESULT_VARIABLE result
            OUTPUT_VARIABLE output ERROR_VARIABLE error)
        if(NOT result EQUAL 2 OR NOT error MATCHES "Usage:" OR NOT output STREQUAL "")
            message(FATAL_ERROR "CLI usage must return 2 with stderr: ${result}: ${output}${error}")
        endif()
    endforeach()
    set(request "${PG_BINARY}/request.json")
    file(MAKE_DIRECTORY "${PG_BINARY}")
    file(WRITE "${request}" "{ malformed")
    execute_process(COMMAND "${PG_TOOL}" generate --request "${request}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 1 OR NOT error MATCHES "invalid_request" OR NOT output STREQUAL "")
        message(FATAL_ERROR "Invalid request must return 1 with stderr: ${result}: ${output}${error}")
    endif()
    set(content [=[{"version":2,"kind":"catalog","target":"Cli","owner":"engine","config":"Debug","engine_source":"@ROOT@","engine_build":"@ROOT@","project_source":"","project_build":"","output_root":"@ROOT@/Generated/PanguMeta/Debug/Cli","module":{},"headers":[],"ownership":[],"context":{},"catalog":[],"outputs":[]}]=])
    string(REPLACE "@ROOT@" "${PG_BINARY}" content "${content}")
    file(WRITE "${request}" "${content}")
    execute_process(COMMAND "${PG_TOOL}" generate --request "${request}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 1 OR NOT error MATCHES "invalid_request" OR NOT output STREQUAL "" OR
       EXISTS "${PG_BINARY}/Generated")
        message(FATAL_ERROR "Unsupported schema must fail accurately without outputs: ${result}: ${output}${error}")
    endif()
    return()
endif()
