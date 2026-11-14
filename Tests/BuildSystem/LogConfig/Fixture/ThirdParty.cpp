#ifdef PG_ENABLE_ASSERTS
#error Engine assert configuration leaked into third-party target
#endif
#ifdef PG_LOG_COMPILED_MIN_LEVEL
#error Engine log configuration leaked into third-party target
#endif
#ifdef NDEBUG
static_assert(EXPECT_NDEBUG == 1);
#else
static_assert(EXPECT_NDEBUG == 0);
#endif
// NOLINTNEXTLINE(misc-use-internal-linkage): 供独立 Consumer 翻译单元执行第三方策略检查。
int ThirdParty() { return 0; }
