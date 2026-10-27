// Included after each real Linux Translation Unit; never copied or modified.
#include <features.h> // IWYU pragma: keep - glibc identity from selected headers.
#include <version>    // IWYU pragma: keep - libc++ implementation/ABI identity.

namespace pg::abi::probe
{
struct DefaultPacking
{
    char Byte;
    double Number;
};
enum NativeEnum
{
    kZero,
};
static_assert(alignof(DefaultPacking) == 8);
static_assert(sizeof(DefaultPacking) == 16);
static_assert(sizeof(NativeEnum) == sizeof(int));
static_assert(sizeof(wchar_t) == 4);
// Compiler semantic fact: -D/-U may forge __CHAR_UNSIGNED__ without changing char.
static_assert(static_cast<char>(-1) < static_cast<char>(0));
} // namespace pg::abi::probe
