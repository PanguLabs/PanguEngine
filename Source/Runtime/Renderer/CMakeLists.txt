cmake_minimum_required(VERSION 4.0)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "PanguRenderer requires Clang; found '${CMAKE_CXX_COMPILER_ID}'.")
endif()

if(NOT TARGET ThirdParty::TheForge)
    message(FATAL_ERROR "PanguRenderer requires ThirdParty::TheForge.")
endif()

pangu_module_library_type(moduleType)
add_library(
    PanguRenderer
    ${moduleType}
    Private/RendererAccess.cpp
    Private/RendererModule.cpp
    Private/RendererSession.cpp
    Private/Backends/ForgeBackend.cpp
)
add_library(Pangu::Renderer ALIAS PanguRenderer)
pangu_configure_module(PanguRenderer EXPORT_MACRO PG_RENDERER_API)
target_include_directories(PanguRenderer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public")
target_link_libraries(
    PanguRenderer
    PUBLIC
        Pangu::Module
    PRIVATE
        Pangu::EngineHost
        ThirdParty::TheForge
)
if(PANGU_ENABLE_VULKAN_PREVIEW AND (ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    target_compile_definitions(PanguRenderer PRIVATE PG_RENDERER_VULKAN_PREVIEW)
endif()
if(WIN32)
    target_compile_definitions(
        PanguRenderer
        PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )
elseif(APPLE)
    set_source_files_properties(
        Private/Backends/ForgeBackend.cpp
        PROPERTIES
            LANGUAGE
                OBJCXX
    )
    target_compile_options(PanguRenderer PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
    set_target_properties(
        PanguRenderer
        PROPERTIES
            OBJCXX_STANDARD
                23
            OBJCXX_STANDARD_REQUIRED
                ON
            OBJCXX_EXTENSIONS
                OFF
            OBJCXX_VISIBILITY_PRESET
                hidden
    )
endif()
pangu_export_module_query(
    TARGET PanguRenderer
    ID pg::renderer::kRendererModuleId
    ID_HEADER RendererModule.h
    QUERY pg::renderer::QueryRendererModule
    HEADER RendererModule.h
)
