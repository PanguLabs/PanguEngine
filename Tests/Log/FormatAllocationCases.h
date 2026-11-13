#pragma once
#include <string>
// 极值场景在 GTest 与独立分配探针进程中复用，参数存储在关卡外准备。
[[nodiscard]] bool RunFormatAllocationCases(const std::string& ownedText) noexcept;
