cmake_minimum_required(VERSION 4.0)
include("${CMAKE_CURRENT_LIST_DIR}/ConsumerTestHelpers.cmake")
_pangu_configure_meta_consumer(PASS "" -DDESCRIPTION=ON)
_pangu_build_meta_consumer(PASS "PanguMetaTool generate")
_pangu_snapshot_meta_outputs(before)
foreach(state IN ITEMS Meta.outputs.json Meta.stamp Meta.d)
    file(SHA256 "${generated}/${state}" "before_${state}_hash")
    file(
        TIMESTAMP "${generated}/${state}"
        "before_${state}_time"
        "%s.%f"
    )
endforeach()
file(READ "${source}/Public/Actor.h" original)
string(
    REPLACE "pga::Type"
    "pga::Type(42)"
    invalid
    "${original}"
)
file(WRITE "${source}/Public/Actor.h" "${invalid}")
_pangu_build_meta_consumer(FAIL "")
_pangu_reject_test_text("${commandOutput}" "Building CXX object")
_pangu_snapshot_meta_outputs(after)
if(NOT before STREQUAL after)
    message(FATAL_ERROR "A failed scan must preserve every previously published output")
endif()
foreach(state IN ITEMS Meta.outputs.json Meta.stamp Meta.d)
    file(SHA256 "${generated}/${state}" hash)
    file(
        TIMESTAMP "${generated}/${state}"
        time
        "%s.%f"
    )
    if(NOT hash STREQUAL before_${state}_hash OR NOT time STREQUAL before_${state}_time)
        message(FATAL_ERROR "Failed scan changed ${state}")
    endif()
endforeach()
file(WRITE "${source}/Public/Actor.h" "${original}")
_pangu_build_meta_consumer(PASS "PanguMetaTool generate")
_pangu_configure_meta_consumer(PASS "" -DDESCRIPTION=OFF)
_pangu_build_meta_consumer(PASS "PanguMetaTool generate")
if(EXISTS "${generated}/Private/ModuleMeta.gen.h" OR EXISTS "${generated}/Private/Actor.gen.cpp")
    message(FATAL_ERROR "Removing description needs must remove private generated data")
endif()
if(NOT EXISTS "${generated}/Public/Actor.gen.h")
    message(FATAL_ERROR "Typed access remains enabled")
endif()
file(WRITE "${generated}/Public/Foreign.txt" "unowned")
_pangu_configure_meta_consumer(PASS "" -DNO_INPUT=ON)
_pangu_build_meta_consumer(PASS "PanguMetaTool generate")
if(EXISTS "${generated}/Public/Actor.gen.h" OR EXISTS "${generated}/Private/ModuleMeta.gen.cpp")
    message(FATAL_ERROR "Removing the final input must retire its old outputs")
endif()
if(NOT EXISTS "${generated}/Public/Foreign.txt")
    message(FATAL_ERROR "Cleanup removed an unowned file")
endif()
_pangu_build_meta_consumer(PASS "no work to do")
