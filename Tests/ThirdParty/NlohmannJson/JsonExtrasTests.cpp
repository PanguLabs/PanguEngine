// json.hpp 消费配置宏后会取消其定义。
#if !defined(JSON_USE_GLOBAL_UDLS) || JSON_USE_GLOBAL_UDLS != 0
#error "ThirdParty::NlohmannJson must disable global JSON literal imports"
#endif

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

#include <string>

static_assert(NLOHMANN_JSON_VERSION_MAJOR == 3 && NLOHMANN_JSON_VERSION_MINOR == 12 &&
              NLOHMANN_JSON_VERSION_PATCH == 0);

namespace
{
TEST(Json, ExplicitLiteralsAndPointer)
{
    // 只在显式命名空间使用 JSON 字面量。
    using namespace nlohmann::literals;
    const nlohmann::json fixture = R"({"value":9,"name":"test"})"_json;
    const auto pointer = "/value"_json_pointer;
    EXPECT_EQ(fixture.at(pointer), 9);
}

TEST(Json, ParsesObjectWithoutExceptions)
{
    const std::string text = R"({"kind":"schema","target":{"triple":"x86_64-pc-windows-msvc"},"count":3})";
    const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
    ASSERT_FALSE(document.is_discarded());
    ASSERT_TRUE(document.is_object());
    ASSERT_TRUE(document.contains("target"));
    EXPECT_EQ(document.at("target").at("triple"), "x86_64-pc-windows-msvc");
    EXPECT_EQ(document.at("count"), 3);
}

TEST(Json, InvalidInputIsDiscarded) { EXPECT_TRUE(nlohmann::json::parse("{", nullptr, false).is_discarded()); }
} // namespace
