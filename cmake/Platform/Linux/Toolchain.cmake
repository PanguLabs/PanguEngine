# Native Linux Clang/libc++ toolchain. Applied before project(), including third-party targets.
# Keep compiler/standard-library selection separate from Pangu's code warning policy.
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Pangu Linux toolchain requires a Linux build host.")
endif()
if("$ENV{LLVM_ROOT}" STREQUAL "" OR NOT IS_DIRECTORY "$ENV{LLVM_ROOT}")
    message(FATAL_ERROR "Set LLVM_ROOT to the selected Clang 23 installation.")
endif()

set(CMAKE_C_COMPILER "$ENV{LLVM_ROOT}/bin/clang")
set(CMAKE_CXX_COMPILER "$ENV{LLVM_ROOT}/bin/clang++")
set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "$ENV{LLVM_ROOT}/bin/clang-scan-deps" CACHE FILEPATH "")

get_filename_component(_pangu_clang_real "${CMAKE_CXX_COMPILER}" REALPATH)
get_filename_component(_pangu_clang_bin "${_pangu_clang_real}" DIRECTORY)
get_filename_component(_pangu_clang_prefix "${_pangu_clang_bin}" DIRECTORY)
set(PANGU_LIBCXX_ROOT "${_pangu_clang_prefix}" CACHE PATH
    "Matching LLVM libc++ prefix (override for a separately installed libc++)")
# Compiler checks reload this toolchain in a separate project/cache.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES PANGU_LIBCXX_ROOT)
list(REMOVE_DUPLICATES CMAKE_TRY_COMPILE_PLATFORM_VARIABLES)

execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -dumpmachine
    OUTPUT_VARIABLE _pangu_target_triple
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

set(_pangu_libcxx_headers "${PANGU_LIBCXX_ROOT}/include/c++/v1")
set(_pangu_libcxx_config "${PANGU_LIBCXX_ROOT}/include/${_pangu_target_triple}/c++/v1")
set(_pangu_libcxx_libdir "${PANGU_LIBCXX_ROOT}/lib/${_pangu_target_triple}")

if(NOT EXISTS "${_pangu_libcxx_headers}/__config"
        OR NOT EXISTS "${_pangu_libcxx_config}/__config_site"
        OR NOT EXISTS "${_pangu_libcxx_libdir}/libc++.so")
    message(FATAL_ERROR
        "Matching LLVM libc++ headers, __config_site or lib missing under PANGU_LIBCXX_ROOT='${PANGU_LIBCXX_ROOT}'.")
endif()

# Compile with exactly the selected libc++ headers; link with libc++ and its matching runtime path.
# These initialization flags also reach ordinary third-party C++ targets.
# CMake may re-enter a toolchain file while detecting compilers; append only once.
string(FIND "${CMAKE_CXX_FLAGS_INIT}" "-nostdinc++" _pangu_has_libcxx_flags)
if(_pangu_has_libcxx_flags EQUAL -1)
    string(APPEND CMAKE_CXX_FLAGS_INIT
        " -nostdinc++"
        " -isystem \"${_pangu_libcxx_headers}\""
        " -isystem \"${_pangu_libcxx_config}\""
    )
endif()
foreach(_pangu_kind IN ITEMS EXE SHARED MODULE)
    # An unrelated library's rpath does not imply any libc++ flag is present.
    foreach(_pangu_link_flag IN ITEMS
            "-stdlib=libc++"
            "-L\"${_pangu_libcxx_libdir}\""
            "-Wl,-rpath,\"${_pangu_libcxx_libdir}\"")
        string(FIND " ${CMAKE_${_pangu_kind}_LINKER_FLAGS_INIT} "
            " ${_pangu_link_flag} " _pangu_has_libcxx_link)
        if(_pangu_has_libcxx_link EQUAL -1)
            string(APPEND CMAKE_${_pangu_kind}_LINKER_FLAGS_INIT " ${_pangu_link_flag}")
        endif()
    endforeach()
endforeach()
unset(_pangu_link_flag)
unset(_pangu_has_libcxx_flags)
unset(_pangu_has_libcxx_link)

unset(_pangu_clang_real)
unset(_pangu_clang_bin)
unset(_pangu_clang_prefix)
unset(_pangu_target_triple)
unset(_pangu_libcxx_headers)
unset(_pangu_libcxx_config)
unset(_pangu_libcxx_libdir)
unset(_pangu_kind)
