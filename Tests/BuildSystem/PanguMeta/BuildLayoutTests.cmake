cmake_minimum_required(VERSION 4.0)
include("${PANGU_ENGINE_SOURCE_DIR}/Tests/BuildSystem/ProjectIntegration/ProjectTestHelpers.cmake")
set(fixture "${PANGU_ENGINE_SOURCE_DIR}/Tests/BuildSystem/PanguMeta/Fixtures")
set(engineBuild "${PANGU_TEST_ROOT}/EngineBuild")
set(enginePlugins "${PANGU_TEST_ROOT}/EnginePlugins")
file(MAKE_DIRECTORY "${enginePlugins}/Plugins/Value")
file(COPY "${fixture}/EnginePlugin/" DESTINATION "${enginePlugins}/Plugins/Value")
file(WRITE "${enginePlugins}/Plugins/Value/Plugin.toml" "[plugin]\nid='pangu.layout-value'\n")
foreach(name IN ITEMS A B)
    file(COPY "${fixture}/Project/" DESTINATION "${PANGU_TEST_ROOT}/${name}")
    set(plugin "${PANGU_TEST_ROOT}/${name}/Plugins/Value")
    file(MAKE_DIRECTORY "${plugin}")
    file(WRITE "${plugin}/Plugin.toml" "[plugin]\nid='project.layout-value'\n")
    file(
        WRITE "${plugin}/Value.cpp"
        "#include <PanguProjectValueExport.h>\nPANGUPROJECTVALUE_API int ProjectValue();\nPANGUPROJECTVALUE_API int ProjectValue() { return 1; }\n"
    )
    file(
        WRITE "${plugin}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 4.0)\npangu_module_library_type(kind)\nadd_library(PanguProjectValue \${kind} Value.cpp)\npangu_configure_module(PanguProjectValue)\nset_property(TARGET PanguProjectValue PROPERTY CXX_SCAN_FOR_MODULES OFF)\n"
    )
endforeach()

function(_pangu_configure_layout_fixture name value config)
    set(mode Modular)
    if(ARGC GREATER 3)
        set(mode "${ARGV3}")
    endif()
    _pangu_run_project_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        -S
        "${PANGU_TEST_ROOT}/${name}"
        -B
        "${PANGU_TEST_ROOT}/${name}Build"
        -G
        Ninja
        ${projectCompilerArgs}
        "-DCMAKE_BUILD_TYPE=${config}"
        "-DPANGU_LINK_MODE=${mode}"
        "-DPANGU_ENGINE_SOURCE_DIR=${PANGU_ENGINE_SOURCE_DIR}"
        "-DPANGU_META_HOST_BUILD_ROOT=${PANGU_META_HOST_BUILD_ROOT}"
        "-DPANGU_ENGINE_BUILD_ROOT=${engineBuild}"
        "-DPANGU_FIXTURE_ENGINE_ROOT=${enginePlugins}"
        "-DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=${PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE}"
        "-DPANGU_PROJECT_VALUE=${value}"
        -DBUILD_TESTING=OFF
        -DPANGU_BUILD_ENGINE_TESTS=OFF
    )
endfunction()

function(_pangu_build_layout_fixture name)
    _pangu_run_project_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        --build
        "${PANGU_TEST_ROOT}/${name}Build"
        --target
        LayoutHost
        --parallel
        "${PANGU_TEST_BUILD_JOBS}"
    )
    set(buildOutput "${commandOutput}" PARENT_SCOPE)
endfunction()

function(_pangu_run_layout_fixture name expected)
    if(CMAKE_HOST_WIN32)
        set(suffix .exe)
    endif()
    _pangu_run_project_command(PASS "value=${expected}" "${PANGU_TEST_ROOT}/${name}Build/Bin/LayoutHost${suffix}")
    set(runOutput "${commandOutput}" PARENT_SCOPE)
endfunction()

function(_pangu_check_layout_object_root name)
    file(READ "${PANGU_TEST_ROOT}/${name}Build/compile_commands.json" commands)
    string(JSON count LENGTH "${commands}")
    math(EXPR last "${count}-1")
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
        if(source MATCHES "FoundationModule.cpp$" OR source MATCHES "/EnginePlugins/Plugins/Value/Value.cpp$")
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
            if(NOT EXISTS "${object}")
                message(FATAL_ERROR "Actual Engine object missing: ${object}")
            endif()
            if(source MATCHES "FoundationModule.cpp$")
                _pangu_require_test_text("${object}" "${engineBuild}/Targets/Source/Runtime/Foundation/")
                set(objectCommand "${object}" PARENT_SCOPE)
                set(foundFoundation ON)
            else()
                _pangu_require_test_text("${object}" "${engineBuild}/Targets/Plugins/Value/")
                set(valueObject "${object}" PARENT_SCOPE)
                set(foundValue ON)
            endif()
        endif()
    endforeach()
    if(NOT foundFoundation OR NOT foundValue)
        message(FATAL_ERROR "Actual Foundation/Value object command missing")
    endif()
endfunction()

if(PANGU_SWITCH_ONLY)
    return()
endif()
_pangu_configure_layout_fixture(A 17 Debug)
_pangu_build_layout_fixture(A)
file(READ "${PANGU_TEST_ROOT}/ABuild/Layout.txt" layout)
_pangu_require_test_text("${layout}" "engine_binary=${engineBuild}/Targets/Source/Runtime/Foundation")
_pangu_require_test_text("${layout}" "engine_file=${engineBuild}/Bin/")
_pangu_require_test_text("${layout}" "${engineBuild}/Generated/Export")
_pangu_require_test_text("${layout}" "engine_owner=engine")
_pangu_require_test_text("${layout}" "plugin_binary=${engineBuild}/Targets/Plugins/Value")
_pangu_require_test_text("${layout}" "plugin_file=${engineBuild}/Bin/")
_pangu_require_test_text("${layout}" "plugin_owner=engine")
_pangu_require_test_text("${layout}" "owned_file=${engineBuild}/Lib/Debug/")
_pangu_require_test_text("${layout}" "project_binary=${PANGU_TEST_ROOT}/ABuild/Targets/Plugins/Value")
_pangu_require_test_text("${layout}" "project_file=${PANGU_TEST_ROOT}/ABuild/Bin/")
_pangu_require_test_text("${layout}" "project_owner=project")
_pangu_check_layout_object_root(A)
_pangu_run_layout_fixture(A 17)
if(CMAKE_HOST_WIN32)
    set(suffix .exe)
endif()
set(tool "${PANGU_TEST_ROOT}/ABuild/BuildTools/PanguBuildTool/PanguBuildTool${suffix}")
foreach(bad IN ITEMS relative "${PANGU_ENGINE_SOURCE_DIR}" "${PANGU_TEST_ROOT}/A")
    _pangu_run_project_command(
        FAIL
        "build root"
        "${tool}"
        layout
        --engine-source
        "${PANGU_ENGINE_SOURCE_DIR}"
        --engine-build
        "${bad}"
        --project-source
        "${PANGU_TEST_ROOT}/A"
        --project-build
        "${PANGU_TEST_ROOT}/ABuild"
        --mode
        Modular
        --config
        Debug
    )
endforeach()
file(WRITE "${PANGU_TEST_ROOT}/NotDirectory" "not a build directory\n")
_pangu_run_project_command(
    FAIL
    "build root"
    "${tool}"
    layout
    --engine-source
    "${PANGU_ENGINE_SOURCE_DIR}"
    --engine-build
    "${engineBuild}"
    --project-source
    "${PANGU_TEST_ROOT}/A"
    --project-build
    "${PANGU_TEST_ROOT}/NotDirectory"
    --mode
    Modular
    --config
    Debug
)
_pangu_run_project_command(
    PASS
    ""
    "${tool}"
    layout
    --engine-source
    "${PANGU_ENGINE_SOURCE_DIR}"
    --engine-build
    "${engineBuild}"
    --project-source
    "${PANGU_TEST_ROOT}/A"
    --project-build
    "${PANGU_TEST_ROOT}/ABuild"
    --mode
    Modular
    --config
    Debug
)
string(JSON version GET "${commandStdout}" version)
if(NOT version EQUAL 1)
    message(FATAL_ERROR "Build layout JSON version must be 1")
endif()
_pangu_configure_layout_fixture(A 17 Debug Monolithic)
_pangu_build_layout_fixture(A)
_pangu_run_layout_fixture(A 17)
file(GLOB libraries "${engineBuild}/Lib/*")
if(NOT libraries)
    message(FATAL_ERROR "Engine static library must reside under EngineBuildRoot/Lib")
endif()
message(STATUS "Actual Engine/Project binary, object, library, export and Plugin ownership passed")
