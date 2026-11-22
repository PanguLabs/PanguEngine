include_guard(GLOBAL)

function(_pangu_prepare_patch_text inputFile outputFile)
    file(READ "${inputFile}" text)
    if(text STREQUAL "")
        file(WRITE "${outputFile}" "")
        return()
    endif()
    string(
        REPLACE "\r\n"
        "\n"
        text
        "${text}"
    )
    # CONFIGURE 写入最后一个 LF。单次变量替换保留文件本身的 @...@ 字面量。
    string(
        REGEX REPLACE "\n$"
        ""
        text
        "${text}"
    )
    file(CONFIGURE OUTPUT "${outputFile}" CONTENT "@text@" @ONLY NEWLINE_STYLE LF)
endfunction()

# 应用单个 UTF-8 文本文件的 Git 补丁，校验 LF 文本的输入和输出。
function(pangu_apply_third_party_patch)
    cmake_parse_arguments(
        PARSE_ARGV 0
        arg
        ""
        "SOURCE_DIR;RELATIVE_PATH;PATCH_FILE;INPUT_SHA256;OUTPUT_SHA256;OUTPUT_FILE"
        ""
    )
    if(arg_UNPARSED_ARGUMENTS OR arg_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "pangu_apply_third_party_patch received invalid arguments")
    endif()
    foreach(
        required
        IN
        ITEMS
            SOURCE_DIR
            RELATIVE_PATH
            PATCH_FILE
            INPUT_SHA256
            OUTPUT_SHA256
            OUTPUT_FILE
    )
        if("${arg_${required}}" STREQUAL "")
            message(FATAL_ERROR "pangu_apply_third_party_patch requires ${required}")
        endif()
    endforeach()
    if(IS_ABSOLUTE "${arg_RELATIVE_PATH}" OR arg_RELATIVE_PATH MATCHES "(^|[/\\\\])\\.\\.([/\\\\]|$)")
        message(FATAL_ERROR "Patch path must remain relative to SOURCE_DIR: ${arg_RELATIVE_PATH}")
    endif()

    cmake_path(
        ABSOLUTE_PATH arg_SOURCE_DIR
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        NORMALIZE
        OUTPUT_VARIABLE sourceDirectory
    )
    cmake_path(
        ABSOLUTE_PATH arg_PATCH_FILE
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        NORMALIZE
        OUTPUT_VARIABLE patchFile
    )
    cmake_path(
        ABSOLUTE_PATH arg_OUTPUT_FILE
        BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        NORMALIZE
        OUTPUT_VARIABLE outputFile
    )
    cmake_path(
        IS_PREFIX
        sourceDirectory
        "${outputFile}"
        NORMALIZE
        outputInSource
    )
    if(outputInSource)
        message(FATAL_ERROR "Patch output must be outside SOURCE_DIR: ${outputFile}")
    endif()
    set(sourceFile "${sourceDirectory}/${arg_RELATIVE_PATH}")
    set_property(
        DIRECTORY
        APPEND
        PROPERTY
            CMAKE_CONFIGURE_DEPENDS
                "${sourceFile}"
                "${patchFile}"
    )

    # 各输出使用独立副本，短目录名控制 Windows 深层构建的路径长度。
    string(SHA256 patchKey "${outputFile}")
    string(SUBSTRING "${patchKey}" 0 16 patchKey)
    set(workDirectory "${CMAKE_CURRENT_BINARY_DIR}/PatchWork/${patchKey}")
    set(patchRoot "${workDirectory}/Source")
    set(patchInput "${patchRoot}/${arg_RELATIVE_PATH}")
    set(patchCopy "${workDirectory}/Input.patch")
    _pangu_prepare_patch_text("${sourceFile}" "${patchInput}")
    _pangu_prepare_patch_text("${patchFile}" "${patchCopy}")

    file(SHA256 "${patchInput}" actualHash)
    if(NOT actualHash STREQUAL arg_INPUT_SHA256)
        message(FATAL_ERROR "Third-party patch input changed: ${sourceFile} (SHA-256 ${actualHash})")
    endif()

    find_package(Git REQUIRED)
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${workDirectory}" "${GIT_EXECUTABLE}" -c
            core.autocrlf=false apply "${patchCopy}"
        WORKING_DIRECTORY "${patchRoot}"
        RESULT_VARIABLE result
        ERROR_VARIABLE error
    )
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Third-party patch failed: ${patchFile}\n${error}")
    endif()
    file(SHA256 "${patchInput}" actualHash)
    if(NOT actualHash STREQUAL arg_OUTPUT_SHA256)
        message(FATAL_ERROR "Third-party patch output changed: ${patchFile} (SHA-256 ${actualHash})")
    endif()
    cmake_path(GET outputFile PARENT_PATH outputDirectory)
    file(MAKE_DIRECTORY "${outputDirectory}")
    file(COPY_FILE "${patchInput}" "${outputFile}" ONLY_IF_DIFFERENT)
endfunction()
