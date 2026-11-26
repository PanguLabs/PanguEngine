include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/PanguTriangleShaders.cmake")

macro(_pangu_enable_apple_graphics_languages)
    if(NOT CMAKE_OBJC_COMPILER_ID OR NOT CMAKE_OBJCXX_COMPILER_ID)
        set(CMAKE_OBJC_COMPILER "${CMAKE_C_COMPILER}")
        set(CMAKE_OBJCXX_COMPILER "${CMAKE_CXX_COMPILER}")
        enable_language(
            OBJC
            OBJCXX
        )
    endif()
endmacro()

function(pangu_require_graphics_renderer)
    if(TARGET Pangu::Renderer)
        return()
    endif()
    if(NOT WIN32 AND NOT APPLE AND NOT ANDROID AND NOT LINUX)
        message(FATAL_ERROR "Pangu Renderer currently requires Win32, Linux, Apple or Android")
    endif()
    set(engineSource "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    if(NOT TARGET ThirdParty::TheForge)
        pangu_add_owned_subdirectory(
            SOURCE_DIR "${engineSource}/Source/ThirdParty/TheForge"
            OWNER engine
            RELATIVE_BINARY_DIR Source/ThirdParty/TheForge
            EXCLUDE_FROM_ALL
        )
    endif()
    pangu_add_owned_subdirectory(
        SOURCE_DIR "${engineSource}/Source/Runtime/Renderer"
        OWNER engine
        RELATIVE_BINARY_DIR Source/Runtime/Renderer
        EXCLUDE_FROM_ALL
    )
endfunction()

function(_pangu_stage_win32_graphics_runtime targetName)
    get_property(sdkVersion TARGET TheForge PROPERTY PANGU_D3D12_AGILITY_SDK_VERSION)
    set(agilityEntry
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Source/Runtime/Application/Private/Platform/Win32Agility.cpp"
    )
    set_property(
        SOURCE
            "${agilityEntry}"
            TARGET_DIRECTORY
            "${targetName}"
        PROPERTY
            COMPILE_DEFINITIONS
                D3D12_AGILITY_SDK_VERSION=${sdkVersion}
    )
    target_sources("${targetName}" PRIVATE "${agilityEntry}")

    _pangu_set_graphics_runtime_files("${targetName}" PANGU_D3D12_RUNTIME_FILES)
    get_property(agilityFiles TARGET TheForge PROPERTY PANGU_D3D12_AGILITY_FILES)
    set_property(
        TARGET
            "${targetName}"
        PROPERTY
            PANGU_AGILITY_RUNTIME_FILES
                "${agilityFiles}"
    )
endfunction()

function(_pangu_require_wayland)
    if(NOT TARGET ThirdParty::WaylandWindow)
        pangu_add_owned_subdirectory(
            SOURCE_DIR "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Source/ThirdParty/WaylandWindow"
            OWNER engine
            RELATIVE_BINARY_DIR Source/ThirdParty/WaylandWindow
            EXCLUDE_FROM_ALL
        )
    endif()
endfunction()

function(_pangu_set_graphics_runtime_files targetName fileProperty)
    get_property(runtimeFiles TARGET TheForge PROPERTY "${fileProperty}")
    set_property(
        TARGET
            "${targetName}"
        PROPERTY
            PANGU_GRAPHICS_RUNTIME_FILES
                "${runtimeFiles}"
    )
endfunction()

# Apple 语言须在应用入口和 Forge 的共同目录作用域启用。
if(APPLE AND NOT IOS)
    macro(pangu_add_application)
        cmake_parse_arguments(panguApplication "TRIANGLE_SHADERS;INDEXED_MESH" "KIND;TARGET" "SOURCES" ${ARGN})
        if(panguApplication_KIND STREQUAL "GRAPHICS")
            _pangu_enable_apple_graphics_languages()
        endif()
        _pangu_parse_application(${ARGN})
    endmacro()
else()
    function(pangu_add_application)
        cmake_parse_arguments(PARSE_ARGV 0 arg "TRIANGLE_SHADERS;INDEXED_MESH" "TARGET;KIND" "SOURCES")
        _pangu_add_application()
    endfunction()
endif()

function(_pangu_parse_application)
    cmake_parse_arguments(PARSE_ARGV 0 arg "TRIANGLE_SHADERS;INDEXED_MESH" "TARGET;KIND" "SOURCES")
    _pangu_add_application()
endfunction()

function(_pangu_add_application)
    if(
        DEFINED
            arg_UNPARSED_ARGUMENTS
        OR DEFINED
            arg_KEYWORDS_MISSING_VALUES
        OR "${arg_TARGET}"
            STREQUAL
            ""
        OR "${arg_SOURCES}"
            STREQUAL
            ""
    )
        message(FATAL_ERROR "pangu_add_application requires TARGET <name> KIND <HEADLESS|GRAPHICS> SOURCES <files...>")
    endif()
    if(NOT arg_KIND MATCHES "^(HEADLESS|GRAPHICS)$")
        message(FATAL_ERROR "Unsupported Pangu Application KIND '${arg_KIND}'; expected HEADLESS or GRAPHICS")
    endif()
    if(arg_TRIANGLE_SHADERS AND NOT arg_KIND STREQUAL "GRAPHICS")
        message(FATAL_ERROR "pangu_add_application TRIANGLE_SHADERS requires KIND GRAPHICS")
    endif()
    if(arg_INDEXED_MESH AND NOT arg_KIND STREQUAL "GRAPHICS")
        message(FATAL_ERROR "pangu_add_application INDEXED_MESH requires KIND GRAPHICS")
    endif()
    if(NOT TARGET Pangu::EngineHost)
        message(FATAL_ERROR "Call pangu_initialize before adding a Pangu Application")
    endif()

    set(applicationSource "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Source/Runtime/Application")
    set(rendererSource "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Source/Runtime/Renderer")
    if(ANDROID AND arg_KIND STREQUAL "GRAPHICS")
        add_library("${arg_TARGET}" SHARED ${arg_SOURCES})
        set(applicationOutputProperty LIBRARY_OUTPUT_DIRECTORY)
    else()
        add_executable("${arg_TARGET}" ${arg_SOURCES})
        set(applicationOutputProperty RUNTIME_OUTPUT_DIRECTORY)
    endif()
    target_include_directories("${arg_TARGET}" PRIVATE "${applicationSource}/Public")
    target_link_libraries(
        "${arg_TARGET}"
        PRIVATE
            PanguBuildOptions
            Pangu::EngineHost
    )

    pangu_owned_build_root(projectBuildRoot project)
    set(applicationOutputDirectory "${projectBuildRoot}/Bin/${arg_TARGET}")
    # 运行库按目录部署，应用使用独立目录以避免将同一 Project 的其他应用依赖合入。
    set_target_properties(
        "${arg_TARGET}"
        PROPERTIES
            "${applicationOutputProperty}"
                "${applicationOutputDirectory}"
    )
    if(arg_KIND STREQUAL "GRAPHICS")
        set(entryTarget "${arg_TARGET}EngineEntry")
        if(WIN32)
            set(platformEntry Win32.cpp)
            set(platformLink user32)
        elseif(ANDROID)
            set(platformEntry Android.cpp)
            set(platformLink
                android
                log
            )
        elseif(APPLE AND NOT IOS)
            set(platformEntry MacApplication.mm)
            find_library(platformLink AppKit REQUIRED NO_CACHE)
            find_library(quartzCore QuartzCore REQUIRED NO_CACHE)
            list(APPEND platformLink "${quartzCore}")
        elseif(LINUX)
            _pangu_require_wayland()
            set(platformEntry Wayland.cpp)
            set(platformLink ThirdParty::WaylandWindow)
        else()
            message(FATAL_ERROR "Pangu graphical Application has no entry point for ${CMAKE_SYSTEM_NAME}")
        endif()
        pangu_require_graphics_renderer()
        add_library(
            "${entryTarget}"
            OBJECT
            "${applicationSource}/Private/GraphicsApplication.cpp"
            "${applicationSource}/Private/Platform/${platformEntry}"
        )
        if(arg_INDEXED_MESH)
            target_compile_definitions("${entryTarget}" PRIVATE PG_APPLICATION_INDEXED_MESH)
        endif()
        target_link_libraries(
            "${entryTarget}"
            PRIVATE
                PanguBuildOptions
                Pangu::EngineHost
                Pangu::Renderer
                ${platformLink}
        )
        target_include_directories(
            "${entryTarget}"
            PRIVATE
                "${applicationSource}/Public"
                "${rendererSource}/Private"
        )
        if(WIN32)
            target_compile_definitions(
                "${entryTarget}"
                PRIVATE
                    NOMINMAX
                    WIN32_LEAN_AND_MEAN
                    UNICODE
                    _UNICODE
            )
            _pangu_stage_win32_graphics_runtime("${arg_TARGET}")
        elseif(ANDROID)
            set(nativeAppGlue "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
            target_sources("${entryTarget}" PRIVATE "${nativeAppGlue}/android_native_app_glue.c")
            target_include_directories("${entryTarget}" SYSTEM PRIVATE "${nativeAppGlue}")
            set_property(
                TARGET
                    "${entryTarget}"
                PROPERTY
                    POSITION_INDEPENDENT_CODE
                        ON
            )
        elseif(APPLE AND NOT IOS)
            target_compile_options("${entryTarget}" PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
            set_target_properties(
                "${entryTarget}"
                PROPERTIES
                    OBJCXX_STANDARD
                        23
                    OBJCXX_STANDARD_REQUIRED
                        ON
                    OBJCXX_EXTENSIONS
                        OFF
            )
            _pangu_set_graphics_runtime_files("${arg_TARGET}" PANGU_METAL_RUNTIME_FILES)
        elseif(LINUX)
            _pangu_set_graphics_runtime_files("${arg_TARGET}" PANGU_VULKAN_RUNTIME_FILES)
        endif()
        target_link_libraries(
            "${arg_TARGET}"
            PRIVATE
                "${entryTarget}"
                Pangu::Renderer
        )
        if(arg_TRIANGLE_SHADERS)
            set(shaderTarget "${arg_TARGET}TriangleShaders")
            if(ANDROID)
                pangu_add_triangle_shaders(TARGET "${shaderTarget}" BUILD_ROOT "${projectBuildRoot}")
            else()
                _pangu_runtime_output_directory(runtimeDirectory "${arg_TARGET}")
                pangu_add_triangle_shaders(
                    TARGET "${shaderTarget}"
                    BUILD_ROOT "${projectBuildRoot}"
                    DEPLOY_DIRECTORY "${runtimeDirectory}/CompiledShaders"
                )
            endif()
            add_dependencies("${arg_TARGET}" "${shaderTarget}")
        endif()
    else()
        target_sources("${arg_TARGET}" PRIVATE "${applicationSource}/Private/HeadlessMain.cpp")
        target_include_directories("${arg_TARGET}" PRIVATE "${rendererSource}/Public")
    endif()
    pangu_stage_host_runtime(TARGET "${arg_TARGET}" APPLICATION)
endfunction()
