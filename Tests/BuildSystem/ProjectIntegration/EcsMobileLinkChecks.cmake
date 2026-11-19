include("${CMAKE_CURRENT_LIST_DIR}/EcsArtifactChecks.cmake")

function(_pangu_inspect_ecs_mobile_image path kind)
    _pangu_inspect_native_dependencies("${path}")
    if(PANGU_TARGET_PRESET STREQUAL "android-arm64")
        _pangu_require_test_text("${nativeHeaders}" "elf64-littleaarch64")
        _pangu_require_test_text("${nativeHeaders}" "EM_AARCH64")
        _pangu_require_test_text("${nativeHeaders}" "Type: SharedObject")
        _pangu_require_test_text("${nativeHeaders}" "Type: NT_ANDROID_TYPE_IDENT")
        if(kind STREQUAL "executable")
            _pangu_require_test_text("${nativeHeaders}" "Type: PT_INTERP")
        else()
            _pangu_reject_test_text("${nativeHeaders}" "Type: PT_INTERP")
        endif()
    else()
        _pangu_run_project_command(
            PASS
            ""
            file
            "${path}"
        )
        if(kind STREQUAL "executable")
            _pangu_require_test_text("${commandStdout}" "Mach-O 64-bit executable arm64")
        else()
            _pangu_require_test_text("${commandStdout}" "Mach-O 64-bit dynamically linked shared library arm64")
        endif()
        file(APPEND "${PANGU_TEST_ROOT}/EcsMobileArchitecture.txt" "${commandStdout}\n")
        if(
            NOT nativeHeaders
                MATCHES
                "\n[ \t]+cmd LC_BUILD_VERSION\r?\n[ \t]+cmdsize [0-9]+\r?\n[ \t]+platform (2|IOS)(\r?\n|$)"
        )
            message(FATAL_ERROR "ECS Mach-O image must target iPhoneOS: ${path}\n${nativeHeaders}")
        endif()
        _pangu_reject_test_text("${nativeHeaders}" "/opt/local")
    endif()
    foreach(dependency IN LISTS dependencies)
        if(dependency MATCHES "^(lib)?(LLVM|clang|stdc[+][+])")
            message(FATAL_ERROR "Unexpected host toolchain dependency ${dependency} in ${path}")
        endif()
    endforeach()
    file(APPEND "${PANGU_TEST_ROOT}/EcsMobileHeaders.txt" "${path}\n${nativeHeaders}\n")
    set(dependencies "${dependencies}" PARENT_SCOPE)
endfunction()

function(_pangu_check_ecs_mobile_symbol_count symbols symbol expected path)
    string(
        REGEX MATCHALL "[ \t]_?${symbol}(\r?\n|$)"
        matches
        "${symbols}"
    )
    list(LENGTH matches count)
    if(NOT count EQUAL expected)
        message(FATAL_ERROR "Expected ${expected} '${symbol}' symbol(s) in ${path}, found ${count}")
    endif()
endfunction()

function(_pangu_inspect_ecs_mobile_exports path)
    if(PANGU_TARGET_PRESET STREQUAL "android-arm64")
        set(exportArgs --dynamic)
    endif()
    _pangu_run_project_command(
        PASS
        ""
        "${symbolTool}"
        ${exportArgs}
        --defined-only
        --extern-only
        "${path}"
    )
    file(APPEND "${PANGU_TEST_ROOT}/EcsMobileExports.txt" "${path}\n${commandStdout}\n")
    set(symbols "${commandStdout}" PARENT_SCOPE)
endfunction()

function(_pangu_check_ecs_mobile_flecs_definitions path expected)
    _pangu_run_project_command(
        PASS
        ""
        "${symbolTool}"
        --defined-only
        --demangle
        "${path}"
    )
    foreach(symbol IN ITEMS ecs_init ecs_progress)
        _pangu_check_ecs_mobile_symbol_count("${commandStdout}" "${symbol}" "${expected}" "${path}")
    endforeach()
    set(symbols "${commandStdout}" PARENT_SCOPE)
endfunction()

include("${binary}/EcsArtifacts.cmake")
foreach(
    artifact
    IN
    ITEMS
        host
        gameplay
        consumer
        ecs
        flecs
)
    if(NOT IS_ABSOLUTE "${${artifact}}" OR NOT EXISTS "${${artifact}}")
        message(FATAL_ERROR "Missing ECS mobile artifact ${artifact}: ${${artifact}}")
    endif()
endforeach()

if(PANGU_TARGET_PRESET STREQUAL "android-arm64")
    _pangu_inspect_ecs_mobile_image("${host}" shared)
    set(hostDependencies "${dependencies}")
    _pangu_inspect_ecs_mobile_image("${caller}" executable)
    cmake_path(GET host FILENAME hostName)
    if(NOT hostName IN_LIST dependencies)
        message(FATAL_ERROR "Android ECS caller must import ${hostName}: ${dependencies}")
    endif()
    _pangu_run_project_command(
        PASS
        ""
        "${symbolTool}"
        --dynamic
        --undefined-only
        "${caller}"
    )
    _pangu_check_ecs_mobile_symbol_count("${commandStdout}" PanguEcsHostCheck 1 "${caller}")
    file(WRITE "${PANGU_TEST_ROOT}/EcsMobileCallerImports.txt" "${commandStdout}")
    set(dependencies "${hostDependencies}")
else()
    _pangu_inspect_ecs_mobile_image("${host}" executable)
endif()

foreach(dependency IN LISTS dependencies)
    if(dependency MATCHES "^(lib)?MyGameEcs(Gameplay|Consumer)[.]")
        message(FATAL_ERROR "ECS mobile Host must load business Modules through the catalog: ${dependency}")
    endif()
endforeach()
_pangu_inspect_ecs_mobile_exports("${host}")
_pangu_check_ecs_mobile_symbol_count("${symbols}" PanguModuleQueryV1 0 "${host}")
if(PANGU_TARGET_PRESET STREQUAL "android-arm64")
    _pangu_check_ecs_mobile_symbol_count("${symbols}" PanguEcsHostCheck 1 "${host}")
endif()
cmake_path(GET flecs FILENAME flecsName)
cmake_path(GET ecs FILENAME ecsName)
if(PANGU_LINK_MODE STREQUAL "Monolithic")
    foreach(dependency IN LISTS dependencies)
        if(dependency MATCHES "^(lib)?Pangu(ThirdPartyFlecs|ECS)[.]")
            message(FATAL_ERROR "Monolithic ECS mobile Host has a dynamic ECS/flecs dependency: ${dependency}")
        endif()
    endforeach()
    _pangu_check_ecs_mobile_flecs_definitions("${host}" 1)
    file(WRITE "${PANGU_TEST_ROOT}/EcsMobileHostSymbols.txt" "${symbols}")
    foreach(
        symbol
        IN
        ITEMS
            "pg::fixture::ecs::QueryEcsGameplay()"
            "pg::fixture::ecs::QueryEcsConsumer()"
            "pg::ecs::QueryEcsModule()"
            "pg::ecs::World::Progress(double)"
    )
        _pangu_require_test_text("${symbols}" "${symbol}")
    endforeach()
else()
    if(NOT flecsName IN_LIST dependencies OR NOT ecsName IN_LIST dependencies)
        message(FATAL_ERROR "Missing direct ECS/flecs dependencies of ${host}: ${dependencies}")
    endif()
    _pangu_check_ecs_mobile_flecs_definitions("${host}" 0)
    foreach(artifact IN ITEMS gameplay consumer ecs)
        _pangu_inspect_ecs_mobile_image("${${artifact}}" shared)
        if(NOT flecsName IN_LIST dependencies OR (NOT artifact STREQUAL "ecs" AND NOT ecsName IN_LIST dependencies))
            message(FATAL_ERROR "Missing direct ECS/flecs dependencies of ${${artifact}}: ${dependencies}")
        endif()
        _pangu_inspect_ecs_mobile_exports("${${artifact}}")
        _pangu_check_ecs_mobile_symbol_count("${symbols}" PanguModuleQueryV1 1 "${${artifact}}")
        _pangu_check_ecs_mobile_flecs_definitions("${${artifact}}" 0)
        if(artifact STREQUAL "ecs")
            _pangu_require_test_text("${symbols}" "pg::ecs::World::Progress(double)")
        endif()
    endforeach()
    _pangu_inspect_ecs_mobile_image("${flecs}" shared)
    _pangu_check_ecs_mobile_flecs_definitions("${flecs}" 1)
    _pangu_inspect_ecs_mobile_exports("${flecs}")
    foreach(symbol IN ITEMS ecs_init ecs_progress)
        _pangu_check_ecs_mobile_symbol_count("${symbols}" "${symbol}" 1 "${flecs}")
    endforeach()
    _pangu_check_ecs_mobile_symbol_count("${symbols}" PanguModuleQueryV1 0 "${flecs}")
endif()
message(
    STATUS
    "${PANGU_TARGET_PRESET}/${PANGU_LINK_MODE}: ECS final host/caller link, target OS, Module Query and flecs checks passed"
)
