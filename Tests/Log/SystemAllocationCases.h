#pragma once

[[nodiscard]] bool PrepareSystemAllocationCases() noexcept;
[[nodiscard]] bool RunSystemAllocationCases() noexcept;
[[nodiscard]] bool RunSystemOutOfMemoryCase() noexcept;
[[nodiscard]] bool RunSystemInitializationAllocationCase() noexcept;
[[nodiscard]] bool RunDebugSystemOutOfMemoryCases() noexcept;
