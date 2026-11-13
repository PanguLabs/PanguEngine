#include "LogTypes.h"
#include <NativeLogIo.h>
#include <SpdlogBackend.h>
#include <cstdint>
#include <gtest/gtest.h>

TEST(LogOutput, FailingConsoleDoesNotStopFileSlot)
{
    pg::log::detail::NativeIoFaults faults;
    pg::log::detail::NativeLogIo console(&faults);
    ASSERT_TRUE(console.OpenConsole());
    faults.WriteFailure = pg::log::detail::NativeIoError(127);
    std::uint64_t delivered{};
    pg::log::detail::OutputPort const file{
        .Context = &delivered,
        .Write =
            [](void* context, std::string_view text) noexcept
        {
            *static_cast<std::uint64_t*>(context) += text.size();
            return pg::log::detail::OutputOutcome{
                .BytesWritten = text.size(),
                .Complete = true,
            };
        },
        .Flush = nullptr,
    };
    pg::log::detail::SpdlogBackend backend(console.Port(), file);
    const auto outcomes = backend.Emit(pg::log::Level::kInfo, "record\n");
    EXPECT_FALSE(std::get<0>(outcomes).Complete);
    EXPECT_TRUE(std::get<1>(outcomes).Complete);
    EXPECT_EQ(delivered, 7U);
    faults.WriteFailure.reset();
    EXPECT_FALSE(console.Write("no reopen").Complete);
}

TEST(LogOutput, NativeConsoleCloseDoesNotCloseHostStderr)
{
    pg::log::detail::NativeLogIo first;
    ASSERT_TRUE(first.OpenConsole());
    ASSERT_TRUE(first.Close());
    pg::log::detail::NativeLogIo second;
    EXPECT_TRUE(second.OpenConsole());
    EXPECT_TRUE(second.Write("LogOutput UTF-8: 中文\n").Complete);
    EXPECT_TRUE(second.Flush().Complete);
}
