#include "CollectionRequest.h"
#include "JsonSchema.h"
#include "ToolIo.h"
#include "ToolReport.h"
#include <Result.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h> // IWYU pragma: keep - supplies Windows SDK prerequisites.
#include <minwindef.h>
#include <stringapiset.h>
#include <winnls.h>
#endif

namespace pg::abi
{
namespace
{
[[nodiscard]] Status Reject(const std::filesystem::path& path, FailureReport& report, std::string_view field)
{
    return std::unexpected(
        report.Add(ToolError::kInvalidRequest, "validate_request", path.generic_string(), {}, {}, field));
}

[[nodiscard]] bool ValidText(std::string_view text) { return !text.empty() && !text.contains('\0'); }

[[nodiscard]] bool ValidName(std::string_view text)
{
    return ValidText(text) && text != "." && text != ".." && text.back() != '.' &&
           std::ranges::all_of(text,
                               [](unsigned char value)
                               {
                                   return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                                          (value >= '0' && value <= '9') || value == '_' || value == '-' ||
                                          value == '.';
                               });
}

[[nodiscard]] std::optional<Platform> ParsePlatform(std::string_view text)
{
    for (const auto& [name, value] : {
             std::pair{"windows", Platform::kWindows},
             std::pair{"linux", Platform::kLinux},
             std::pair{"macos", Platform::kMacos},
             std::pair{"android", Platform::kAndroid},
             std::pair{"ios", Platform::kIos},
         })
    {
        if (text == name)
        {
            return value;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::filesystem::path Utf8Path(std::string_view text)
{
    return {std::u8string(text.begin(), text.end())};
}

// Resolve existing parent aliases, but never require a cold-build artifact to exist.
[[nodiscard]] Result<std::filesystem::path> Resolve(const std::filesystem::path& path, FailureReport& report)
{
    return ResolvePath(path, "resolve_request_path", report);
}

[[nodiscard]] bool SameSpelling(const std::filesystem::path& left, const std::filesystem::path& right)
{
#ifdef _WIN32
    // Ordinal host comparison also handles absent outputs on a case-insensitive host.
    return ::CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
    return left == right;
#endif
}

[[nodiscard]] Result<bool> SamePath(const std::filesystem::path& left, const std::filesystem::path& right,
                                    FailureReport& report)
{
    const auto same = IsSameFile(left, right, report);
    if (!same || *same)
    {
        return same;
    }
    const auto first = Resolve(left, report);
    if (!first)
    {
        return std::unexpected(first.error());
    }
    const auto second = Resolve(right, report);
    return second ? Result<bool>{SameSpelling(*first, *second)} : std::unexpected(second.error());
}

[[nodiscard]] bool Within(const std::filesystem::path& parent, const std::filesystem::path& child)
{
    auto component = child.begin();
    for (const auto& part : parent)
    {
        if (component == child.end() || !SameSpelling(part, *component))
        {
            return false;
        }
        ++component;
    }
    return component != child.end();
}

[[nodiscard]] Status CheckWorkTree(const CollectionRequest& request, FailureReport& report)
{
    const auto work = request.OutputDirectory / "work";
    const auto check = [&](const std::filesystem::path& path) -> Status
    {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        if (error == std::errc::no_such_file_or_directory)
        {
            return {};
        }
        if (error || std::filesystem::is_symlink(status) ||
            (status.type() != std::filesystem::file_type::not_found && !std::filesystem::is_directory(status) &&
             !std::filesystem::is_regular_file(status)))
        {
            return std::unexpected(
                report.Add(ToolError::kPathConflict, "collection_work_alias", path.generic_string(), error));
        }
        if (std::filesystem::is_regular_file(status))
        {
            const auto links = std::filesystem::hard_link_count(path, error);
            if (error || links > 1)
            {
                return std::unexpected(
                    report.Add(ToolError::kPathConflict, "collection_work_hardlink", path.generic_string(), error));
            }
        }
        return {};
    };
    if (const auto checked = check(work); !checked)
    {
        return checked;
    }
    std::error_code error;
    // Never follow generated-tree links: a nested symlink or an output hard
    // link can redirect a later truncate to an input outside the reserved tree.
    std::filesystem::recursive_directory_iterator entry(work, error);
    if (error == std::errc::no_such_file_or_directory)
    {
        return {};
    }
    const std::filesystem::recursive_directory_iterator end;
    while (!error && entry != end)
    {
        // Probe owners atomically create fresh direct children and remove them
        // on completion. Never inspect or descend into another owner's tree:
        // its children may disappear while this pre-lock validation runs.
        if (entry.depth() == 0 && entry->path().filename().generic_string().starts_with(".observation-"))
        {
            entry.disable_recursion_pending();
        }
        if (const auto checked = check(entry->path()); !checked)
        {
            return checked;
        }
        entry.increment(error);
    }
    return error ? Status{std::unexpected(
                       report.Add(ToolError::kPathConflict, "collection_work_tree", work.generic_string(), error))}
                 : Status{};
}

[[nodiscard]] Status CheckPaths(const CollectionRequest& request, const std::filesystem::path& path,
                                FailureReport& report)
{
    const auto directory = Resolve(request.OutputDirectory, report);
    if (!directory)
    {
        return std::unexpected(directory.error());
    }
    const auto build = Resolve(request.BuildRoot, report);
    if (!build)
    {
        return std::unexpected(build.error());
    }
    const auto expected = Resolve(request.BuildRoot / "Abi" / request.Configuration / request.TargetName, report);
    if (!expected)
    {
        return std::unexpected(expected.error());
    }
    const auto header = Resolve(request.SignatureHeader, report);
    if (!header)
    {
        return std::unexpected(header.error());
    }
    if (!SameSpelling(*directory, *expected) || !Within(*build, *directory) || !Within(*directory, *header))
    {
        return Reject(path, report, "/output_directory");
    }
    const auto outputs = CollectionOutputFiles(request);
    const std::array inputs{
        path,
        request.SourceRoot,
        request.BuildRoot,
        request.Compiler,
        request.Database,
        request.ObjectManifest,
        request.Library,
        request.SchemaFile,
        request.LinkGraph,
    };
    if (const auto checked = CheckCollectionInputPaths(request, inputs, report); !checked)
    {
        return checked;
    }
    for (const auto& reserved : CollectionOutputDirectories(request))
    {
        const auto resolved = Resolve(reserved, report);
        if (!resolved)
        {
            return std::unexpected(resolved.error());
        }
        if (SameSpelling(*header, *resolved) || Within(*resolved, *header))
        {
            return Reject(path, report, "/signature_header");
        }
    }
    for (const auto& input : inputs)
    {
        const auto sameDirectory = SamePath(request.OutputDirectory, input, report);
        if (!sameDirectory)
        {
            return std::unexpected(sameDirectory.error());
        }
        if (*sameDirectory)
        {
            return std::unexpected(
                report.Add(ToolError::kPathConflict, "request_output_directory", input.generic_string()));
        }
    }
    for (auto output = outputs.begin(); output != outputs.end(); ++output)
    {
        std::error_code error;
        const bool directoryOutput = std::filesystem::is_directory(*output, error);
        if (error == std::errc::no_such_file_or_directory)
        {
            error.clear();
        }
        if (error || directoryOutput)
        {
            return std::unexpected(
                report.Add(ToolError::kPathConflict, "request_output_file", output->generic_string(), error));
        }
        // Some CIFS mounts report different inode identities for two hard links.
        // Conservatively reject pre-existing multiply-linked output files.
        const bool exists = std::filesystem::exists(*output, error);
        if (error)
        {
            return std::unexpected(
                report.Add(ToolError::kPathConflict, "request_output_hardlink", output->generic_string(), error));
        }
        if (exists)
        {
            const auto links = std::filesystem::hard_link_count(*output, error);
            if (error || links > 1)
            {
                return std::unexpected(
                    report.Add(ToolError::kPathConflict, "request_output_hardlink", output->generic_string(), error));
            }
        }
        for (const auto& input : inputs)
        {
            const auto same = SamePath(*output, input, report);
            if (!same)
            {
                return std::unexpected(same.error());
            }
            if (*same)
            {
                return std::unexpected(
                    report.Add(ToolError::kPathConflict, "request_output_alias", output->generic_string()));
            }
        }
        for (auto other = std::next(output); other != outputs.end(); ++other)
        {
            const auto same = SamePath(*output, *other, report);
            if (!same)
            {
                return std::unexpected(same.error());
            }
            const auto resolvedOutput = Resolve(*output, report);
            const auto resolvedOther = Resolve(*other, report);
            if (!resolvedOutput || !resolvedOther)
            {
                return std::unexpected(report.Add(ToolError::kResolvePath, "request_output_ancestor"));
            }
            if (*same || Within(*resolvedOutput, *resolvedOther) || Within(*resolvedOther, *resolvedOutput))
            {
                return std::unexpected(
                    report.Add(ToolError::kPathConflict, "request_output_alias", output->generic_string()));
            }
        }
    }
    return {};
}
} // namespace

std::vector<std::filesystem::path> CollectionOutputFiles(const CollectionRequest& request)
{
    return {
        request.SignatureHeader,
        request.OutputDirectory / "profile.json",
        request.OutputDirectory / "canonical.json",
        request.OutputDirectory / "fingerprint.sha256",
        request.OutputDirectory / "evidence.json",
        request.OutputDirectory / "complete.json",
        request.OutputDirectory / ".invalid",
        request.OutputDirectory / "archive-binding.json",
        request.OutputDirectory / "group-complete.json",
    };
}
std::vector<std::filesystem::path> CollectionOutputDirectories(const CollectionRequest& request)
{
    std::vector<std::filesystem::path> directories{
        request.OutputDirectory / "work",
        request.OutputDirectory / "receipts",
        request.OutputDirectory / ".collection-lock",
    };
    const auto relative =
        request.SignatureHeader.lexically_normal().lexically_relative(request.OutputDirectory.lexically_normal());
    if (!relative.empty() && relative.begin()->string().ends_with(".compiling"))
    {
        // Per-object lock names are dynamic; the entire suffix is reserved.
        directories.push_back(request.OutputDirectory / *relative.begin());
    }
    return directories;
}
Status CheckCollectionInputPaths(const CollectionRequest& request, std::span<const std::filesystem::path> inputs,
                                 FailureReport& report)
{
    if (const auto checked = CheckWorkTree(request, report); !checked)
    {
        return checked;
    }
    auto reserved = CollectionOutputFiles(request);
    const auto directories = CollectionOutputDirectories(request);
    reserved.insert(reserved.end(), directories.begin(), directories.end());
    std::vector<std::filesystem::path> logicalReserved;
    logicalReserved.reserve(reserved.size());
    for (auto& output : reserved)
    {
        std::error_code error;
        const auto logical = std::filesystem::absolute(output, error);
        if (error)
        {
            return std::unexpected(
                report.Add(ToolError::kResolvePath, "logical_collection_output", output.generic_string(), error));
        }
        logicalReserved.push_back(logical.lexically_normal());
        const auto resolved = Resolve(output, report);
        if (!resolved)
        {
            return std::unexpected(resolved.error());
        }
        output = *resolved;
    }
    const auto directory = Resolve(request.OutputDirectory, report);
    if (!directory)
    {
        return std::unexpected(directory.error());
    }
    // Typed lifecycle/backend callers retain the same request input protection
    // as the parser, even when they supply only their newly discovered paths.
    std::vector<std::filesystem::path> allInputs{
        request.RequestFile,    request.SourceRoot, request.BuildRoot,  request.Compiler,  request.Database,
        request.ObjectManifest, request.Library,    request.SchemaFile, request.LinkGraph,
    };
    allInputs.insert(allInputs.end(), inputs.begin(), inputs.end());
    for (const auto& input : allInputs)
    {
        if (input.empty())
        {
            continue;
        }
        // Canonical comparison also protects external aliases into work. Keep
        // the caller's spelling too: a private-tree symlink must not turn an
        // input logically within reserved work into an apparently external one.
        std::error_code error;
        const auto logical = std::filesystem::absolute(input, error);
        if (error)
        {
            return std::unexpected(
                report.Add(ToolError::kResolvePath, "logical_collection_input", input.generic_string(), error));
        }
        const auto normalized = logical.lexically_normal();
        for (const auto& output : logicalReserved)
        {
            if (SameSpelling(output, normalized) || Within(output, normalized))
            {
                return std::unexpected(
                    report.Add(ToolError::kPathConflict, "collection_logical_input_alias", input.generic_string()));
            }
        }
        const auto resolvedInput = Resolve(input, report);
        if (!resolvedInput)
        {
            return std::unexpected(resolvedInput.error());
        }
        if (Within(*directory, *resolvedInput))
        {
            const auto relative = resolvedInput->lexically_relative(*directory);
            if (relative.begin()->string().ends_with(".compiling"))
            {
                return std::unexpected(report.Add(ToolError::kPathConflict, "collection_compile_lock_input_alias",
                                                  input.generic_string()));
            }
        }
        for (const auto& output : reserved)
        {
            // File outputs are separately required to have at most one hard
            // link before any lifecycle write. Resolve symlink aliases once,
            // rather than repeating filesystem queries for every header pair.
            if (SameSpelling(output, *resolvedInput) || Within(output, *resolvedInput))
            {
                return std::unexpected(
                    report.Add(ToolError::kPathConflict, "collection_output_input_alias", input.generic_string()));
            }
        }
    }
    return {};
}

Result<CollectionRequest> ReadCollectionRequest(const std::filesystem::path& path, FailureReport& report)
{
    const auto contents = ReadFile(path, "read_request", report);
    if (!contents)
    {
        return std::unexpected(contents.error());
    }
    ParseDiagnostic diagnostic;
    const auto document = ParseUnique(*contents, &diagnostic);
    if (!document || !document->is_object())
    {
        return std::unexpected(
            report.Add(ToolError::kInvalidRequest, "parse_request", path.generic_string(), {}, diagnostic.ByteOffset));
    }
    // All v2 fields below are required; reject extra root fields rather than
    // silently accepting a hand-written Profile as collection evidence.
    constexpr std::size_t kFieldCount = 18;
    if (document->size() != kFieldCount)
    {
        return std::unexpected(Reject(path, report, "/").error());
    }
    const auto* version = Find(*document, "schema");
    if (version == nullptr || !version->is_number_integer() || *version != 2)
    {
        return std::unexpected(Reject(path, report, "/schema").error());
    }
    CollectionRequest request;
    const auto requestIdentity = Resolve(path, report);
    if (!requestIdentity)
    {
        return std::unexpected(requestIdentity.error());
    }
    request.RequestFile = *requestIdentity;
    for (const auto& [key, member] : {
             std::pair{"source_root", &CollectionRequest::SourceRoot},
             std::pair{"build_root", &CollectionRequest::BuildRoot},
             std::pair{"compiler", &CollectionRequest::Compiler},
             std::pair{"database", &CollectionRequest::Database},
             std::pair{"object_manifest", &CollectionRequest::ObjectManifest},
             std::pair{"library", &CollectionRequest::Library},
             std::pair{"output_directory", &CollectionRequest::OutputDirectory},
             std::pair{"schema_file", &CollectionRequest::SchemaFile},
             std::pair{"signature_header", &CollectionRequest::SignatureHeader},
             std::pair{"link_graph", &CollectionRequest::LinkGraph},
         })
    {
        const auto* value = Find(*document, key);
        if (value == nullptr || !value->is_string() || !ValidText(Text(*value)))
        {
            return std::unexpected(Reject(path, report, std::string("/") + key).error());
        }
        auto parsed = Utf8Path(Text(*value));
        if (!parsed.is_absolute())
        {
            return std::unexpected(Reject(path, report, std::string("/") + key).error());
        }
        request.*member = std::move(parsed);
    }
    for (const auto& [key, member] : {
             std::pair{"target_name", &CollectionRequest::TargetName},
             std::pair{"configuration", &CollectionRequest::Configuration},
         })
    {
        const auto* value = Find(*document, key);
        if (value == nullptr || !value->is_string() || !ValidName(Text(*value)))
        {
            return std::unexpected(Reject(path, report, std::string("/") + key).error());
        }
        request.*member = Text(*value);
    }
    for (const auto& [key, member] : {
             std::pair{"host_platform", &CollectionRequest::HostPlatform},
             std::pair{"target_platform", &CollectionRequest::TargetPlatform},
         })
    {
        const auto* value = Find(*document, key);
        const auto platform = value != nullptr && value->is_string() ? ParsePlatform(Text(*value)) : std::nullopt;
        if (!platform || (std::string_view(key) == "host_platform" &&
                          (*platform == Platform::kAndroid || *platform == Platform::kIos)))
        {
            return std::unexpected(Reject(path, report, std::string("/") + key).error());
        }
        request.*member = *platform;
    }
    const auto* quoting = Find(*document, "command_line_style");
    if (quoting == nullptr || !quoting->is_string() ||
        Text(*quoting) != (request.HostPlatform == Platform::kWindows ? "windows" : "posix"))
    {
        return std::unexpected(Reject(path, report, "/command_line_style").error());
    }
    request.CommandLineStyle =
        request.HostPlatform == Platform::kWindows ? CommandLineStyle::kWindows : CommandLineStyle::kPosix;
    const auto* context = Find(*document, "platform_context");
    const auto* environment = Find(*document, "environment_baseline");
    if (context == nullptr || !context->is_object() || environment == nullptr || !environment->is_object())
    {
        return std::unexpected(Reject(path, report, "/platform_context or /environment_baseline").error());
    }
    request.PlatformContext = *context;
    for (const auto& [key, value] : environment->items())
    {
        if (!ValidText(key) || key.contains('=') || (!value.is_null() && !value.is_string()) ||
            (value.is_string() && Text(value).contains('\0')))
        {
            return std::unexpected(Reject(path, report, "/environment_baseline").error());
        }
        request.EnvironmentBaseline.emplace(key, value.is_null() ? std::nullopt : std::optional{Text(value)});
    }
    const auto checked = CheckPaths(request, path, report);
    return checked ? Result<CollectionRequest>{std::move(request)} : std::unexpected(checked.error());
}
} // namespace pg::abi
