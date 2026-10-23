#include <expected>

namespace pg::probe
{
[[nodiscard]] std::expected<int, int> ThirdPartyCompiledResult() noexcept;
[[nodiscard]] std::expected<int, int> PanguPolicyResult() noexcept;
} // namespace pg::probe

int main()
{
    const auto thirdParty = pg::probe::ThirdPartyCompiledResult();
    const auto pangu = pg::probe::PanguPolicyResult();
    return thirdParty && pangu && *thirdParty == 23 && *pangu == 23 ? 0 : 1;
}
