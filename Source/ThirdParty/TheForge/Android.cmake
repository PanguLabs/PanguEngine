list(
    APPEND forgeSources
    ${forgeCompressionSources}
    Graphics/Vulkan/Vulkan.c
    Graphics/Vulkan/Vulkan_Cxx.cpp
    Graphics/Vulkan/VulkanRaytracing.c
    OS/Android/AndroidFileSystem.cpp
    OS/Android/AndroidLog.c
    OS/Android/AndroidThread.c
    OS/Android/AndroidTime.c
    Utilities/FileSystem/UnixFileSystem.c
)

option(PANGU_FORGE_USE_PREFAB_SWAPPY "Use the external AndroidX Swappy Prefab package" OFF)
if(PANGU_FORGE_USE_PREFAB_SWAPPY)
    find_package(games-frame-pacing REQUIRED CONFIG)
    set(swappyLibrary games-frame-pacing::swappy_static)
else()
    set(forgeAgdk "${forgeCommon}/OS/ThirdParty/OpenSource/agdk")
    target_include_directories(TheForge SYSTEM PRIVATE "${forgeAgdk}/include")
    set(swappyLibrary "${forgeAgdk}/libs/arm64-v8a_cpp_static_Release/libswappy_static.a")
endif()
target_sources(TheForge PRIVATE "${CMAKE_CURRENT_LIST_DIR}/AndroidSwappy.c")
# Forge 的 IOperatingSystem.h 直接包含 NDK 的 Glue 头。
target_include_directories(
    TheForge
    SYSTEM
    PUBLIC
        "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue"
)
target_link_libraries(
    TheForge
    PRIVATE
        "${swappyLibrary}"
        android
        log
)
# Swappy 是预编译归档，通过最终的 Android 链接隐藏其符号。
target_link_options(TheForge INTERFACE "LINKER:--exclude-libs,libswappy_static.a")
