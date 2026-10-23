#include <expected>

#ifndef __clang__
#error "Pangu Module must use the selected Clang compiler."
#endif
#if defined(__EXCEPTIONS) || defined(_CPPUNWIND) || defined(__cpp_exceptions)
#error "Pangu Module exception support must be off."
#endif
#ifdef _WIN32
#ifndef _MSVC_STL_VERSION
#error "Pangu Module on Windows must use MSVC STL."
#endif
#else
#ifndef _LIBCPP_VERSION
#error "Pangu Module on Linux/Apple/Android must use libc++."
#endif
#endif
#if __cplusplus < 202302L
#error "Pangu Module must use C++23."
#endif

namespace pg::probe
{
// NOLINTNEXTLINE(misc-use-internal-linkage): exported symbol for an inter-library link test.
std::expected<int, int> PanguPolicyResult() noexcept { return 23; }
} // namespace pg::probe
