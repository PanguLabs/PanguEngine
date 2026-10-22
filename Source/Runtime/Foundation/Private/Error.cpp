#include <Error.h>

#include <cstdint>

namespace pg
{
Error MakeError(ErrorDomainId domain, std::uint32_t code) noexcept { return Error{domain, code}; }
} // namespace pg
