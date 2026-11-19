# Verify that missing build toolchain environment is rejected before compiler probing.
if(NOT DEFINED PANGU_SOURCE_ROOT)
    message(FATAL_ERROR "PANGU_SOURCE_ROOT must be supplied to the test.")
endif()

set(validationScript "${PANGU_SOURCE_ROOT}/cmake/ValidateBuildEnvironment.cmake")

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env --unset=LLVM_ROOT "${CMAKE_COMMAND}" -P "${validationScript}"
    RESULT_VARIABLE llvmExit
    OUTPUT_VARIABLE llvmStdout
    ERROR_VARIABLE llvmStderr
)

if(llvmExit EQUAL 0 OR NOT llvmStderr MATCHES "Pangu Clang preflight: set LLVM_ROOT")
    message(FATAL_ERROR "Missing LLVM_ROOT must fail with a Pangu-specific diagnostic.")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E env --unset=ANDROID_HOME --unset=ANDROID_NDK_HOME "${CMAKE_COMMAND}"
        "-DCMAKE_TOOLCHAIN_FILE=/tmp/example/build/cmake/android.toolchain.cmake" -P "${validationScript}"
    RESULT_VARIABLE androidExit
    OUTPUT_VARIABLE androidStdout
    ERROR_VARIABLE androidStderr
)

if(androidExit EQUAL 0 OR NOT androidStderr MATCHES "Pangu Android preflight: set ANDROID_HOME")
    message(FATAL_ERROR "Missing ANDROID_HOME must fail with a Pangu-specific diagnostic.")
endif()
