include_guard(GLOBAL)

# Collection is a build-time host operation, independent of CTest.
function(pangu_enable_windows_abi_profiles)
    if(NOT WIN32 OR CMAKE_CROSSCOMPILING)
        message(STATUS "Pangu ABI Profile collection pending for this platform.")
        return()
    endif()
    if(NOT CMAKE_GENERATOR STREQUAL "Ninja" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Windows ABI Profile collection requires native Ninja x64.")
    endif()
    if(NOT TARGET PanguAbiProfileTool)
        message(FATAL_ERROR "Create PanguAbiProfileTool before enabling ABI collection.")
    endif()
    get_filename_component(repository "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." ABSOLUTE)
    foreach(target IN LISTS ARGN)
        if(NOT TARGET "${target}")
            message(FATAL_ERROR "Unknown ABI collection target: ${target}")
        endif()
        get_target_property(enabled "${target}" PANGU_ABI_PROFILE_ENABLED)
        if(enabled)
            continue()
        endif()
        get_target_property(kind "${target}" TYPE)
        get_target_property(unity "${target}" UNITY_BUILD)
        if(NOT kind STREQUAL "STATIC_LIBRARY" OR unity)
            message(FATAL_ERROR "Windows ABI collection currently requires non-unity STATIC targets: ${target}")
        endif()
        get_target_property(links "${target}" LINK_LIBRARIES)
        if(links)
            foreach(link IN LISTS links)
                if(NOT link STREQUAL "PanguBuildOptions")
                    message(FATAL_ERROR "ABI link-witness dependency closure is not implemented for ${target}: ${link}")
                endif()
            endforeach()
        endif()
        set(output "${CMAKE_BINARY_DIR}/Abi/${target}")
        set(objects "${output}/objects.txt")
        set(request "${output}/request.json")
        file(GENERATE OUTPUT "${objects}" CONTENT "$<JOIN:$<TARGET_OBJECTS:${target}>,\n>\n")
        set(job "{}")
        string(JSON job SET "${job}" schema 1)
        foreach(pair IN ITEMS
                "target|${target}"
                "compiler|${CMAKE_CXX_COMPILER}"
                "tool|$<TARGET_FILE:PanguAbiProfileTool>"
                "schema_file|${repository}/wiki/foundation/abi-profile-v1.schema.json"
                "database|${CMAKE_BINARY_DIR}/compile_commands.json"
                "objects_file|${objects}"
                "output_directory|${output}"
                "probe|${repository}/Tools/AbiProfile/Probe/Windows.cpp")
            string(FIND "${pair}" "|" separator)
            string(SUBSTRING "${pair}" 0 ${separator} key)
            math(EXPR start "${separator} + 1")
            string(SUBSTRING "${pair}" ${start} -1 value)
            string(REPLACE "\\" "/" value "${value}")
            string(REPLACE "\"" "\\\"" value "${value}")
            string(JSON job SET "${job}" "${key}" "\"${value}\"")
        endforeach()
        file(GENERATE OUTPUT "${request}" CONTENT "${job}\n")
        add_custom_target("${target}AbiPrepare"
            COMMAND "${CMAKE_COMMAND}" "-DPANGU_ABI_OUTPUT=${output}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Prepare.cmake"
            VERBATIM)
        # This custom command runs before the actual target's sources compile.
        # In particular, a compile failure cannot leave last build's profile valid.
        add_dependencies("${target}" "${target}AbiPrepare")
        add_custom_target("${target}AbiProfile" ALL
            COMMAND "${CMAKE_COMMAND}" "-DPANGU_ABI_REQUEST=${request}"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CollectWindows.cmake"
            DEPENDS "${target}" PanguAbiProfileTool
            VERBATIM)
        set_property(TARGET "${target}" PROPERTY PANGU_ABI_PROFILE_ENABLED TRUE)
    endforeach()
endfunction()
