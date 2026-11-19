include("${CMAKE_CURRENT_LIST_DIR}/Helpers.cmake")
if(CMAKE_HOST_WIN32)
    set(CMAKE_EXECUTABLE_SUFFIX .exe)
endif()
foreach(mode IN ITEMS Modular Monolithic Modular)
    _pangu_configure_link_mode_fixture(
        Switch
        PASS
        ""
        "-DPANGU_LINK_MODE=${mode}"
        "-DEXPECT_MODE=${mode}"
    )
    _pangu_build_and_run_link_mode_fixture(Switch)
    _pangu_run_link_mode_command(
        PASS
        "ModeConsumerSmoke"
        "${CMAKE_CTEST_COMMAND}"
        --test-dir
        "${PANGU_TEST_ROOT}/Switch"
        -N
    )
    if(
        mode
            STREQUAL
            "Modular"
        AND (
            NOT commandOutput
                MATCHES
                "QueryFirstSmoke"
            OR NOT commandOutput
                MATCHES
                "QueryEngineHostSmoke"
        )
    )
        message(FATAL_ERROR "Modular Query smoke tests are missing")
    elseif(mode STREQUAL "Monolithic" AND commandOutput MATCHES "Query(First|EngineHost)Smoke")
        message(FATAL_ERROR "Monolithic retained stale Modular Query tests")
    endif()
endforeach()
