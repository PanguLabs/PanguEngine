include("${CMAKE_CURRENT_LIST_DIR}/Helpers.cmake")
if(CMAKE_HOST_WIN32)
    set(CMAKE_EXECUTABLE_SUFFIX .exe)
endif()
foreach(mode IN ITEMS Modular Monolithic Modular)
    _pangu_configure_link_mode_fixture(
        Switch
        PASS
        ""
        -DFIXTURE_STAGE_RUNTIME=ON
        "-DPANGU_LINK_MODE=${mode}"
        "-DEXPECT_MODE=${mode}"
    )
    _pangu_build_and_run_link_mode_fixture(Switch)
    if(mode STREQUAL "Modular")
        include("${PANGU_TEST_ROOT}/Switch/DeploymentArtifacts.cmake")
        if(NOT EXISTS "${deploy}/${firstName}" OR NOT EXISTS "${deploy}/${secondName}")
            message(FATAL_ERROR "Modular deployment lacks its catalog libraries")
        endif()
        if(CMAKE_HOST_WIN32)
            set(graphicsFiles
                "${deploy}/FixtureCompiler.dll"
                "${deploy}/D3D12/FixtureCore.dll"
            )
            set(graphicsReplacement "${deploy}/D3D12/FixtureLayers.dll")
            foreach(file IN LISTS graphicsFiles graphicsReplacement)
                if(NOT EXISTS "${file}")
                    message(FATAL_ERROR "Modular deployment lacks ${file}")
                endif()
            endforeach()
        endif()
        if(NOT DEFINED userLibrary)
            file(GLOB staleCopies "${deploy}/*${librarySuffix}")
            list(REMOVE_ITEM staleCopies "${deploy}/${secondName}")
            set(userLibrary "${deploy}/UserOwned${librarySuffix}")
            file(WRITE "${userLibrary}" "keep")
            file(WRITE "${deploy}/${secondName}" "replacement")
            if(CMAKE_HOST_WIN32)
                file(WRITE "${graphicsReplacement}" "graphics replacement")
            endif()
        endif()
    else()
        foreach(copy IN LISTS staleCopies graphicsFiles)
            if(EXISTS "${copy}")
                message(FATAL_ERROR "Monolithic deployment retained ${copy}")
            endif()
        endforeach()
        file(READ "${deploy}/${secondName}" replacement)
        file(READ "${userLibrary}" userFile)
        if(NOT replacement STREQUAL "replacement" OR NOT userFile STREQUAL "keep")
            message(FATAL_ERROR "Runtime cleanup changed a user file")
        endif()
        if(CMAKE_HOST_WIN32)
            file(READ "${graphicsReplacement}" layers)
            if(NOT layers STREQUAL "graphics replacement")
                message(FATAL_ERROR "Runtime cleanup changed a replacement graphics library")
            endif()
        endif()
    endif()
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
