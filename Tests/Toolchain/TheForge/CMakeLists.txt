cmake_minimum_required(VERSION 4.0)

set(toolchainRuntimeDirectory "${CMAKE_BINARY_DIR}/Tests/Toolchain")
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(rendererLinkCheck PanguMetalRendererLinkCheck)
endif()
if(PANGU_BUILD_TRIANGLE_SHADER_CHECK)
    set(shaderDeployment)
    if(ANDROID AND PANGU_NATIVE_ACTIVITY_SHADER_OUTPUT_ROOT)
        set(shaderDeployment
            DEPLOY_DIRECTORY
            "${PANGU_NATIVE_ACTIVITY_SHADER_OUTPUT_ROOT}/CompiledShaders"
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND PANGU_BUILD_FORGE_LINK_CHECK)
        # iOS 的 NSBundle.resourceURL 指向 .app 根目录。
        set(shaderDeployment
            DEPLOY_DIRECTORY
            "${toolchainRuntimeDirectory}/${rendererLinkCheck}.app/CompiledShaders"
        )
    endif()
    pangu_add_triangle_shaders(TARGET PanguTriangleShaders BUILD_ROOT "${CMAKE_BINARY_DIR}" ${shaderDeployment})
endif()
if(NOT PANGU_BUILD_FORGE_LINK_CHECK)
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "PanguForgeLinkCheck requires the target Clang compiler.")
endif()
if(APPLE)
    _pangu_enable_apple_graphics_languages()
    set_source_files_properties(
        Compile.cpp
        PROPERTIES
            LANGUAGE
                OBJCXX
    )
endif()
if(NOT TARGET ThirdParty::TheForge)
    pangu_add_owned_subdirectory(
        SOURCE_DIR "${PROJECT_SOURCE_DIR}/Source/ThirdParty/TheForge"
        OWNER engine
        RELATIVE_BINARY_DIR Source/ThirdParty/TheForge
        EXCLUDE_FROM_ALL
    )
endif()

add_executable(PanguForgeLinkCheck Compile.cpp)
target_compile_features(PanguForgeLinkCheck PRIVATE cxx_std_23)
set_target_properties(
    PanguForgeLinkCheck
    PROPERTIES
        CXX_SCAN_FOR_MODULES
            OFF
        OBJCXX_STANDARD
            23
        OBJCXX_STANDARD_REQUIRED
            ON
        RUNTIME_OUTPUT_DIRECTORY
            "${toolchainRuntimeDirectory}"
)
target_link_libraries(PanguForgeLinkCheck PRIVATE ThirdParty::TheForge)
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    if(ANDROID)
        option(PANGU_BUILD_NATIVE_ACTIVITY_TRIANGLE "Build the NativeActivity triangle fixture" OFF)
        set(rendererLinkCheck PanguVulkanRendererLinkCheck)
        add_executable(
            PanguAndroidJniThreadCheck
            "${PROJECT_SOURCE_DIR}/Tests/Renderer/NativeActivity/JniThreadCheck.cpp"
        )
        target_include_directories(
            PanguAndroidJniThreadCheck
            PRIVATE
                "${PROJECT_SOURCE_DIR}/Source/Runtime/Application/Private/Platform"
        )
        target_link_libraries(
            PanguAndroidJniThreadCheck
            PRIVATE
                PanguBuildOptions
                Pangu::Renderer
        )
        set_target_properties(
            PanguAndroidJniThreadCheck
            PROPERTIES
                OUTPUT_NAME
                    PanguJniThreadCheck
                RUNTIME_OUTPUT_DIRECTORY
                    "${toolchainRuntimeDirectory}"
        )
        add_dependencies(PanguForgeLinkCheck PanguAndroidJniThreadCheck)
        set(nativeActivityFixture FirstFrame)
        if(PANGU_BUILD_NATIVE_ACTIVITY_TRIANGLE)
            set(nativeActivityFixture FirstTriangle)
        endif()
        pangu_add_application(
            TARGET PanguNativeActivityCheck
            KIND GRAPHICS
            SOURCES
                "${PROJECT_SOURCE_DIR}/Tests/Renderer/Project/${nativeActivityFixture}.cpp"
        )
        if(TARGET PanguTriangleShaders AND PANGU_NATIVE_ACTIVITY_SHADER_OUTPUT_ROOT)
            add_dependencies(PanguNativeActivityCheck PanguTriangleShaders)
        endif()
        add_dependencies(PanguForgeLinkCheck PanguNativeActivityCheck)
    endif()
    # 移动平台先验证实际 Renderer 的最终链接，独立于应用入口。
    pangu_require_graphics_renderer()
    add_executable("${rendererLinkCheck}" RendererLink.cpp)
    target_link_libraries(
        "${rendererLinkCheck}"
        PRIVATE
            PanguBuildOptions
            Pangu::Renderer
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        target_compile_definitions("${rendererLinkCheck}" PRIVATE PG_RENDERER_IOS_TRIANGLE_LINK_CHECK)
        target_link_libraries("${rendererLinkCheck}" PRIVATE Pangu::EngineHost)
    endif()
    set_target_properties(
        "${rendererLinkCheck}"
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY
                "${toolchainRuntimeDirectory}"
    )
    add_dependencies(PanguForgeLinkCheck "${rendererLinkCheck}")
endif()
