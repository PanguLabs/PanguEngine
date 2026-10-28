cmake_minimum_required(VERSION 4.0)

# Toolchain regression audit: replay real CMake TU commands to inspect compiler/STL macros.
# This evidence is diagnostic. It is NOT an authenticated ABI Profile v1.
foreach(_required IN ITEMS PANGU_COMPILE_COMMANDS PANGU_EVIDENCE_OUTPUT)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "Missing required ${_required}.")
    endif()
endforeach()
file(REMOVE "${PANGU_EVIDENCE_OUTPUT}")
if(NOT EXISTS "${PANGU_COMPILE_COMMANDS}")
    message(FATAL_ERROR "Compiler command database not found: ${PANGU_COMPILE_COMMANDS}")
endif()
file(READ "${PANGU_COMPILE_COMMANDS}" _database)
string(JSON _num LENGTH "${_database}")

function(_pangu_define out text name)
    string(REGEX MATCH "\n#define ${name} ([^\n\r]*)" _hit "\n${text}")
    if(_hit)
        set(${out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
    else()
        set(${out} "absent" PARENT_SCOPE)
    endif()
endfunction()

# Test all target classes. The two Runtime files exercise actual production modules.
set(_sources
    "Source/Runtime/Foundation/Private/Error.cpp"
    "Source/Runtime/Module/Private/ModuleIdentity.cpp"
    "Tests/Toolchain/ThirdParty/CompiledProbe.cpp"
    "Tests/Toolchain/Module/PolicyProbe.cpp"
)
set(_names "Foundation" "Module" "ThirdParty" "PanguPolicy")
set(_values "{}")
set(_baseline_values "")
set(_row 0)
foreach(_source IN LISTS _sources)
    set(_found FALSE)
    math(EXPR _last "${_num} - 1")
    foreach(_i RANGE 0 ${_last})
        string(JSON _file GET "${_database}" ${_i} file)
        string(REPLACE "\\" "/" _normalized "${_file}")
        string(FIND "${_normalized}" "${_source}" _pos)
        if(NOT _pos EQUAL -1)
            string(JSON _cmd GET "${_database}" ${_i} command)
            string(JSON _working GET "${_database}" ${_i} directory)
            set(_found TRUE)
            break()
        endif()
    endforeach()
    if(NOT _found)
        message(FATAL_ERROR "ABI audit missing compiled source: ${_source}")
    endif()

    separate_arguments(_args NATIVE_COMMAND "${_cmd}")
    set(_filtered "")
    set(_skip FALSE)
    foreach(_arg IN LISTS _args)
        if(_skip)
            set(_skip FALSE)
        elseif(_arg STREQUAL "-o" OR _arg STREQUAL "-MF" OR _arg STREQUAL "-MT" OR _arg STREQUAL "-MQ")
            set(_skip TRUE)
        elseif(_arg STREQUAL "-c" OR _arg STREQUAL "-MD" OR _arg STREQUAL "-MMD" OR _arg STREQUAL "${_file}")
            # Compile-only/dependency flags must not overwrite the existing .obj/.o or .d files.
        else()
            list(APPEND _filtered "${_arg}")
        endif()
    endforeach()
    list(GET _names ${_row} _label)
    # Test-only override: this changes the actual Clang preprocessing flags.
    if(PANGU_AUDIT_INJECT_RTTI AND _label STREQUAL "ThirdParty")
        # Foundation was audited first. Flip its effective RTTI state so that
        # a globally disabled RTTI baseline still produces a real disagreement.
        if(_probe_Foundation___GXX_RTTI STREQUAL "absent" AND _probe_Foundation__CPPRTTI STREQUAL "absent")
            list(APPEND _filtered -frtti)
        else()
            list(APPEND _filtered -fno-rtti)
        endif()
    elseif(PANGU_AUDIT_INJECT_EXCEPTIONS AND _label STREQUAL "PanguPolicy")
        list(APPEND _filtered -fexceptions)
    endif()
    execute_process(
        COMMAND ${_filtered} -dM -E -x c++ "${_file}"
        WORKING_DIRECTORY "${_working}"
        RESULT_VARIABLE _exit
        OUTPUT_VARIABLE _macros
        ERROR_VARIABLE _stderr
    )
    if(NOT _exit EQUAL 0)
        message(FATAL_ERROR "ABI compiler macro audit failed on ${_source}:\n${_stderr}")
    endif()

    set(_one "{}")
    list(GET _names ${_row} _label)
    string(JSON _one SET "${_one}" "target" "\"${_label}\"")
    string(JSON _one SET "${_one}" "source" "\"${_source}\"")
    string(SHA256 _command_digest "${_cmd}")
    string(SHA256 _macro_digest "${_macros}")
    string(JSON _one SET "${_one}" "compile_command_sha256" "\"${_command_digest}\"")
    string(JSON _one SET "${_one}" "predefined_macros_sha256" "\"${_macro_digest}\"")
    foreach(_macro IN ITEMS
            __clang_major__ __clang_minor__ __clang_patchlevel__
            __cplusplus __SIZEOF_POINTER__ __SIZEOF_WCHAR_T__ __BYTE_ORDER__
            __ORDER_LITTLE_ENDIAN__ __ORDER_BIG_ENDIAN__
            __EXCEPTIONS __cpp_exceptions _CPPUNWIND __GXX_RTTI _CPPRTTI
            _LIBCPP_VERSION _LIBCPP_ABI_VERSION _LIBCPP_ABI_NAMESPACE
            _MSVC_STL_VERSION _MSVC_STL_UPDATE _ITERATOR_DEBUG_LEVEL
            _DLL _MT _MSC_VER _MSC_FULL_VER __ANDROID__ __ANDROID_API__ __APPLE__ _WIN32 __linux__
            __SSE4_2__ __AVX__ __AVX2__ __AVX512F__ __ARM_NEON)
        _pangu_define(_value "${_macros}" "${_macro}")
        # Predefined macro values may be quoted expressions; stringify safely as JSON.
        string(JSON _encoded SET "{}" value "\"${_value}\"")
        string(JSON _value_json GET "${_encoded}" value)
        # Audit only simple integral/token macros; non-JSON-safe tokens must be rejected.
        if(NOT _value MATCHES "^[A-Za-z0-9_().+ -]+$")
            message(FATAL_ERROR "Unsafe/unknown macro expansion for ${_macro}: ${_value}")
        endif()
        string(JSON _one SET "${_one}" "${_macro}" "\"${_value}\"")
        set("_probe_${_label}_${_macro}" "${_value}")
    endforeach()
    string(JSON _values SET "${_values}" "${_label}" "${_one}")
    math(EXPR _row "${_row} + 1")
endforeach()

# ABI-defining facts must be the same for all four real C++ target types.
foreach(_macro IN ITEMS
        __clang_major__ __clang_minor__ __clang_patchlevel__ __cplusplus
        __SIZEOF_POINTER__ __SIZEOF_WCHAR_T__ __BYTE_ORDER__
        _LIBCPP_VERSION _LIBCPP_ABI_VERSION _LIBCPP_ABI_NAMESPACE
        _MSVC_STL_VERSION _MSVC_STL_UPDATE _ITERATOR_DEBUG_LEVEL
        _DLL _MT _MSC_VER _MSC_FULL_VER __ANDROID_API__
        __SSE4_2__ __AVX__ __AVX2__ __AVX512F__ __ARM_NEON
        _CPPRTTI __GXX_RTTI)
    foreach(_target IN ITEMS Module ThirdParty PanguPolicy)
        if(NOT "${_probe_Foundation_${_macro}}" STREQUAL "${_probe_${_target}_${_macro}}")
            message(FATAL_ERROR
                "ABI macro disagreement ${_macro}: Foundation='${_probe_Foundation_${_macro}}' "
                "vs ${_target}='${_probe_${_target}_${_macro}}'.")
        endif()
    endforeach()
endforeach()

# Pangu Modules have no exceptions; ThirdParty does NOT have this Pangu-specific restriction.
foreach(_target IN ITEMS Foundation Module PanguPolicy)
    foreach(_macro IN ITEMS __EXCEPTIONS __cpp_exceptions _CPPUNWIND)
        if(NOT "${_probe_${_target}_${_macro}}" STREQUAL "absent")
            message(FATAL_ERROR "${_target} violates no-exceptions policy: ${_macro} is defined.")
        endif()
    endforeach()
endforeach()
if("${_probe_Foundation___cplusplus}" STREQUAL "absent"
        OR "${_probe_Foundation___clang_major__}" STREQUAL "absent")
    message(FATAL_ERROR "Missing required Clang/C++ macros.")
endif()
# Mark as evidence; never treat an equal macro set as proof that SDK/CRT/ABI binaries match.
set(_result "{}")
string(JSON _result SET "${_result}" schema 1)
string(JSON _result SET "${_result}" kind "\"compiled-tu-macro-evidence\"")
string(JSON _result SET "${_result}" trusted false)
string(JSON _result SET "${_result}" targets "${_values}")
get_filename_component(_dir "${PANGU_EVIDENCE_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_dir}")
file(WRITE "${PANGU_EVIDENCE_OUTPUT}" "${_result}\n")
message(STATUS "ABI compile audit passed; generated UNTRUSTED TU macro evidence: ${PANGU_EVIDENCE_OUTPUT}")
