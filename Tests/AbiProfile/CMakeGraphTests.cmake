cmake_minimum_required(VERSION 4.0)

foreach(_name IN ITEMS PANGU_ABI_ROOT PANGU_ABI_BINARY PANGU_ABI_COMPILER
                       PANGU_ABI_CMAKE PANGU_ABI_RUNNER)
    if(NOT DEFINED ${_name})
        message(FATAL_ERROR "Real CMake graph test requires ${_name}")
    endif()
endforeach()

function(run_fixture config imported)
    set(_id "${config}")
    if(imported)
        string(APPEND _id "Imported")
    endif()
    set(_dir "${PANGU_ABI_BINARY}/RealCMakeGraphFixture/${_id}")
    file(MAKE_DIRECTORY "${_dir}/.cmake/api/v1/query")
    file(WRITE "${_dir}/.cmake/api/v1/query/codemodel-v2" "")

    execute_process(
        COMMAND "${PANGU_ABI_CMAKE}" -S "${PANGU_ABI_ROOT}/Tests/AbiProfile/Fixtures"
            -B "${_dir}" -G Ninja
            "-DCMAKE_CXX_COMPILER=${PANGU_ABI_COMPILER}"
            "-DCMAKE_BUILD_TYPE=${config}"
            "-DPANGU_ABI_SOURCE_ROOT=${PANGU_ABI_ROOT}"
            "-DPANGU_ABI_LINK_IMPORTED=${imported}"
        RESULT_VARIABLE _configure OUTPUT_VARIABLE _out ERROR_VARIABLE _err
    )
    if(NOT _configure EQUAL 0)
        message(FATAL_ERROR "ABI graph ${_id} configure failed:\n${_out}\n${_err}")
    endif()
    execute_process(
        COMMAND "${PANGU_ABI_CMAKE}" --build "${_dir}"
        RESULT_VARIABLE _build OUTPUT_VARIABLE _out ERROR_VARIABLE _err
    )
    if(NOT _build EQUAL 0)
        message(FATAL_ERROR "ABI graph ${_id} build failed:\n${_out}\n${_err}")
    endif()

    set(_type valid)
    if(imported)
        set(_type imported)
    endif()
    execute_process(
        COMMAND "${PANGU_ABI_RUNNER}" "${_dir}/graph-${config}.json" "${config}" "${_type}"
        RESULT_VARIABLE _verified OUTPUT_VARIABLE _out ERROR_VARIABLE _err
    )
    if(NOT _verified EQUAL 0)
        message(FATAL_ERROR "ABI graph ${_id} C++ verification failed:\n${_out}\n${_err}")
    endif()

    # File API confirms the actual generated CMake build targets; its build
    # dependency array is NOT the complete link closure, notably INTERFACE and
    # IMPORTED identities are absent. Never infer missing links as absent.
    file(GLOB _indexes "${_dir}/.cmake/api/v1/reply/index-*.json")
    list(LENGTH _indexes _index_count)
    if(_index_count LESS 1)
        message(FATAL_ERROR "CMake File API did not emit a codemodel index")
    endif()
    list(GET _indexes 0 _index)
    file(READ "${_index}" _content)
    string(JSON _code_model_file GET "${_content}" reply codemodel-v2 jsonFile)
    file(READ "${_dir}/.cmake/api/v1/reply/${_code_model_file}" _code_model)
    string(JSON _configuration_count LENGTH "${_code_model}" configurations)
    if(NOT _configuration_count EQUAL 1)
        message(FATAL_ERROR "CMake File API configuration count unexpectedly changed")
    endif()
    string(JSON _target_count LENGTH "${_code_model}" configurations 0 targets)
    if(_target_count LESS 3)
        message(FATAL_ERROR "CMake File API omitted actual build targets")
    endif()
    set(_build_targets "")
    math(EXPR _end "${_target_count} - 1")
    foreach(_index RANGE 0 ${_end})
        string(JSON _target GET "${_code_model}" configurations 0 targets ${_index} name)
        list(APPEND _build_targets "${_target}")
    endforeach()
    foreach(_required IN ITEMS Root TechA TechB)
        if(NOT _required IN_LIST _build_targets)
            message(FATAL_ERROR "Missing File API compiled target: ${_required}")
        endif()
    endforeach()
    foreach(_absent IN ITEMS Options Prebuilt AliasA)
        if(_absent IN_LIST _build_targets)
            message(FATAL_ERROR "Unexpected File API build target: ${_absent}")
        endif()
    endforeach()
endfunction()

# CMake accepts both an archive SCC propagated through INTERFACE targets and
# a cycle consisting entirely of INTERFACE propagation. Verify their exported
# graphs without manufacturing build targets or Profiles for the headers.
foreach(_mode IN ITEMS bridge managed-bridge interface-only)
    set(_cycle_dir "${PANGU_ABI_BINARY}/RealCMakeGraphFixture/${_mode}")
    execute_process(
        COMMAND "${PANGU_ABI_CMAKE}" -S "${PANGU_ABI_ROOT}/Tests/AbiProfile/Fixtures"
            -B "${_cycle_dir}" -G Ninja
            "-DCMAKE_CXX_COMPILER=${PANGU_ABI_COMPILER}"
            "-DCMAKE_BUILD_TYPE=Release"
            "-DPANGU_ABI_SOURCE_ROOT=${PANGU_ABI_ROOT}"
            "-DPANGU_ABI_CYCLE_MODE=${_mode}"
            -DPANGU_ABI_LINK_WITNESS=ON
        RESULT_VARIABLE _configure OUTPUT_VARIABLE _out ERROR_VARIABLE _err
    )
    if(NOT _configure EQUAL 0)
        message(FATAL_ERROR "INTERFACE cycle ${_mode} configure failed:\n${_out}\n${_err}")
    endif()
    execute_process(
        COMMAND "${PANGU_ABI_CMAKE}" --build "${_cycle_dir}" --target AbiLinkWitness
        RESULT_VARIABLE _build OUTPUT_VARIABLE _out ERROR_VARIABLE _err
    )
    if(NOT _build EQUAL 0)
        message(FATAL_ERROR "INTERFACE cycle ${_mode} native host link failed:\n${_out}\n${_err}")
    endif()
    execute_process(
        COMMAND "${PANGU_ABI_RUNNER}" "${_cycle_dir}/graph-Release.json" Release "${_mode}"
        RESULT_VARIABLE _verified OUTPUT_VARIABLE _out ERROR_VARIABLE _err
    )
    if(NOT _verified EQUAL 0)
        message(FATAL_ERROR "INTERFACE cycle ${_mode} C++ verification failed:\n${_out}\n${_err}")
    endif()
endforeach()

run_fixture(Release FALSE)
run_fixture(Debug FALSE)
run_fixture(Release TRUE)

# INTERFACE_SOURCES can inject C++ translation units into consumers, even
# when attached to a normal STATIC target. CMake exports, C++ must reject.
foreach(_kind IN ITEMS INTERFACE STATIC)
    set(_source_dir "${PANGU_ABI_BINARY}/RealCMakeGraphFixture/${_kind}Sources")
    if(_kind STREQUAL "INTERFACE")
        set(_source_option -DPANGU_ABI_INTERFACE_SOURCE=ON)
    else()
        set(_source_option -DPANGU_ABI_STATIC_INTERFACE_SOURCE=ON)
    endif()
    execute_process(
        COMMAND "${PANGU_ABI_CMAKE}" -S "${PANGU_ABI_ROOT}/Tests/AbiProfile/Fixtures"
            -B "${_source_dir}" -G Ninja
            "-DCMAKE_CXX_COMPILER=${PANGU_ABI_COMPILER}"
            "-DCMAKE_BUILD_TYPE=Release"
            "-DPANGU_ABI_SOURCE_ROOT=${PANGU_ABI_ROOT}"
            "${_source_option}"
        RESULT_VARIABLE _source_result OUTPUT_VARIABLE _source_out ERROR_VARIABLE _source_err
    )
    if(NOT _source_result EQUAL 0)
        message(FATAL_ERROR "${_kind} source graph export failed:\n${_source_out}\n${_source_err}")
    endif()
    execute_process(
        COMMAND "${PANGU_ABI_RUNNER}" "${_source_dir}/graph-Release.json" Release invalid
        RESULT_VARIABLE _verified OUTPUT_VARIABLE _out ERROR_VARIABLE _err
    )
    if(NOT _verified EQUAL 0)
        message(FATAL_ERROR "${_kind} transitive source escaped C++ verification:\n${_out}\n${_err}")
    endif()
endforeach()
# A generator expression contributes a source only in the selected configuration.
foreach(_config IN ITEMS Release Debug)
    set(_source_dir "${PANGU_ABI_BINARY}/RealCMakeGraphFixture/Conditional${_config}")
    execute_process(
        COMMAND "${PANGU_ABI_CMAKE}" -S "${PANGU_ABI_ROOT}/Tests/AbiProfile/Fixtures"
            -B "${_source_dir}" -G Ninja
            "-DCMAKE_CXX_COMPILER=${PANGU_ABI_COMPILER}"
            "-DCMAKE_BUILD_TYPE=${_config}"
            "-DPANGU_ABI_SOURCE_ROOT=${PANGU_ABI_ROOT}"
            -DPANGU_ABI_CONDITIONAL_INTERFACE_SOURCE=ON
        RESULT_VARIABLE _source_result OUTPUT_VARIABLE _source_out ERROR_VARIABLE _source_err
    )
    if(NOT _source_result EQUAL 0)
        message(FATAL_ERROR "Conditional source ${_config} export failed:\n${_source_out}\n${_source_err}")
    endif()
    set(_expected valid)
    if(_config STREQUAL "Debug")
        set(_expected invalid)
    endif()
    execute_process(
        COMMAND "${PANGU_ABI_RUNNER}" "${_source_dir}/graph-${_config}.json" "${_config}" "${_expected}"
        RESULT_VARIABLE _verified OUTPUT_VARIABLE _out ERROR_VARIABLE _err
    )
    if(NOT _verified EQUAL 0)
        message(FATAL_ERROR "Conditional source ${_config} failed C++ verification:\n${_out}\n${_err}")
    endif()
endforeach()
# The final host provides symbols intentionally unresolved in technical
# archives. A->B->A requires archive resolution across the cycle; the
# native linker may use repeated archives or implicit rescanning. Never run
# this witness binary; its successful final link is the contract.
set(_witness_dir "${PANGU_ABI_BINARY}/RealCMakeGraphFixture/LinkWitness")
execute_process(
    COMMAND "${PANGU_ABI_CMAKE}" -S "${PANGU_ABI_ROOT}/Tests/AbiProfile/Fixtures"
        -B "${_witness_dir}" -G Ninja
        "-DCMAKE_CXX_COMPILER=${PANGU_ABI_COMPILER}"
        "-DCMAKE_BUILD_TYPE=Release"
        "-DPANGU_ABI_SOURCE_ROOT=${PANGU_ABI_ROOT}"
        -DPANGU_ABI_LINK_WITNESS=ON
    RESULT_VARIABLE _witness_configure OUTPUT_VARIABLE _out ERROR_VARIABLE _err
)
if(NOT _witness_configure EQUAL 0)
    message(FATAL_ERROR "STATIC SCC host witness configuration failed:\n${_out}\n${_err}")
endif()
execute_process(
    COMMAND "${PANGU_ABI_CMAKE}" --build "${_witness_dir}" --target AbiLinkWitness
    RESULT_VARIABLE _witness_build OUTPUT_VARIABLE _out ERROR_VARIABLE _err
)
if(NOT _witness_build EQUAL 0)
    message(FATAL_ERROR "STATIC SCC host witness link failed:\n${_out}\n${_err}")
endif()
# GNU-style archives require the second pass through A after B introduced a
# new unresolved A symbol. On Apple ld and Windows link.exe/lld-link, archive
# searching may be implicit, so successful native linking is the requirement.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    file(READ "${_witness_dir}/build.ninja" _build_rules)
    string(REGEX MATCH "LINK_LIBRARIES = [^\r\n]*" _witness_libraries "${_build_rules}")
    string(REGEX MATCHALL "libTechA\\.a" _archive_a "${_witness_libraries}")
    string(REGEX MATCHALL "libTechB\\.a" _archive_b "${_witness_libraries}")
    list(LENGTH _archive_a _a_passes)
    list(LENGTH _archive_b _b_passes)
    if(_a_passes LESS 2 OR _b_passes LESS 2)
        message(FATAL_ERROR
            "GNU archive SCC links must retain repeated archives; got: ${_witness_libraries}")
    endif()
endif()
message(STATUS "CMake4 graph and real technical STATIC SCC host-link witness passed")
