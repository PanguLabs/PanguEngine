cmake_minimum_required(VERSION 4.0)
include("${CMAKE_CURRENT_LIST_DIR}/Helpers.cmake")
set(build "${PANGU_TEST_ROOT}/Build")
_pangu_run_link_mode_command(
    PASS
    ""
    "${CMAKE_COMMAND}"
    -G
    Ninja
    -S
    "${CMAKE_CURRENT_LIST_DIR}/RuntimeDeployment"
    -B
    "${build}"
    ${compilerArgs}
    -DCMAKE_BUILD_TYPE=Release
    "-DPANGU_SOURCE_ROOT=${PANGU_SOURCE_ROOT}"
)
_pangu_run_link_mode_command(
    PASS
    ""
    "${CMAKE_COMMAND}"
    --build
    "${build}"
    --parallel
    "${PANGU_TEST_BUILD_JOBS}"
    --target
    App
    Unrelated
)
_pangu_run_link_mode_command(
    PASS
    "no work to do"
    "${CMAKE_COMMAND}"
    --build
    "${build}"
    --target
    App
)
file(READ "${build}/Unrelated.path" unrelated)
if(CMAKE_HOST_APPLE)
    _pangu_run_link_mode_command(
        PASS
        "/preferred"
        otool
        -l
        "${unrelated}"
    )
else()
    _pangu_run_link_mode_command(
        PASS
        "/preferred"
        "$ENV{LLVM_ROOT}/bin/llvm-readobj"
        --dynamic-table
        "${unrelated}"
    )
endif()
if(commandOutput MATCHES "(@loader_path|@executable_path|\\$ORIGIN)")
    message(FATAL_ERROR "Application deployment changed the unrelated library's RPATH")
endif()
set(deploy "${PANGU_TEST_ROOT}/Deploy")
file(COPY "${build}/Bin/" DESTINATION "${deploy}")
if(CMAKE_HOST_APPLE)
    if(NOT IS_SYMLINK "${deploy}/Widget.framework/Versions/Current")
        message(FATAL_ERROR "Missing Framework version link")
    endif()
    file(READ "${deploy}/Widget.framework/Resources/sample.txt" resource)
    if(NOT resource STREQUAL "bundle resource\n")
        message(FATAL_ERROR "Missing Framework resource")
    endif()
endif()
file(RENAME "${build}" "${PANGU_TEST_ROOT}/OfflineBuild")
execute_process(COMMAND "${deploy}/App" WORKING_DIRECTORY "${deploy}" RESULT_VARIABLE result ERROR_VARIABLE error)
file(RENAME "${PANGU_TEST_ROOT}/OfflineBuild" "${build}")
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Relocated App failed: ${result} ${error}")
endif()
if(CMAKE_HOST_APPLE)
    set(resource "${build}/Bin/Widget.framework/Resources/sample.txt")
    file(WRITE "${build}/sample.txt" "updated resource\n")
    _pangu_run_link_mode_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        --build
        "${build}"
        --target
        App
    )
    file(READ "${resource}" updated)
    if(NOT updated STREQUAL "updated resource\n")
        message(FATAL_ERROR "Framework resource update was not deployed")
    endif()
    file(REMOVE "${resource}")
    _pangu_run_link_mode_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        --build
        "${build}"
        --target
        App
    )
    if(NOT EXISTS "${resource}")
        message(FATAL_ERROR "Missing Framework resource was not restored")
    endif()
    _pangu_run_link_mode_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        -S
        "${CMAKE_CURRENT_LIST_DIR}/RuntimeDeployment"
        -B
        "${build}"
        -DUSE_SHARED_WIDGET=OFF
    )
    _pangu_run_link_mode_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        --build
        "${build}"
        --target
        App
    )
    if(EXISTS "${build}/Bin/Widget.framework")
        message(FATAL_ERROR "Obsolete Framework remained in deployment")
    endif()
    _pangu_run_link_mode_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        -S
        "${CMAKE_CURRENT_LIST_DIR}/RuntimeDeployment"
        -B
        "${build}"
        -DUSE_SHARED_WIDGET=ON
    )
    _pangu_run_link_mode_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        --build
        "${build}"
        --target
        App
    )
    set(userFile "${build}/Bin/Widget.framework/user-file")
    file(WRITE "${userFile}" "keep")
    _pangu_run_link_mode_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        -S
        "${CMAKE_CURRENT_LIST_DIR}/RuntimeDeployment"
        -B
        "${build}"
        -DUSE_SHARED_WIDGET=OFF
    )
    _pangu_run_link_mode_command(
        PASS
        ""
        "${CMAKE_COMMAND}"
        --build
        "${build}"
        --target
        App
    )
    file(READ "${userFile}" preserved)
    if(NOT preserved STREQUAL "keep")
        message(FATAL_ERROR "Framework cleanup changed a user file")
    endif()
endif()
