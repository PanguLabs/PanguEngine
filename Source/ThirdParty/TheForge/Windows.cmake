# 使用与实际目标相同的链接库名称，检查当前工具链选中的 SDK。
block()
    set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
    try_compile(
        forgeD3d12SdkHasWorkGraphGuids
        SOURCE_FROM_CONTENT
            CheckD3d12WorkGraphGuids.c
            [=[
#include <guiddef.h>

extern const GUID IID_ID3D12StateObjectProperties1;
extern const GUID IID_ID3D12WorkGraphProperties;
extern const GUID IID_ID3D12GraphicsCommandList10;

int main(void)
{
    const volatile GUID* const stateObject = &IID_ID3D12StateObjectProperties1;
    const volatile GUID* const workGraph = &IID_ID3D12WorkGraphProperties;
    const volatile GUID* const commandList = &IID_ID3D12GraphicsCommandList10;
    return (stateObject->Data1 | workGraph->Data1 | commandList->Data1) == 0;
}
]=]
        LINK_LIBRARIES
            dxguid
        OUTPUT_VARIABLE forgeD3d12GuidLinkOutput
        NO_CACHE
    )
    if(NOT forgeD3d12SdkHasWorkGraphGuids)
        message(
            FATAL_ERROR
            "The Forge D3D12 requires a Windows SDK whose dxguid.lib provides the Work Graphs IIDs. "
            "Select Windows SDK 10.0.26100.0 or a newer SDK that provides these symbols.\n"
            "${forgeD3d12GuidLinkOutput}"
        )
    endif()
endblock()

list(
    APPEND forgeSources
    ${forgeCompressionSources}
    Graphics/Direct3D12/Direct3D12Hooks.c
    Graphics/Direct3D12/Direct3D12Raytracing.c
    Graphics/Direct3D12/Direct3D12_Cxx.cpp
    OS/CPUConfig.cpp
    OS/ThirdParty/OpenSource/cpu_features/src/impl_x86_windows.c
    OS/ThirdParty/OpenSource/hidapi/windows/hid.c
    OS/WindowSystem/WindowSystem.cpp
    OS/Windows/WindowsBase.cpp
    OS/Windows/WindowsFileSystem.cpp
    OS/Windows/WindowsInput.cpp
    OS/Windows/WindowsLog.c
    OS/Windows/WindowsThread.c
    OS/Windows/WindowsTime.c
    OS/Windows/WindowsWindow.cpp
    Utilities/Math/Algorithms.c
)

set(forgeD3d12Source "${CMAKE_CURRENT_BINARY_DIR}/Direct3D12.c")
pangu_apply_third_party_patch(
    SOURCE_DIR "${forgeRepo}"
    RELATIVE_PATH Common_3/Graphics/Direct3D12/Direct3D12.c
    PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Patch/0002-d3d12-use-sdk-guids.patch"
    INPUT_SHA256 9a47dfa9dde51cd0cd81d892eea948219edbe466af5ba33092b850dd90f4ca7b
    OUTPUT_SHA256 80082fafa991be8a7a80bbdff38b7e8983fb99e69464221dac64277843e0ef66
    OUTPUT_FILE "${forgeD3d12Source}"
)
target_sources(TheForge PRIVATE "${forgeD3d12Source}")
set_source_files_properties(
    "${forgeD3d12Source}"
    PROPERTIES
        INCLUDE_DIRECTORIES
            "${forgeCommon}/Graphics/Direct3D12"
)

set(forgeAgilitySdkVersion 715)
target_compile_definitions(
    TheForge
    PUBLIC
        D3D12_AGILITY_SDK=1
        D3D12_AGILITY_SDK_VERSION=${forgeAgilitySdkVersion}
    PRIVATE
        UNICODE
        _UNICODE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
)
set_property(
    TARGET
        TheForge
    PROPERTY
        PANGU_D3D12_AGILITY_SDK_VERSION
            "${forgeAgilitySdkVersion}"
)
target_compile_options(TheForge PRIVATE -Wno-unknown-pragmas)
set_source_files_properties(
    "${forgeD3d12Source}"
    "${forgeCommon}/Graphics/Direct3D12/Direct3D12Hooks.c"
    "${forgeCommon}/Graphics/Direct3D12/Direct3D12Raytracing.c"
    PROPERTIES
        COMPILE_OPTIONS
            -Wno-error=incompatible-pointer-types
)

set(forgeGraphicsDeps "${forgeCommon}/Graphics/ThirdParty/OpenSource")
set_property(
    TARGET
        TheForge
    PROPERTY
        PANGU_D3D12_RUNTIME_FILES
            "${forgeCommon}/OS/Windows/pc_gpu.data"
            "${forgeRepo}/Examples_3/Unit_Tests/src/01_Transformations/GPUCfg/gpu.cfg"
            "${forgeGraphicsDeps}/ags/ags_lib/lib/amd_ags_x64.dll"
            "${forgeGraphicsDeps}/DirectXShaderCompiler/bin/x64/dxcompiler.dll"
            "${forgeGraphicsDeps}/DirectXShaderCompiler/bin/x64/dxil.dll"
            "${forgeGraphicsDeps}/winpixeventruntime/bin/WinPixEventRuntime.dll"
)
set_property(
    TARGET
        TheForge
    PROPERTY
        PANGU_D3D12_AGILITY_FILES
            "${forgeGraphicsDeps}/Direct3d12Agility/bin/x64/D3D12Core.dll"
            "${forgeGraphicsDeps}/Direct3d12Agility/bin/x64/d3d12SDKLayers.dll"
)
target_link_libraries(
    TheForge
    PRIVATE
        "${forgeGraphicsDeps}/ags/ags_lib/lib/amd_ags_x64.lib"
        "${forgeGraphicsDeps}/nvapi/amd64/nvapi64.lib"
        "${forgeGraphicsDeps}/DirectXShaderCompiler/lib/x64/dxcompiler.lib"
        "${forgeGraphicsDeps}/winpixeventruntime/bin/WinPixEventRuntime.lib"
        d3d12
        dxgi
        dxguid
        setupapi
        xinput9_1_0
)
