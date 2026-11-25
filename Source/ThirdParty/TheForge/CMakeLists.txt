cmake_minimum_required(VERSION 4.0)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "The Forge requires the preset's Clang C and C++ compilers.")
endif()

set(forgeRepo "${CMAKE_CURRENT_SOURCE_DIR}/Repo")
set(forgeCommon "${forgeRepo}/Common_3")
if(NOT EXISTS "${forgeCommon}/Application/Config.h")
    message(FATAL_ERROR "The Forge v1.63 is missing. Run git submodule update --init Source/ThirdParty/TheForge/Repo.")
endif()

include("../ApplyPatch.cmake")

pangu_apply_third_party_patch(
    SOURCE_DIR "${forgeRepo}"
    RELATIVE_PATH Common_3/Application/Config.h
    PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Patch/0001-application-config.patch"
    INPUT_SHA256 79309c1a024ea1fb54d056ad5f392fe25ae61adb2e997d801a8f48deb6f9e1a0
    OUTPUT_SHA256 cfc00817f62a03d568edc01eb901ef11fc4103aab7f7fddd638abadbf8ac847d
    OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/PanguForgeConfig.h"
)

set(forgeSources
    Graphics/GraphicsConfig.cpp
    Resources/ResourceLoader/ResourceLoader.cpp
    Utilities/FileSystem/FileSystem.c
    Utilities/Log/Log.c
    Utilities/Math/StbDs.c
    Utilities/MemoryTracking/MemoryTracking.c
    Utilities/ThirdParty/OpenSource/bstrlib/bstrlib.c
    Utilities/Timer.c
)
set(forgeCompressionSources
    Utilities/ThirdParty/OpenSource/lz4/lz4.c
    Utilities/ThirdParty/OpenSource/zstd/common/entropy_common.c
    Utilities/ThirdParty/OpenSource/zstd/common/error_private.c
    Utilities/ThirdParty/OpenSource/zstd/common/fse_decompress.c
    Utilities/ThirdParty/OpenSource/zstd/common/xxhash.c
    Utilities/ThirdParty/OpenSource/zstd/common/zstd_common.c
    Utilities/ThirdParty/OpenSource/zstd/decompress/huf_decompress.c
    Utilities/ThirdParty/OpenSource/zstd/decompress/zstd_ddict.c
    Utilities/ThirdParty/OpenSource/zstd/decompress/zstd_decompress.c
    Utilities/ThirdParty/OpenSource/zstd/decompress/zstd_decompress_block.c
)

add_library(TheForge STATIC)
add_library(ThirdParty::TheForge ALIAS TheForge)
set_target_properties(
    TheForge
    PROPERTIES
        C_STANDARD
            17
        C_STANDARD_REQUIRED
            ON
        CXX_STANDARD
            17
        CXX_STANDARD_REQUIRED
            ON
        CXX_SCAN_FOR_MODULES
            OFF
        POSITION_INDEPENDENT_CODE
            ON
)
target_include_directories(
    TheForge
    SYSTEM
    PUBLIC
        "${forgeRepo}"
        "${CMAKE_CURRENT_BINARY_DIR}"
)
target_compile_definitions(TheForge PUBLIC EXTERNAL_CONFIG_FILEPATH="PanguForgeConfig.h")
target_compile_options(
    TheForge
    PRIVATE
        -ffunction-sections
        -fdata-sections
)

if(WIN32)
    include(Windows.cmake)
elseif(APPLE)
    include(Apple.cmake)
elseif(ANDROID)
    include(Android.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(Linux.cmake)
else()
    message(FATAL_ERROR "The Forge is unsupported for ${CMAKE_SYSTEM_NAME}.")
endif()

if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Renderer 私有链接此归档，避免 Forge 的原生入口进入 ELF 动态符号表。
    set_target_properties(
        TheForge
        PROPERTIES
            C_VISIBILITY_PRESET
                hidden
            CXX_VISIBILITY_PRESET
                hidden
            VISIBILITY_INLINES_HIDDEN
                ON
    )
endif()

list(TRANSFORM forgeSources PREPEND "${forgeCommon}/")
target_sources(TheForge PRIVATE ${forgeSources})
