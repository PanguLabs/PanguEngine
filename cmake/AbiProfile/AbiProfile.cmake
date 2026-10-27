include_guard(GLOBAL)

# CMake-only integration: export target facts for the C++ dependency graph verifier.
# Root Module cutover remains Task 11.
function(pangu_export_abi_dependency_graph)
    cmake_parse_arguments(ABI "" "OUTPUT;ROOT" "TARGETS;ALIASES;MANAGED" ${ARGN})
    if(ABI_UNPARSED_ARGUMENTS OR ABI_KEYWORDS_MISSING_VALUES
       OR NOT ABI_OUTPUT OR NOT ABI_ROOT OR NOT ABI_TARGETS)
        message(FATAL_ERROR "Invalid ABI dependency graph arguments")
    endif()
    if(NOT TARGET "${ABI_ROOT}")
        message(FATAL_ERROR "ABI graph root not defined: ${ABI_ROOT}")
    endif()
    list(FIND ABI_TARGETS "${ABI_ROOT}" _root_index)
    if(_root_index EQUAL -1)
        message(FATAL_ERROR "ABI root is absent from graph enumeration")
    endif()
    if(CMAKE_CONFIGURATION_TYPES AND NOT ABI_OUTPUT MATCHES "\\$<CONFIG>")
        message(FATAL_ERROR "ABI graph output must be per configuration")
    endif()
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        message(FATAL_ERROR "ABI graph requires a named build configuration")
    endif()

    set(_metadata "{\n\"schema\":1,\"format\":\"cmake-actual-v1\",")
    string(APPEND _metadata "\"configuration\":\"$<CONFIG>\",\"aliases\":[")
    set(_separator "")
    foreach(alias IN LISTS ABI_ALIASES)
        if(NOT TARGET "${alias}")
            message(FATAL_ERROR "ABI graph alias missing: ${alias}")
        endif()
        get_target_property(_real "${alias}" ALIASED_TARGET)
        if(NOT _real OR NOT _real IN_LIST ABI_TARGETS)
            message(FATAL_ERROR "ABI graph alias unresolved: ${alias}")
        endif()
        if(NOT alias MATCHES "^[A-Za-z_][A-Za-z0-9_:+.-]*$"
           OR NOT _real MATCHES "^[A-Za-z_][A-Za-z0-9_:+.-]*$")
            message(FATAL_ERROR "ABI graph unsafe alias: ${alias}")
        endif()
        string(APPEND _metadata "${_separator}{\"name\":\"${alias}\",\"target\":\"${_real}\"}")
        set(_separator ",")
    endforeach()
    string(APPEND _metadata "],\"targets\":[")

    set(_separator "")
    foreach(target IN LISTS ABI_TARGETS)
        if(NOT TARGET "${target}" OR NOT target MATCHES "^[A-Za-z_][A-Za-z0-9_:+.-]*$")
            message(FATAL_ERROR "ABI graph target missing or unsafe: ${target}")
        endif()
        get_target_property(_aliased "${target}" ALIASED_TARGET)
        if(_aliased)
            message(FATAL_ERROR "Register ALIAS separately: ${target}")
        endif()
        get_target_property(_type "${target}" TYPE)
        get_target_property(_imported "${target}" IMPORTED)
        list(FIND ABI_MANAGED "${target}" _managed_index)
        if(_managed_index EQUAL -1)
            set(_managed false)
        else()
            set(_managed true)
        endif()
        if(_imported)
            set(_imported_json true)
        else()
            set(_imported_json false)
        endif()
        # CMake owns generator-expression evaluation for each Target. Direct
        # repeats are not removed; INTERFACE-only edges are additionally read.
        # LINK_LIBRARIES cannot recreate original PUBLIC/PRIVATE provenance:
        # the C++ graph conservatively models the union.
        string(APPEND _metadata
            "${_separator}{\"name\":\"${target}\",\"cmake_type\":\"${_type}\","
            "\"imported\":${_imported_json},\"managed\":${_managed},\"links\":[],"
            "\"sources_evaluated\":\"$<TARGET_GENEX_EVAL:${target},$<TARGET_PROPERTY:${target},SOURCES>>\","
            "\"interface_sources_evaluated\":\"$<TARGET_GENEX_EVAL:${target},$<TARGET_PROPERTY:${target},INTERFACE_SOURCES>>\","
            "\"request_evaluated\":\"$<TARGET_GENEX_EVAL:${target},$<TARGET_PROPERTY:${target},PANGU_ABI_REQUEST_FILE>>\","
            "\"links_evaluated\":\"$<TARGET_GENEX_EVAL:${target},$<TARGET_PROPERTY:${target},LINK_LIBRARIES>>\","
            "\"interface_evaluated\":\"$<TARGET_GENEX_EVAL:${target},$<TARGET_PROPERTY:${target},INTERFACE_LINK_LIBRARIES>>\"}")
        set(_separator ",")
    endforeach()
    string(APPEND _metadata "]}\n")
    file(GENERATE OUTPUT "${ABI_OUTPUT}" CONTENT "${_metadata}" TARGET "${ABI_ROOT}")
endfunction()

# Task 11: register one real CMake Module archive without introducing a
# parallel library target. Invoke from the directory owning that target.
function(_pangu_abi_check_launcher target)
    get_property(_configured TARGET "${target}" PROPERTY CXX_COMPILER_LAUNCHER SET)
    if(_configured)
        get_target_property(_launcher "${target}" CXX_COMPILER_LAUNCHER)
        if(NOT "${_launcher}" STREQUAL "")
            message(FATAL_ERROR "Unsupported pre-existing CXX_COMPILER_LAUNCHER for ${target}")
        endif()
    endif()
endfunction()

function(pangu_register_abi_module target)
    if(NOT TARGET "${target}" OR target MATCHES "[^A-Za-z0-9_]")
        message(FATAL_ERROR "ABI Module requires a defined, safe target name: ${target}")
    endif()
    get_target_property(_kind "${target}" TYPE)
    if(NOT _kind STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "ABI v1 currently requires STATIC Module: ${target}")
    endif()
    get_target_property(_already "${target}" PANGU_ABI_REQUEST_FILE)
    if(_already)
        message(FATAL_ERROR "ABI Module registered more than once: ${target}")
    endif()
    _pangu_abi_check_launcher("${target}")
    set(_output "${CMAKE_BINARY_DIR}/Abi/$<CONFIG>/${target}")
    set(_request "${_output}/request.json")
    set_property(TARGET "${target}" PROPERTY PANGU_ABI_REQUEST_FILE "${_request}")
    # Dynamic module mapper response files do not exist before Guard.
    # ABI v1 rejects C++ module TU participation; use ordinary source scanning.
    set_property(TARGET "${target}" PROPERTY CXX_SCAN_FOR_MODULES OFF)
    set_property(GLOBAL APPEND PROPERTY PANGU_ABI_MODULE_TARGETS "${target}")
    # CMake 3.30+ performs config-aware transitive link-property evaluation.
    # Only ordering metadata is carried; C++ still owns ABI graph verification.
    set_property(TARGET "${target}" APPEND PROPERTY TRANSITIVE_LINK_PROPERTIES
        PANGU_ABI_MODULE_PREREQUISITES PANGU_ABI_TECH_TARGETS)
    set_property(TARGET "${target}" PROPERTY INTERFACE_PANGU_ABI_MODULE_PREREQUISITES "${target}")
    set_property(TARGET "${target}" PROPERTY PANGU_ABI_SIGNATURE_HEADER "${_output}/inputs.h")

    # TARGET POST_BUILD must be registered in its owning CMake directory.
    # This also preserves direct cmake --build --target <Module> semantics.
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../BuildTools.cmake")
    set(PANGU_BUILD_TOOLS_SOURCE_ROOT "${PANGU_ABI_SOURCE_ROOT}")
    pangu_prepare_build_tools(_nativeTool)
    set(_guard "${target}AbiGuard")
    add_custom_target("${_guard}"
        COMMAND "${_nativeTool}" guard --request "${_request}"
        DEPENDS "$<TARGET_PROPERTY:${target},PANGU_ABI_MODULE_PREREQUISITES>"
        BYPRODUCTS "${_output}/inputs.h"
        VERBATIM)
    set_property(TARGET "${target}" PROPERTY
        CXX_COMPILER_LAUNCHER "${_nativeTool};compile;--request;${_request};--")
    target_compile_options("${target}" PRIVATE "-include" "${_output}/inputs.h")
    add_dependencies("${target}" "${_guard}")
    add_custom_command(TARGET "${target}" POST_BUILD
        COMMAND "${_nativeTool}" collect --request "${_request}"
        VERBATIM)
endfunction()

# Technical STATIC libraries are ordinary CMake targets, not Pangu Modules.
# Their strongly connected components are guarded and collected as a group.
function(pangu_register_abi_static target)
    if(NOT TARGET "${target}" OR target MATCHES "[^A-Za-z0-9_]")
        message(FATAL_ERROR "Invalid technical STATIC target: ${target}")
    endif()
    get_target_property(_kind "${target}" TYPE)
    if(NOT _kind STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "Technical ABI dependency must be STATIC: ${target}")
    endif()
    get_target_property(_already "${target}" PANGU_ABI_REQUEST_FILE)
    if(_already)
        message(FATAL_ERROR "ABI Target registered more than once: ${target}")
    endif()
    _pangu_abi_check_launcher("${target}")
    set(_output "${CMAKE_BINARY_DIR}/Abi/$<CONFIG>/${target}")
    set(_request "${_output}/request.json")
    set_property(TARGET "${target}" PROPERTY PANGU_ABI_REQUEST_FILE "${_request}")
    set_property(TARGET "${target}" PROPERTY CXX_SCAN_FOR_MODULES OFF)
    set_property(GLOBAL APPEND PROPERTY PANGU_ABI_TECH_TARGETS "${target}")
    set_property(TARGET "${target}" PROPERTY INTERFACE_PANGU_ABI_TECH_TARGETS "${target}")
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../BuildTools.cmake")
    set(PANGU_BUILD_TOOLS_SOURCE_ROOT "${PANGU_ABI_SOURCE_ROOT}")
    pangu_prepare_build_tools(_nativeTool)
    set_property(TARGET "${target}" PROPERTY
        CXX_COMPILER_LAUNCHER "${_nativeTool};compile;--request;${_request};--")
    target_compile_options("${target}" PRIVATE "-include" "${_output}/inputs.h")
endfunction()

# Encode raw text without path normalization. Environment values are compared
# byte for byte, including empty strings, backslashes and control characters.
function(_pangu_abi_request_text out json key value)
    string(REPLACE "\\" "\\\\" _text "${value}")
    string(REPLACE "\"" "\\\"" _text "${_text}")
    foreach(_code RANGE 1 31)
        string(ASCII ${_code} _control)
        math(EXPR _hex "${_code}" OUTPUT_FORMAT HEXADECIMAL)
        string(SUBSTRING "${_hex}" 2 -1 _hex)
        string(LENGTH "${_hex}" _length)
        if(_length EQUAL 1)
            set(_hex "0${_hex}")
        endif()
        string(REPLACE "${_control}" "\\u00${_hex}" _text "${_text}")
    endforeach()
    string(JSON _result SET "${json}" "${key}" "\"${_text}\"")
    set(${out} "${_result}" PARENT_SCOPE)
endfunction()

# JSON paths are always encoded with forward slashes, including on Windows.
function(_pangu_abi_request_string out json key value)
    string(REPLACE "\\" "/" _path "${value}")
    _pangu_abi_request_text(_result "${json}" "${key}" "${_path}")
    set(${out} "${_result}" PARENT_SCOPE)
endfunction()

# Complete CMake integration occurs after all relevant library targets exist.
function(pangu_finalize_abi_profiles)
    cmake_parse_arguments(ABI "" "" "TARGETS;ALIASES" ${ARGN})
    if(ABI_UNPARSED_ARGUMENTS OR ABI_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "Invalid ABI finalize arguments")
    endif()
    get_property(_managed GLOBAL PROPERTY PANGU_ABI_MODULE_TARGETS)
    if(NOT _managed)
        message(FATAL_ERROR "No managed ABI Modules registered")
    endif()
    if(NOT ABI_TARGETS)
        message(FATAL_ERROR "ABI finalize requires full CMake Target enumeration")
    endif()
    get_property(_technical GLOBAL PROPERTY PANGU_ABI_TECH_TARGETS)
    set(_tracked ${_managed} ${_technical})
    foreach(_module IN LISTS _tracked)
        if(NOT _module IN_LIST ABI_TARGETS)
            message(FATAL_ERROR "Registered ABI Target absent from graph: ${_module}")
        endif()
    endforeach()
    list(GET _managed 0 _graphRoot)
    set(_graph "${CMAKE_BINARY_DIR}/Abi/$<CONFIG>/link-graph.json")
    set(_aliasArgs)
    if(ABI_ALIASES)
        list(APPEND _aliasArgs ALIASES ${ABI_ALIASES})
    endif()
    pangu_export_abi_dependency_graph(
        OUTPUT "${_graph}" ROOT "${_graphRoot}"
        TARGETS ${ABI_TARGETS} ${_aliasArgs} MANAGED ${_managed})


    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(_host "macos")
    else()
        string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" _host)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(_target "macos")
    else()
        string(TOLOWER "${CMAKE_SYSTEM_NAME}" _target)
    endif()
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(_style "windows")
    else()
        set(_style "posix")
    endif()

    # Raw configuration facts only; the C++ Guard owns comparison and invalidation.
    # Preserve unset separately from an explicitly empty environment entry.
    set(_environment "{}")
    foreach(_name IN ITEMS PATH LLVM_ROOT ANDROID_NDK_HOME
            PANGU_MACOS_SDK_PATH PANGU_IOS_SDK_PATH DEVELOPER_DIR
            CPATH CPLUS_INCLUDE_PATH C_INCLUDE_PATH LIBRARY_PATH
            COMPILER_PATH GCC_EXEC_PREFIX SDKROOT MACOSX_DEPLOYMENT_TARGET
            IPHONEOS_DEPLOYMENT_TARGET CFLAGS CXXFLAGS LDFLAGS
            INCLUDE LIB LIBPATH CL _CL_ WindowsSdkDir WindowsSDKVersion
            VCToolsInstallDir VCToolsVersion UniversalCRTSdkDir UCRTVersion)
        if(DEFINED ENV{${_name}})
            set(_value "$ENV{${_name}}")
            # file(GENERATE) must not evaluate generator syntax inside a raw value.
            string(REPLACE "$<" "$<1:$><" _value "${_value}")
            _pangu_abi_request_text(_environment "${_environment}" "${_name}" "${_value}")
        else()
            string(JSON _environment SET "${_environment}" "${_name}" null)
        endif()
    endforeach()

    foreach(_module IN LISTS _tracked)
        get_target_property(_request "${_module}" PANGU_ABI_REQUEST_FILE)
        get_target_property(_signature "${_module}" PANGU_ABI_SIGNATURE_HEADER)
        if(NOT _signature)
            set(_signature "${CMAKE_BINARY_DIR}/Abi/$<CONFIG>/${_module}/inputs.h")
        endif()
        set(_output "${CMAKE_BINARY_DIR}/Abi/$<CONFIG>/${_module}")
        set(_objects "${_output}/objects.txt")
        file(GENERATE OUTPUT "${_objects}"
            CONTENT "$<JOIN:$<TARGET_OBJECTS:${_module}>,\n>\n"
            TARGET "${_module}")

        set(_json "{\"schema\":2,\"platform_context\":{},\"environment_baseline\":{}}")
        string(JSON _json SET "${_json}" environment_baseline "${_environment}")
        foreach(_field_value IN ITEMS
                "source_root|${PANGU_ABI_SOURCE_ROOT}"
                "build_root|${CMAKE_BINARY_DIR}"
                "target_name|${_module}"
                "configuration|$<CONFIG>"
                "host_platform|${_host}"
                "target_platform|${_target}"
                "command_line_style|${_style}"
                "compiler|${CMAKE_CXX_COMPILER}"
                "database|${CMAKE_BINARY_DIR}/compile_commands.json"
                "object_manifest|${_objects}"
                "library|$<TARGET_FILE:${_module}>"
                "output_directory|${_output}"
                "schema_file|${PANGU_ABI_SOURCE_ROOT}/wiki/foundation/abi-profile-v1.schema.json"
                "signature_header|${_signature}"
                "link_graph|${_graph}")
            string(FIND "${_field_value}" "|" _separator)
            string(SUBSTRING "${_field_value}" 0 ${_separator} _field)
            math(EXPR _offset "${_separator} + 1")
            string(SUBSTRING "${_field_value}" ${_offset} -1 _value)
            _pangu_abi_request_string(_json "${_json}" "${_field}" "${_value}")
        endforeach()
        file(GENERATE OUTPUT "${_request}" CONTENT "${_json}\n" TARGET "${_module}")

        # Guard dependencies are emitted in the owning target above using
        # CMake's config-aware transitive link property. Do not approximate
        # the resolved closure from raw LINK_LIBRARIES text here.

    endforeach()

    if(_technical)
        # One shared technical Guard prevents circular per-member Guard edges.
        # Each technical library remains an ordinary STATIC CMake target.
        include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../BuildTools.cmake")
        set(PANGU_BUILD_TOOLS_SOURCE_ROOT "${PANGU_ABI_SOURCE_ROOT}")
        pangu_prepare_build_tools(_nativeTool)
        set(_guardCommands)
        set(_signaturePaths)
        foreach(_module IN LISTS _managed)
            get_target_property(_request "${_module}" PANGU_ABI_REQUEST_FILE)
            list(APPEND _guardCommands COMMAND "${_nativeTool}" guard-technical --request "${_request}")
        endforeach()
        foreach(_technicalTarget IN LISTS _technical)
            list(APPEND _signaturePaths "${CMAKE_BINARY_DIR}/Abi/$<CONFIG>/${_technicalTarget}/inputs.h")
        endforeach()
        add_custom_target(PanguAbiTechnicalGuard
            ${_guardCommands}
            BYPRODUCTS ${_signaturePaths}
            VERBATIM)
        foreach(_technicalTarget IN LISTS _technical)
            add_dependencies("${_technicalTarget}" PanguAbiTechnicalGuard)
        endforeach()
        # The generator computes each Module's *selected* transitive
        # technical closure, including INTERFACE libraries and CONFIG genex.
        # C++ then collects exactly those reachable technical SCCs.
        # Do not build unrelated or configuration-disabled technical targets.
        foreach(_module IN LISTS _managed)
            get_target_property(_request "${_module}" PANGU_ABI_REQUEST_FILE)
            add_custom_target("${_module}AbiTechnicalCollect"
                COMMAND "${_nativeTool}" collect-technical --request "${_request}"
                DEPENDS "$<TARGET_PROPERTY:${_module},PANGU_ABI_TECH_TARGETS>"
                VERBATIM)
            add_dependencies("${_module}AbiGuard" "${_module}AbiTechnicalCollect")
        endforeach()
    endif()
endfunction()
