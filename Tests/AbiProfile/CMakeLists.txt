cmake_minimum_required(VERSION 4.0)

if(NOT CMAKE_CROSSCOMPILING)
    add_test(NAME AbiCollectionLauncherContract
        COMMAND "${CMAKE_COMMAND}"
            "-DPANGU_SOURCE=${PROJECT_SOURCE_DIR}" "-DPANGU_BINARY=${CMAKE_CURRENT_BINARY_DIR}"
            "-DPANGU_COMPILER=${CMAKE_CXX_COMPILER}" -P "${CMAKE_CURRENT_SOURCE_DIR}/LauncherTests.cmake")
    set_tests_properties(AbiCollectionLauncherContract PROPERTIES TIMEOUT 600)
    add_executable(PanguBuildContractTests BuildContractTests.cpp)
    target_link_libraries(PanguBuildContractTests PRIVATE PanguBuildOptions PanguAbiProfile)
    add_test(NAME AbiCollectionBuildContract COMMAND PanguBuildContractTests
        "${CMAKE_COMMAND}" "${CMAKE_CXX_COMPILER}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(AbiCollectionBuildContract PROPERTIES TIMEOUT 600)

    add_test(NAME AbiCollectionBuildContractTransitive COMMAND PanguBuildContractTests
        "${CMAKE_COMMAND}" "${CMAKE_CXX_COMPILER}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}" transitive)
    add_test(NAME AbiCollectionBuildContractConditionalDebug COMMAND PanguBuildContractTests
        "${CMAKE_COMMAND}" "${CMAKE_CXX_COMPILER}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}" conditional-debug)
    add_test(NAME AbiCollectionBuildContractEnvironment COMMAND PanguBuildContractTests
        "${CMAKE_COMMAND}" "${CMAKE_CXX_COMPILER}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}" environment)
    add_test(NAME AbiCollectionBuildContractDiagnostic COMMAND PanguBuildContractTests
        "${CMAKE_COMMAND}" "${CMAKE_CXX_COMPILER}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}" diagnostic)
    add_test(NAME AbiCollectionBuildContractInterfaceCycle COMMAND PanguBuildContractTests
        "${CMAKE_COMMAND}" "${CMAKE_CXX_COMPILER}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}" interface-cycle)
    set_tests_properties(AbiCollectionBuildContractTransitive
        AbiCollectionBuildContractConditionalDebug AbiCollectionBuildContractEnvironment
        AbiCollectionBuildContractDiagnostic PROPERTIES TIMEOUT 600)
    set_tests_properties(AbiCollectionBuildContractInterfaceCycle PROPERTIES TIMEOUT 600)
    add_test(NAME AbiCollectionBuildContractRecovery COMMAND PanguBuildContractTests
        "${CMAKE_COMMAND}" "${CMAKE_CXX_COMPILER}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}" completion-recovery)
    set_tests_properties(AbiCollectionBuildContractRecovery PROPERTIES TIMEOUT 600)

    add_executable(PanguHostToolsTests HostToolsTests.cpp)
    target_link_libraries(PanguHostToolsTests PRIVATE PanguBuildOptions PanguAbiProfile)
    add_test(NAME AbiCollectionHostTools COMMAND PanguHostToolsTests
        "${CMAKE_COMMAND}" "${PROJECT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/HostToolsSmoke.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CXX_COMPILER}")
    set_tests_properties(AbiCollectionHostTools PROPERTIES TIMEOUT 600)
endif()

if(NOT CMAKE_CROSSCOMPILING)
    add_executable(PanguCollectionGroupTests CollectionGroupTests.cpp)
    target_link_libraries(PanguCollectionGroupTests PRIVATE PanguBuildOptions PanguAbiProfile)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/GroupBaseline.objects"
        CONTENT "$<JOIN:$<TARGET_OBJECTS:PanguFoundation>,\n>\n")
    add_test(NAME AbiCollectionGroup COMMAND PanguCollectionGroupTests
        "${PROJECT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" "${CMAKE_CXX_COMPILER}"
        "${CMAKE_CURRENT_BINARY_DIR}/GroupBaseline.objects" "${CMAKE_BUILD_TYPE}")
    set_tests_properties(AbiCollectionGroup PROPERTIES TIMEOUT 600)
endif()

add_executable(PanguCollectionStateTests CollectionStateTests.cpp)
target_link_libraries(PanguCollectionStateTests PRIVATE PanguBuildOptions PanguAbiProfile)
set_target_properties(PanguCollectionStateTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_test(NAME AbiCollectionState COMMAND PanguCollectionStateTests "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CXX_COMPILER}" "${PROJECT_SOURCE_DIR}/wiki/foundation" "$<TARGET_FILE:PanguProcessFixture>")
set_tests_properties(AbiCollectionState PROPERTIES TIMEOUT 600)

add_executable(PanguCompileCommandsTests CompileCommandsTests.cpp)
target_link_libraries(PanguCompileCommandsTests PRIVATE PanguBuildOptions PanguAbiProfile)
set_target_properties(PanguCompileCommandsTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_test(NAME AbiCollectionCompileCommands COMMAND PanguCompileCommandsTests "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CXX_COMPILER}")

add_executable(PanguProcessFixture ProcessFixture.cpp)
target_link_libraries(PanguProcessFixture PRIVATE PanguBuildOptions PanguAbiProfile)
set_target_properties(PanguProcessFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_executable(PanguProcessTests ProcessTests.cpp)
target_link_libraries(PanguProcessTests PRIVATE PanguBuildOptions PanguAbiProfile)
set_target_properties(PanguProcessTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_test(NAME AbiCollectionProcess COMMAND PanguProcessTests "$<TARGET_FILE:PanguProcessFixture>"
    "$<TARGET_FILE:PanguAbiProfileTool>" "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(AbiCollectionProcess PROPERTIES TIMEOUT 30)

add_executable(PanguCollectionRequestTests CollectionRequestTests.cpp)
target_link_libraries(PanguCollectionRequestTests PRIVATE PanguBuildOptions PanguAbiProfile)
set_target_properties(PanguCollectionRequestTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_test(NAME AbiCollectionRequest COMMAND PanguCollectionRequestTests "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(PanguAbiProfileTests
    AbiProfileTests.cpp
    JsonForwardTests.cpp
)

target_link_libraries(PanguAbiProfileTests PRIVATE PanguBuildOptions PanguAbiProfile)
target_include_directories(PanguAbiProfileTests PRIVATE
    "${PROJECT_SOURCE_DIR}/Tools/AbiProfile/Private"
)
set_target_properties(PanguAbiProfileTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin"
)
add_test(NAME AbiProfileVectors COMMAND PanguAbiProfileTests
    "${PROJECT_SOURCE_DIR}/wiki/foundation")
add_test(NAME AbiProfileCli
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_ABI_PROFILE_TOOL=$<TARGET_FILE:PanguAbiProfileTool>"
        "-DPANGU_TEST_SCHEMA=${PROJECT_SOURCE_DIR}/wiki/foundation/abi-profile-v1.schema.json"
        "-DPANGU_TEST_VECTORS=${PROJECT_SOURCE_DIR}/wiki/foundation/abi-profile-v1.vectors.json"
        "-DPANGU_TEST_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CliTests.cmake"
)

add_executable(PanguJsonExtrasTests JsonExtrasTests.cpp)
target_link_libraries(PanguJsonExtrasTests PRIVATE PanguBuildOptions ThirdParty::NlohmannJson)
set_target_properties(PanguJsonExtrasTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_test(NAME JsonExtras COMMAND PanguJsonExtrasTests)

add_executable(PanguAbiProfileIoTests IoTests.cpp)
target_link_libraries(PanguAbiProfileIoTests PRIVATE PanguBuildOptions PanguAbiProfile)
target_include_directories(PanguAbiProfileIoTests PRIVATE "${PROJECT_SOURCE_DIR}/Tools/AbiProfile/Private")
set_target_properties(PanguAbiProfileIoTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_test(NAME AbiProfileIo COMMAND PanguAbiProfileIoTests "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(PanguWindowsProfileTests WindowsProfileTests.cpp)
target_link_libraries(PanguWindowsProfileTests PRIVATE PanguBuildOptions PanguAbiProfile)
target_include_directories(PanguWindowsProfileTests PRIVATE "${PROJECT_SOURCE_DIR}/Tools/AbiProfile/Private")
set_target_properties(PanguWindowsProfileTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin")
add_test(NAME AbiWindowsProfileMapping COMMAND PanguWindowsProfileTests
    "${PROJECT_SOURCE_DIR}/wiki/foundation/abi-profile-v1.schema.json")
add_test(NAME AbiWindowsCollectionCli
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_ABI_PROFILE_TOOL=$<TARGET_FILE:PanguAbiProfileTool>"
        "-DPANGU_ABI_REQUEST=${CMAKE_BINARY_DIR}/Abi/$<CONFIG>/PanguFoundation/request.json"
        "-DPANGU_TEST_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/WindowsCollectionCli"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/WindowsCollectionCliTests.cmake")

if(WIN32 AND NOT CMAKE_CROSSCOMPILING)
    add_library(PanguAbiCollectionFixture STATIC CollectionFirst.cpp CollectionSecond.cpp)
    target_link_libraries(PanguAbiCollectionFixture PRIVATE PanguBuildOptions)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/WindowsFixture.objects"
        CONTENT "$<JOIN:$<TARGET_OBJECTS:PanguAbiCollectionFixture>,\n>\n")
    # Same source in a different target must not be mistaken for a selected TU.
    add_library(PanguAbiCollectionUnrelated STATIC CollectionFirst.cpp)
    target_link_libraries(PanguAbiCollectionUnrelated PRIVATE PanguBuildOptions)
    target_compile_options(PanguAbiCollectionUnrelated PRIVATE -fno-rtti)
    add_library(PanguAbiCollectionBadStl STATIC CollectionFirst.cpp CollectionAlteredStl.cpp)
    target_link_libraries(PanguAbiCollectionBadStl PRIVATE PanguBuildOptions)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/BadStl.objects"
        CONTENT "$<JOIN:$<TARGET_OBJECTS:PanguAbiCollectionBadStl>,\n>\n")
    # The Android NDK toolchain is a cross-compiler run from the Windows Host.
    set(_pangu_ndk "$ENV{LOCALAPPDATA}/Android/Sdk/ndk/30.0.16248370")
    if(DEFINED ENV{ANDROID_NDK_HOME} AND EXISTS "$ENV{ANDROID_NDK_HOME}/source.properties")
        set(_pangu_ndk "$ENV{ANDROID_NDK_HOME}")
    endif()
    set(_pangu_ndk_clang "${_pangu_ndk}/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe")
    if(EXISTS "${_pangu_ndk_clang}" AND EXISTS "${PROJECT_SOURCE_DIR}/Build/Android/compile_commands.json")
        add_executable(PanguAndroidCollectorCliTests AndroidCollectorCliTests.cpp)
        target_link_libraries(PanguAndroidCollectorCliTests PRIVATE PanguBuildOptions PanguAbiProfile)
        add_dependencies(PanguAndroidCollectorCliTests PanguAbiProfileTool)
        add_test(NAME AbiCollectorAndroidCli COMMAND PanguAndroidCollectorCliTests
            "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/Build/Android" "${_pangu_ndk_clang}"
            "$<TARGET_FILE:PanguAbiProfileTool>")
        set_tests_properties(AbiCollectorAndroidCli PROPERTIES TIMEOUT 240)
        add_executable(PanguAndroidCollectorTests AndroidCollectorTests.cpp)
        target_link_libraries(PanguAndroidCollectorTests PRIVATE PanguBuildOptions PanguAbiProfile)
        add_test(NAME AbiCollectorAndroid COMMAND PanguAndroidCollectorTests
            "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/Build/Android" "${_pangu_ndk_clang}"
            "${CMAKE_CURRENT_BINARY_DIR}/AndroidCollectorCases")
        set_tests_properties(AbiCollectorAndroid PROPERTIES TIMEOUT 180)
    endif()
    add_executable(PanguWindowsCollectorCliTests WindowsCollectorCliTests.cpp)
    target_link_libraries(PanguWindowsCollectorCliTests PRIVATE PanguBuildOptions PanguAbiProfile)
    add_dependencies(PanguWindowsCollectorCliTests PanguFoundation)
    add_test(NAME AbiCollectorWindowsCli COMMAND PanguWindowsCollectorCliTests
        "${PROJECT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" "${CMAKE_CXX_COMPILER}"
        "$<TARGET_FILE:PanguAbiProfileTool>" "${CMAKE_BUILD_TYPE}")
    set_tests_properties(AbiCollectorWindowsCli PROPERTIES TIMEOUT 450)
    add_executable(PanguWindowsCollectorTests WindowsCollectorTests.cpp)
    target_link_libraries(PanguWindowsCollectorTests PRIVATE PanguBuildOptions PanguAbiProfile)
    add_test(NAME AbiCollectorWindows COMMAND PanguWindowsCollectorTests
        "${PROJECT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" "${CMAKE_CXX_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}")
    # Missing/duplicate/response, RTTI, layout, and STL ABI regressions run
    # through the native Windows Collector C++ tests above, not legacy scripts.
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
    add_executable(PanguLinuxCollectorCliTests LinuxCollectorCliTests.cpp)
    target_link_libraries(PanguLinuxCollectorCliTests PRIVATE PanguBuildOptions PanguAbiProfile)
    add_dependencies(PanguLinuxCollectorCliTests PanguFoundation PanguModule PanguAbiProfileTool)
    add_test(NAME AbiCollectorLinuxCli COMMAND PanguLinuxCollectorCliTests
        "${PROJECT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" "${CMAKE_CXX_COMPILER}"
        "$<TARGET_FILE:PanguAbiProfileTool>")
    set_tests_properties(AbiCollectorLinuxCli PROPERTIES TIMEOUT 360)
    add_executable(PanguLinuxCollectorTests LinuxCollectorTests.cpp)
    target_link_libraries(PanguLinuxCollectorTests PRIVATE PanguBuildOptions PanguAbiProfile)
    add_dependencies(PanguLinuxCollectorTests PanguFoundation PanguModule)
    add_test(NAME AbiCollectorLinux COMMAND PanguLinuxCollectorTests
        "${PROJECT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" "${CMAKE_CXX_COMPILER}")
    set_tests_properties(AbiCollectorLinux PROPERTIES TIMEOUT 180)
endif()

if(APPLE AND NOT CMAKE_CROSSCOMPILING)
    add_executable(PanguAppleCollectorCliTests AppleCollectorCliTests.cpp)
    target_link_libraries(PanguAppleCollectorCliTests PRIVATE PanguBuildOptions PanguAbiProfile)
    add_dependencies(PanguAppleCollectorCliTests PanguFoundation PanguModule PanguAbiProfileTool)
    add_test(NAME AbiCollectorAppleCli COMMAND PanguAppleCollectorCliTests
        "${PROJECT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" "${CMAKE_CXX_COMPILER}"
        "$<TARGET_FILE:PanguAbiProfileTool>" "macos")
    set_tests_properties(AbiCollectorAppleCli PROPERTIES TIMEOUT 450)
    add_executable(PanguAppleCollectorTests AppleCollectorTests.cpp)
    target_link_libraries(PanguAppleCollectorTests PRIVATE PanguBuildOptions PanguAbiProfile)
    add_dependencies(PanguAppleCollectorTests PanguFoundation PanguModule)
    add_test(NAME AbiCollectorApple COMMAND PanguAppleCollectorTests
        "${PROJECT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" "${CMAKE_CXX_COMPILER}" "macos")
    set_tests_properties(AbiCollectorApple PROPERTIES TIMEOUT 180)
    # Both Apple build trees are siblings on the host APFS volume, outside SourceRoot.
    get_filename_component(_pangu_ios_build_root "${CMAKE_BINARY_DIR}/../iOS" ABSOLUTE)
    if(EXISTS "${_pangu_ios_build_root}/compile_commands.json")
        add_test(NAME AbiCollectorAppleIOS COMMAND PanguAppleCollectorTests
            "${PROJECT_SOURCE_DIR}" "${_pangu_ios_build_root}" "${CMAKE_CXX_COMPILER}" "ios")
        set_tests_properties(AbiCollectorAppleIOS PROPERTIES TIMEOUT 180)
        add_test(NAME AbiCollectorAppleIOSCli COMMAND PanguAppleCollectorCliTests
            "${PROJECT_SOURCE_DIR}" "${_pangu_ios_build_root}" "${CMAKE_CXX_COMPILER}"
            "$<TARGET_FILE:PanguAbiProfileTool>" "ios")
        set_tests_properties(AbiCollectorAppleIOSCli PROPERTIES TIMEOUT 450)
        add_test(NAME AbiCollectorAppleProfilesDiffer COMMAND "${CMAKE_COMMAND}"
            "-DPANGU_MACOS_PROOF=${CMAKE_BINARY_DIR}/Tests/AbiProfile/AppleCppCollectorCli"
            "-DPANGU_IOS_PROOF=${_pangu_ios_build_root}/Tests/AbiProfile/AppleCppCollectorCli"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/AppleProfilesDiffer.cmake")
        set_tests_properties(AbiCollectorAppleProfilesDiffer PROPERTIES
            DEPENDS "AbiCollectorAppleCli;AbiCollectorAppleIOSCli")
    endif()
endif()
add_executable(PanguDependencyGraphTests DependencyGraphTests.cpp)
target_link_libraries(PanguDependencyGraphTests PRIVATE PanguBuildOptions PanguAbiProfile)
add_test(NAME AbiCollectionDependencyGraph COMMAND PanguDependencyGraphTests "${CMAKE_CURRENT_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/wiki/foundation/abi-profile-v1.vectors.json")

add_executable(PanguCMakeGraphTests CMakeGraphTests.cpp)
target_link_libraries(PanguCMakeGraphTests PRIVATE PanguBuildOptions PanguAbiProfile)
add_test(NAME AbiCollectionCMakeGraph
    COMMAND "${CMAKE_COMMAND}"
        "-DPANGU_ABI_ROOT=${PROJECT_SOURCE_DIR}"
        "-DPANGU_ABI_BINARY=${CMAKE_CURRENT_BINARY_DIR}"
        "-DPANGU_ABI_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DPANGU_ABI_CMAKE=${CMAKE_COMMAND}"
        "-DPANGU_ABI_RUNNER=$<TARGET_FILE:PanguCMakeGraphTests>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CMakeGraphTests.cmake")
set_tests_properties(AbiCollectionCMakeGraph PROPERTIES TIMEOUT 180)
