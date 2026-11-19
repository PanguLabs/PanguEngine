function(_pangu_check_mobile_link_inputs binaryDir)
    _pangu_run_project_command(
        PASS
        ""
        ninja
        -C
        "${binaryDir}"
        -t
        rules
    )
    string(
        REGEX MATCHALL "(C|CXX)_(EXECUTABLE|SHARED_LIBRARY|MODULE_LIBRARY)_LINKER__[^ \t\r\n]+"
        linkRules
        "${commandStdout}"
    )
    if(NOT linkRules)
        message(FATAL_ERROR "Cannot inspect target-native linker rules in ${binaryDir}")
    endif()

    # compdb -x 展开 Ninja 的响应文件，只读取目标侧最终链接规则，跳过宿主工具的自定义规则。
    _pangu_run_project_command(
        PASS
        ""
        ninja
        -C
        "${binaryDir}"
        -t
        compdb
        -x
        ${linkRules}
    )
    file(WRITE "${PANGU_TEST_ROOT}/MobileFinalLinkCommands.json" "${commandStdout}")
    string(JSON count LENGTH "${commandStdout}")
    if(count EQUAL 0)
        message(FATAL_ERROR "Missing target-native final link commands in ${binaryDir}")
    endif()
    # clang_rt 是目标编译器的运行库。clangFrontend、clang-cpp 等属于宿主 SDK。
    set(library "(lib)?((gtest|gmock|googletest|googlemock|llvm)[a-z0-9_.+-]*|clang[a-z0-9.+-]*|pangumetafrontend)")
    set(archivePath "(^|[/ \t,;:\"'=])${library}[.](a|lib|so|dylib)([.][0-9]+)*($|[ \t,;\"'])")
    set(libraryFlag "(^|[ \t,;\"'])-l[ \t]*[\"']?${library}($|[ \t,;\"'])")
    math(EXPR last "${count} - 1")
    foreach(index RANGE ${last})
        string(
            JSON command
            GET "${commandStdout}"
            ${index}
            command
        )
        string(
            REPLACE "\\"
            "/"
            command
            "${command}"
        )
        string(TOLOWER "${command}" command)
        if(command MATCHES "${archivePath}|${libraryFlag}")
            set(input "${CMAKE_MATCH_0}")
            string(
                JSON output
                GET "${commandStdout}"
                ${index}
                output
            )
            message(FATAL_ERROR "Unexpected target-native library input '${input}' in final link of ${output}")
        endif()
    endforeach()
endfunction()
