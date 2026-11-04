#include "PluginManifest.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>

namespace
{
[[nodiscard]] bool WriteText(const std::filesystem::path& path, std::string_view content)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    return out.good();
}

[[nodiscard]] bool Check(const std::filesystem::path& root, std::string_view label, std::string_view toml,
                         bool shouldPass)
{
    const auto path = root / (std::string(label) + ".toml");
    if (!WriteText(path, toml))
    {
        std::cerr << "Cannot write fixture: " << path << '\n';
        return false;
    }
    const auto result = pg::build::plugin::ParsePluginManifest(path);
    if (result.has_value() != shouldPass)
    {
        std::cerr << "Unexpected parse result for " << label << ": " << (result ? "success" : result.error()) << '\n';
        return false;
    }
    if (!result && !result.error().contains(path.string()))
    {
        std::cerr << "Missing diagnostic path for " << label << ": " << result.error() << '\n';
        return false;
    }
    if (result && label == "valid")
    {
        return result->Id == "pangu.physics" && result->Name == "中文物理" && result->Version == "1.2.3-alpha.1+sha.7";
    }
    if (result && label == "minimal")
    {
        return result->Id == "mygame.gameplay" && result->Name.empty() && result->Version.empty();
    }
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: PanguPluginManifestTests <fixture-dir>\n";
        return 2;
    }
    const std::filesystem::path root(argv[1]);
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
    {
        std::cerr << "Cannot create fixtures: " << error.message() << '\n';
        return 2;
    }
    const std::array cases{
        std::tuple{"valid", "[plugin]\nid = 'pangu.physics'\nname = '中文物理'\nversion = '1.2.3-alpha.1+sha.7'\n",
                   true},
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
    bool passed = true;
    for (const auto& [label, input, shouldPass] : cases)
    {
        passed &= Check(root, label, input, shouldPass);
    }
    const auto missing = root / "never-created.toml";
    const auto missingResult = pg::build::plugin::ParsePluginManifest(missing);
    if (missingResult || !missingResult.error().contains(missing.string()))
    {
        std::cerr << "Missing input must fail with full path\n";
        passed = false;
    }
    return passed ? 0 : 1;
}
