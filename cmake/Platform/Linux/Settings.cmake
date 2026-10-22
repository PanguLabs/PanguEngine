# Prefer Clang's matching libc++ installation; never fall back to libstdc++.
# The compiler may be reached through a symlink outside the LLVM prefix.
get_filename_component(_pangu_clang_real "${CMAKE_CXX_COMPILER}" REALPATH)
get_filename_component(_pangu_clang_bin "${_pangu_clang_real}" DIRECTORY)
get_filename_component(_pangu_clang_prefix "${_pangu_clang_bin}" DIRECTORY)

set(PANGU_LIBCXX_ROOT "${_pangu_clang_prefix}" CACHE PATH
    "LLVM libc++ installation prefix (override for a separately installed libc++)")

execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -dumpmachine
    OUTPUT_VARIABLE _pangu_target_triple
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)

set(_pangu_libcxx_headers "${PANGU_LIBCXX_ROOT}/include/c++/v1")
set(_pangu_libcxx_config "${PANGU_LIBCXX_ROOT}/include/${_pangu_target_triple}/c++/v1")
set(_pangu_libcxx_libdir "${PANGU_LIBCXX_ROOT}/lib/${_pangu_target_triple}")

if(NOT EXISTS "${_pangu_libcxx_headers}/__config")
    message(FATAL_ERROR "libc++ headers missing; set PANGU_LIBCXX_ROOT to the matching LLVM prefix.")
endif()
if(NOT EXISTS "${_pangu_libcxx_libdir}/libc++.so")
    message(FATAL_ERROR "libc++ library missing in '${_pangu_libcxx_libdir}'.")
endif()

if(EXISTS "${_pangu_libcxx_headers}/__config")
    if(NOT EXISTS "${_pangu_libcxx_config}/__config_site")
        message(FATAL_ERROR
            "libc++ __config_site missing for '${_pangu_target_triple}' in '${_pangu_libcxx_config}'.")
    endif()

    target_compile_options(PanguBuildOptions INTERFACE
        -nostdinc++
    )
    target_include_directories(PanguBuildOptions SYSTEM INTERFACE
        "${_pangu_libcxx_headers}"
        "${_pangu_libcxx_config}"
    )
    if(EXISTS "${_pangu_libcxx_libdir}/libc++.so")
        target_link_directories(PanguBuildOptions INTERFACE "${_pangu_libcxx_libdir}")
        target_link_options(PanguBuildOptions INTERFACE
            "LINKER:-rpath,${_pangu_libcxx_libdir}"
        )
    endif()
endif()

unset(_pangu_clang_real)
unset(_pangu_clang_bin)
unset(_pangu_clang_prefix)
unset(_pangu_target_triple)
unset(_pangu_libcxx_headers)
unset(_pangu_libcxx_config)
unset(_pangu_libcxx_libdir)
