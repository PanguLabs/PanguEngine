find_package(PkgConfig REQUIRED)
pkg_check_modules(ForgeX11 REQUIRED x11)

list(
    APPEND forgeSources
    ${forgeCompressionSources}
    Graphics/Vulkan/Vulkan.c
    Graphics/Vulkan/Vulkan_Cxx.cpp
    Graphics/Vulkan/VulkanRaytracing.c
    OS/Linux/LinuxFileSystem.c
    OS/Linux/LinuxLog.c
    OS/Linux/LinuxThread.c
    OS/Linux/LinuxTime.c
    Utilities/FileSystem/UnixFileSystem.c
)

# zstd 的 C 解码路径无需单独引入 x86-64 汇编语言。
target_compile_definitions(TheForge PRIVATE ZSTD_DISABLE_ASM)
target_compile_options(TheForge PRIVATE -mavx2)
# IOperatingSystem.h 引用了 X11 类型，消费方也需要 X11 头。
target_include_directories(TheForge SYSTEM PUBLIC ${ForgeX11_INCLUDE_DIRS})
target_link_libraries(
    TheForge
    PRIVATE
        "${CMAKE_DL_LIBS}"
        pthread
        m
)

set_property(
    TARGET
        TheForge
    PROPERTY
        PANGU_VULKAN_RUNTIME_FILES
            "${forgeCommon}/OS/Windows/pc_gpu.data"
            "${forgeRepo}/Examples_3/Unit_Tests/src/01_Transformations/GPUCfg/gpu.cfg"
)
