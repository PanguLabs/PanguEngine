include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/PanguTesting.cmake")
function(_pangu_finalize_build_tools_tests tool)
    get_property(source GLOBAL PROPERTY "PANGU_BUILD_TOOLS_${tool}_SOURCE")
    get_property(args GLOBAL PROPERTY "PANGU_BUILD_TOOLS_${tool}_ARGS")
    set(binary "${CMAKE_BINARY_DIR}/Tests/BuildTools/${tool}")
    if(CMAKE_HOST_WIN32)
        set(suffix .exe)
    else()
        set(suffix "")
    endif()
    file(TO_CMAKE_PATH "$ENV{LLVM_ROOT}/bin/clang++${suffix}" compiler)
    if(NOT EXISTS "${compiler}")
        message(FATAL_ERROR "BuildTools testing requires the host Clang under LLVM_ROOT: ${compiler}")
    endif()
    set(toolchainArgs)
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND toolchainArgs
            "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Platform/Linux/Toolchain.cmake")
        if(DEFINED PANGU_LIBCXX_ROOT)
            list(APPEND toolchainArgs "-DPANGU_LIBCXX_ROOT:PATH=${PANGU_LIBCXX_ROOT}")
        endif()
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        list(APPEND toolchainArgs
            "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Platform/macOS/Toolchain.cmake")
    endif()
    add_custom_command(TARGET "${tool}TestsConfigure" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${binary}" -G Ninja
            "-DCMAKE_CXX_COMPILER:FILEPATH=${compiler}" -DCMAKE_BUILD_TYPE:STRING=Release
            ${toolchainArgs} ${args} -DBUILD_TESTING:BOOL=ON -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON
        VERBATIM)
    foreach(tier IN ITEMS Fast Precommit Full)
        get_property(targets GLOBAL PROPERTY "PANGU_BUILD_TOOLS_${tool}_${tier}_TARGETS")
        if(targets)
            list(REMOVE_DUPLICATES targets)
            add_custom_command(TARGET "${tool}Tests${tier}" POST_BUILD
                COMMAND "${CMAKE_COMMAND}" --build "${binary}" --target ${targets} --parallel "${PANGU_TEST_BUILD_JOBS}"
                VERBATIM)
        endif()
    endforeach()
endfunction()

function(pangu_add_build_tools_tests)
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/PanguTestBuildJobs.cmake")
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "TOOL_NAME;SOURCE_DIR;GROUP;BOUNDARY;TIER;TEST_REGEX"
        "BUILD_TARGETS;CMAKE_ARGS")
    if(arg_UNPARSED_ARGUMENTS OR arg_KEYWORDS_MISSING_VALUES OR
       NOT arg_TOOL_NAME MATCHES "^[A-Za-z][A-Za-z0-9_]*$" OR
       NOT arg_GROUP MATCHES "^[A-Za-z][A-Za-z0-9_]*$" OR
       NOT IS_ABSOLUTE "${arg_SOURCE_DIR}" OR NOT EXISTS "${arg_SOURCE_DIR}/CMakeLists.txt" OR
       NOT arg_BUILD_TARGETS OR NOT arg_TEST_REGEX OR NOT arg_TIER MATCHES "^(fast|precommit|full)$")
        message(FATAL_ERROR "pangu_add_build_tools_tests requires TOOL_NAME, absolute SOURCE_DIR, GROUP, BOUNDARY, TIER, BUILD_TARGETS and TEST_REGEX")
    endif()
    set(arg_AREA build)
    _pangu_test_metadata(arg)
    file(REAL_PATH "${arg_SOURCE_DIR}" source)
    set(tool "${arg_TOOL_NAME}")
    get_property(existing GLOBAL PROPERTY "PANGU_BUILD_TOOLS_${tool}_SOURCE")
    if(existing)
        get_property(args GLOBAL PROPERTY "PANGU_BUILD_TOOLS_${tool}_ARGS")
        if(NOT "${source}" STREQUAL "${existing}" OR NOT "${arg_CMAKE_ARGS}" STREQUAL "${args}")
            message(FATAL_ERROR "BuildTools ${tool} source/configuration conflict")
        endif()
    else()
        set_property(GLOBAL PROPERTY "PANGU_BUILD_TOOLS_${tool}_SOURCE" "${source}")
        set_property(GLOBAL PROPERTY "PANGU_BUILD_TOOLS_${tool}_ARGS" "${arg_CMAKE_ARGS}")
        add_custom_target("${tool}TestsConfigure")
        add_custom_target("${tool}TestsFast")
        add_custom_target("${tool}TestsPrecommit")
        add_custom_target("${tool}TestsFull")
        add_dependencies("${tool}TestsFast" "${tool}TestsConfigure")
        add_dependencies("${tool}TestsPrecommit" "${tool}TestsFast")
        add_dependencies("${tool}TestsFull" "${tool}TestsPrecommit")
        # 捕获此时的名称，延迟到调用目录结束后收集所有显式测试组。
        cmake_language(EVAL CODE "cmake_language(DEFER CALL _pangu_finalize_build_tools_tests [[${tool}]])")
    endif()
    if(arg_TIER STREQUAL "fast")
        set(tier Fast)
    elseif(arg_TIER STREQUAL "precommit")
        set(tier Precommit)
    else()
        set(tier Full)
    endif()
    set_property(GLOBAL APPEND PROPERTY "PANGU_BUILD_TOOLS_${tool}_${tier}_TARGETS" ${arg_BUILD_TARGETS})
    # 每个累计入口显式接入对应工具入口；低层准备由工具目标依赖串行完成。
    foreach(level IN ITEMS fast precommit full)
        if(level STREQUAL "fast")
            set(preparation Fast)
        elseif(level STREQUAL "precommit")
            set(preparation Precommit)
        else()
            set(preparation Full)
        endif()
        pangu_test_build_dependencies(TIER "${level}" TARGETS "${tool}Tests${preparation}")
    endforeach()
    pangu_register_test(NAME "BuildTools.${tool}.${arg_GROUP}"
        COMMAND "${CMAKE_COMMAND}" -E env "CMAKE_BUILD_PARALLEL_LEVEL=${PANGU_TEST_BUILD_JOBS}"
            "${CMAKE_COMMAND}"
            "-DPANGU_BUILD_TOOLS_TEST_BINARY_DIR=${CMAKE_BINARY_DIR}/Tests/BuildTools/${tool}"
            "-DPANGU_BUILD_TOOLS_TEST_REGEX=${arg_TEST_REGEX}"
            "-DPANGU_BUILD_TOOLS_TEST_RESULT_DIR=${CMAKE_BINARY_DIR}/Tests/Results/BuildTools.${tool}.${arg_GROUP}"
            "-DPANGU_BUILD_TOOLS_TEST_JOBS=${PANGU_TEST_BUILD_JOBS}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Testing/RunBuildToolsTests.cmake"
        BOUNDARY "${arg_BOUNDARY}" AREA build TIER "${arg_TIER}"
        REQUIRES subprocess PROCESSORS "${PANGU_TEST_BUILD_JOBS}" TIMEOUT 900)
endfunction()
