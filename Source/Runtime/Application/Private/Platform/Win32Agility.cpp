#include <cstdint>

extern "C"
{
    // D3D12 Loader 从主程序导出读取 SDK 版本和相对于 EXE 的目录。
    // NOLINTNEXTLINE(readability-identifier-naming)
    __declspec(dllexport) extern const std::uint32_t D3D12SDKVersion = D3D12_AGILITY_SDK_VERSION;
    __declspec(dllexport) const char* D3D12SDKPath = ".\\D3D12\\"; // NOLINT(readability-identifier-naming)
}
