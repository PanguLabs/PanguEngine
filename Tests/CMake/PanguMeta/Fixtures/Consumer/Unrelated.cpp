// 保留真实归档符号，避免 Darwin 工具链为空符号对象生成不可链接的归档索引。
// NOLINTNEXTLINE(misc-use-internal-linkage)
int UnrelatedValue() noexcept;
int UnrelatedValue() noexcept { return 1; }
