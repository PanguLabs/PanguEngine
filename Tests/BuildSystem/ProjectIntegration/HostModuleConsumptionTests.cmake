cmake_minimum_required(VERSION 4.0)
include("${CMAKE_CURRENT_LIST_DIR}/ProjectTestHelpers.cmake")

set(enabled "fixture.physics;mygame.gameplay;mygame.editor-extras")
file(
    APPEND "${source}/CMakeLists.txt"
    [=[
if(PANGU_FIXTURE_BUILD_HOSTS AND PANGU_LINK_MODE STREQUAL "Modular")
    get_target_property(hostLinks MyGame LINK_LIBRARIES)
    if("MyGameGameplay" IN_LIST hostLinks)
        message(FATAL_ERROR "Modular Host must not import Gameplay")
    endif()
endif()
]=]
)
foreach(mode IN ITEMS Modular Monolithic)
    foreach(host IN ITEMS MyGame MyEditor)
        _pangu_configure_project_fixture(
            "${host}-${mode}"
            "${enabled}"
            PASS
            ""
            -DPANGU_FIXTURE_BUILD_HOSTS=ON
            "-DPANGU_LINK_MODE=${mode}"
        )
        _pangu_build_project_fixture("${host}")
        if(host STREQUAL "MyGame")
            set(unused EditorExtras)
            set(dependent Gameplay)
            set(catalog GameCatalog.cpp)
        else()
            set(unused Gameplay)
            set(dependent EditorExtras)
            set(catalog EditorCatalog.cpp)
        endif()
        file(
            GLOB unusedFiles
            "${binary}/Lib/*${unused}*"
            "${binary}/Bin/*${unused}*"
            "${binary}/EngineBuild/Lib/*${unused}*"
            "${binary}/EngineBuild/Bin/*${unused}*"
        )
        if(unusedFiles)
            message(FATAL_ERROR "${host} built unrelated ${unused}: ${unusedFiles}")
        endif()
        _pangu_run_project_command(
            PASS
            ""
            ninja
            -C
            "${binary}"
            -t
            commands
            "${host}"
        )
        _pangu_reject_test_text("${commandOutput}" "${unused}.cpp")
        _pangu_require_test_text("${commandOutput}" "Physics.cpp")
        if(mode STREQUAL "Monolithic")
            _pangu_require_test_text("${commandOutput}" "Private/StaticModuleCatalog.gen.cpp")
            _pangu_require_test_text("${commandOutput}" "EngineHost.cpp")
            file(READ "${binary}/Generated/PanguMeta/Release/${host}/Meta.request.json" request)
            string(JSON catalogLength LENGTH "${request}" catalog)
            if(NOT catalogLength EQUAL 2)
                message(FATAL_ERROR "Host catalog must contain exactly its explicit pair")
            endif()
            string(
                JSON first
                GET "${request}"
                catalog
                0
                target
            )
            string(
                JSON second
                GET "${request}"
                catalog
                1
                target
            )
            if(NOT first STREQUAL "MyGame${dependent}" OR NOT second STREQUAL "FixtureEnginePhysics")
                message(FATAL_ERROR "Host explicit catalog order/set changed")
            endif()
        else()
            _pangu_reject_test_text("${commandOutput}" "Source/${catalog}")
            _pangu_reject_test_text("${commandOutput}" "Private/StaticModuleCatalog.gen.cpp")
        endif()
        if(CMAKE_HOST_WIN32)
            set(suffix .exe)
        else()
            set(suffix "")
        endif()
        _pangu_check_project_consumer("${host}" "${mode}" "${dependent}" "${unused}")
        _pangu_build_project_fixture("${host}")
        _pangu_require_test_text("${commandOutput}" "no work to do")
    endforeach()
endforeach()

_pangu_configure_project_fixture(
    Assets
    "fixture.physics;mygame.gameplay;mygame.assets-only"
    PASS
    ""
    -DPANGU_FIXTURE_BUILD_HOSTS=OFF
)
_pangu_build_project_fixture(all)
file(
    GLOB pluginLibraries
    "${binary}/EngineBuild/Lib/*Physics*"
    "${binary}/EngineBuild/Bin/*Physics*"
    "${binary}/Lib/*Gameplay*"
    "${binary}/Bin/*Gameplay*"
    "${binary}/Lib/*Assets*"
)
if(pluginLibraries)
    message(FATAL_ERROR "Assets/enabled-only plugins must not build without consumers")
endif()
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
_pangu_reject_test_text("${commandOutput}" "MyGameEditorExtras")
_pangu_reject_test_text("${commandOutput}" "PanguAssets")
file(REMOVE "${source}/Plugins/AssetsOnly/Plugin.toml")
_pangu_run_project_command(
    FAIL
    "both Plugin.toml and CMakeLists.txt"
    "${CMAKE_COMMAND}"
    --build
    "${binary}"
)
message(STATUS "Independent Game/Editor catalogs, registration order and assets-only plugins passed")
