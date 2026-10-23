// Included after each original translation unit; the source is never copied.
#include <corecrt.h>   // IWYU pragma: keep -- opening the selected SDK header is evidence.
#include <sdkddkver.h> // IWYU pragma: keep -- opening the selected SDK header is evidence.
#include <yvals_core.h>

namespace pg::abi::probe
{
namespace
{
struct DefaultPacking
{
    char Byte;
    double Number;
};

// Deliberately unscoped: -fshort-enums must change this layout and fail the probe.
// An unfixed underlying type is intentional: detect changes to native enum layout.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class, performance-enum-size)
enum NativeEnum
{
    kZero,
};

static_assert(alignof(DefaultPacking) == 8);
static_assert(sizeof(DefaultPacking) == 16);
static_assert(sizeof(NativeEnum) == sizeof(int));
static_assert(sizeof(wchar_t) == 2);
static_assert(sizeof(float) == 4);
static_assert(sizeof(double) == 8);
} // namespace
} // namespace pg::abi::probe
