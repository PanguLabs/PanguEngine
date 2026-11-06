#include "PluginManifest.h"
#include "TestWorkspace.h"

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>

namespace
{
const std::array kCases{
    std::tuple{"valid", "[plugin]\nid = 'pangu.physics'\nname = '中文物理'\nversion = '1.2.3-alpha.1+sha.7'\n", true},
    std::tuple{"minimal", "[plugin]\nid = 'mygame.gameplay'\n", true},
    std::tuple{"missing-id", "[plugin]\nname = 'No ID'\n", false},
    std::tuple{"uppercase-id", "[plugin]\nid = 'Pangu.Physics'\n", false},
    std::tuple{"empty-segment", "[plugin]\nid = 'foo..bar'\n", false},
    std::tuple{"bad-hyphen", "[plugin]\nid = 'foo.-bar'\n", false},
    std::tuple{"leading-zero-version", "[plugin]\nid = 'valid.id'\nversion = '01.2.3'\n", false},
    std::tuple{"leading-zero-prerelease", "[plugin]\nid = 'valid.id'\nversion = '1.0.0-alpha.01'\n", false},
    std::tuple{"unknown-key", "[plugin]\nid = 'valid.id'\nenabled = true\n", false},
    std::tuple{"unknown-section", "[plugin]\nid = 'valid.id'\n[dependencies]\nname = 'x'\n", false},
    std::tuple{"missing-section", "id = 'valid.id'\n", false},
    std::tuple{"wrong-id-type", "[plugin]\nid = 42\n", false},
    std::tuple{"wrong-name-type", "[plugin]\nid = 'valid.id'\nname = 42\n", false},
    std::tuple{"wrong-version-type", "[plugin]\nid = 'valid.id'\nversion = false\n", false},
    std::tuple{"damaged-toml", "[plugin]\nid = 'bad\n", false},
    std::tuple{"valid-build-zero", "[plugin]\nid = 'pangu.test-1'\nversion = '0.0.0+001'\n", true},
    std::tuple{"valid-prerelease-zero", "[plugin]\nid = 'pangu.test-2'\nversion = '1.0.0-0'\n", true},
    std::tuple{"missing-patch", "[plugin]\nid = 'valid.id'\nversion = '1.2'\n", false},
    std::tuple{"extra-version-part", "[plugin]\nid = 'valid.id'\nversion = '1.2.3.4'\n", false},
    std::tuple{"empty-prerelease", "[plugin]\nid = 'valid.id'\nversion = '1.0.0-a..b'\n", false},
    std::tuple{"empty-build", "[plugin]\nid = 'valid.id'\nversion = '1.0.0+'\n", false},
    std::tuple{"bad-build", "[plugin]\nid = 'valid.id'\nversion = '1.0.0+hash!'\n", false},
    std::tuple{"bad-id-digit", "[plugin]\nid = '7game.play'\n", false},
};

class PluginManifest : public testing::TestWithParam<std::tuple<const char*, const char*, bool>>
{
};

TEST_P(PluginManifest, ParsesAndDiagnoses)
{
    const auto& [label, input, shouldPass] = GetParam();
    std::error_code error;
    const auto work = std::filesystem::current_path(error);
    ASSERT_FALSE(error);
    const auto root = pg::test::CreateTestWorkspace(work, label);
    ASSERT_TRUE(root);
    const auto path = *root / "Plugin.toml";
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out);
    out << input;
    out.close();
    ASSERT_TRUE(out.good());
    const auto result = pg::build::plugin::ParsePluginManifest(path);
    ASSERT_EQ(result.has_value(), shouldPass) << (result ? "success" : result.error());
    if (!result)
    {
        EXPECT_TRUE(result.error().contains(path.string())) << result.error();
        return;
    }
    if (std::string_view(label) == "valid")
    {
        EXPECT_EQ(result->Id, "pangu.physics");
        EXPECT_EQ(result->Name, "中文物理");
        EXPECT_EQ(result->Version, "1.2.3-alpha.1+sha.7");
    }
    if (std::string_view(label) == "minimal")
    {
        EXPECT_EQ(result->Id, "mygame.gameplay");
        EXPECT_TRUE(result->Name.empty());
        EXPECT_TRUE(result->Version.empty());
    }
}

INSTANTIATE_TEST_SUITE_P(ManifestCases, PluginManifest, testing::ValuesIn(kCases));

TEST(PluginManifest, MissingInputIncludesFullDiagnosticPath)
{
    std::error_code error;
    const auto work = std::filesystem::current_path(error);
    ASSERT_FALSE(error);
    const auto root = pg::test::CreateTestWorkspace(work, "missing-input");
    ASSERT_TRUE(root);
    const auto path = *root / "never-created.toml";
    const auto result = pg::build::plugin::ParsePluginManifest(path);
    ASSERT_FALSE(result);
    EXPECT_TRUE(result.error().contains(path.string())) << result.error();
}
} // namespace
