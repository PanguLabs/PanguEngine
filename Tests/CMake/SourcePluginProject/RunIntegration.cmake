cmake_minimum_required(VERSION 4.0)

if(NOT DEFINED PANGU_ENGINE_SOURCE_DIR OR NOT DEFINED PANGU_TEST_ROOT)
    message(FATAL_ERROR "SourcePluginProject requires PANGU_ENGINE_SOURCE_DIR and an isolated PANGU_TEST_ROOT")
endif()
if(NOT DEFINED PANGU_PROJECT_TEST_STAGE)
    set(PANGU_PROJECT_TEST_STAGE All)
endif()
if(NOT PANGU_PROJECT_TEST_STAGE MATCHES "^(All|Base|Engine)$")
    message(FATAL_ERROR "SourcePluginProject requires stage All, Base or Engine")
endif()
# 正式场景各用独立冷根；手动 All 保持原有完整入口。
if(NOT PANGU_PROJECT_TEST_STAGE STREQUAL "All")
    set(PANGU_TEST_ROOT "${PANGU_TEST_ROOT}/${PANGU_PROJECT_TEST_STAGE}")
endif()
set(projectFixture "${PANGU_ENGINE_SOURCE_DIR}/Tests/CMake/SourcePluginProject/MyGame")
if(NOT EXISTS "${projectFixture}/CMakeLists.txt")
    message(FATAL_ERROR "MyGame fixture source is missing: ${projectFixture}")
endif()
file(MAKE_DIRECTORY "${PANGU_TEST_ROOT}")
file(COPY "${projectFixture}" DESTINATION "${PANGU_TEST_ROOT}")
set(source "${PANGU_TEST_ROOT}/MyGame")
if(NOT DEFINED PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE)
    set(PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE OFF)
endif()
execute_process(COMMAND git -C "${PANGU_ENGINE_SOURCE_DIR}" rev-parse HEAD
    RESULT_VARIABLE revResult OUTPUT_VARIABLE engineHead OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT revResult EQUAL 0)
    message(FATAL_ERROR "Cannot resolve current Engine HEAD for MyGame fixture")
endif()
file(WRITE "${source}/Engine.commit" "${engineHead}\n")
message(STATUS "Project Engine lock: ${engineHead}; local override=${PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE}")

if(CMAKE_HOST_WIN32)
    set(compiler "$ENV{LLVM_ROOT}/bin/clang++.exe")
    set(compilerArgs "-DCMAKE_CXX_COMPILER:FILEPATH=${compiler}")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(compilerArgs "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${PANGU_ENGINE_SOURCE_DIR}/cmake/Platform/Linux/Toolchain.cmake")
else()
    set(compilerArgs "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${PANGU_ENGINE_SOURCE_DIR}/cmake/Platform/macOS/Toolchain.cmake")
endif()

function(ConfigureProject name testing engineTests)
    set(binary "${PANGU_TEST_ROOT}/${name}")
    set(servicePlugins "")
    if(name STREQUAL "ServiceHosts")
        set(servicePlugins "pangu.physics;mygame.gameplay;mygame.editor-extras")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${binary}" -G Ninja
            "${compilerArgs}"
            -DCMAKE_BUILD_TYPE=Release
            "-DPANGU_ENGINE_BUILD_ROOT:PATH=${binary}/EngineBuild"
            "-DPANGU_ENGINE_SOURCE_DIR:PATH=${PANGU_ENGINE_SOURCE_DIR}"
            "-DPANGU_META_SDK_CONFIG=${PANGU_META_SDK_CONFIG}"
            "-DPANGU_META_HOST_BUILD_ROOT=${PANGU_META_HOST_BUILD_ROOT}"
            "-DBUILD_TESTING:BOOL=${testing}"
            "-DPANGU_BUILD_ENGINE_TESTS:BOOL=${engineTests}"
            "-DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE:BOOL=${PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE}"
            "-DPANGU_TEST_BUILD_JOBS=${PANGU_TEST_BUILD_JOBS}"
            -DPANGU_FIXTURE_UNCLASSIFIED_ENGINE_TEST=OFF
            "-DPANGU_ENABLED_PLUGINS=${servicePlugins}"
            ${ARGN}
        RESULT_VARIABLE rc OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "MyGame configure ${name} failed: ${rc}\n${output}\n${error}")
    endif()
    file(READ "${binary}/CMakeCache.txt" cache)
    if(NOT cache MATCHES "CMAKE_PROJECT_NAME:STATIC=MyGame")
        message(FATAL_ERROR "MyGame must own top-level CMake project: ${name}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${binary}" --parallel "${PANGU_TEST_BUILD_JOBS}" --target MyGameProbe
        RESULT_VARIABLE rc OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "MyGameProbe build ${name} failed: ${rc}\n${output}\n${error}")
    endif()
    if(engineTests)
        # PRE_TEST 发现必须基于已构建的程序，不能接受 NOT_BUILT 占位条目。
        # 聚合目标包含嵌套工具准备，外层串行，工具内部消费同一处理器预算。
        execute_process(COMMAND "${CMAKE_COMMAND}" --build "${binary}" --parallel 1 --target PanguTestsFull
            RESULT_VARIABLE rc OUTPUT_VARIABLE output ERROR_VARIABLE error)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "Engine unit tests build ${name} failed: ${rc}\n${output}\n${error}")
        endif()
        execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${binary}"
            --show-only=json-v1
            RESULT_VARIABLE rc OUTPUT_VARIABLE testsJson ERROR_VARIABLE errors)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "Engine unit tests discovery ${name} failed: ${rc}\n${errors}")
        endif()
        set("${name}_CTEST_JSON" "${testsJson}" PARENT_SCOPE)
    endif()
    execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${binary}" --show-only=json-v1
        RESULT_VARIABLE rc OUTPUT_VARIABLE listed ERROR_VARIABLE errors)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "MyGame CTest query failed: ${rc}\n${errors}")
    endif()
    set("${name}_CTEST_LIST" "${listed}" PARENT_SCOPE)
endfunction()

function(CheckEmbeddedClassification name expected)
    execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${PANGU_TEST_ROOT}/EngineOptIn"
        -R "^TestingScriptClassification$" --no-tests=error --output-on-failure
        --parallel "${PANGU_TEST_BUILD_JOBS}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    file(WRITE "${PANGU_TEST_ROOT}/EngineOptIn-classification-${name}.log"
        "command=${CMAKE_CTEST_COMMAND} --test-dir ${PANGU_TEST_ROOT}/EngineOptIn -R ^TestingScriptClassification$ --no-tests=error --output-on-failure --parallel ${PANGU_TEST_BUILD_JOBS}\nexit=${status}\n${output}\n${error}")
    if(expected STREQUAL "PASS")
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "Embedded Engine classification failed: ${status}\n${output}\n${error}")
        endif()
    else()
        string(REGEX REPLACE "[ \t\r\n]+" " " diagnostic "${output}\n${error}")
        string(FIND "${diagnostic}"
            "UnclassifiedEngineProbe: an existing Engine regression is missing classification/full ownership:" found)
        if(status EQUAL 0 OR found LESS 0)
            message(FATAL_ERROR "Unclassified Engine test was not rejected: ${status}\n${output}\n${error}")
        endif()
    endif()
    message(STATUS "Embedded classification ${name}: expected ${expected}, exit=${status}")
endfunction()

function(ConfigureUnclassifiedEngineCase enabled)
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${PANGU_TEST_ROOT}/EngineOptIn"
        "-DPANGU_FIXTURE_UNCLASSIFIED_ENGINE_TEST=${enabled}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    file(WRITE "${PANGU_TEST_ROOT}/EngineOptIn-unclassified-${enabled}.log"
        "exit=${status}\n${output}\n${error}")
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Cannot configure unclassified Engine test (${enabled}): ${status}\n${output}\n${error}")
    endif()
endfunction()

if(NOT PANGU_PROJECT_TEST_STAGE STREQUAL "Engine")
    ConfigureProject(Enabled ON OFF -DPANGU_LINK_MODE=Monolithic)
    string(JSON enabledCount LENGTH "${Enabled_CTEST_LIST}" tests)
    set(projectProbeFound OFF)
    if(enabledCount GREATER 0)
        math(EXPR lastEnabled "${enabledCount} - 1")
        foreach(index RANGE 0 ${lastEnabled})
            string(JSON name GET "${Enabled_CTEST_LIST}" tests ${index} name)
            if(NOT name STREQUAL "MyGameProbe")
                message(FATAL_ERROR "Default Project registered an unexpected Engine test: ${name}")
            endif()
            set(projectProbeFound ON)
        endforeach()
    endif()
    if(NOT projectProbeFound)
        message(FATAL_ERROR "Default Project omitted MyGameProbe")
    endif()
    execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${PANGU_TEST_ROOT}/Enabled"
        -R "^MyGameProbe$" --output-on-failure RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "MyGameProbe standalone CTest failed")
    endif()

    ConfigureProject(Disabled OFF OFF -DPANGU_LINK_MODE=Modular)
    string(JSON disabledCount LENGTH "${Disabled_CTEST_LIST}" tests)
    if(NOT disabledCount EQUAL 0)
        message(FATAL_ERROR "Project BUILD_TESTING=OFF must register no tests:\n${Disabled_CTEST_LIST}")
    endif()

    file(MAKE_DIRECTORY "${PANGU_TEST_ROOT}/FixtureEngine/Plugins")
    file(COPY "${CMAKE_CURRENT_LIST_DIR}/FixtureEnginePlugins/"
        DESTINATION "${PANGU_TEST_ROOT}/FixtureEngine/Plugins")
    ConfigureProject(ServiceHosts OFF OFF -DPANGU_LINK_MODE=Monolithic
        -DPANGU_FIXTURE_BUILD_HOSTS=ON
        -DPANGU_BUILD_SAMPLES=ON
        "-DPANGU_FIXTURE_ENGINE_ROOT=${PANGU_TEST_ROOT}/FixtureEngine")
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${PANGU_TEST_ROOT}/ServiceHosts"
        --parallel "${PANGU_TEST_BUILD_JOBS}" --target MyGame MyEditor PanguMinimalHost RESULT_VARIABLE hostBuild)
    if(NOT hostBuild EQUAL 0)
        message(FATAL_ERROR "Service hosts build failed")
    endif()
    if(CMAKE_HOST_WIN32)
        set(hostSuffix .exe)
    endif()
    execute_process(COMMAND "${PANGU_TEST_ROOT}/ServiceHosts/EngineBuild/Bin/PanguMinimalHost${hostSuffix}"
        RESULT_VARIABLE sampleResult OUTPUT_VARIABLE sampleOutput ERROR_VARIABLE sampleError)
    if(NOT sampleResult EQUAL 0 OR NOT sampleOutput MATCHES "Counter Context values: 7 -> 8; isolated=7")
        message(FATAL_ERROR "Monolithic sample failed: ${sampleResult}\n${sampleOutput}\n${sampleError}")
    endif()
    foreach(host IN ITEMS MyGame MyEditor)
        execute_process(COMMAND "${PANGU_TEST_ROOT}/ServiceHosts/Bin/${host}${hostSuffix}"
            RESULT_VARIABLE hostResult OUTPUT_VARIABLE hostOutput ERROR_VARIABLE hostError)
        if(host STREQUAL "MyGame")
            set(expected "Gameplay Context values: 7 -> 8; isolated=7")
        else()
            set(expected "EditorExtras Context values: 11 -> 12; isolated=11")
        endif()
        string(FIND "${hostOutput}" "${expected}" found)
        if(NOT hostResult EQUAL 0 OR found LESS 0)
            message(FATAL_ERROR "Missing service isolation result '${expected}': ${hostResult}\n${hostOutput}\n${hostError}")
        endif()
    endforeach()

endif()

if(NOT PANGU_PROJECT_TEST_STAGE STREQUAL "Base")
    ConfigureProject(EngineOptIn ON ON)
    CheckEmbeddedClassification(valid PASS)
    string(JSON engineTestCount LENGTH "${EngineOptIn_CTEST_JSON}" tests)
    if(engineTestCount EQUAL 0 OR
       NOT EngineOptIn_CTEST_LIST MATCHES "PanguModuleUnitTests[.]ModuleIdentity[.]ParseAndFormat")
        message(FATAL_ERROR "Project explicit PANGU_BUILD_ENGINE_TESTS=ON must discover actual Engine unit tests")
    endif()
    math(EXPR lastEngineTest "${engineTestCount} - 1")
    foreach(index RANGE 0 ${lastEngineTest})
        string(JSON testName GET "${EngineOptIn_CTEST_JSON}" tests ${index} name)
        if(testName STREQUAL "MyGameProbe")
            continue()
        endif()
        if(testName MATCHES "NOT_BUILT")
            message(FATAL_ERROR "Engine unit test discovery returned an invalid entry: ${testName}")
        endif()
        string(JSON propertyCount LENGTH "${EngineOptIn_CTEST_JSON}" tests ${index} properties)
        math(EXPR lastProperty "${propertyCount} - 1")
        set(hasEngineOwner OFF)
        foreach(propertyIndex RANGE 0 ${lastProperty})
            string(JSON property GET "${EngineOptIn_CTEST_JSON}" tests ${index} properties ${propertyIndex})
            string(JSON propertyName GET "${property}" name)
            if(propertyName STREQUAL "LABELS")
                string(JSON labels GET "${property}" value)
                if(labels MATCHES "\"owner[.]engine\"" AND NOT labels MATCHES "\"owner[.]project\"")
                    set(hasEngineOwner ON)
                endif()
            endif()
        endforeach()
        if(NOT hasEngineOwner)
            message(FATAL_ERROR "Engine unit test must retain owner.engine when embedded in Project: ${testName}")
        endif()
    endforeach()

    # 同次运行生成反例并恢复；不依赖预存 fixture，也不递归执行 Engine 系统测试。
    ConfigureUnclassifiedEngineCase(ON)
    CheckEmbeddedClassification(unclassified-engine FAIL)
    ConfigureUnclassifiedEngineCase(OFF)
    CheckEmbeddedClassification(restored PASS)

endif()

if(NOT PANGU_PROJECT_TEST_STAGE STREQUAL "Engine")
    file(WRITE "${source}/Engine.commit" "invalid-new-lock\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${PANGU_TEST_ROOT}/Enabled" --parallel "${PANGU_TEST_BUILD_JOBS}" --target MyGameProbe
        RESULT_VARIABLE lockStatus OUTPUT_VARIABLE lockOutput ERROR_VARIABLE lockError)
    if(lockStatus EQUAL 0 OR NOT "${lockOutput}\n${lockError}" MATCHES "Engine[.]commit")
        message(FATAL_ERROR "Changing only Engine.commit must trigger lock revalidation: ${lockStatus}\n${lockOutput}\n${lockError}")
    endif()
    file(WRITE "${source}/Engine.commit" "${engineHead}\n")
endif()
message(STATUS "SourcePluginProject ${PANGU_PROJECT_TEST_STAGE}: selected ownership, lifecycle and lock checks passed")
