if(NOT CMAKE_OBJC_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_OBJCXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Enable OBJC and OBJCXX with the preset's Clang compilers before adding The Forge.")
endif()

set_property(
    TARGET
        TheForge
    PROPERTY
        PANGU_METAL_RUNTIME_FILES
            "${forgeCommon}/OS/Darwin/apple_gpu.data"
            "${forgeRepo}/Examples_3/Unit_Tests/src/01_Transformations/GPUCfg/gpu.cfg"
)

list(
    APPEND forgeSources
    ${forgeCompressionSources}
    Graphics/Metal/MetalRenderer.mm
    Graphics/Metal/MetalRaytracing.mm
    OS/CPUConfig.cpp
    OS/Darwin/CocoaFileSystem.mm
    OS/Darwin/DarwinInput.mm
    OS/Darwin/DarwinLog.c
    OS/Darwin/DarwinThread.c
    # 上游 ARM64 检测实现也支持 macOS，源文件按目标架构启用。
    OS/ThirdParty/OpenSource/cpu_features/src/impl_aarch64_iOS.c
    OS/WindowSystem/WindowSystem.cpp
    Utilities/FileSystem/UnixFileSystem.c
)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    list(
        APPEND forgeSources
        OS/Darwin/iOSAppDelegate.m
        OS/Darwin/iOSBase.mm
        OS/Darwin/iOSWindow.mm
    )
    set(forgeUiFrameworks UIKit)
else()
    list(
        APPEND forgeSources
        OS/Darwin/macOSAppDelegate.m
        OS/Darwin/macOSBase.mm
        OS/Darwin/macOSWindow.mm
        OS/ThirdParty/OpenSource/cpu_features/src/impl_x86_macos.c
    )
    set(forgeUiFrameworks
        AppKit
        IOKit
    )
endif()

# 上游 Apple 工程将共享 .cpp 文件作为 Objective-C++ 编译。
set_source_files_properties(
    "${forgeCommon}/Graphics/GraphicsConfig.cpp"
    "${forgeCommon}/OS/CPUConfig.cpp"
    "${forgeCommon}/OS/WindowSystem/WindowSystem.cpp"
    PROPERTIES
        LANGUAGE
            OBJCXX
)
set_target_properties(
    TheForge
    PROPERTIES
        OBJCXX_STANDARD
            17
        OBJCXX_STANDARD_REQUIRED
            ON
        C_VISIBILITY_PRESET
            hidden
        CXX_VISIBILITY_PRESET
            hidden
        OBJC_VISIBILITY_PRESET
            hidden
        OBJCXX_VISIBILITY_PRESET
            hidden
        VISIBILITY_INLINES_HIDDEN
            ON
)
target_compile_options(TheForge PRIVATE "$<$<COMPILE_LANGUAGE:OBJC,OBJCXX>:-fobjc-arc>")
target_compile_definitions(TheForge PRIVATE ZSTD_DISABLE_ASM)
foreach(
    framework
    IN
    ITEMS
        Metal
        MetalPerformanceShaders
        GameController
        CoreHaptics
        QuartzCore
        Foundation
        CoreFoundation
        CoreGraphics
        CoreVideo
        ${forgeUiFrameworks}
)
    find_library(forgeFramework_${framework} NAMES "${framework}" REQUIRED NO_CACHE)
    target_link_libraries(TheForge PRIVATE "${forgeFramework_${framework}}")
endforeach()
