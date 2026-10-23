#include "AbiProfile.h"
#include "JsonSchema.h"
#include "Sha256.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

namespace pg::abi
{
namespace
{
[[nodiscard]] std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return stream ? std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()) : "";
}

struct Checks
{
    int Count = 0;
    int Failed = 0;

    void Expect(bool passed, std::string_view label)
    {
        ++Count;
        if (!passed)
        {
            ++Failed;
            std::cerr << "FAIL: " << label << '\n';
        }
    }
};

void Change(Json& profile, std::string_view group, std::string_view key, std::string_view value)
{
    profile.at(std::string(group)).at(std::string(key)) = value;
}

void ChangeOption(Json& profile, std::size_t index, std::string_view value)
{
    profile.at("language").at("abi_options").at(index) = value;
}

// All fixtures are synthetic protocol vectors, NOT attested toolchain configurations.
[[nodiscard]] Json PlatformProfile(const Json& base, std::string_view platform)
{
    Json profile = base;
    if (platform == "windows")
    {
        return profile;
    }

    Change(profile, "target", "cpu_abi", "aapcs64");
    Change(profile, "target", "pointer_bits", "64");
    Change(profile, "target", "endian", "little");
    Change(profile, "target", "object", "elf");
    Change(profile, "compiler", "cxx_abi", "itanium");
    ChangeOption(profile, 0, "bitfield_layout=itanium");
    ChangeOption(profile, 1, "calling_convention=aapcs64");
    ChangeOption(profile, 6, "vector_abi=aapcs64");
    ChangeOption(profile, 7, "wchar_width=32");
    Change(profile, "stdlib", "id", "libc++");
    Change(profile, "stdlib", "version", "200100");
    Change(profile, "stdlib", "abi_version", "1");
    Change(profile, "stdlib", "abi_namespace", "__1");
    Change(profile, "stdlib", "iterator_debug_level", "na");
    if (platform == "linux")
    {
        Change(profile, "target", "triple", "aarch64-unknown-linux-gnu");
        Change(profile, "runtime", "crt", "glibc");
        Change(profile, "runtime", "cxx_runtime", "libcxx-shared");
        Change(profile, "runtime", "cxxabi_runtime", "libcxxabi-shared");
        Change(profile, "runtime", "sdk_abi_id", "linux-sysroot-ubuntu-24.04");
    }
    else if (platform == "android")
    {
        Change(profile, "target", "triple", "aarch64-linux-android24");
        Change(profile, "compiler", "distribution", "android-ndk");
        Change(profile, "compiler", "version", "19.0.0");
        Change(profile, "runtime", "crt", "bionic");
        Change(profile, "runtime", "cxx_runtime", "ndk-libcxx-shared");
        Change(profile, "runtime", "cxxabi_runtime", "ndk-libcxxabi");
        Change(profile, "runtime", "sdk_abi_id", "android-ndk-r27c-api-24");
    }
    else
    {
        const bool isMac = platform == "macos";
        Change(profile, "target", "triple", isMac ? "arm64-apple-macosx15.0" : "arm64-apple-ios18.2");
        Change(profile, "target", "object", "macho");
        Change(profile, "compiler", "distribution", "macports");
        Change(profile, "runtime", "crt", "apple-libsystem");
        Change(profile, "runtime", "cxx_runtime", "apple-libcxx-shared");
        Change(profile, "runtime", "cxxabi_runtime", "apple-libcxxabi");
        Change(profile, "runtime", "sdk_abi_id", isMac ? "macos-sdk-15.0" : "ios-sdk-18.2");
    }
    return profile;
}

int Main(const std::filesystem::path& wiki)
{
    Checks checks;
    const auto rawSchema = ReadFile(wiki / "abi-profile-v1.schema.json");
    const auto rawVectors = ReadFile(wiki / "abi-profile-v1.vectors.json");
    auto schema = ParseUnique(rawSchema);
    auto vectors = ParseUnique(rawVectors);
    if (!schema || !vectors)
    {
        std::cerr << "Schema/vectors missing or invalid JSON\n";
        return 2;
    }
    checks.Expect(SchemaUsesSupportedKeywords(*schema), "supported pinned Schema vocabulary");
    checks.Expect(schema->at("$schema") == "https://json-schema.org/draft/2020-12/schema", "draft 2020-12");
    const Json& baseline = vectors->at("baseline").at("profile");
    checks.Expect(Validate(*schema, baseline), "baseline Schema validation");

    for (const auto& test : vectors->at("schema_cases"))
    {
        Json profile = baseline;
        for (const auto& item : test.at("replacements").items())
        {
            profile.at(Json::json_pointer(item.key())) = item.value();
        }
        checks.Expect(Validate(*schema, profile) == test.at("valid").get<bool>(),
                      test.at("name").get_ref<const std::string&>());
    }
    for (const char* const platform : {"windows", "linux", "android", "macos", "ios"})
    {
        const auto profile = PlatformProfile(baseline, platform);
        checks.Expect(Validate(*schema, profile), platform);
    }
    for (const char* const platform : {"windows", "linux", "android", "macos", "ios"})
    {
        Json profile = PlatformProfile(baseline, platform);
        Change(profile, "stdlib", "id", platform == std::string_view("windows") ? "libc++" : "msvc-stl");
        checks.Expect(!Validate(*schema, profile), "mixed platform STL rejected");
        profile = PlatformProfile(baseline, platform);
        Change(profile, "compiler", "distribution",
               platform == std::string_view("android") ? "macports" : "android-ndk");
        checks.Expect(!Validate(*schema, profile), "mixed platform compiler rejected");
    }

    {
        Json profile = baseline;
        ChangeOption(profile, 6, "vector_abi=unknown");
        checks.Expect(!Validate(*schema, profile), "unknown vector ABI");
        profile = baseline;
        Change(profile, "target", "cpu_abi", "unknown");
        checks.Expect(!Validate(*schema, profile), "unknown CPU ABI");
        profile = baseline;
        Change(profile, "runtime", "sdk_abi_id", "windows-sdk-unverified");
        checks.Expect(!Validate(*schema, profile), "unknown SDK identity");
        profile = baseline;
        Change(profile, "compiler", "version", "22.0.0");
        checks.Expect(!Validate(*schema, profile), "old Clang disallowed");
        profile = baseline;
        auto& options = profile.at("language").at("abi_options");
        std::swap(options.at(0), options.at(7));
        checks.Expect(!Validate(*schema, profile), "ABI option order");
        profile = baseline;
        profile.at("compiler").update(Json{{"extra", "not-v1"}});
        checks.Expect(!Validate(*schema, profile), "unknown field");
        profile = baseline;
        profile.at("schema") = 1.0;
        checks.Expect(!Validate(*schema, profile), "integer token 1.0");
    }

    const auto& expected = vectors->at("baseline");
    const auto canonical = CanonicalJson(baseline);
    checks.Expect(canonical == expected.at("canonical").get_ref<const std::string&>(), "RFC8785 canonical");
    checks.Expect(canonical.size() == expected.at("canonical_byte_length").get<std::size_t>(), "canonical length");
    const auto digest = Fingerprint(baseline);
    checks.Expect(digest == expected.at("sha256").get_ref<const std::string&>(), "frozen SHA256");
    Json mutated = baseline;
    Change(mutated, "stdlib", "iterator_debug_level", "2");
    checks.Expect(Validate(*schema, mutated), "iterator debug 2 is schema-valid");
    checks.Expect(Fingerprint(mutated) == vectors->at("mutation").at("sha256").get_ref<const std::string&>(),
                  "changed ABI fingerprint");
    checks.Expect(digest != Fingerprint(mutated), "changed config changes digest");
    checks.Expect(Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                  "SHA256 NIST abc");

    const auto prefix = vectors->at("domain_prefix_ascii_plus_nul").get_ref<const std::string&>();
    constexpr auto kPrefix = std::to_array("PANGU_ABI_PROFILE_V1");
    checks.Expect(prefix.size() == kPrefix.size() && prefix == std::string(kPrefix.data(), kPrefix.size()) &&
                      prefix.back() == '\0',
                  "exact SHA256 domain prefix");
    checks.Expect(!ParseUnique(R"({"schema":1,"schema":1})"), "duplicate keys rejected");
    checks.Expect(!ParseUnique(R"({"nested":{"x":1,"x":2}})"), "nested duplicate keys rejected");
    checks.Expect(!ParseUnique("{"), "truncated JSON rejected");
    const auto nulSuffix = baseline.dump() + std::string(1, '\0') + "garbage";
    checks.Expect(!ParseUnique(nulSuffix), "raw NUL cannot truncate JSON input");
    checks.Expect(!ParseValidatedProfile(nulSuffix, *schema), "raw NUL Profile rejected");
    checks.Expect(ParseUnique(R"({"value":"\u0000"})").has_value(), "escaped NUL remains valid JSON");
    {
        Json invalid = baseline;
        Change(invalid, "stdlib", "id", "invalid");
        ValidationDiagnostic diagnostic;
        checks.Expect(!Validate(*schema, invalid, &diagnostic) && diagnostic.Path.View() == "/stdlib/id",
                      "validation reports the failing field");
        invalid = baseline;
        invalid.emplace("unknown/key~", true);
        checks.Expect(!Validate(*schema, invalid, &diagnostic) && diagnostic.Path.View() == "/unknown~1key~0",
                      "validation escapes JSON Pointer tokens");
        ParseDiagnostic parse;
        checks.Expect(!ParseUnique(nulSuffix, &parse) && parse.ByteOffset == baseline.dump().size() + 1,
                      "raw NUL failure reports its byte offset");
    }
    checks.Expect(!ParseValidatedProfile(R"({"schema":1})", *schema), "incomplete profile rejected");
    checks.Expect(ParseValidatedProfile(baseline.dump(), *schema).has_value(), "round trip accepted");
    const auto formatted = ParseUnique(baseline.dump(2));
    checks.Expect(formatted && CanonicalJson(*formatted) == canonical, "input whitespace invariant");
    checks.Expect(!SchemaUsesSupportedKeywords(Json{{"unsupportedSchemaKeyword", true}}),
                  "unsupported Schema keyword rejected");

    std::cout << "ABI Profile v1 checks " << (checks.Count - checks.Failed) << "/" << checks.Count << "\n";
    return checks.Failed == 0 ? 0 : 1;
}
} // namespace
} // namespace pg::abi

int main(int argc, char* const* argv)
{
    if (argc != 2)
    {
        return 2;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc)).subspan<1>();
    return pg::abi::Main(args.front());
}
