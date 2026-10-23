#include <expected>

#ifndef __clang__
#error "ThirdParty must use the selected Clang compiler."
#endif
#ifdef _WIN32
#ifndef _MSVC_STL_VERSION
#error "ThirdParty on Windows must use MSVC STL."
#endif
#else
#ifndef _LIBCPP_VERSION
#error "ThirdParty on Linux/Apple/Android must use libc++."
#endif
#endif
#if __cplusplus < 202302L
#error "ThirdParty must use C++23."
#endif

// Real compiled C++23 code, not an INTERFACE or synthetic header-only target.
namespace pg::probe
{
// NOLINTNEXTLINE(misc-use-internal-linkage): exported symbol for a real link-time ThirdParty test.
std::expected<int, int> ThirdPartyCompiledResult() noexcept { return 23; }
} // namespace pg::probe
