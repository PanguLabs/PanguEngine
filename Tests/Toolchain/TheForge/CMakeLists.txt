cmake_minimum_required(VERSION 4.0)

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
            "${CMAKE_BINARY_DIR}/Tests/Toolchain"
)
target_link_libraries(PanguForgeLinkCheck PRIVATE ThirdParty::TheForge)
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    if(ANDROID)
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
                    "${CMAKE_BINARY_DIR}/Tests/Toolchain"
        )
        add_dependencies(PanguForgeLinkCheck PanguAndroidJniThreadCheck)
        pangu_add_application(
            TARGET PanguNativeActivityCheck
            KIND GRAPHICS
            SOURCES
                "${PROJECT_SOURCE_DIR}/Tests/Renderer/Project/FirstFrame.cpp"
        )
        add_dependencies(PanguForgeLinkCheck PanguNativeActivityCheck)
    else()
        set(rendererLinkCheck PanguMetalRendererLinkCheck)
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
    set_target_properties(
        "${rendererLinkCheck}"
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/Tests/Toolchain"
    )
    add_dependencies(PanguForgeLinkCheck "${rendererLinkCheck}")
endif()
