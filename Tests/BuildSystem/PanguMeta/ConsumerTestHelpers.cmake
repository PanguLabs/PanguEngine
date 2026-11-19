include("${PANGU_ENGINE_SOURCE_DIR}/Tests/BuildSystem/ProjectIntegration/ProjectTestHelpers.cmake")
set(source "${PANGU_TEST_ROOT}/Consumer")
set(binary "${PANGU_TEST_ROOT}/Build")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/Fixtures/Consumer/" DESTINATION "${source}")
set(generated "${binary}/Generated/PanguMeta/Release/Owner")

function(_pangu_configure_meta_consumer expected diagnostic)
    _pangu_run_project_command(
        "${expected}"
        "${diagnostic}"
        "${CMAKE_COMMAND}"
        -S
        "${source}"
        -B
        "${binary}"
        -G
        Ninja
        "${compilerArgs}"
        -DCMAKE_BUILD_TYPE=Release
        "-DPANGU_ENGINE_SOURCE_DIR=${PANGU_ENGINE_SOURCE_DIR}"
        "-DPANGU_META_TOOL_SOURCE_DIR=${PANGU_ENGINE_SOURCE_DIR}/BuildTools/PanguMetaTool"
        "-DPANGU_META_HOST_BUILD_ROOT=${PANGU_META_HOST_BUILD_ROOT}"
        -DFIXTURE_VALUE=1
        ${metaArguments}
        ${ARGN}
    )
endfunction()

function(_pangu_build_meta_consumer expected diagnostic)
    _pangu_run_project_command(
        "${expected}"
        "${diagnostic}"
        "${CMAKE_COMMAND}"
        --build
        "${binary}"
        --parallel
        "${PANGU_TEST_BUILD_JOBS}"
        ${ARGN}
    )
    set(commandOutput "${commandOutput}" PARENT_SCOPE)
endfunction()

function(_pangu_snapshot_meta_outputs result)
    file(READ "${generated}/Meta.outputs.json" index)
    string(JSON count LENGTH "${index}" outputs)
    set(snapshot "")
    if(count GREATER 0)
        math(EXPR last "${count}-1")
        foreach(i RANGE 0 ${last})
            string(
                JSON relative
                GET "${index}"
                outputs
                ${i}
            )
            file(SHA256 "${generated}/${relative}" hash)
            file(
                TIMESTAMP "${generated}/${relative}"
                time
                "%s.%f"
            )
            string(APPEND snapshot "${relative}:${hash}:${time}\n")
        endforeach()
    endif()
    set(${result} "${snapshot}" PARENT_SCOPE)
endfunction()
