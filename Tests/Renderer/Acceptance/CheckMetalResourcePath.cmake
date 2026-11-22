cmake_minimum_required(VERSION 4.0)

# Forge 将路径、gpu.data 和 NUL 存入 512 字节缓冲区。
set(longPath "${rendererProjectRunRoot}/资源")
string(LENGTH "${longPath}/" pathBytes)
if(pathBytes GREATER 504)
    message(FATAL_ERROR "Renderer test root is too long to prepare the resource boundary case")
endif()
while(pathBytes LESS 504)
    math(EXPR remaining "504 - ${pathBytes}")
    if(remaining EQUAL 1)
        string(APPEND longPath "x")
    else()
        if(remaining GREATER 200)
            set(remaining 160)
        endif()
        math(EXPR segmentBytes "${remaining} - 1")
        string(REPEAT "x" "${segmentBytes}" segment)
        string(APPEND longPath "/${segment}")
    endif()
    string(LENGTH "${longPath}/" pathBytes)
endwhile()

file(COPY "${deploy}/" DESTINATION "${longPath}")
execute_process(
    COMMAND
        "${longPath}/${executableName}"
    WORKING_DIRECTORY "${longPath}"
    RESULT_VARIABLE rejected
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 15
)
if(
    NOT rejected STREQUAL "1"
    OR NOT error MATCHES "Pangu Application: error domain=70672e72656e64 code=262"
    OR "${output}${error}" MATCHES "first-frame |GPU selection rules|Failed to enumerate any physical Metal devices"
)
    message(
        FATAL_ERROR
        "Metal must reject a 504-byte resource prefix before GPU init (exit=${rejected}):\n${output}${error}"
    )
endif()
message(STATUS "Metal rejected the 504-byte resource prefix before GPU init")
