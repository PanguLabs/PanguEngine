#pragma once

#include <FailureReport.h>

#include <source_location>
#include <string_view>

namespace pg
{
// 仅在一次同步回调内借用局部报告；不得跨回调保存，也不得并发共写。
class FailureContext final
{
public:
    explicit FailureContext(FailureReport& report) noexcept
        : Report(report)
    {
    }

    // 同一 Error 幂等；不同 Error 只能成为次项。未知起源须显式传 source_location{}。
    void SetPrimary(Error cause, std::string_view message,
                    std::source_location location = std::source_location::current()) noexcept
    {
        Report.SetPrimary(cause, message, location);
    }

    void AddContext(std::string_view key, std::string_view value) noexcept { Report.AddContext(key, value); }

    void AddSecondary(Error cause, std::string_view message,
                      std::source_location location = std::source_location::current()) noexcept
    {
        Report.AddSecondary(cause, message, location);
    }

    void AddSecondary(const FailureSite& site) noexcept { Report.AddSecondary(site); }

private:
    // 非空借用明确禁止重新绑定；Context 本身没有赋值语义。
    FailureReport& Report; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};
} // namespace pg
