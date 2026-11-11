cmake_minimum_required(VERSION 4.0)

if(PG_CLI_USAGE)
    foreach(args IN ITEMS "" "--unknown" "generate;--request;request.json")
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

if(PG_PREPARE_GUARD)
    find_package(Git REQUIRED)
    string(RANDOM LENGTH 10 ALPHABET 0123456789abcdef nonce)
    set(fixture "${PG_BINARY}/Prepare-${nonce}")
    file(MAKE_DIRECTORY "${fixture}/Source/llvm" "${fixture}/Source/cmake/Modules" "${fixture}/Sdk")
    file(WRITE "${fixture}/Source/llvm/CMakeLists.txt"
        "cmake_minimum_required(VERSION 4.0)\nproject(SdkFixture LANGUAGES NONE)\nmessage(FATAL_ERROR unexpected_build)\n")
    file(WRITE "${fixture}/Source/cmake/Modules/LLVMVersion.cmake" "set(LLVM_VERSION_MAJOR 23)\n")
    file(WRITE "${fixture}/Sdk/keep.txt" "existing installation\n")
    execute_process(COMMAND "${GIT_EXECUTABLE}" init "${fixture}/Source" COMMAND_ERROR_IS_FATAL ANY
        OUTPUT_QUIET ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${fixture}/Source" add . COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${fixture}/Source"
        -c user.name=SdkFixture -c user.email=sdk-fixture@example.invalid
        -c commit.gpgsign=false "-c" "core.hooksPath=${fixture}/NoHooks" commit -m "测试：SDK 准备夹具"
        COMMAND_ERROR_IS_FATAL ANY OUTPUT_QUIET ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${fixture}/Source" rev-parse HEAD
        OUTPUT_VARIABLE commit OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DPG_LLVM_SOURCE_DIR=${fixture}/Source"
        "-DPG_LLVM_SOURCE_COMMIT=${commit}" "-DPG_SDK_BUILD_ROOT=${fixture}/Sdk"
        "-DPG_HOST_COMPILER=${PG_COMPILER}" "-DPG_SDK_CONFIG_FILE=${fixture}/PanguMetaSdk.cmake"
        -P "${PG_SOURCE}/Sdk/Prepare.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    file(READ "${fixture}/Sdk/keep.txt" kept)
    if(result EQUAL 0 OR NOT "${output}\n${error}" MATCHES "refusing a non-empty SDK build root" OR
       NOT kept STREQUAL "existing installation\n" OR EXISTS "${fixture}/Sdk/PanguMetaSdkSource.txt")
        message(FATAL_ERROR "Prepare must reject occupied roots before writing:\n${output}\n${error}")
    endif()
    return()
endif()

# 每个负例使用独立子构建树，不能把缺失 SDK 误认作预期拒绝。
foreach(required IN ITEMS PG_SOURCE PG_BINARY PG_COMPILER PG_LLVM_DIR PG_CLANG_DIR PG_RESOURCE_DIR PG_CASE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing test prerequisite: ${required}")
    endif()
endforeach()
if(NOT EXISTS "${PG_LLVM_DIR}/LLVMConfig.cmake" OR
   NOT EXISTS "${PG_CLANG_DIR}/ClangConfig.cmake" OR
   NOT EXISTS "${PG_RESOURCE_DIR}/include/stddef.h")
    message(FATAL_ERROR "Missing SDK test prerequisite")
endif()

set(args)
if(PG_DEPENDENCY_PREFIX)
    list(APPEND args "-DCMAKE_PREFIX_PATH:PATH=${PG_DEPENDENCY_PREFIX}")
endif()
if(PG_CASE STREQUAL "RejectsNoRtti")
    set(expected sdk_rtti_required)
    if(PG_WRAP_NO_RTTI)
        set(rtti OFF)
        if(PG_FORCE_RTTI_ON)
            set(rtti ON)
        endif()
        file(MAKE_DIRECTORY "${PG_BINARY}/Sdk")
        file(WRITE "${PG_BINARY}/Sdk/LLVMConfig.cmake"
            "include([==[${PG_LLVM_DIR}/LLVMConfig.cmake]==])\nset(LLVM_ENABLE_RTTI ${rtti})\n")
        configure_file("${PG_LLVM_DIR}/LLVMConfigVersion.cmake"
            "${PG_BINARY}/Sdk/LLVMConfigVersion.cmake" COPYONLY)
        set(PG_LLVM_DIR "${PG_BINARY}/Sdk")
    endif()
elseif(PG_CASE STREQUAL "RejectsWrongStdlib")
    set(expected sdk_stdlib_mismatch)
    # 在真实 SDK 上改变消费者标准库选择，要求配置阶段识别实际编译失败。
    if(CMAKE_HOST_WIN32)
        # Release SDK 不能接纳使用 Debug 迭代器布局的 MSVC STL 消费者。
        list(APPEND args "-DCMAKE_CXX_FLAGS:STRING=-D_ITERATOR_DEBUG_LEVEL=2 -D_DEBUG")
    else()
        list(APPEND args "-DCMAKE_CXX_FLAGS:STRING=-stdlib=libstdc++")
    endif()
else()
    message(FATAL_ERROR "Unknown SDK case: ${PG_CASE}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${PG_SOURCE}" -B "${PG_BINARY}" -G Ninja
    "-DCMAKE_CXX_COMPILER:FILEPATH=${PG_COMPILER}" -DBUILD_TESTING=OFF
    "-DPANGU_META_LLVM_DIR:PATH=${PG_LLVM_DIR}"
    "-DPANGU_META_CLANG_DIR:PATH=${PG_CLANG_DIR}"
    "-DPANGU_META_RESOURCE_DIR:PATH=${PG_RESOURCE_DIR}" ${args}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}\n${error}" MATCHES "${expected}")
    message(FATAL_ERROR "Expected ${expected}, got ${result}:\n${output}\n${error}")
endif()
