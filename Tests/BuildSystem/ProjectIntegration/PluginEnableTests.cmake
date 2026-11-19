cmake_minimum_required(VERSION 4.0)
include("${CMAKE_CURRENT_LIST_DIR}/ProjectTestHelpers.cmake")

# 缺失启用接线、未启用 CMakeLists 被执行或 ALL 泄漏均须失败。
foreach(linkMode IN ITEMS Monolithic Modular)
    set(selection "Selection-${linkMode}")
    _pangu_configure_project_fixture(
        "${selection}"
        "fixture.physics;mygame.gameplay"
        PASS
        ""
        "-DPANGU_LINK_MODE=${linkMode}"
    )
    file(READ "${binary}/PluginVisits.txt" visits)
    if(NOT visits STREQUAL "fixture.physics\nmygame.gameplay\n")
        message(FATAL_ERROR "Only enabled plugins should execute, in ID order: ${visits}")
    endif()
    file(READ "${binary}/compile_commands.json" commands)
    _pangu_require_test_text("${commands}" "Gameplay.cpp")
    _pangu_require_test_text("${commands}" "Physics.cpp")
    _pangu_reject_test_text("${commands}" "EditorExtras.cpp")
    _pangu_build_project_fixture(all)
    file(
        GLOB libraries
        "${binary}/EngineBuild/Lib/*Physics*"
        "${binary}/EngineBuild/Bin/*Physics*"
        "${binary}/Lib/*Gameplay*"
        "${binary}/Bin/*Physics*"
        "${binary}/Bin/*Gameplay*"
    )
    if(libraries)
        message(FATAL_ERROR "Unconsumed enabled plugins leaked into ALL: ${libraries}")
    endif()
    file(APPEND "${source}/Plugins/EditorExtras/Private/EditorExtras.cpp" "\n// 未启用插件的增量输入\n")
    _pangu_build_project_fixture(all)
    _pangu_require_test_text("${commandOutput}" "no work to do")

    _pangu_configure_project_fixture(
        "${selection}"
        "fixture.physics;mygame.gameplay;mygame.editor-extras"
        PASS
        ""
        "-DPANGU_FIXTURE_PROBE_DEPENDENCIES:STRING=MyGameGameplay"
        "-DPANGU_LINK_MODE=${linkMode}"
    )
    _pangu_build_project_fixture(MyGameProbe)
    file(
        GLOB physics
        "${binary}/EngineBuild/Lib/*Physics*"
        "${binary}/EngineBuild/Bin/*Physics*"
    )
    file(
        GLOB gameplay
        "${binary}/Lib/*Gameplay*"
        "${binary}/Bin/*Gameplay*"
    )
    file(
        GLOB editor
        "${binary}/Lib/*EditorExtras*"
        "${binary}/Bin/*EditorExtras*"
    )
    if(NOT physics OR NOT gameplay OR editor)
        message(FATAL_ERROR "Probe must build only its transitive plugin libraries")
    endif()
    _pangu_run_project_command(
        PASS
        ""
        ninja
        -C
        "${binary}"
        -t
        commands
        MyGameProbe
    )
    _pangu_require_test_text("${commandOutput}" "Gameplay.cpp")
    _pangu_require_test_text("${commandOutput}" "Physics.cpp")
    _pangu_reject_test_text("${commandOutput}" "EditorExtras.cpp")
    _pangu_run_project_command(
        PASS
        ""
        ninja
        -C
        "${binary}"
        -t
        targets
        all
    )
    _pangu_require_test_text("${commandOutput}" "MyGameEditorExtras")
    _pangu_build_project_fixture(MyGameEditorExtras)
    file(
        GLOB editor
        "${binary}/Lib/*EditorExtras*"
        "${binary}/Bin/*EditorExtras*"
    )
    if(NOT editor)
        message(FATAL_ERROR "Explicit plugin target did not build")
    endif()
endforeach()

_pangu_configure_project_fixture(Selection "missing.plugin" FAIL "missing.plugin")
_pangu_configure_project_fixture(Selection "fixture.physics;fixture.physics" FAIL "Duplicate enabled")
_pangu_configure_project_fixture(Selection "Fixture.Physics" FAIL "Fixture.Physics")
_pangu_configure_project_fixture(Selection "mygame.gameplay" FAIL "FixtureEnginePhysics")
file(
    APPEND "${source}/Plugins/EditorExtras/CMakeLists.txt"
    "\nadd_library(FixtureEnginePhysics STATIC Private/EditorExtras.cpp)\n"
)
_pangu_configure_project_fixture(Selection "fixture.physics;mygame.editor-extras" FAIL "FixtureEnginePhysics")
message(STATUS "Plugin selection, late target dependencies and consumer-only builds passed")
