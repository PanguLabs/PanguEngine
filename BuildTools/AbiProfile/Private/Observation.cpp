#include "Observation.h"
#include "CollectionRequest.h"
#include "CollectionState.h"
#include "CompileCommands.h"
#include "HeaderTrace.h"
#include "ProbeProcess.h"
#include "Process.h"
#include "ToolIo.h"
#include "ToolReport.h"
#include <Result.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h> // IWYU pragma: keep - native SDK prerequisites.
#include <errhandlingapi.h>
#include <fileapi.h>
#include <handleapi.h>
#include <minwinbase.h>
#include <winnt.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace pg::abi
{
namespace
{
[[nodiscard]] Result<std::vector<std::filesystem::path>> GeneratedPaths(const std::filesystem::path& directory,
                                                                        FailureReport& report)
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(directory, error);
    if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))
    {
        return std::unexpected(
            report.Add(ToolError::kRemoveOutput, "observation_private_directory", directory.generic_string(), error));
    }
    std::vector<std::filesystem::path> paths{directory};
    std::filesystem::recursive_directory_iterator current(directory, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && current != end)
    {
        const auto kind = current->symlink_status(error);
        if (error || std::filesystem::is_symlink(kind) ||
            (!std::filesystem::is_regular_file(kind) && !std::filesystem::is_directory(kind)))
        {
            return std::unexpected(report.Add(ToolError::kRemoveOutput, "observation_private_entry",
                                              current->path().generic_string(), error));
        }
        paths.push_back(current->path());
        current.increment(error);
    }
    if (error)
    {
        return std::unexpected(
            report.Add(ToolError::kRemoveOutput, "observation_private_entries", directory.generic_string(), error));
    }
    return paths;
}

[[nodiscard]] bool PathCharacterEqual(char left, char right)
{
    if ((left == '/' || left == '\\') && (right == '/' || right == '\\'))
    {
        return true;
    }
#ifdef _WIN32
    return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
#else
    return left == right;
#endif
}
[[nodiscard]] bool PathAt(std::string_view text, std::size_t position, std::string_view path)
{
    return position <= text.size() && path.size() <= text.size() - position &&
           std::ranges::equal(text.substr(position, path.size()), path, PathCharacterEqual);
}
[[nodiscard]] bool PathBoundary(char character)
{
    return std::isspace(static_cast<unsigned char>(character)) != 0 ||
           std::string_view("\"'=:,;()[]{}<>").contains(character);
}
struct GeneratedPath
{
    std::string Physical;
    std::string Logical;
};
[[nodiscard]] Status NormalizeFacts(nlohmann::json& value, std::span<const GeneratedPath> paths,
                                    std::string_view privateDirectory, FailureReport& report)
{
    std::vector<nlohmann::json*> pending{&value};
    while (!pending.empty())
    {
        auto& current = *pending.back();
        pending.pop_back();
        if (current.is_object() || current.is_array())
        {
            // Preserve the platform-defined order without recursive stack use.
            for (auto child = current.rbegin(); child != current.rend(); ++child)
            {
                pending.push_back(&*child);
            }
            continue;
        }
        if (!current.is_string())
        {
            continue;
        }
        auto& text = current.get_ref<std::string&>();
        for (std::size_t index = 0; index < text.size();)
        {
            bool replaced = false;
            if (index == 0 || PathBoundary(text.at(index - 1)))
            {
                for (const auto& path : paths)
                {
                    const auto end = index + path.Physical.size();
                    if (PathAt(text, index, path.Physical) &&
                        (end == text.size() || (end < text.size() && PathBoundary(text.at(end)))))
                    {
                        text.replace(index, path.Physical.size(), path.Logical);
                        index += path.Logical.size();
                        replaced = true;
                        break;
                    }
                }
            }
            if (!replaced)
            {
                ++index;
            }
        }
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            if (PathAt(text, index, privateDirectory))
            {
                return std::unexpected(
                    report.Add(ToolError::kInvalidProfile, "observation_unmodeled_private_path", privateDirectory));
            }
        }
    }
    return {};
}
} // namespace
Result<ProcessResult> ExecuteObservation(const std::filesystem::path& program, const std::filesystem::path& directory,
                                         std::vector<std::string> arguments, std::string_view stage,
                                         FailureReport& report)
{
    ProcessRequest process;
    process.Program = program;
    process.Directory = directory;
    process.Arguments = std::move(arguments);
    const auto result = RunProbe(process, stage, report);
    if (!result)
    {
        return std::unexpected(result.error());
    }
    if (result->ExitCode != 0)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, stage, program.generic_string()));
    }
    return result;
}

Status CheckObservationOutput(const std::filesystem::path& path, FailureReport& report)
{
    std::error_code error;
    const auto kind = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory ||
        (!error && kind.type() == std::filesystem::file_type::not_found))
    {
        return {};
    }
    if (error || !std::filesystem::is_regular_file(kind) || std::filesystem::is_symlink(kind) ||
        std::filesystem::hard_link_count(path, error) != 1 || error)
    {
        return std::unexpected(
            report.Add(ToolError::kPathConflict, "observation_output_alias", path.generic_string(), error));
    }
    return {};
}

ObservationWork::~ObservationWork()
{
    if (!PrivateDirectory.empty())
    {
        Report->CleanupIncomplete |= !Cleanup();
    }
#ifdef _WIN32
    if (Handle != nullptr)
    {
        (void)::CloseHandle(Handle);
    }
#else
    if (Descriptor >= 0)
    {
        (void)::close(Descriptor);
    }
#endif
}
Result<std::unique_ptr<ObservationWork>> ObservationWork::Acquire(const CollectionRequest& request,
                                                                  std::span<const CompileUnit> units,
                                                                  std::span<const std::filesystem::path> knownInputs,
                                                                  std::string_view stage, FailureReport& report)
{
    std::vector<std::filesystem::path> inputs(knownInputs.begin(), knownInputs.end());
    for (const auto& unit : units)
    {
        inputs.push_back(unit.Source);
        inputs.push_back(unit.Object);
        inputs.insert(inputs.end(), unit.ResponseFiles.begin(), unit.ResponseFiles.end());
    }
    if (const auto checked = CheckCollectionInputPaths(request, inputs, report, CollectionOutputWait::Waiting());
        !checked)
    {
        return std::unexpected(checked.error());
    }
    // Discover physical headers for all compilation inputs without writing
    // generated paths first. Known probes and the witness can include headers
    // absent from every target TU; their reverse aliases need the same guard.
    for (const auto& unit : units)
    {
        const auto discover = [&](const std::filesystem::path& source) -> Status
        {
            auto arguments = unit.AbiArguments;
            arguments.insert(arguments.end(), {"-E", "-dM", "-H", source.generic_string()});
            const auto result =
                ExecuteObservation(request.Compiler, unit.Directory, std::move(arguments), stage, report);
            if (!result)
            {
                return std::unexpected(result.error());
            }
            // Header-less C++ sources are valid.
            if (result->Stderr.starts_with(". ") || result->Stderr.contains("\n. "))
            {
                const auto headers = ParseClangHeaderTrace(result->Stderr, unit.Directory, report);
                if (!headers)
                {
                    return std::unexpected(headers.error());
                }
                return CheckCollectionInputPaths(request, *headers, report, CollectionOutputWait::Waiting());
            }
            return {};
        };
        if (const auto checked = discover(unit.Source); !checked)
        {
            return std::unexpected(checked.error());
        }
        for (const auto& input : knownInputs)
        {
            if (const auto checked = discover(input); !checked)
            {
                return std::unexpected(checked.error());
            }
        }
    }
    const auto directory = request.OutputDirectory / "work";
    std::error_code error;
    const auto state = std::filesystem::symlink_status(directory, error);
    if (error == std::errc::no_such_file_or_directory)
    {
        error.clear();
    }
    else if (error || !std::filesystem::is_directory(state) || std::filesystem::is_symlink(state))
    {
        return std::unexpected(
            report.Add(ToolError::kPathConflict, "observation_work_alias", directory.generic_string(), error));
    }
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        return std::unexpected(
            report.Add(ToolError::kOpenOutput, "observation_work", directory.generic_string(), error));
    }
    const auto path = directory / ".observation.lock";
    if (const auto checked = CheckObservationOutput(path, report); !checked)
    {
        return std::unexpected(checked.error());
    }
    auto owner = std::unique_ptr<ObservationWork>(new ObservationWork);
    owner->Request = &request;
    owner->Report = &report;
    owner->LogicalDirectory = directory;

#ifdef _WIN32
    auto* const handle = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return std::unexpected(report.Add(ToolError::kOpenOutput, "observation_lock", path.generic_string(),
                                          std::error_code(static_cast<int>(::GetLastError()), std::system_category())));
    }
    owner->Handle = handle;
    OVERLAPPED overlapped{};
    if (::LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlapped) == 0)
    {
        return std::unexpected(report.Add(ToolError::kOpenOutput, "observation_lock", path.generic_string(),
                                          std::error_code(static_cast<int>(::GetLastError()), std::system_category())));
    }
#else
    // POSIX open requires its mode argument through a variadic ABI for O_CREAT.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    owner->Descriptor = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (owner->Descriptor < 0)
    {
        return std::unexpected(report.Add(ToolError::kOpenOutput, "observation_lock", path.generic_string(),
                                          std::error_code(errno, std::generic_category())));
    }
    while (::flock(owner->Descriptor, LOCK_EX) != 0)
    {
        if (errno != EINTR)
        {
            return std::unexpected(report.Add(ToolError::kOpenOutput, "observation_lock", path.generic_string(),
                                              std::error_code(errno, std::generic_category())));
        }
    }
#endif
    // The atomic creation, rather than the name's entropy, establishes exclusive
    // ownership. Never reuse an existing directory, even after a killed process.
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    for (std::size_t attempt = 0; attempt < 64; ++attempt)
    {
        const auto candidate = directory / (".observation-" + std::to_string(nonce) + "-" + std::to_string(attempt));
        if (std::filesystem::create_directory(candidate, error))
        {
            owner->PrivateDirectory = candidate;
            return owner;
        }
        if (error && error != std::errc::file_exists)
        {
            return std::unexpected(
                report.Add(ToolError::kOpenOutput, "observation_private_create", candidate.generic_string(), error));
        }
        error.clear();
    }
    return std::unexpected(
        report.Add(ToolError::kOpenOutput, "observation_private_exhausted", directory.generic_string()));
}

Status ObservationWork::Cleanup()
{
    // No recursive deletion follows an alias: verify this exact direct child,
    // enumerate without following links, then remove children before parents.
    if (PrivateDirectory.empty())
    {
        return {};
    }
    std::error_code error;
    const auto expectedParent = std::filesystem::canonical(LogicalDirectory, error);
    const auto actual = std::filesystem::canonical(PrivateDirectory, error);
    if (error || actual.parent_path() != expectedParent || actual.filename() != PrivateDirectory.filename())
    {
        return std::unexpected(Report->Add(ToolError::kRemoveOutput, "observation_cleanup_boundary",
                                           PrivateDirectory.generic_string(), error));
    }
    const auto paths = GeneratedPaths(PrivateDirectory, *Report);
    if (!paths)
    {
        return std::unexpected(paths.error());
    }
    for (const auto& path : std::views::reverse(*paths))
    {
        if (!std::filesystem::remove(path, error) || error)
        {
            return std::unexpected(
                Report->Add(ToolError::kRemoveOutput, "observation_cleanup", path.generic_string(), error));
        }
    }
    PrivateDirectory.clear();
    return {};
}

Result<CompileObservation> ObservationWork::Finish(CompileObservation observation)
{
    auto inputs = observation.ObservedInputs;
    inputs.insert(inputs.end(), observation.Headers.begin(), observation.Headers.end());
    if (const auto checked = CheckCollectionInputPaths(*Request, inputs, *Report, CollectionOutputWait::Waiting());
        !checked)
    {
        return std::unexpected(checked.error());
    }
    const auto generated = GeneratedPaths(PrivateDirectory, *Report);
    if (!generated)
    {
        return std::unexpected(generated.error());
    }
    std::vector<GeneratedPath> paths;
    paths.reserve(generated->size());
    for (const auto& physical : *generated)
    {
        const auto relative = physical.lexically_relative(PrivateDirectory);
        const auto logical = relative == "." ? LogicalDirectory : LogicalDirectory / relative;
        paths.push_back({.Physical = physical.generic_string(), .Logical = logical.generic_string()});
    }
    if (const auto normalized = NormalizeFacts(observation.Facts, paths, PrivateDirectory.generic_string(), *Report);
        !normalized)
    {
        return std::unexpected(normalized.error());
    }
    if (const auto cleaned = Cleanup(); !cleaned)
    {
        Report->CleanupIncomplete = true;
        return std::unexpected(cleaned.error());
    }
    return observation;
}

Result<TranslationUnitObservation> ObserveTranslationUnit(const CollectionRequest& request, const CompileUnit& unit,
                                                          const std::filesystem::path& probe,
                                                          const std::filesystem::path& directory, std::size_t index,
                                                          std::string_view stage, FailureReport& report)
{
    const auto& work = directory;
    const auto source = work / ("unit-" + std::to_string(index) + ".cpp");
    const auto overlay = work / ("unit-" + std::to_string(index) + ".vfs.json");
    const auto original = ReadFile(unit.Source, "observation_source", report);
    if (!original)
    {
        return std::unexpected(original.error());
    }
    nlohmann::json mapping{
        {"version", 0},
        {"case-sensitive", "true"},
        {"roots", nlohmann::json::array()},
    };
    mapping.at("roots").push_back({
        {"type", "file"},
        {"name", unit.Source.generic_string()},
        {"external-contents", source.generic_string()},
        {"use-external-name", false},
    });
    if (!CheckObservationOutput(source, report) || !CheckObservationOutput(overlay, report) ||
        !WriteFile(source, *original + "\n\n#include \"" + probe.generic_string() + "\"\n", report) ||
        !WriteFile(overlay, mapping.dump(), report))
    {
        return std::unexpected(report.Add(ToolError::kWriteOutput, "observation_translation_unit"));
    }
    std::error_code timestampError;
    const auto timestamp = std::filesystem::last_write_time(unit.Source, timestampError);
    if (!timestampError)
    {
        std::filesystem::last_write_time(source, timestamp, timestampError);
    }
    if (timestampError)
    {
        return std::unexpected(report.Add(ToolError::kWriteOutput, "observation_source_timestamp",
                                          source.generic_string(), timestampError));
    }
    auto flags = unit.AbiArguments;
    flags.insert(flags.end(), {"-ivfsoverlay", overlay.generic_string()});
    auto macroFlags = flags;
    macroFlags.insert(macroFlags.end(),
                      {"-Werror=unknown-warning-option", "-E", "-dM", "-H", unit.Source.generic_string()});
    auto macros = ExecuteObservation(request.Compiler, unit.Directory, std::move(macroFlags), stage, report);
    if (!macros)
    {
        return std::unexpected(macros.error());
    }
    const auto headers = ParseClangHeaderTrace(macros->Stderr, unit.Directory, report);
    if (!headers)
    {
        return std::unexpected(headers.error());
    }
    if (const auto checked = CheckCollectionInputPaths(request, *headers, report, CollectionOutputWait::Waiting());
        !checked)
    {
        return std::unexpected(checked.error());
    }
    auto driverFlags = unit.AbiArguments;
    driverFlags.insert(driverFlags.end(), {"-###", "-fsyntax-only", unit.Source.generic_string()});
    auto driver = ExecuteObservation(request.Compiler, unit.Directory, std::move(driverFlags), stage, report);
    if (!driver)
    {
        return std::unexpected(driver.error());
    }
    flags.insert(flags.end(), {"-Werror=unknown-warning-option", "-fsyntax-only", unit.Source.generic_string()});
    if (const auto syntax = ExecuteObservation(request.Compiler, unit.Directory, std::move(flags), stage, report);
        !syntax)
    {
        return std::unexpected(syntax.error());
    }
    return TranslationUnitObservation{.Macros = std::move(*macros), .Driver = std::move(*driver)};
}
} // namespace pg::abi
