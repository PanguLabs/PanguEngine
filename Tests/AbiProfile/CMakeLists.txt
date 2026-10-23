cmake_minimum_required(VERSION 4.0)

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
        "-DPANGU_TEST_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/WindowsCollectionCli"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/WindowsCollectionCliTests.cmake")

if(WIN32 AND NOT CMAKE_CROSSCOMPILING)
    add_library(PanguAbiCollectionFixture STATIC CollectionFirst.cpp CollectionSecond.cpp)
    target_link_libraries(PanguAbiCollectionFixture PRIVATE PanguBuildOptions)
    # Same source in a different target must not be mistaken for a selected TU.
    add_library(PanguAbiCollectionUnrelated STATIC CollectionFirst.cpp)
    target_link_libraries(PanguAbiCollectionUnrelated PRIVATE PanguBuildOptions)
    target_compile_options(PanguAbiCollectionUnrelated PRIVATE -fno-rtti)
    add_library(PanguAbiCollectionBadStl STATIC CollectionFirst.cpp CollectionAlteredStl.cpp)
    target_link_libraries(PanguAbiCollectionBadStl PRIVATE PanguBuildOptions)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/BadStl.objects"
        CONTENT "$<JOIN:$<TARGET_OBJECTS:PanguAbiCollectionBadStl>,\n>\n")
    pangu_enable_windows_abi_profiles(PanguAbiCollectionFixture)
    foreach(case IN ITEMS BASELINE RTTI UNKNOWN MISSING DUPLICATE RESPONSE MISSING_RESPONSE STL_CONFIG)
        add_test(NAME "AbiWindowsCollect${case}"
            COMMAND "${CMAKE_COMMAND}"
                "-DPANGU_ABI_REQUEST=${CMAKE_BINARY_DIR}/Abi/PanguAbiCollectionFixture/request.json"
                "-DPANGU_COLLECT_SCRIPT=${PROJECT_SOURCE_DIR}/cmake/AbiProfile/CollectWindows.cmake"
                "-DPANGU_TEST_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/Collection"
                "-DPANGU_CASE=${case}"
                "-DPANGU_STL_OBJECTS=${CMAKE_CURRENT_BINARY_DIR}/BadStl.objects"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/WindowsCollectionTests.cmake")
    endforeach()
endif()
