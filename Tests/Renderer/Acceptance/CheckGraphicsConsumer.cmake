cmake_minimum_required(VERSION 4.0)

file(READ "${compileDatabase}" commands)
string(JSON commandCount LENGTH "${commands}")
set(callbackCount 0)
math(EXPR lastEntry "${commandCount} - 1")
foreach(index RANGE 0 "${lastEntry}")
    string(
        JSON source
        GET "${commands}"
        "${index}"
        file
    )
    if(source MATCHES "[/\\\\]Project[/\\\\]FirstFrame[.]cpp$")
        math(EXPR callbackCount "${callbackCount} + 1")
        string(
            JSON command
            GET "${commands}"
            "${index}"
            command
        )
        if(
            command
                MATCHES
                "D3D12_AGILITY_SDK|EXTERNAL_CONFIG_FILEPATH|TheForge|Renderer[/\\\\]Private|VK_USE_PLATFORM|PG_RENDERER_TEST"
        )
            message(FATAL_ERROR "FirstFrame callback received a private renderer compile setting:\n${command}")
        endif()
    endif()
endforeach()
if(NOT callbackCount EQUAL 1)
    message(FATAL_ERROR "Expected exactly one FirstFrame callback compile command, got ${callbackCount}")
endif()
