#include <Check.h>
#include <string_view>

// 隔离字符串构造的异常规格，只验证 Fatal 入口本身。
static_assert(noexcept(pg::Fatal(std::string_view{})));
static_assert(PG_ENABLE_ASSERTS == EXPECT_ASSERTS);
static_assert(PG_LOG_COMPILED_MIN_LEVEL == EXPECT_LEVEL);
int OtherConsumer();
int ThirdParty();
int main() { return OtherConsumer() == PG_ENABLE_ASSERTS && ThirdParty() == 0 ? 0 : 1; }
