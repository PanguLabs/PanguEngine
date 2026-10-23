#include "AbiProfile.h"
#include "JsonSchema.h"
#include "Sha256.h"
#include "ToolIo.h"
#include "ToolReport.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace pg::abi
{
namespace
{
[[nodiscard]] std::string NormalizeSchemaNewlines(std::string_view raw)
{
    std::string normalized(raw);
    std::size_t index = 0;
    while ((index = normalized.find("\r\n", index)) != std::string::npos)
    {
        normalized.replace(index, 2, "\n");
    }
    return normalized;
}

[[nodiscard]] int Fail(FailureReport& report, ToolError error, int exitCode, std::string_view stage,
                       const std::filesystem::path& path = {}, std::optional<std::size_t> byteOffset = std::nullopt,
                       std::string_view jsonPointer = {}, std::source_location origin = std::source_location::current())
{
    // The CLI returns the exit category; the complete error is retained in the report.
    (void)report.Add(error, stage, path.generic_string(), {}, byteOffset, jsonPointer, origin);
    return exitCode;
}

// The tool validates JSON content only; compiler/runtime provenance is unverified.
int Main(const std::filesystem::path& schemaPath, const std::filesystem::path& profilePath,
         const std::filesystem::path& canonicalOutput, const std::filesystem::path& fingerprintOutput,
         FailureReport& report)
{
    const std::array pairs{
        std::pair{&canonicalOutput, &schemaPath},        std::pair{&canonicalOutput, &profilePath},
        std::pair{&fingerprintOutput, &schemaPath},      std::pair{&fingerprintOutput, &profilePath},
        std::pair{&canonicalOutput, &fingerprintOutput},
    };
    for (const auto& [left, right] : pairs)
    {
        const auto same = IsSameFile(*left, *right, report);
        if (!same)
        {
            return 2;
        }
        if (*same)
        {
            return Fail(report, ToolError::kPathConflict, 2, "check_paths", *left);
        }
    }
    // Invalidation must clear artifacts left by any prior successful invocation.
    if (!ClearGeneratedFiles(canonicalOutput, fingerprintOutput, report))
    {
        return 4;
    }

    const auto schemaText = ReadFile(schemaPath, "read_schema", report);
    if (!schemaText)
    {
        return 2;
    }

    // Checked-in frozen v1 Schema normalized to LF (CRLF/LF are equivalent).
    constexpr std::string_view kPinnedSchemaSha256 = "8f08c59ad5afaa4bda53eb2767fc0bfb374c1104b3b1943d066f3125a4c41e25";
    if (schemaText->empty() || Sha256Hex(NormalizeSchemaNewlines(*schemaText)) != kPinnedSchemaSha256)
    {
        return Fail(report, ToolError::kSchemaMismatch, 2, "validate_schema", schemaPath);
    }

    const auto schema = ParseUnique(*schemaText);
    if (!schema || !schema->is_object() || !SchemaUsesSupportedKeywords(*schema))
    {
        return Fail(report, ToolError::kInvalidSchema, 2, "parse_schema", schemaPath);
    }
    const auto* id = Find(*schema, "$schema");
    if (id == nullptr || !id->is_string() || Text(*id) != "https://json-schema.org/draft/2020-12/schema")
    {
        return Fail(report, ToolError::kInvalidSchema, 2, "validate_schema", schemaPath);
    }

    const auto profileText = ReadFile(profilePath, "read_profile", report);
    if (!profileText)
    {
        return 3;
    }
    ParseDiagnostic diagnostic;
    const auto profile = ParseUnique(*profileText, &diagnostic);
    if (!profile)
    {
        return Fail(report, ToolError::kInvalidProfile, 3, "parse_profile", profilePath, diagnostic.ByteOffset);
    }
    ValidationDiagnostic validation;
    if (!Validate(*schema, *profile, &validation))
    {
        report.Truncated |= validation.Path.Truncated;
        return Fail(report, ToolError::kInvalidProfile, 3, "validate_profile", profilePath, std::nullopt,
                    validation.Path.View(), validation.Origin);
    }

    const auto canonical = CanonicalJson(*profile);
    const auto digest = Fingerprint(*profile);
    if (!WriteFile(canonicalOutput, canonical, report))
    {
        // Preserve the write/commit error as primary; append every rollback error.
        (void)ClearGeneratedFiles(canonicalOutput, fingerprintOutput, report);
        return 4;
    }
    // Creation reveals filesystem aliases (case folding, normalization, short
    // names) that cannot always be identified while both output names are absent.
    const auto sameOutput = IsSameFile(canonicalOutput, fingerprintOutput, report);
    if (!sameOutput || *sameOutput)
    {
        const int result = sameOutput ? Fail(report, ToolError::kPathConflict, 2, "check_paths", fingerprintOutput) : 2;
        (void)ClearGeneratedFiles(canonicalOutput, fingerprintOutput, report);
        return result;
    }
    if (!WriteFile(fingerprintOutput, digest, report))
    {
        (void)ClearGeneratedFiles(canonicalOutput, fingerprintOutput, report);
        return 4;
    }
    std::cerr << "Profile Schema checked; provenance and TU ABI options NOT ATTESTED.\n";
    return 0;
}
} // namespace
} // namespace pg::abi

int main(int argc, char* const* argv)
{
    pg::abi::FailureReport report;
    if (argc != 5)
    {
        (void)report.Add(pg::abi::ToolError::kArguments, "arguments");
        report.WriteJson(std::cout);
        std::cerr << "Usage: PanguAbiProfileTool <schema.json> <profile.json> <canonical-out> <sha256-out>\n";
        return 2;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc)).subspan<1>();
    // Exactly four parameters, guarded by argc above.
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const int result = pg::abi::Main(args[0], args[1], args[2], args[3], report);
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    if (result != 0)
    {
        report.WriteJson(std::cout);
        std::cerr << "ABI Profile operation failed; structured failure report is on stdout.\n";
    }
    return result;
}
