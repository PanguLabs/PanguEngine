include_guard(GLOBAL)

function(_pangu_runtime_output_directory output targetName)
    get_target_property(hostBinary "${targetName}" BINARY_DIR)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" configuration)
    set(outputProperty "RUNTIME_OUTPUT_DIRECTORY_${configuration}")
    get_property(hasConfigurationDirectory TARGET "${targetName}" PROPERTY "${outputProperty}" SET)
    if(NOT hasConfigurationDirectory)
        set(outputProperty RUNTIME_OUTPUT_DIRECTORY)
    endif()
    get_property(hasOutputDirectory TARGET "${targetName}" PROPERTY "${outputProperty}" SET)
    set(runtimeDirectory "")
    if(hasOutputDirectory)
        get_target_property(runtimeDirectory "${targetName}" "${outputProperty}")
    endif()
    if(runtimeDirectory STREQUAL "")
        set(runtimeDirectory "${hostBinary}")
    endif()
    # 单配置部署规则需要在生成构建图前确定输出目录。
    string(
        REPLACE "$<CONFIG>"
        "${CMAKE_BUILD_TYPE}"
        runtimeDirectory
        "${runtimeDirectory}"
    )
    if(runtimeDirectory MATCHES "\\$<")
        message(
            FATAL_ERROR
            "unsupported output-directory generator expression:\n${runtimeDirectory}\n"
            "Pangu runtime staging requires a literal path or $<CONFIG> for single-config destination identity"
        )
    endif()
    cmake_path(ABSOLUTE_PATH runtimeDirectory BASE_DIRECTORY "${hostBinary}" NORMALIZE)
    set("${output}" "${runtimeDirectory}" PARENT_SCOPE)
endfunction()

function(pangu_stage_host_runtime)
    cmake_parse_arguments(PARSE_ARGV 0 arg "APPLICATION" "TARGET" "")
    if(arg_UNPARSED_ARGUMENTS OR arg_KEYWORDS_MISSING_VALUES OR NOT TARGET "${arg_TARGET}")
        message(FATAL_ERROR "pangu_stage_host_runtime requires a local TARGET")
    endif()
    get_target_property(imported "${arg_TARGET}" IMPORTED)
    get_target_property(alias "${arg_TARGET}" ALIASED_TARGET)
    if(imported OR alias)
        message(FATAL_ERROR "pangu_stage_host_runtime requires a local TARGET")
    endif()
    get_property(graphicsFiles TARGET "${arg_TARGET}" PROPERTY PANGU_GRAPHICS_RUNTIME_FILES)
    if(NOT WIN32 AND (NOT (LINUX OR (APPLE AND NOT IOS)) OR (NOT arg_APPLICATION AND NOT graphicsFiles)))
        return()
    endif()
    if(
        NOT WIN32
        AND NOT graphicsFiles
        AND PANGU_LINK_MODE
            STREQUAL
            "Monolithic"
        AND NOT IS_DIRECTORY
            "${CMAKE_BINARY_DIR}/Generated/Runtime"
    )
        return()
    endif()
    if(NOT WIN32 AND arg_APPLICATION AND PANGU_LINK_MODE STREQUAL "Modular")
        _pangu_set_local_runtime_path("${arg_TARGET}")
        set_property(
            GLOBAL
            APPEND
            PROPERTY
                PANGU_RUNTIME_APPLICATIONS
                    "${arg_TARGET}"
        )
        set_property(
            TARGET
                "${arg_TARGET}"
            APPEND
            PROPERTY
                TRANSITIVE_LINK_PROPERTIES
                    PANGU_RUNTIME_FILES
        )
        get_property(scheduled GLOBAL PROPERTY PANGU_RUNTIME_LIBRARIES_SCHEDULED)
        if(NOT scheduled)
            set_property(
                GLOBAL
                PROPERTY
                    PANGU_RUNTIME_LIBRARIES_SCHEDULED
                        ON
            )
            cmake_language(
                DEFER
                    DIRECTORY "${CMAKE_SOURCE_DIR}"
                CALL _pangu_publish_runtime_libraries
                "${CMAKE_SOURCE_DIR}"
            )
        endif()
    endif()
    get_target_property(hostType "${arg_TARGET}" TYPE)
    if(NOT hostType STREQUAL "EXECUTABLE" AND NOT hostType STREQUAL "SHARED_LIBRARY")
        message(FATAL_ERROR "Runtime staging requires an executable or shared Host")
    endif()
    # 同一目录共享运行库复制规则，文件级依赖使部署可在 Host 未重新链接时更新。
    _pangu_runtime_output_directory(runtimeDirectory "${arg_TARGET}")
    string(SHA256 key "${runtimeDirectory}")
    string(TOUPPER "${key}" propertyKey)
    get_property(producer GLOBAL PROPERTY "PANGU_RUNTIME_PRODUCER_${propertyKey}")
    if(producer)
        # 同一目的地的一组 Host 共享生产者及运行依赖并集。
        set_property(
            GLOBAL
            APPEND
            PROPERTY
                "PANGU_RUNTIME_HOSTS_${propertyKey}"
                    "${arg_TARGET}"
        )
        add_dependencies("${arg_TARGET}" "${producer}")
        return()
    endif()
    set(producer "PanguRuntime_${key}")
    set_property(
        GLOBAL
        PROPERTY
            "PANGU_RUNTIME_PRODUCER_${propertyKey}"
                "${producer}"
    )
    set_property(
        GLOBAL
        PROPERTY
            "PANGU_RUNTIME_HOSTS_${propertyKey}"
                "${arg_TARGET}"
    )
    # 延迟到全部 Host 已登记后生成一次目的地生产者。
    cmake_language(
        EVAL CODE
            "cmake_language(DEFER DIRECTORY [[${CMAKE_SOURCE_DIR}]] CALL _pangu_finalize_runtime [[${key}]] [[${producer}]])"
    )
    add_custom_target("${producer}")
    add_dependencies("${arg_TARGET}" "${producer}")
endfunction()

function(_pangu_set_local_runtime_path target)
    if(APPLE AND NOT IOS)
        set(origin "@loader_path")
    else()
        set(origin "$ORIGIN")
        set_property(
            TARGET
                "${target}"
            PROPERTY
                BUILD_RPATH_USE_ORIGIN
                    ON
        )
    endif()
    get_property(paths TARGET "${target}" PROPERTY BUILD_RPATH)
    list(PREPEND paths "${origin}")
    list(REMOVE_DUPLICATES paths)
    set_property(
        TARGET
            "${target}"
        PROPERTY
            BUILD_RPATH
                "${paths}"
    )
endfunction()

# 目标图完成后发布本地共享库。共享库的私有依赖也属于应用的运行依赖。
function(_pangu_publish_runtime_libraries directory)
    get_property(applications GLOBAL PROPERTY PANGU_RUNTIME_APPLICATIONS)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(kind "${target}" TYPE)
        if(kind STREQUAL "SHARED_LIBRARY")
            set_property(
                TARGET
                    "${target}"
                APPEND
                PROPERTY
                    TRANSITIVE_LINK_PROPERTIES
                        PANGU_RUNTIME_FILES
            )
            # 由生成器按实际链接闭包启用选项，保留其他目标原有的 RPATH 及搜索顺序。
            set(consumers)
            foreach(application IN LISTS applications)
                list(
                    APPEND consumers
                    "$<IN_LIST:$<TARGET_FILE:${target}>,$<TARGET_PROPERTY:${application},PANGU_RUNTIME_FILES>>"
                )
            endforeach()
            list(JOIN consumers "," consumers)
            if(APPLE)
                set(origin "@executable_path")
            else()
                set(origin "$ORIGIN")
            endif()
            target_link_options("${target}" PRIVATE "$<$<OR:${consumers}>:LINKER:-rpath,${origin}>")
            set_property(
                TARGET
                    "${target}"
                PROPERTY
                    INTERFACE_PANGU_RUNTIME_FILES
                        "$<TARGET_FILE:${target}>;$<TARGET_SONAME_FILE:${target}>;$<TARGET_PROPERTY:${target},PANGU_RUNTIME_FILES>"
            )
        endif()
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        _pangu_publish_runtime_libraries("${child}")
    endforeach()
endfunction()

# 旧部署脚本保存了源文件和目的地。只接管已完成部署且内容仍与源库相同的副本。
function(_pangu_import_runtime_copies script stamp ownedDirectory)
    if(IS_DIRECTORY "${ownedDirectory}" OR NOT EXISTS "${script}" OR NOT EXISTS "${stamp}")
        return()
    endif()
    file(
        TIMESTAMP "${script}"
        generated
        "%s"
    )
    file(
        TIMESTAMP "${stamp}"
        completed
        "%s"
    )
    file(READ "${script}" previous)
    # Windows 过渡版本已使用脚本模板，但尚未启用归属记录。
    if(WIN32 AND previous MATCHES "set\\(ownedDirectory \\[==\\[\\]==\\]\\)")
        string(
            REGEX REPLACE "^cmake_minimum_required[^\n]*\n"
            ""
            previous
            "${previous}"
        )
    endif()
    if(
        generated
            GREATER
            completed
        OR NOT previous
            MATCHES
            "^set\\(files \\[==\\[([^\r\n]*)\\]==\\]\\)\nset\\(destination \\[==\\[([^\r\n]*)\\]==\\]\\)"
    )
        return()
    endif()
    set(files "${CMAKE_MATCH_1}")
    set(destination "${CMAKE_MATCH_2}")
    file(MAKE_DIRECTORY "${ownedDirectory}")
    foreach(file IN LISTS files)
        cmake_path(GET file FILENAME name)
        set(copy "${destination}/${name}")
        if(EXISTS "${file}" AND EXISTS "${copy}" AND NOT IS_SYMLINK "${copy}")
            file(REAL_PATH "${file}" sourcePath)
            file(REAL_PATH "${copy}" copyPath)
            if(WIN32)
                string(TOLOWER "${sourcePath}" sourcePath)
                string(TOLOWER "${copyPath}" copyPath)
            endif()
            if(NOT sourcePath STREQUAL copyPath)
                file(SHA256 "${file}" sourceHash)
                file(SHA256 "${copy}" copyHash)
                if(sourceHash STREQUAL copyHash)
                    file(WRITE "${ownedDirectory}/${name}" "${copyHash}\n${copy}")
                endif()
            endif()
        endif()
    endforeach()
endfunction()

function(_pangu_finalize_runtime key producer)
    string(TOUPPER "${key}" propertyKey)
    get_property(hosts GLOBAL PROPERTY "PANGU_RUNTIME_HOSTS_${propertyKey}")
    list(GET hosts 0 representativeHost)
    set(destination "$<TARGET_FILE_DIR:${representativeHost}>")
    set(runtimeFiles)
    set(graphicsFiles)
    set(agilityFiles)
    foreach(host IN LISTS hosts)
        if(WIN32)
            list(APPEND runtimeFiles "$<TARGET_RUNTIME_DLLS:${host}>")
        elseif(PANGU_LINK_MODE STREQUAL "Modular")
            list(APPEND runtimeFiles "$<TARGET_PROPERTY:${host},PANGU_RUNTIME_FILES>")
        endif()
        get_property(hostGraphicsFiles TARGET "${host}" PROPERTY PANGU_GRAPHICS_RUNTIME_FILES)
        get_property(hostAgilityFiles TARGET "${host}" PROPERTY PANGU_AGILITY_RUNTIME_FILES)
        list(APPEND graphicsFiles ${hostGraphicsFiles})
        list(APPEND agilityFiles ${hostAgilityFiles})
    endforeach()
    list(REMOVE_DUPLICATES graphicsFiles)
    list(REMOVE_DUPLICATES agilityFiles)
    set(files "$<REMOVE_DUPLICATES:$<JOIN:${runtimeFiles},;>>")
    set(script "${CMAKE_BINARY_DIR}/Generated/Runtime/${key}.cmake")
    set(stamp "${CMAKE_BINARY_DIR}/Generated/Runtime/${key}.stamp")
    set(depfile "${stamp}.d")
    # 运行库列表在生成阶段求值，不能用于 OUTPUT/BYPRODUCTS。
    # 原生 depfile 跟踪部署文件，缺失时重跑本生产者，不建立另一套运行依赖图。
    set(ownedDirectory "${script}.owned")
    _pangu_import_runtime_copies("${script}" "${stamp}" "${ownedDirectory}")
    configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/StageRuntimeFiles.cmake.in" "${script}.in" @ONLY)
    file(GENERATE OUTPUT "${script}" INPUT "${script}.in")
    set(stageHelpers)
    if(APPLE AND NOT IOS)
        list(APPEND stageHelpers "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/StageRuntimeFramework.cmake")
    endif()
    add_custom_command(
        OUTPUT
            "${stamp}"
        COMMAND
            "${CMAKE_COMMAND}" -P "${script}"
        COMMAND
            "${CMAKE_COMMAND}" -E touch "${stamp}"
        DEPENDS
            "${script}"
            ${stageHelpers}
            ${runtimeFiles}
            ${graphicsFiles}
            ${agilityFiles}
        DEPFILE "${depfile}"
        VERBATIM
        COMMAND_EXPAND_LISTS
    )
    add_custom_target("${producer}_Files" DEPENDS "${stamp}")
    add_dependencies("${producer}" "${producer}_Files")
endfunction()
