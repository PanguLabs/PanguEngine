#include <gtest/gtest.h>

#include <any>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string_view>

namespace
{
TEST(Registration, First)
{
    std::stringstream stream;
    std::iostream& base = stream;
    EXPECT_EQ(dynamic_cast<std::stringstream*>(&base), &stream);
    EXPECT_EQ(typeid(base), typeid(std::stringstream));
    const std::any value{42};
    ASSERT_NE(std::any_cast<int>(&value), nullptr);
    EXPECT_EQ(*std::any_cast<int>(&value), 42);
    EXPECT_EQ(value.type(), typeid(int));
}

TEST(Registration, Second)
{
    // 独立夹具进程只读取父进程设置的环境，没有并发写入。
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* result = std::getenv("PANGU_TEST_FIXTURE_RESULT");
    const std::string_view value = result == nullptr ? "pass" : result;
    if (value == "skip")
    {
        GTEST_SKIP() << "Fixture requested a skip";
    }
    EXPECT_EQ(value, "pass");
}
} // namespace
