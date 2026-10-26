cmake_minimum_required(VERSION 4.0)
project(PanguAbiDependencyGraphFixture LANGUAGES CXX)
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 23 OR
   NOT CMAKE_CXX_COMPILER_VERSION VERSION_LESS 24)
    message(FATAL_ERROR "Host ABI graph fixtures require LLVM Clang 23.x")
endif()

if(NOT DEFINED PANGU_ABI_SOURCE_ROOT)
    message(FATAL_ERROR "PANGU_ABI_SOURCE_ROOT is required")
endif()
include("${PANGU_ABI_SOURCE_ROOT}/cmake/AbiProfile/AbiProfile.cmake")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/fixture.cpp" "void abi_dependency_graph_fixture() {}\n")
add_library(Root STATIC "${CMAKE_CURRENT_BINARY_DIR}/fixture.cpp")
add_library(TechA STATIC "${CMAKE_CURRENT_BINARY_DIR}/fixture.cpp")
add_library(TechB STATIC "${CMAKE_CURRENT_BINARY_DIR}/fixture.cpp")
# A technical STATIC exports its own independently collectible request identity.
set_property(TARGET TechA PROPERTY PANGU_ABI_REQUEST_FILE
    "${CMAKE_CURRENT_BINARY_DIR}/TechA-request.json")
add_library(Options INTERFACE)
if(PANGU_ABI_INTERFACE_SOURCE)
    target_sources(Options INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/fixture.cpp")
endif()
if(PANGU_ABI_STATIC_INTERFACE_SOURCE)
    target_sources(TechA INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/fixture.cpp")
endif()
if(PANGU_ABI_CONDITIONAL_INTERFACE_SOURCE)
    target_sources(Options INTERFACE "$<$<CONFIG:Debug>:${CMAKE_CURRENT_BINARY_DIR}/fixture.cpp>")
endif()
add_library(AliasA ALIAS TechA)

set(_graph_targets Root TechA TechB Options Prebuilt)
set(_managed_targets Root)
if(PANGU_ABI_CYCLE_MODE STREQUAL "bridge" OR PANGU_ABI_CYCLE_MODE STREQUAL "managed-bridge")
    add_library(Leaf STATIC "${CMAKE_CURRENT_BINARY_DIR}/fixture.cpp")
    list(APPEND _graph_targets Leaf)
    target_link_libraries(TechA PRIVATE Options)
    target_link_libraries(Options INTERFACE TechB Leaf)
    if(PANGU_ABI_CYCLE_MODE STREQUAL "managed-bridge")
        list(APPEND _managed_targets TechB)
    endif()
elseif(PANGU_ABI_CYCLE_MODE STREQUAL "interface-only")
    add_library(Facade INTERFACE)
    list(APPEND _graph_targets Facade)
    target_link_libraries(Options INTERFACE Facade)
    target_link_libraries(Facade INTERFACE Options)
    target_link_libraries(TechA PRIVATE TechB)
else()
    target_link_libraries(TechA PRIVATE TechB)
endif()
target_link_libraries(TechB PRIVATE TechA)
target_link_libraries(Options INTERFACE "$<$<CONFIG:Release>:TechB>")
target_link_libraries(Root PRIVATE AliasA Options TechA TechA)
target_link_libraries(Root PUBLIC "$<$<CONFIG:Debug>:TechB>")

# The compiled SCC is A(first -> B) -> B(second -> A) -> A(second -> Host).
# The two A members live in distinct translation units, so a single pass
# over the first A archive cannot satisfy the symbol requested by B.
# A real consumer supplies HostOnly. This verifies final link order/rescans
# without requiring either STATIC archive to independently link into an EXE.
if(PANGU_ABI_LINK_WITNESS)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/witness_root.cpp"
        "int WitnessFirst(); int WitnessRoot() { return WitnessFirst(); }\n")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/witness_first.cpp"
        "int WitnessSecond(); int WitnessFirst() { return WitnessSecond(); }\n")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/witness_second.cpp"
        "int WitnessThird(); int WitnessSecond() { return WitnessThird(); }\n")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/witness_third.cpp"
        "int WitnessHostOnly(); int WitnessThird() { return WitnessHostOnly() + 34; }\n")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/witness_host.cpp"
        "int WitnessRoot(); int WitnessHostOnly() { return 1200; }\nint main() { return WitnessRoot() != 1234; }\n")
    target_sources(Root PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/witness_root.cpp")
    target_sources(TechA PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/witness_first.cpp"
        "${CMAKE_CURRENT_BINARY_DIR}/witness_third.cpp")
    target_sources(TechB PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/witness_second.cpp")
    add_executable(AbiLinkWitness "${CMAKE_CURRENT_BINARY_DIR}/witness_host.cpp")
    target_link_libraries(AbiLinkWitness PRIVATE Root)
endif()

# A reachable imported binary with an attested digest is STILL not source visible.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/untrusted-prebuilt.a" "")
add_library(Prebuilt STATIC IMPORTED)
set_target_properties(Prebuilt PROPERTIES IMPORTED_LOCATION
    "${CMAKE_CURRENT_BINARY_DIR}/untrusted-prebuilt.a")
if(PANGU_ABI_LINK_IMPORTED)
    target_link_libraries(Root PRIVATE Prebuilt)
endif()

pangu_export_abi_dependency_graph(
    OUTPUT "${CMAKE_BINARY_DIR}/graph-$<CONFIG>.json"
    ROOT Root
    TARGETS ${_graph_targets}
    ALIASES AliasA
    MANAGED ${_managed_targets}
)
