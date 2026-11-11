#pragma once
#include <ModuleDescriptor.h>
namespace pg::module
{
// 由登记 Catalog 的 Monolithic Host 定义；Module 库不提供该符号。
[[nodiscard]] StaticModuleCatalog GeneratedStaticModuleCatalog() noexcept;
} // namespace pg::module
