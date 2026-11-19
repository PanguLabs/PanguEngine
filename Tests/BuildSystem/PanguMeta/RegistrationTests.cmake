cmake_minimum_required(VERSION 4.0)
include("${CMAKE_CURRENT_LIST_DIR}/ConsumerTestHelpers.cmake")
_pangu_configure_meta_consumer(PASS "")
file(READ "${binary}/Registration.txt" registration)
_pangu_require_test_text("${registration}" "Public/Actor.h")
_pangu_require_test_text("${registration}" "Actor.gen.h")
_pangu_require_test_text("${registration}" "PG_META_HAS_DESCRIPTION=0")
_pangu_require_test_text("${registration}" "PanguMetaUsage_Owner")
_pangu_reject_test_text("${registration}" "links=Owner")
if(EXISTS "${generated}/Public/Actor.gen.h")
    message(FATAL_ERROR "Generation must occur at build time")
endif()
_pangu_build_meta_consumer(PASS "PanguMetaTool generate")
if(NOT EXISTS "${generated}/Private/ModuleQuery.gen.cpp")
    message(FATAL_ERROR "Scan must merge its native Query into the same generation batch")
endif()
_pangu_configure_meta_consumer(
    PASS
    ""
    -DDESCRIPTION=ON
    -DIDENTITY_TEST=ON
)
_pangu_build_meta_consumer(PASS "PanguMetaTool generate")
file(READ "${generated}/Meta.request.json" request)
_pangu_require_test_text("${request}" "\"typed_access\" : true")
_pangu_require_test_text("${request}" "\"description\" : true")
if(NOT EXISTS "${generated}/Private/ModuleMeta.gen.cpp")
    message(FATAL_ERROR "Module description union missing")
endif()
_pangu_run_project_command(
    PASS
    "100% tests passed"
    "${CMAKE_CTEST_COMMAND}"
    --test-dir
    "${binary}"
    -R
    "^MetadataDefinitionUsesSharedIdentity$"
    --no-tests=error
    --output-on-failure
)
foreach(case IN ITEMS DUPLICATE_OWNER PRIVATE_CONSUME UNKNOWN_CONSUME MISSING_CONSUME)
    _pangu_configure_meta_consumer(FAIL "" "-D${case}=ON")
    _pangu_configure_meta_consumer(PASS "" "-D${case}=OFF")
endforeach()
_pangu_configure_meta_consumer(PASS "" -DEXTRA_INPUTS=ON)
_pangu_build_meta_consumer(PASS "PanguMetaTool generate")
file(READ "${generated}/Meta.request.json" request)
string(JSON length LENGTH "${request}" outputs)
if(NOT length EQUAL 7)
    message(FATAL_ERROR "Expected Actor both + Unicode description-only + Module, and no check-only output")
endif()
foreach(
    relative
    IN
    ITEMS
        Public/Actor.gen.h
        Private/Actor.gen.cpp
        "Public/目录/描述.gen.h"
        "Private/目录/描述.gen.cpp"
        Private/ModuleMeta.gen.h
        Private/ModuleMeta.gen.cpp
)
    if(NOT EXISTS "${generated}/${relative}")
        message(FATAL_ERROR "Missing precisely registered output: ${relative}")
    endif()
endforeach()
file(READ "${generated}/Public/目录/描述.gen.h" description)
_pangu_require_test_text("${description}" "HasTypedAccess = false")
_pangu_require_test_text("${description}" "HasDescription = true")
# Query-only 也是实际任务，必须有可用的宿主工具。
set(binary "${PANGU_TEST_ROOT}/QueryOnlyBuild")
_pangu_configure_meta_consumer(
    FAIL
    "toolchain_error"
    -DNO_INPUT=ON
    "-DPANGU_META_TOOL_SOURCE_DIR=${PANGU_TEST_ROOT}/MissingTool"
)
_pangu_configure_meta_consumer(PASS "" -DNO_INPUT=ON)
_pangu_build_meta_consumer(PASS "PanguMetaTool generate")
# 无 scan、静态 Module 登记、未启用 Catalog 时不启动工具。
set(binary "${PANGU_TEST_ROOT}/CatalogOnlyBuild")
_pangu_configure_meta_consumer(
    FAIL
    "toolchain_error"
    -DNO_INPUT=ON
    -DCATALOG_ONLY=ON
    "-DPANGU_META_TOOL_SOURCE_DIR=${PANGU_TEST_ROOT}/MissingTool"
)
_pangu_configure_meta_consumer(
    PASS
    ""
    -DNO_INPUT=ON
    -DCATALOG_ONLY=ON
)
_pangu_build_meta_consumer(PASS "PanguMetaTool generate Consumer")
set(binary "${PANGU_TEST_ROOT}/NoTaskBuild")
_pangu_configure_meta_consumer(
    PASS
    ""
    -DNO_INPUT=ON
    -DNO_TASK=ON
    "-DPANGU_META_TOOL_SOURCE_DIR=${PANGU_TEST_ROOT}/MissingTool"
    "-DPANGU_META_HOST_BUILD_ROOT=${PANGU_TEST_ROOT}/Unprepared"
)
_pangu_build_meta_consumer(PASS "")
_pangu_reject_test_text("${commandOutput}" "PanguMetaTool")
if(EXISTS "${PANGU_TEST_ROOT}/Unprepared")
    message(FATAL_ERROR "Registration without active tasks bootstrapped a host tool")
endif()
_pangu_run_project_command(
    FAIL
    "Meta requires Ninja single-config"
    "${CMAKE_COMMAND}"
    -S
    "${source}"
    -B
    "${PANGU_TEST_ROOT}/MultiConfig"
    -G
    "Ninja Multi-Config"
    "${compilerArgs}"
    "-DPANGU_ENGINE_SOURCE_DIR=${PANGU_ENGINE_SOURCE_DIR}"
    -DFIXTURE_VALUE=1
    -DNO_INPUT=ON
)
