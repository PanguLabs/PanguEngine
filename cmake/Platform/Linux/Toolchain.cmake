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

get_filename_component(panguClangRealPath "${CMAKE_CXX_COMPILER}" REALPATH)
get_filename_component(panguClangDirectory "${panguClangRealPath}" DIRECTORY)
get_filename_component(panguClangPrefix "${panguClangDirectory}" DIRECTORY)
set(PANGU_LIBCXX_ROOT
    "${panguClangPrefix}"
    CACHE PATH
    "Matching LLVM libc++ prefix (override for a separately installed libc++)"
)
# Compiler checks reload this toolchain in a separate project/cache.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES PANGU_LIBCXX_ROOT)
list(REMOVE_DUPLICATES CMAKE_TRY_COMPILE_PLATFORM_VARIABLES)

execute_process(
    COMMAND
        "${CMAKE_CXX_COMPILER}" -dumpmachine
    OUTPUT_VARIABLE panguTargetTriple
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

set(panguLibcxxHeaders "${PANGU_LIBCXX_ROOT}/include/c++/v1")
set(panguLibcxxConfig "${PANGU_LIBCXX_ROOT}/include/${panguTargetTriple}/c++/v1")
set(panguLibcxxLibraryDirectory "${PANGU_LIBCXX_ROOT}/lib/${panguTargetTriple}")

if(
    NOT EXISTS
        "${panguLibcxxHeaders}/__config"
    OR NOT EXISTS
        "${panguLibcxxConfig}/__config_site"
    OR NOT EXISTS
        "${panguLibcxxLibraryDirectory}/libc++.so"
)
    message(
        FATAL_ERROR
        "Matching LLVM libc++ headers, __config_site or lib missing under PANGU_LIBCXX_ROOT='${PANGU_LIBCXX_ROOT}'."
    )
endif()

# Compile with exactly the selected libc++ headers; link with libc++ and its matching runtime path.
# These initialization flags also reach ordinary third-party C++ targets.
# CMake may re-enter a toolchain file while detecting compilers; append only once.
string(
    FIND "${CMAKE_CXX_FLAGS_INIT}"
    "-nostdinc++"
    panguLibcxxFlagIndex
)
if(panguLibcxxFlagIndex EQUAL -1)
    string(
        APPEND CMAKE_CXX_FLAGS_INIT
        " -nostdinc++"
        " -isystem \"${panguLibcxxHeaders}\""
        " -isystem \"${panguLibcxxConfig}\""
    )
endif()
foreach(panguLinkerKind IN ITEMS EXE SHARED MODULE)
    # An unrelated library's rpath does not imply any libc++ flag is present.
    foreach(
        panguLinkerFlag
        IN
        ITEMS
            "-stdlib=libc++"
            "-L\"${panguLibcxxLibraryDirectory}\""
            "-Wl,-rpath,\"${panguLibcxxLibraryDirectory}\""
    )
        string(
            FIND " ${CMAKE_${panguLinkerKind}_LINKER_FLAGS_INIT} "
            " ${panguLinkerFlag} "
            panguLibcxxLinkFlagIndex
        )
        if(panguLibcxxLinkFlagIndex EQUAL -1)
            string(APPEND CMAKE_${panguLinkerKind}_LINKER_FLAGS_INIT " ${panguLinkerFlag}")
        endif()
    endforeach()
endforeach()
unset(panguLinkerFlag)
unset(panguLibcxxFlagIndex)
unset(panguLibcxxLinkFlagIndex)

unset(panguClangRealPath)
unset(panguClangDirectory)
unset(panguClangPrefix)
unset(panguTargetTriple)
unset(panguLibcxxHeaders)
unset(panguLibcxxConfig)
unset(panguLibcxxLibraryDirectory)
unset(panguLinkerKind)
