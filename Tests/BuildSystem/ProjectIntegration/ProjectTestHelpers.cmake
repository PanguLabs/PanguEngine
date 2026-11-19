include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/../CommandTiming.cmake")

if(NOT DEFINED PANGU_ENGINE_SOURCE_DIR OR NOT DEFINED PANGU_TEST_ROOT)
    message(FATAL_ERROR "Project tests require Engine source and an isolated test root")
endif()
include("${PANGU_ENGINE_SOURCE_DIR}/cmake/PanguTestBuildJobs.cmake")
set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} "${PANGU_TEST_BUILD_JOBS}")
if(NOT DEFINED PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE)
    set(PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE OFF)
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef runId)
set(PANGU_TEST_ROOT "${PANGU_TEST_ROOT}/${runId}")
file(MAKE_DIRECTORY "${PANGU_TEST_ROOT}/FixtureEngine/Plugins")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/MyGame" DESTINATION "${PANGU_TEST_ROOT}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/FixtureEnginePlugins/" DESTINATION "${PANGU_TEST_ROOT}/FixtureEngine/Plugins")
set(source "${PANGU_TEST_ROOT}/MyGame")
execute_process(
    COMMAND
        git -C "${PANGU_ENGINE_SOURCE_DIR}" rev-parse HEAD
    RESULT_VARIABLE revisionStatus
    OUTPUT_VARIABLE engineHead
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT revisionStatus EQUAL 0)
    message(FATAL_ERROR "Cannot read Engine HEAD")
endif()
file(WRITE "${source}/Engine.commit" "${engineHead}\n")
message(STATUS "Project Engine lock: ${engineHead}; local override=${PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE}")
set(projectCompilerArgs)
if(CMAKE_HOST_WIN32)
    set(projectCompilerArgs "-DCMAKE_C_COMPILER:FILEPATH=$ENV{LLVM_ROOT}/bin/clang.exe")
    set(compilerArgs "-DCMAKE_CXX_COMPILER:FILEPATH=$ENV{LLVM_ROOT}/bin/clang++.exe")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(compilerArgs "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${PANGU_ENGINE_SOURCE_DIR}/cmake/Platform/Linux/Toolchain.cmake")
else()
    set(compilerArgs "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${PANGU_ENGINE_SOURCE_DIR}/cmake/Platform/macOS/Toolchain.cmake")
endif()
list(APPEND projectCompilerArgs "${compilerArgs}")

function(_pangu_run_project_command expected diagnostic)
    set(command)
    math(EXPR last "${ARGC} - 1")
    foreach(index RANGE 2 ${last})
        string(
            REPLACE ";"
            "\\;"
            argument
            "${ARGV${index}}"
        )
        list(APPEND command "${argument}")
    endforeach()
    string(TIMESTAMP commandStarted "%s%f" UTC)
    execute_process(COMMAND ${command} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    _pangu_record_test_command_time("${commandStarted}" "${result}" ${command})
    string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef logId)
    file(WRITE "${PANGU_TEST_ROOT}/${logId}.log" "${ARGN}\nexit=${result}\n${output}\n${error}")
    if(expected STREQUAL "PASS")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Command failed (${result}): ${ARGN}\n${output}\n${error}")
        endif()
    elseif(result EQUAL 0)
        message(FATAL_ERROR "Command unexpectedly passed: ${ARGN}")
    endif()
    if(NOT diagnostic STREQUAL "")
        string(
            FIND "${output}\n${error}"
            "${diagnostic}"
            found
        )
        if(found LESS 0)
            message(FATAL_ERROR "Missing diagnostic '${diagnostic}': ${ARGN}\n${output}\n${error}")
        endif()
    endif()
    set(commandOutput "${output}\n${error}" PARENT_SCOPE)
    set(commandStdout "${output}" PARENT_SCOPE)
    set(commandResult "${result}" PARENT_SCOPE)
endfunction()

function(_pangu_configure_project_fixture name enabled expected diagnostic)
    _pangu_run_project_command(
        "${expected}"
        "${diagnostic}"
        "${CMAKE_COMMAND}"
        -S
        "${source}"
        -B
        "${PANGU_TEST_ROOT}/${name}"
        -G
        Ninja
        ${projectCompilerArgs}
        -DCMAKE_BUILD_TYPE=Release
        "-DPANGU_ENGINE_SOURCE_DIR:PATH=${PANGU_ENGINE_SOURCE_DIR}"
        "-DPANGU_META_HOST_BUILD_ROOT:PATH=${PANGU_META_HOST_BUILD_ROOT}"
        "-DPANGU_ENGINE_BUILD_ROOT:PATH=${PANGU_TEST_ROOT}/${name}/EngineBuild"
        "-DPANGU_FIXTURE_ENGINE_ROOT:PATH=${PANGU_TEST_ROOT}/FixtureEngine"
        "-DPANGU_ENABLED_PLUGINS:STRING=${enabled}"
        "-DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=${PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE}"
        -DBUILD_TESTING=OFF
        ${ARGN}
    )
    set(binary "${PANGU_TEST_ROOT}/${name}" PARENT_SCOPE)
    set(commandOutput "${commandOutput}" PARENT_SCOPE)
endfunction()

function(_pangu_build_project_fixture target)
    _pangu_run_project_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        --build
        "${binary}"
        --parallel
        "${PANGU_TEST_BUILD_JOBS}"
        --target
        "${target}"
    )
    set(commandOutput "${commandOutput}" PARENT_SCOPE)
    set(commandStdout "${commandStdout}" PARENT_SCOPE)
endfunction()

function(_pangu_require_test_text text needle)
    string(
        FIND "${text}"
        "${needle}"
        found
    )
    if(found LESS 0)
        message(FATAL_ERROR "Missing '${needle}' in:\n${text}")
    endif()
endfunction()

function(_pangu_reject_test_text text needle)
    string(
        FIND "${text}"
        "${needle}"
        found
    )
    if(NOT found LESS 0)
        message(FATAL_ERROR "Unexpected '${needle}' in:\n${text}")
    endif()
endfunction()

function(_pangu_check_log_outcome text)
    set(complete "Log outputs complete: H=1 accepted=1 dropped=0 console=1 file=1 resources=1 complete=1")
    set(incomplete "Log outputs incomplete: H=0 accepted=0 dropped=1 console=0 file=0 resources=1 complete=0")
    string(
        REGEX MATCHALL "Log outputs [^\r\n]*"
        outcomes
        "${text}"
    )
    list(LENGTH outcomes count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Expected exactly one log outcome: ${text}")
    endif()
    list(GET outcomes 0 outcome)
    if(NOT outcome STREQUAL complete AND NOT outcome STREQUAL incomplete)
        message(FATAL_ERROR "Inconsistent log outcome: ${outcome}")
    endif()
endfunction()

function(_pangu_check_project_consumer host mode dependent unused)
    file(READ "${binary}/${host}Consumer.txt" consumer)
    _pangu_require_test_text("${consumer}" "mode=${mode}")
    string(
        REGEX MATCH "links=([^\r\n]*)"
        consumerLinkLine
        "${consumer}"
    )
    set(consumerLinks "${CMAKE_MATCH_1}")
    # 头文件接口目标可包含业务库名称，必须按目标成员精确检查链接边。
    foreach(target IN ITEMS "MyGame${dependent}" FixtureEnginePhysics)
        if(mode STREQUAL "Modular")
            if("${target}" IN_LIST consumerLinks)
                message(FATAL_ERROR "Modular Host must not import ${target}")
            endif()
        elseif(NOT "${target}" IN_LIST consumerLinks)
            message(FATAL_ERROR "Monolithic Host must link ${target}")
        endif()
    endforeach()
    if("MyGame${unused}" IN_LIST consumerLinks)
        message(FATAL_ERROR "Host must not link unrelated MyGame${unused}")
    endif()
    if(CMAKE_HOST_WIN32)
        set(suffix .exe)
    endif()
    set(executable "${binary}/Bin/${host}${suffix}")
    if(mode STREQUAL "Modular")
        file(READ "${binary}/Generated/${host}/ModuleCandidates.h" candidates)
        _pangu_require_test_text("${candidates}" "MyGame${dependent}")
        _pangu_require_test_text("${candidates}" "FixtureEnginePhysics")
        _pangu_reject_test_text("${candidates}" "MyGame${unused}")
        if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
            _pangu_run_project_command(
                PASS
                ""
                otool
                -L
                "${executable}"
            )
        else()
            _pangu_run_project_command(
                PASS
                ""
                "$ENV{LLVM_ROOT}/bin/llvm-readobj${suffix}"
                --needed-libs
                --coff-imports
                "${executable}"
            )
        endif()
        _pangu_require_test_text("${commandOutput}" "PanguModule")
        _pangu_require_test_text("${commandOutput}" "PanguEngineHost")
        _pangu_require_test_text("${commandOutput}" "PanguLog")
        if(CMAKE_HOST_WIN32)
            file(GLOB nativeLog "${binary}/Bin/*PanguLog*")
            if(NOT nativeLog)
                message(FATAL_ERROR "Native runtime graph did not deploy Log")
            endif()
        endif()
        _pangu_reject_test_text("${commandOutput}" "MyGame${dependent}")
        _pangu_reject_test_text("${commandOutput}" "FixtureEnginePhysics")
        _pangu_reject_test_text("${commandOutput}" "MyGame${unused}")
        file(STRINGS "${binary}/${host}DeployManifest.txt" paths)
        foreach(index IN ITEMS 0 2)
            math(EXPR next "${index}+1")
            list(GET paths ${index} original)
            list(GET paths ${next} deployed)
            file(SHA256 "${original}" originalHash)
            file(SHA256 "${deployed}" deployedHash)
            if(NOT originalHash STREQUAL deployedHash)
                message(FATAL_ERROR "Business Module deployment differs from its native target")
            endif()
            if(CMAKE_HOST_WIN32)
                _pangu_run_project_command(
                    PASS
                    ""
                    "$ENV{LLVM_ROOT}/bin/llvm-readobj${suffix}"
                    --coff-exports
                    "${original}"
                )
            elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
                _pangu_run_project_command(
                    PASS
                    ""
                    nm
                    -gU
                    "${original}"
                )
            else()
                _pangu_run_project_command(
                    PASS
                    ""
                    "$ENV{LLVM_ROOT}/bin/llvm-nm"
                    --dynamic
                    --defined-only
                    "${original}"
                )
            endif()
            string(
                REGEX MATCHALL "PanguModuleQueryV1"
                nativeQueries
                "${commandOutput}"
            )
            list(LENGTH nativeQueries nativeCount)
            if(NOT nativeCount EQUAL 1)
                message(FATAL_ERROR "Expected exactly one native Query export: ${original}")
            endif()
        endforeach()
    endif()
    _pangu_run_project_command(PASS "Activated Physics ${dependent}; shutdown complete" "${executable}")
    _pangu_check_log_outcome("${commandOutput}")
    if(dependent STREQUAL "Gameplay")
        _pangu_require_test_text("${commandOutput}" "Gameplay Context values: 7 -> 8; isolated=7")
    else()
        _pangu_require_test_text("${commandOutput}" "EditorExtras Context values: 11 -> 12; isolated=11")
    endif()
    set(commandOutput "${commandOutput}" PARENT_SCOPE)
endfunction()
