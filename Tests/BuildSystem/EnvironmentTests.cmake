# Verify that missing build toolchain environment is rejected before compiler probing.
if(NOT DEFINED PANGU_SOURCE_ROOT)
    message(FATAL_ERROR "PANGU_SOURCE_ROOT must be supplied to the test.")
endif()

set(_pangu_validation "${PANGU_SOURCE_ROOT}/cmake/ValidateBuildEnvironment.cmake")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env --unset=LLVM_ROOT
        "${CMAKE_COMMAND}" -P "${_pangu_validation}"
    RESULT_VARIABLE _pangu_llvm_exit
    OUTPUT_VARIABLE _pangu_llvm_stdout
    ERROR_VARIABLE _pangu_llvm_stderr
)

if(_pangu_llvm_exit EQUAL 0
        OR NOT _pangu_llvm_stderr MATCHES "Pangu Clang preflight: set LLVM_ROOT")
    message(FATAL_ERROR "Missing LLVM_ROOT must fail with a Pangu-specific diagnostic.")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env --unset=ANDROID_HOME --unset=ANDROID_NDK_HOME
        "${CMAKE_COMMAND}"
        "-DCMAKE_TOOLCHAIN_FILE=/tmp/example/build/cmake/android.toolchain.cmake"
        -P "${_pangu_validation}"
    RESULT_VARIABLE _pangu_android_exit
    OUTPUT_VARIABLE _pangu_android_stdout
    ERROR_VARIABLE _pangu_android_stderr
)

if(_pangu_android_exit EQUAL 0
        OR NOT _pangu_android_stderr MATCHES "Pangu Android preflight: set ANDROID_HOME")
    message(FATAL_ERROR "Missing ANDROID_HOME must fail with a Pangu-specific diagnostic.")
endif()
