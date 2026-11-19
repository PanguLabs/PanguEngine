cmake_minimum_required(VERSION 4.0)
set(PANGU_SWITCH_ONLY ON)
include("${CMAKE_CURRENT_LIST_DIR}/BuildLayoutTests.cmake")

function(_pangu_check_meta_request name config key)
    set(root "${engineBuild}/Generated/PanguMeta/${config}/PanguLayoutValue")
    file(READ "${root}/Meta.request.json" request)
    string(JSON actualConfig GET "${request}" config)
    string(JSON actualSource GET "${request}" project_source)
    string(
        JSON actualDatabase
        GET "${request}"
        context
        database
    )
    if(
        NOT actualConfig
            STREQUAL
            config
        OR NOT actualSource
            STREQUAL
            "${PANGU_TEST_ROOT}/${name}"
        OR NOT actualDatabase
            STREQUAL
            "${PANGU_TEST_ROOT}/${name}Build/compile_commands.json"
    )
        message(FATAL_ERROR "Stale cross-project/configuration request: ${request}")
    endif()
    file(READ "${root}/Public/Context.gen.h" generated)
    _pangu_require_test_text("${generated}" ".Key = \"${key}\"")
endfunction()

function(_pangu_check_current_project name)
    file(READ "${engineBuild}/Generated/BuildContext/CurrentProject.json" current)
    string(JSON source GET "${current}" project_source)
    string(JSON build GET "${current}" project_build)
    if(NOT source STREQUAL "${PANGU_TEST_ROOT}/${name}" OR NOT build STREQUAL "${PANGU_TEST_ROOT}/${name}Build")
        message(FATAL_ERROR "Stale shared CurrentProject: ${current}")
    endif()
endfunction()

# 编译数据库只描述意图；检查共享路径上实际产物，防止回切后沿用另一配置的第三方对象。
function(_pangu_check_third_party_configuration name config)
    if(NOT EXISTS "${PANGU_TEST_INSPECTOR}")
        message(FATAL_ERROR "ProjectSwitch requires the selected LLVM object inspector")
    endif()
    file(READ "${PANGU_TEST_ROOT}/${name}Build/compile_commands.json" commands)
    string(JSON count LENGTH "${commands}")
    math(EXPR last "${count}-1")
    set(found 0)
    foreach(index RANGE 0 ${last})
        string(
            JSON source
            GET "${commands}"
            ${index}
            file
        )
        string(
            REPLACE "\\"
            "/"
            source
            "${source}"
        )
        if(NOT source MATCHES "/(Fmt/Repo/src/format[.]cc|Spdlog/Repo/src/spdlog[.]cpp)$")
            continue()
        endif()
        string(
            JSON command
            GET "${commands}"
            ${index}
            command
        )
        string(
            REPLACE "\\"
            "/"
            command
            "${command}"
        )
        string(
            REGEX MATCH "-o ([^ ]+)"
            ignored
            "${command}"
        )
        set(object "${CMAKE_MATCH_1}")
        cmake_path(ABSOLUTE_PATH object BASE_DIRECTORY "${PANGU_TEST_ROOT}/${name}Build" NORMALIZE)
        if(CMAKE_HOST_WIN32)
            _pangu_run_project_command(
                PASS
                ""
                "${PANGU_TEST_INSPECTOR}"
                --coff-directives
                "${object}"
            )
            if(config STREQUAL "Debug")
                _pangu_require_test_text("${commandOutput}" "msvcrtd")
                _pangu_require_test_text("${commandOutput}" "_ITERATOR_DEBUG_LEVEL=2")
            else()
                _pangu_reject_test_text("${commandOutput}" "msvcrtd")
                _pangu_require_test_text("${commandOutput}" "_ITERATOR_DEBUG_LEVEL=0")
            endif()
        else()
            _pangu_run_project_command(
                PASS
                ""
                "${PANGU_TEST_INSPECTOR}"
                --sections
                "${object}"
            )
            set(debug OFF)
            if(commandOutput MATCHES "([.]debug_info|__debug_info)")
                set(debug ON)
            endif()
            if(config STREQUAL "Debug" AND NOT debug)
                message(FATAL_ERROR "Debug graph kept a non-Debug third-party object: ${object}")
            elseif(config STREQUAL "Release" AND debug)
                message(FATAL_ERROR "Release graph kept a Debug third-party object: ${object}")
            endif()
        endif()
        file(
            APPEND "${PANGU_TEST_ROOT}/ThirdPartyConfigurations.txt"
            "${name}/${config}: ${object}\n${commandOutput}\n"
        )
        math(EXPR found "${found}+1")
    endforeach()
    if(NOT found EQUAL 2)
        message(FATAL_ERROR "Expected actual fmt and spdlog object commands, got ${found}")
    endif()
endfunction()

# 两个 Project 的宏与配置完全相同，仅来源变化也必须重新生成成功 stamp。
set(stamp "${engineBuild}/Generated/PanguMeta/Debug/PanguLayoutValue/Meta.stamp")
_pangu_configure_layout_fixture(A 17 Debug)
_pangu_build_layout_fixture(A)
_pangu_run_layout_fixture(A 17)
_pangu_check_current_project(A)
_pangu_check_meta_request(A Debug A)
file(
    TIMESTAMP "${stamp}"
    sameArgumentsA
    "%s.%f"
)
if(sameArgumentsA STREQUAL "")
    message(FATAL_ERROR "Initial A build must publish a success stamp")
endif()
_pangu_configure_layout_fixture(B 17 Debug)
_pangu_check_current_project(B)
_pangu_check_meta_request(B Debug A)
_pangu_build_layout_fixture(B)
_pangu_run_layout_fixture(B 17)
_pangu_check_current_project(B)
_pangu_check_meta_request(B Debug A)
file(
    TIMESTAMP "${stamp}"
    sameArgumentsB
    "%s.%f"
)
if(sameArgumentsB STREQUAL "" OR sameArgumentsA STREQUAL sameArgumentsB)
    message(FATAL_ERROR "Same-argument A to B switch reused the old success stamp")
endif()
# 不显式 Configure A，由旧 A 图的原生依赖恢复当前来源。
_pangu_build_layout_fixture(A)
_pangu_run_layout_fixture(A 17)
_pangu_check_current_project(A)
_pangu_check_meta_request(A Debug A)
file(
    TIMESTAMP "${stamp}"
    sameArgumentsRestored
    "%s.%f"
)
if(sameArgumentsRestored STREQUAL "" OR sameArgumentsB STREQUAL sameArgumentsRestored)
    message(FATAL_ERROR "Same-argument B to A switch reused the old success stamp")
endif()
_pangu_build_layout_fixture(A)
_pangu_require_test_text("${buildOutput}" "no work to do")
file(
    TIMESTAMP "${stamp}"
    sameArgumentsNoOp
    "%s.%f"
)
if(NOT sameArgumentsRestored STREQUAL sameArgumentsNoOp)
    message(FATAL_ERROR "Restored same-argument A must preserve the success stamp on no-op")
endif()

foreach(config IN ITEMS Debug Release)
    _pangu_configure_layout_fixture(A 17 Debug)
    _pangu_build_layout_fixture(A)
    _pangu_run_layout_fixture(A 17)
    _pangu_check_third_party_configuration(A Debug)
    set(resultA1 "${runOutput}")
    _pangu_check_meta_request(A Debug A)
    _pangu_check_layout_object_root(A)
    set(commandA "${objectCommand}")
    set(valueA "${valueObject}")
    _pangu_configure_layout_fixture(B 29 "${config}")
    _pangu_build_layout_fixture(B)
    _pangu_run_layout_fixture(B 29)
    _pangu_check_third_party_configuration(B "${config}")
    set(resultB "${runOutput}")
    _pangu_check_meta_request(B "${config}" B)
    if(config STREQUAL "Debug")
        configure_file(
            "${engineBuild}/Generated/PanguMeta/Debug/PanguLayoutValue/Meta.request.json"
            "${PANGU_TEST_ROOT}/StaleDebug.request.json"
            COPYONLY
        )
    else()
        if(CMAKE_HOST_WIN32)
            set(toolSuffix .exe)
        endif()
        _pangu_run_project_command(
            FAIL
            "invalid_context"
            "${PANGU_META_HOST_BUILD_ROOT}/BuildTools/PanguMetaTool/PanguMetaTool${toolSuffix}"
            generate
            --request
            "${PANGU_TEST_ROOT}/StaleDebug.request.json"
        )
        if(NOT commandResult STREQUAL "1")
            message(FATAL_ERROR "Stale Debug request must fail semantically, not crash: ${commandResult}")
        endif()
    endif()
    _pangu_check_layout_object_root(B)
    # Debug/Release 的选项可以不同；实际输出对象路径必须完全相同。
    set(objectB "${objectCommand}")
    set(objectA "${commandA}")
    if(NOT objectA STREQUAL objectB OR NOT valueA STREQUAL valueObject)
        message(FATAL_ERROR "Projects must compile the same Engine object path: ${objectA} / ${objectB}")
    endif()
    _pangu_build_layout_fixture(A)
    _pangu_run_layout_fixture(A 17)
    _pangu_check_third_party_configuration(A Debug)
    _pangu_check_meta_request(A Debug A)
    set(resultA2 "${runOutput}")
    _pangu_require_test_text("${resultA1}" "value=17")
    _pangu_require_test_text("${resultB}" "value=29")
    _pangu_require_test_text("${resultA2}" "value=17")
    _pangu_build_layout_fixture(A)
    _pangu_require_test_text("${buildOutput}" "no work to do")
endforeach()
# B 的 Meta 扫描失败后，直接构建参数不变的 A，必须恢复共享请求与产物。
_pangu_configure_layout_fixture(B 29 Debug)
file(READ "${enginePlugins}/Plugins/Value/Context.h" originalContext)
string(
    REPLACE "pga::Type"
    "pga::Type(42)"
    invalidContext
    "${originalContext}"
)
file(WRITE "${enginePlugins}/Plugins/Value/Context.h" "${invalidContext}")
_pangu_run_project_command(
    FAIL
    "invalid_type_id"
    "${CMAKE_COMMAND}"
    --build
    "${PANGU_TEST_ROOT}/BBuild"
    --target
    LayoutHost
    --parallel
    "${PANGU_TEST_BUILD_JOBS}"
)
if(NOT commandResult STREQUAL "1")
    message(FATAL_ERROR "Failed switch must report a normal build failure, not crash: ${commandResult}")
endif()
file(WRITE "${enginePlugins}/Plugins/Value/Context.h" "${originalContext}")
_pangu_build_layout_fixture(A)
_pangu_run_layout_fixture(A 17)
_pangu_check_meta_request(A Debug A)
_pangu_build_layout_fixture(A)
_pangu_require_test_text("${buildOutput}" "no work to do")
if(CMAKE_HOST_WIN32)
    function(_pangu_check_meta_host_runtime host)
        get_filename_component(hostDirectory "${host}" DIRECTORY)
        _pangu_run_project_command(PASS "value=17" "${host}")
        file(
            TIMESTAMP "${host}"
            hostBefore
            "%s.%f"
        )
        # 改变 DLL 文件而保留导入库，模拟无需 Host 重链接的 Runtime 更新。
        file(APPEND "${engineBuild}/Bin/PanguLayoutValue.dll" "deployment-update")
        _pangu_build_layout_fixture(A)
        file(
            TIMESTAMP "${host}"
            hostAfter
            "%s.%f"
        )
        file(SHA256 "${engineBuild}/Bin/PanguLayoutValue.dll" engineHash)
        file(SHA256 "${hostDirectory}/PanguLayoutValue.dll" stagedHash)
        if(NOT hostBefore STREQUAL hostAfter OR NOT engineHash STREQUAL stagedHash)
            message(FATAL_ERROR "Runtime deployment must update without Host relinking")
        endif()
        _pangu_run_project_command(PASS "value=17" "${host}")
        _pangu_build_layout_fixture(A)
        _pangu_require_test_text("${buildOutput}" "no work to do")
        file(REMOVE "${hostDirectory}/PanguLayoutValue.dll")
        _pangu_build_layout_fixture(A)
        if(NOT EXISTS "${hostDirectory}/PanguLayoutValue.dll")
            message(FATAL_ERROR "Missing deployed Runtime DLL was not restored by ordinary build")
        endif()
        _pangu_run_project_command(PASS "value=17" "${host}")
        file(
            TIMESTAMP "${host}"
            hostAfterRestore
            "%s.%f"
        )
        if(NOT hostAfter STREQUAL hostAfterRestore)
            message(FATAL_ERROR "Restoring a deployed Runtime DLL must not relink Host")
        endif()
        _pangu_build_layout_fixture(A)
        _pangu_require_test_text("${buildOutput}" "no work to do")
    endfunction()

    _pangu_check_meta_host_runtime("${PANGU_TEST_ROOT}/ABuild/Bin/LayoutHost.exe")
    _pangu_run_project_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        -S
        "${PANGU_TEST_ROOT}/A"
        -B
        "${PANGU_TEST_ROOT}/ABuild"
        -DPANGU_LAYOUT_CONFIG_HOST_DIR=ON
    )
    _pangu_build_layout_fixture(A)
    file(STRINGS "${PANGU_TEST_ROOT}/ABuild/Layout.txt" hostEntry REGEX "^host=")
    string(
        REGEX REPLACE "^host="
        ""
        configuredHost
        "${hostEntry}"
    )
    _pangu_require_test_text("${configuredHost}" "/Bin/Debug/LayoutHost.exe")
    _pangu_check_meta_host_runtime("${configuredHost}")
    _pangu_run_project_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        --build
        "${PANGU_TEST_ROOT}/ABuild"
        --target
        LayoutOtherHost
        --parallel
        "${PANGU_TEST_BUILD_JOBS}"
    )
    file(READ "${PANGU_TEST_ROOT}/ABuild/OtherHost.txt" otherHost)
    _pangu_require_test_text("${otherHost}" "/Bin/OtherDebug/LayoutOtherHost.exe")
    _pangu_run_project_command(PASS "value=17" "${otherHost}")
    _pangu_run_project_command(
        FAIL
        "unsupported output-directory generator expression"
        "${CMAKE_COMMAND}"
        -S
        "${PANGU_TEST_ROOT}/A"
        -B
        "${PANGU_TEST_ROOT}/UnsupportedHostBuild"
        -G
        Ninja
        ${projectCompilerArgs}
        -DCMAKE_BUILD_TYPE=Debug
        -DPANGU_LINK_MODE=Modular
        -DPANGU_PROJECT_VALUE=17
        "-DPANGU_ENGINE_SOURCE_DIR=${PANGU_ENGINE_SOURCE_DIR}"
        "-DPANGU_ENGINE_BUILD_ROOT=${PANGU_TEST_ROOT}/UnsupportedEngineBuild"
        "-DPANGU_FIXTURE_ENGINE_ROOT=${enginePlugins}"
        -DPANGU_LAYOUT_CONFIG_HOST_DIR=Unsupported
        -DBUILD_TESTING=OFF
        -DPANGU_BUILD_ENGINE_TESTS=OFF
    )
endif()
message(STATUS "ProjectSwitchRestoresSamePathObjects and Debug/Release switch passed")
