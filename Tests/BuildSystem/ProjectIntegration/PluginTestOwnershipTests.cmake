cmake_minimum_required(VERSION 4.0)
if(NOT DEFINED PANGU_OWNERSHIP_STAGE)
    set(PANGU_OWNERSHIP_STAGE All)
endif()
if(NOT PANGU_OWNERSHIP_STAGE MATCHES "^(All|Base|Project|Engine|Provider)$")
    message(FATAL_ERROR "Ownership requires stage All, Base, Project, Engine or Provider")
endif()
# 每个正式Mac场景拥有自己的冷根；单独选择时也从当前源完整准备，不借旧结果。
if(NOT PANGU_OWNERSHIP_STAGE STREQUAL "All")
    set(PANGU_TEST_ROOT "${PANGU_TEST_ROOT}/${PANGU_OWNERSHIP_STAGE}")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/ProjectTestHelpers.cmake")

function(_pangu_check_plugin_test_ownership engineExpected projectExpected collaborationExpected probeExpected)
    _pangu_run_project_command(
        PASS
        ""
        "${CMAKE_CTEST_COMMAND}"
        --test-dir
        "${binary}"
        --show-only=json-v1
    )
    set(json "${commandStdout}")
    foreach(
        entry
        IN
        ITEMS
            "FixtureEnginePhysicsPluginTests.PhysicsPlugin.DefinitionMatchesModule|${engineExpected}|engine|fixture.physics|unit"
            "MyGameGameplayPluginTests.GameplayPlugin.DependsOnPhysics|${projectExpected}|project|mygame.gameplay|unit"
            "MyGamePluginCollaborationTests.ProjectPluginCollaboration.RegistersProviderFirst|${collaborationExpected}|project||integration"
            "MyGameProbe|${probeExpected}|project||integration"
    )
        string(
            REPLACE "|"
            ";"
            fields
            "${entry}"
        )
        list(GET fields 0 wanted)
        list(GET fields 1 expected)
        list(GET fields 2 owner)
        list(GET fields 3 plugin)
        list(GET fields 4 boundary)
        if(NOT expected)
            string(
                REGEX REPLACE "[.].*$"
                ""
                disabledTarget
                "${wanted}"
            )
            _pangu_reject_test_text("${json}" "${disabledTarget}")
        endif()
        set(found OFF)
        string(JSON count LENGTH "${json}" tests)
        if(count GREATER 0)
            math(EXPR last "${count} - 1")
            foreach(index RANGE 0 ${last})
                string(
                    JSON name
                    GET "${json}"
                    tests
                    ${index}
                    name
                )
                if(NOT name STREQUAL wanted)
                    continue()
                endif()
                set(found ON)
                string(
                    JSON properties
                    GET "${json}"
                    tests
                    ${index}
                    properties
                )
                _pangu_require_test_text("${properties}" "owner.${owner}")
                _pangu_require_test_text("${properties}" "boundary.${boundary}")
                _pangu_require_test_text("${properties}" "area.build")
                _pangu_require_test_text("${properties}" "run.full")
                if(plugin)
                    _pangu_require_test_text("${properties}" "plugin.${plugin}")
                else()
                    _pangu_reject_test_text("${properties}" "plugin.")
                endif()
                if(NOT wanted STREQUAL "MyGameProbe")
                    _pangu_require_test_text("${properties}" "run.fast")
                else()
                    _pangu_require_test_text("${properties}" "run.precommit")
                    _pangu_reject_test_text("${properties}" "run.fast")
                endif()
            endforeach()
        endif()
        if(expected AND NOT found)
            message(FATAL_ERROR "Missing owned test ${wanted}: ${json}")
        elseif(NOT expected AND found)
            message(FATAL_ERROR "Disabled owner/Plugin registered ${wanted}")
        endif()
    endforeach()
endfunction()

if(PANGU_OWNERSHIP_STAGE STREQUAL "All" OR PANGU_OWNERSHIP_STAGE STREQUAL "Base")
    # 工具的来源字段是接线契约；无效值不能退回到路径或调用方猜测。
    set(originSource "${PANGU_TEST_ROOT}/OriginValidation")
    file(MAKE_DIRECTORY "${originSource}")
    file(
        WRITE "${originSource}/CMakeLists.txt"
        [=[
    cmake_minimum_required(VERSION 4.0)
    project(OriginValidation LANGUAGES NONE)
    include("${PANGU_ENGINE_SOURCE_DIR}/cmake/PanguPlugins.cmake")
    function(pangu_prepare_build_tool output)
        set(${output} origin-fixture-tool PARENT_SCOPE)
    endfunction()
    function(execute_process)
        cmake_parse_arguments(arg "" "RESULT_VARIABLE;OUTPUT_VARIABLE;ERROR_VARIABLE" "COMMAND" ${ARGN})
        set(json "{\"plugins\":[{\"id\":\"fixture.physics\",\"name\":\"\",\"version\":\"\",\"source_dir\":\"unused\",\"manifest_path\":\"unused\"")
        if(NOT PANGU_MOCK_ORIGIN STREQUAL "missing")
            string(APPEND json ",\"origin\":\"${PANGU_MOCK_ORIGIN}\"")
        endif()
        string(APPEND json "}]}")
        set(${arg_RESULT_VARIABLE} 0 PARENT_SCOPE)
        set(${arg_OUTPUT_VARIABLE} "${json}" PARENT_SCOPE)
        set(${arg_ERROR_VARIABLE} "" PARENT_SCOPE)
    endfunction()
    pangu_discover_plugins(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_JSON json)
    ]=]
    )
    foreach(origin IN ITEMS missing plugin)
        _pangu_run_project_command(
            FAIL
            ".origin"
            "${CMAKE_COMMAND}"
            -S
            "${originSource}"
            -B
            "${PANGU_TEST_ROOT}/Origin-${origin}"
            -G
            Ninja
            "-DPANGU_ENGINE_SOURCE_DIR=${PANGU_ENGINE_SOURCE_DIR}"
            "-DPANGU_MOCK_ORIGIN=${origin}"
        )
    endforeach()

    _pangu_configure_project_fixture(
        NoPlugins
        ""
        PASS
        ""
        -DBUILD_TESTING=ON
        -DPANGU_BUILD_ENGINE_TESTS=OFF
    )
    _pangu_build_project_fixture(MyGameProbe)
    _pangu_check_plugin_test_ownership(OFF OFF OFF ON)
    _pangu_run_project_command(
        PASS
        ""
        "${CMAKE_CTEST_COMMAND}"
        --test-dir
        "${binary}"
        --no-tests=error
        -R
        "^MyGameProbe$"
        --output-on-failure
    )
endif()

if(PANGU_OWNERSHIP_STAGE STREQUAL "All" OR PANGU_OWNERSHIP_STAGE STREQUAL "Project")
    _pangu_configure_project_fixture(
        ProjectOnly
        "fixture.physics;mygame.gameplay"
        PASS
        ""
        -DBUILD_TESTING=ON
        -DPANGU_BUILD_ENGINE_TESTS=OFF
        -DPANGU_LINK_MODE=Monolithic
    )
    _pangu_build_project_fixture(MyGameProbe)
    _pangu_reject_test_text("${commandOutput}" "PluginBehaviorTests.cpp")
    _pangu_reject_test_text("${commandOutput}" "PluginCollaborationTests.cpp")
    _pangu_reject_test_text("${commandOutput}" "Physics.cpp")
    _pangu_reject_test_text("${commandOutput}" "Gameplay.cpp")
    foreach(tier IN ITEMS Fast Precommit Full)
        _pangu_build_project_fixture("PanguTests${tier}")
        if(tier STREQUAL "Fast")
            foreach(input IN ITEMS Physics.cpp Gameplay.cpp PluginBehaviorTests.cpp PluginCollaborationTests.cpp)
                _pangu_require_test_text("${commandOutput}" "${input}")
            endforeach()
        endif()
    endforeach()
    _pangu_check_plugin_test_ownership(OFF ON ON ON)
    _pangu_run_project_command(
        PASS
        ""
        ninja
        -C
        "${binary}"
        -t
        commands
        MyGamePluginCollaborationTests
    )
    _pangu_require_test_text("${commandOutput}" "EngineHost.cpp")
    _pangu_run_project_command(
        PASS
        ""
        "${CMAKE_CTEST_COMMAND}"
        --test-dir
        "${binary}"
        --no-tests=error
        --output-on-failure
    )
    _pangu_build_project_fixture(MyGameProbe)
    _pangu_require_test_text("${commandOutput}" "no work to do")
endif()

if(PANGU_OWNERSHIP_STAGE STREQUAL "All" OR PANGU_OWNERSHIP_STAGE STREQUAL "Engine")
    _pangu_configure_project_fixture(
        EngineAndProject
        "fixture.physics;mygame.gameplay"
        PASS
        ""
        -DBUILD_TESTING=ON
        -DPANGU_BUILD_ENGINE_TESTS=ON
        -DPANGU_LINK_MODE=Modular
    )
    _pangu_build_project_fixture(MyGameProbe)
    _pangu_reject_test_text("${commandOutput}" "PluginBehaviorTests.cpp")
    # Engine 集合含嵌套工具准备；外层串行，内层继续使用同一处理器预算。
    _pangu_run_project_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        --build
        "${binary}"
        --parallel
        1
        --target
        PanguTestsFast
    )
    _pangu_check_plugin_test_ownership(ON ON ON ON)
    _pangu_run_project_command(
        PASS
        ""
        "${CMAKE_CTEST_COMMAND}"
        --test-dir
        "${binary}"
        --no-tests=error
        -R
        "^(FixtureEnginePhysicsPluginTests|MyGameGameplayPluginTests|MyGamePluginCollaborationTests)[.]"
        --output-on-failure
    )
endif()

if(PANGU_OWNERSHIP_STAGE STREQUAL "All" OR PANGU_OWNERSHIP_STAGE STREQUAL "Provider")
    _pangu_configure_project_fixture(
        EngineProviderOnly
        "fixture.physics"
        PASS
        ""
        -DBUILD_TESTING=ON
        -DPANGU_BUILD_ENGINE_TESTS=ON
        -DPANGU_LINK_MODE=Modular
    )
    _pangu_build_project_fixture(MyGameProbe)
    _pangu_reject_test_text("${commandOutput}" "Physics.cpp")
    _pangu_reject_test_text("${commandOutput}" "PluginBehaviorTests.cpp")
    file(
        GLOB unconsumedPhysics
        "${binary}/EngineBuild/Lib/*Physics*"
        "${binary}/EngineBuild/Bin/*Physics*"
    )
    if(unconsumedPhysics)
        message(FATAL_ERROR "An unrelated host must not build the enabled Physics test runtime")
    endif()

    _pangu_configure_project_fixture(
        ProviderOnly
        "fixture.physics"
        PASS
        ""
        -DBUILD_TESTING=ON
        -DPANGU_BUILD_ENGINE_TESTS=OFF
    )
    _pangu_build_project_fixture(PanguTestsPrecommit)
    _pangu_check_plugin_test_ownership(OFF OFF OFF ON)

    set(physicsSource "${PANGU_TEST_ROOT}/PhysicsConsumer")
    set(physicsBinary "${PANGU_TEST_ROOT}/PhysicsConsumerBuild")
    file(
        COPY
            "${PANGU_ENGINE_SOURCE_DIR}/Tests/BuildSystem/PluginIntegration/Fixtures/PhysicsConsumer/"
        DESTINATION "${physicsSource}"
    )
    _pangu_run_project_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        -S
        "${physicsSource}"
        -B
        "${physicsBinary}"
        -G
        Ninja
        ${projectCompilerArgs}
        -DCMAKE_BUILD_TYPE=Release
        -DPANGU_LINK_MODE=Modular
        -DBUILD_TESTING=ON
        -DPANGU_BUILD_ENGINE_TESTS=ON
        "-DPANGU_ENGINE_SOURCE_DIR=${PANGU_ENGINE_SOURCE_DIR}"
        "-DPANGU_ENGINE_BUILD_ROOT=${physicsBinary}/EngineBuild"
        "-DPANGU_META_HOST_BUILD_ROOT=${PANGU_META_HOST_BUILD_ROOT}"
        "-DPANGU_FIXTURE_ENGINE_ROOT=${PANGU_TEST_ROOT}/FixtureEngine"
    )
    _pangu_run_project_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        --build
        "${physicsBinary}"
        --parallel
        "${PANGU_TEST_BUILD_JOBS}"
        --target
        FixtureEnginePhysicsPluginTests
    )
    _pangu_run_project_command(
        PASS
        "100% tests passed"
        "${CMAKE_CTEST_COMMAND}"
        --test-dir
        "${physicsBinary}"
        --no-tests=error
        -R
        "^FixtureEnginePhysicsPluginTests[.]"
        --output-on-failure
    )
endif()

if(PANGU_OWNERSHIP_STAGE STREQUAL "All" OR PANGU_OWNERSHIP_STAGE STREQUAL "Base")
    _pangu_configure_project_fixture(
        AllDisabled
        "fixture.physics;mygame.gameplay"
        PASS
        ""
        -DBUILD_TESTING=OFF
        -DPANGU_BUILD_ENGINE_TESTS=ON
    )
    _pangu_check_plugin_test_ownership(OFF OFF OFF OFF)
    _pangu_run_project_command(
        PASS
        ""
        "${CMAKE_CTEST_COMMAND}"
        --test-dir
        "${binary}"
        --show-only=json-v1
    )
    string(JSON count LENGTH "${commandStdout}" tests)
    if(NOT count EQUAL 0)
        message(FATAL_ERROR "BUILD_TESTING=OFF must register no correctness tests")
    endif()
endif()
