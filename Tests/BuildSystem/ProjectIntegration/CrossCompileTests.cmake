cmake_minimum_required(VERSION 4.0)
include("${CMAKE_CURRENT_LIST_DIR}/ProjectTestHelpers.cmake")
if(NOT PANGU_TARGET_PRESET MATCHES "^(android-arm64|ios-arm64)$")
    message(FATAL_ERROR "CrossCompileTests requires PANGU_TARGET_PRESET=android-arm64 or ios-arm64")
endif()
if(NOT PANGU_LINK_MODE MATCHES "^(Monolithic|Modular)$")
    message(FATAL_ERROR "CrossCompileTests requires PANGU_LINK_MODE=Monolithic or Modular")
endif()
file(
    APPEND "${source}/CMakeLists.txt"
    [=[
if(NOT TARGET MobileHost)
    message(FATAL_ERROR "Mobile final link requires a real Runtime/business MobileHost")
endif()
if(NOT TARGET MyGameEcsHost)
    message(FATAL_ERROR "Mobile final link requires a real MyGameEcsHost")
endif()
get_target_property(ecsHostType MyGameEcsHost TYPE)
if(ANDROID AND NOT ecsHostType STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR "Android MyGameEcsHost must be a native SHARED host")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT ecsHostType STREQUAL "EXECUTABLE")
    message(FATAL_ERROR "iOS MyGameEcsHost must be a native executable")
endif()
get_target_property(mobileType MobileHost TYPE)
if(ANDROID AND NOT mobileType STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR "Android MobileHost must be a native SHARED host")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT mobileType STREQUAL "EXECUTABLE")
    message(FATAL_ERROR "iOS MobileHost must be a minimal executable")
endif()
if(ANDROID)
    # caller 与实现共用声明，编译器验证参数类型；C linkage 符号本身不携带签名。
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/MobileHostCaller.cpp"
        "#include \"MobileHost.h\"\nint main() { return PanguMobileHostCheck(\"/data/user/0/org.pangu.fixture/files/logs\"); }\n")
    add_executable(MobileHostCaller "${CMAKE_CURRENT_BINARY_DIR}/MobileHostCaller.cpp")
    target_include_directories(MobileHostCaller PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    target_link_libraries(MobileHostCaller PRIVATE PanguBuildOptions MobileHost)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/MobileHostCaller.txt" CONTENT "$<TARGET_FILE:MobileHostCaller>")
endif()
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/MobileRuntimePaths.txt"
    CONTENT "runtime=$<TARGET_FILE:Pangu::Module>\nengineHost=$<TARGET_FILE:Pangu::EngineHost>\nlog=$<TARGET_FILE:Pangu::Log>\nfoundation=$<TARGET_FILE:Pangu::Foundation>\n")
]=]
)
set(ENV{PANGU_ENGINE_SOURCE_DIR} "${PANGU_ENGINE_SOURCE_DIR}")
set(binary "${PANGU_TEST_ROOT}/CrossBuild")
_pangu_run_project_command(
    PASS
    ""
    "${CMAKE_COMMAND}"
    --preset
    "${PANGU_TARGET_PRESET}"
    -S
    "${source}"
    -B
    "${binary}"
    "-DPANGU_ENGINE_SOURCE_DIR:PATH=${PANGU_ENGINE_SOURCE_DIR}"
    "-DPANGU_META_HOST_BUILD_ROOT=${PANGU_META_HOST_BUILD_ROOT}"
    "-DPANGU_FIXTURE_ENGINE_ROOT:PATH=${PANGU_TEST_ROOT}/FixtureEngine"
    "-DPANGU_ENABLED_PLUGINS:STRING=fixture.physics;mygame.gameplay;mygame.editor-extras;mygame.assets-only;mygame.shared-probe"
    -DPANGU_FIXTURE_BUILD_HOSTS=ON
    -DPANGU_FIXTURE_BUILD_ECS_HOST=ON
    "-DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=${PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE}"
    "-DPANGU_LINK_MODE=${PANGU_LINK_MODE}"
)
foreach(target IN ITEMS MobileHost MyGameEcsHost)
    _pangu_build_project_fixture("${target}")
endforeach()
if(PANGU_TARGET_PRESET STREQUAL "android-arm64")
    _pangu_build_project_fixture(MobileHostCaller)
    _pangu_build_project_fixture(MyGameEcsHostCaller)
    _pangu_run_project_command(
        PASS
        ""
        ninja
        -C
        "${binary}"
        -t
        commands
        MobileHostCaller
    )
    _pangu_require_test_text("${commandOutput}" "-fno-exceptions")
    _pangu_reject_test_text("${commandOutput}" "-fno-rtti")
    file(WRITE "${PANGU_TEST_ROOT}/CallerCommands.txt" "${commandOutput}")
endif()
_pangu_run_project_command(
    PASS
    ""
    ninja
    -C
    "${binary}"
    -t
    commands
    MobileHost
)
file(WRITE "${PANGU_TEST_ROOT}/TargetCommands.txt" "${commandOutput}")
_pangu_require_test_text("${commandOutput}" "MobileHost.cpp")
_pangu_require_test_text("${commandOutput}" "Gameplay.cpp")
_pangu_require_test_text("${commandOutput}" "ModuleManager.cpp")
_pangu_require_test_text("${commandOutput}" "EngineHost.cpp")
_pangu_require_test_text("${commandOutput}" "LogSystem.cpp")
_pangu_reject_test_text("${commandOutput}" "gtest")
_pangu_reject_test_text("${commandOutput}" "SharedProbe.cpp")
_pangu_reject_test_text("${commandOutput}" "EditorExtras.cpp")
file(STRINGS "${binary}/MobileHostConsumer.txt" consumer)
foreach(line IN LISTS consumer)
    if(line MATCHES "^file=(.*)$")
        set(mobileHost "${CMAKE_MATCH_1}")
    endif()
endforeach()
if(PANGU_TARGET_PRESET STREQUAL "android-arm64")
    _pangu_require_test_text("${commandOutput}" "aarch64")
    _pangu_require_test_text("${commandOutput}" "android")
    set(inspect "$ENV{ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readobj.exe")
    set(symbolTool "$ENV{ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-nm.exe")
    _pangu_run_project_command(
        PASS
        ""
        "${inspect}"
        --file-headers
        --needed-libs
        "${mobileHost}"
    )
    _pangu_require_test_text("${commandOutput}" "elf64-littleaarch64")
    _pangu_require_test_text("${commandOutput}" "SharedObject")
else()
    _pangu_require_test_text("${commandOutput}" "-arch arm64")
    _pangu_require_test_text("${commandOutput}" "iPhoneOS")
    set(symbolTool "$ENV{LLVM_ROOT}/bin/llvm-nm")
    _pangu_run_project_command(
        PASS
        ""
        file
        "${mobileHost}"
    )
    _pangu_require_test_text("${commandOutput}" "Mach-O 64-bit executable arm64")
    file(APPEND "${PANGU_TEST_ROOT}/Architecture.txt" "${commandOutput}\n")
    _pangu_run_project_command(
        PASS
        ""
        otool
        -l
        "${mobileHost}"
    )
    _pangu_require_test_text("${commandOutput}" "platform 2")
    _pangu_run_project_command(
        PASS
        ""
        otool
        -L
        "${mobileHost}"
    )
endif()
file(WRITE "${PANGU_TEST_ROOT}/MobileHostHeaders.txt" "${commandOutput}")
_pangu_reject_test_text("${commandOutput}" "libLLVM")
_pangu_reject_test_text("${commandOutput}" "libclang")
_pangu_reject_test_text("${commandOutput}" "libstdc++")
_pangu_reject_test_text("${commandOutput}" "/opt/local")
_pangu_reject_test_text("${commandOutput}" "MyGameGameplay")
_pangu_reject_test_text("${commandOutput}" "FixtureEnginePhysics")
if(PANGU_LINK_MODE STREQUAL "Modular")
    _pangu_require_test_text("${commandOutput}" "PanguEngineHost")
    _pangu_require_test_text("${commandOutput}" "PanguModule")
    _pangu_require_test_text("${commandOutput}" "PanguFoundation")
    _pangu_require_test_text("${commandOutput}" "PanguLog")
else()
    _pangu_reject_test_text("${commandOutput}" "PanguEngineHost")
    _pangu_reject_test_text("${commandOutput}" "PanguModule")
endif()
if(PANGU_TARGET_PRESET STREQUAL "android-arm64")
    file(READ "${binary}/MobileHostCaller.txt" caller)
    _pangu_run_project_command(
        PASS
        ""
        "${inspect}"
        --file-headers
        --needed-libs
        "${caller}"
    )
    _pangu_require_test_text("${commandOutput}" "elf64-littleaarch64")
    _pangu_require_test_text("${commandOutput}" "libMobileHost.so")
    file(WRITE "${PANGU_TEST_ROOT}/CallerHeaders.txt" "${commandOutput}")
    _pangu_run_project_command(
        PASS
        ""
        "${symbolTool}"
        --demangle
        "${caller}"
    )
    _pangu_require_test_text("${commandOutput}" "PanguMobileHostCheck")
    file(WRITE "${PANGU_TEST_ROOT}/CallerSymbols.txt" "${commandOutput}")
endif()
_pangu_run_project_command(
    PASS
    ""
    "${symbolTool}"
    --demangle
    "${mobileHost}"
)
file(WRITE "${PANGU_TEST_ROOT}/MobileHostSymbols.txt" "${commandOutput}")
_pangu_require_test_text("${commandOutput}" "pg::MakeError")
_pangu_require_test_text("${commandOutput}" "pg::engine::EngineHost::Start")
_pangu_require_test_text("${commandOutput}" "pg::log::LogSystem::Initialize")
_pangu_require_test_text("${commandOutput}" "pg::log::LogSystem::GetWriter")
_pangu_require_test_text("${commandOutput}" "pg::log::LogSystem::Stop")
_pangu_require_test_text("${commandOutput}" "pg::FailureReport::PrimarySite")
_pangu_require_test_text("${commandOutput}" "pg::Fatal")
_pangu_reject_test_text("${commandOutput}" "PanguModuleQueryV1")
if(PANGU_LINK_MODE STREQUAL "Monolithic")
    _pangu_require_test_text("${commandOutput}" "pg::fixture::QueryGameplay")
    _pangu_require_test_text("${commandOutput}" "pg::fixture::QueryPhysics")
    _pangu_run_project_command(
        PASS
        ""
        "${symbolTool}"
        --defined-only
        --demangle
        "${mobileHost}"
    )
    _pangu_require_test_text("${commandOutput}" "pg::MakeError")
    _pangu_require_test_text("${commandOutput}" "pg::module::ModuleManager::Create")
    _pangu_require_test_text("${commandOutput}" "pg::engine::EngineHost::Start")
else()
    file(STRINGS "${binary}/MobileHostDeployManifest.txt" paths)
    list(GET paths 1 gameplay)
    list(GET paths 3 physics)
    file(STRINGS "${binary}/MobileRuntimePaths.txt" runtimePaths)
    foreach(line IN LISTS runtimePaths)
        if(line MATCHES "^(runtime|engineHost|log|foundation)=(.*)$")
            set("${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}")
        endif()
    endforeach()
    _pangu_run_project_command(
        PASS
        ""
        "${symbolTool}"
        --demangle
        "${engineHost}"
    )
    _pangu_require_test_text("${commandOutput}" "pg::module::ModuleManager::Create")
    file(WRITE "${PANGU_TEST_ROOT}/EngineHostSymbols.txt" "${commandOutput}")
    _pangu_run_project_command(
        PASS
        ""
        "${symbolTool}"
        --defined-only
        --demangle
        "${engineHost}"
    )
    _pangu_require_test_text("${commandOutput}" "pg::engine::EngineHost::Start")
    foreach(
        library
        IN
        ITEMS
            "${gameplay}"
            "${physics}"
            "${engineHost}"
            "${runtime}"
            "${log}"
            "${foundation}"
    )
        if(PANGU_TARGET_PRESET STREQUAL "android-arm64")
            _pangu_run_project_command(
                PASS
                ""
                "${symbolTool}"
                --dynamic
                --defined-only
                --extern-only
                "${library}"
            )
        else()
            _pangu_run_project_command(
                PASS
                ""
                "${symbolTool}"
                --defined-only
                --extern-only
                "${library}"
            )
        endif()
        _pangu_reject_test_text("${commandOutput}" "libLLVM")
        _pangu_reject_test_text("${commandOutput}" "libclang")
        _pangu_reject_test_text("${commandOutput}" "libstdc++")
        _pangu_require_test_text("${commandOutput}" "PanguModuleQueryV1")
        file(APPEND "${PANGU_TEST_ROOT}/ModuleQueryExports.txt" "${library}\n${commandOutput}\n")
        if(PANGU_TARGET_PRESET STREQUAL "android-arm64")
            _pangu_run_project_command(
                PASS
                ""
                "${inspect}"
                --file-headers
                --needed-libs
                "${library}"
            )
            _pangu_require_test_text("${commandOutput}" "elf64-littleaarch64")
        else()
            _pangu_run_project_command(
                PASS
                ""
                file
                "${library}"
            )
            _pangu_require_test_text("${commandOutput}" "Mach-O 64-bit dynamically linked shared library arm64")
            file(APPEND "${PANGU_TEST_ROOT}/Architecture.txt" "${commandOutput}\n")
            _pangu_run_project_command(
                PASS
                ""
                otool
                -l
                "${library}"
            )
            _pangu_require_test_text("${commandOutput}" "platform 2")
            _pangu_run_project_command(
                PASS
                ""
                otool
                -L
                "${library}"
            )
            _pangu_reject_test_text("${commandOutput}" "/opt/local")
        endif()
        if(library STREQUAL gameplay OR library STREQUAL physics)
            _pangu_require_test_text("${commandOutput}" "PanguModule")
            if(library STREQUAL gameplay)
                _pangu_require_test_text("${commandOutput}" "PanguEngineHost")
            endif()
        elseif(library STREQUAL engineHost)
            _pangu_require_test_text("${commandOutput}" "PanguModule")
            _pangu_require_test_text("${commandOutput}" "PanguFoundation")
        elseif(library STREQUAL runtime)
            _pangu_require_test_text("${commandOutput}" "PanguFoundation")
            _pangu_require_test_text("${commandOutput}" "PanguLog")
        elseif(library STREQUAL log)
            _pangu_require_test_text("${commandOutput}" "PanguFoundation")
        endif()
        _pangu_reject_test_text("${commandOutput}" "libLLVM")
        _pangu_reject_test_text("${commandOutput}" "libclang")
        _pangu_reject_test_text("${commandOutput}" "libstdc++")
        file(APPEND "${PANGU_TEST_ROOT}/ModuleHeaders.txt" "${library}\n${commandOutput}\n")
    endforeach()
endif()
_pangu_run_project_command(
    PASS
    ""
    ninja
    -C
    "${binary}"
    -t
    commands
    MyGameEcsHost
)
file(WRITE "${PANGU_TEST_ROOT}/EcsMobileTargetCommands.txt" "${commandStdout}")
foreach(
    sourceName
    IN
    ITEMS
        EcsHost.cpp
        EcsGameplay.cpp
        EcsConsumer.cpp
        EcsModule.cpp
        World.cpp
        world.c
)
    _pangu_require_test_text("${commandStdout}" "${sourceName}")
endforeach()
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
# 宿主 Meta 工具的输入也可能出现在 phony 规则中，只检查目标侧 C/C++ 产物规则。
string(
    REGEX MATCHALL "[^\r\n]+: (C|CXX)_[^\r\n]+"
    targetRules
    "${commandStdout}"
)
if(NOT targetRules)
    message(FATAL_ERROR "Cannot inspect target C/C++ build rules")
endif()
string(TOLOWER "${targetRules}" targetGraph)
foreach(
    invalid
    IN
    ITEMS
        googletest
        gtest
        gmock
        llvmcore
        clangfrontend
        pangumetafrontend
)
    _pangu_reject_test_text("${targetGraph}" "${invalid}")
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/MobileLinkInputs.cmake")
_pangu_check_mobile_link_inputs("${binary}")
include("${CMAKE_CURRENT_LIST_DIR}/EcsMobileLinkChecks.cmake")
_pangu_run_project_command(
    PASS
    ""
    "${CMAKE_CTEST_COMMAND}"
    --test-dir
    "${binary}"
    --show-only=json-v1
)
string(JSON testCount LENGTH "${commandStdout}" tests)
if(NOT testCount EQUAL 0)
    message(FATAL_ERROR "Cross build must not discover or execute target GTest")
endif()
if(CMAKE_HOST_WIN32)
    set(hostTool "${binary}/BuildTools/PanguBuildTool/PanguBuildTool.exe")
else()
    set(hostTool "${binary}/BuildTools/PanguBuildTool/PanguBuildTool")
endif()
_pangu_run_project_command(
    PASS
    ""
    "${hostTool}"
    discover
    --engine-root
    "${PANGU_TEST_ROOT}/FixtureEngine"
)
string(JSON count LENGTH "${commandOutput}" plugins)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "Cross build must execute the host tool and discover Physics")
endif()
message(
    STATUS
    "${PANGU_TARGET_PRESET}/${PANGU_LINK_MODE}: real Runtime/business final host link and architecture/Query checks passed; no device execution"
)
