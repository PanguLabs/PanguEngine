#include <Check.h>
#include <string_view>

// 隔离字符串构造的异常规格，只验证 Fatal 入口本身。
static_assert(noexcept(pg::Fatal(std::string_view{})));
static_assert(PG_ENABLE_ASSERTS == EXPECT_ASSERTS);
static_assert(PG_LOG_COMPILED_MIN_LEVEL == EXPECT_LEVEL);
// NOLINTNEXTLINE(misc-use-internal-linkage): 供独立 Consumer 翻译单元执行链接一致性检查。
int OtherConsumer() { return PG_ENABLE_ASSERTS; }
