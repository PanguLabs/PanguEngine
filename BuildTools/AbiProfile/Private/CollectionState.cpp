#include "CollectionState.h"
#include "CollectionRequest.h"
#include "CompileCommands.h"
#include "DependencyGraph.h"
#include "Encoding.h"
#include "Error.h"
#include "JsonSchema.h"
#include "Process.h"
#include "Result.h"
#include "Sha256.h"
#include "ToolIo.h"
#include "ToolReport.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cwctype>
#include <expected>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h> // IWYU pragma: keep - native SDK prerequisites.
#include <errhandlingapi.h>
#include <libloaderapi.h>
#include <minwindef.h>
#include <processenv.h>
#include <stringapiset.h>
#include <winerror.h>
#include <winnls.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace pg::abi
{
namespace
{
constexpr std::array kOutputs{"complete.json", "profile.json", "canonical.json", "fingerprint.sha256", "evidence.json"};
// The CLI owns synchronous compiler forwarding; handlers only store the signal.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
volatile std::sig_atomic_t gInterruption = 0;
void Interrupt(int signal) { gInterruption = signal; }
class InvocationSignals
{
public:
    InvocationSignals()
        : PreviousInt(std::signal(SIGINT, Interrupt))
        , PreviousTerm(std::signal(SIGTERM, Interrupt))
    {
        gInterruption = 0;
    }
    ~InvocationSignals()
    {
        (void)std::signal(SIGINT, PreviousInt);
        (void)std::signal(SIGTERM, PreviousTerm);
    }
    InvocationSignals(const InvocationSignals&) = delete;
    InvocationSignals& operator=(const InvocationSignals&) = delete;
    InvocationSignals(InvocationSignals&&) = delete;
    InvocationSignals& operator=(InvocationSignals&&) = delete;

private:
    using Handler = void (*)(int);
    Handler PreviousInt;
    Handler PreviousTerm;
};
constexpr std::string_view kStateFormat = "pangu-collection-state-1";

std::string PathText(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}
std::filesystem::path Path(const std::string& text) { return {std::u8string(text.begin(), text.end())}; }
Error Fail(FailureReport& report, std::string_view stage, const std::filesystem::path& path = {},
           std::error_code error = {})
{
    return report.Add(ToolError::kInvalidRequest, stage, PathText(path), error);
}
std::string StringField(const nlohmann::json& value, const char* key)
{
    const auto* field = Find(value, key);
    return field != nullptr && field->is_string() ? field->get<std::string>() : "";
}
Result<std::filesystem::path> ToolPath(FailureReport& report)
{
#ifdef _WIN32
    std::array<wchar_t, 32768> buffer{};
    const auto count = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (count == 0 || count == buffer.size())
    {
        return std::unexpected(Fail(report, "tool_identity"));
    }
    return std::filesystem::path(std::wstring(buffer.data(), count));
#elifdef __APPLE__
    std::array<char, 4096> buffer{};
    auto size = static_cast<std::uint32_t>(buffer.size());
    if (::_NSGetExecutablePath(buffer.data(), &size) != 0)
    {
        return std::unexpected(Fail(report, "tool_identity"));
    }
    return std::filesystem::path(buffer.data());
#else
    std::error_code error;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? Result<std::filesystem::path>{std::unexpected(Fail(report, "tool_identity", {}, error))}
                 : Result<std::filesystem::path>{path};
#endif
}
Result<std::string> HashFile(const std::filesystem::path& path, FailureReport& report)
{
    const auto contents = ReadFile(path, "hash_input", report);
    return contents ? Result<std::string>{Sha256Hex(*contents)} : std::unexpected(contents.error());
}
Result<nlohmann::json> ReadJson(const std::filesystem::path& path, FailureReport& report)
{
    const auto contents = ReadFile(path, "read_state", report);
    if (!contents)
    {
        return std::unexpected(contents.error());
    }
    const auto value = ParseUnique(*contents);
    if (!value || !value->is_object())
    {
        return std::unexpected(Fail(report, "invalid_state", path));
    }
    return *value;
}
bool Exists(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::exists(path, error) || error;
}
bool Within(const std::filesystem::path& parent, const std::filesystem::path& child, bool includeParent = false)
{
    auto part = child.begin();
    for (const auto& item : parent)
    {
        if (part == child.end())
        {
            return false;
        }
#ifdef _WIN32
        auto a = item.wstring();
        auto b = part->wstring();
        std::ranges::transform(a, a.begin(), [](wchar_t value) { return std::towlower(value); });
        std::ranges::transform(b, b.begin(), [](wchar_t value) { return std::towlower(value); });
        if (a != b)
        {
            return false;
        }
#else
        if (item != *part)
        {
            return false;
        }
#endif
        ++part;
    }
    return includeParent || part != child.end();
}
Status SafeOutput(const CollectionRequest& request, const std::filesystem::path& path, FailureReport& report)
{
    const auto absolute = path.lexically_normal();
    if (!absolute.is_absolute() || !Within(request.BuildRoot.lexically_normal(), absolute))
    {
        return std::unexpected(Fail(report, "output_boundary", path));
    }
    // Reject links at every generated path component, including absent leaf parents.
    for (auto current = absolute; current != request.BuildRoot && !current.empty(); current = current.parent_path())
    {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory)
        {
            error.clear();
        }
        if (error || std::filesystem::is_symlink(status))
        {
            return std::unexpected(Fail(report, "output_symlink", current, error));
        }
        if (current == current.parent_path())
        {
            break;
        }
    }
    std::error_code error;
    if (std::filesystem::is_regular_file(absolute, error) && std::filesystem::hard_link_count(absolute, error) > 1)
    {
        return std::unexpected(Fail(report, "output_hardlink", path));
    }
    if (error && error != std::errc::no_such_file_or_directory)
    {
        return std::unexpected(Fail(report, "output_status", path, error));
    }
    return {};
}
Status CheckOutputs(const CollectionRequest& request, FailureReport& report)
{
    const auto outputs = CollectionOutputFiles(request);
    for (const auto& parent : CollectionOutputDirectories(request))
    {
        std::error_code error;
        const auto signature = std::filesystem::weakly_canonical(request.SignatureHeader, error);
        const auto directory = std::filesystem::weakly_canonical(parent, error);
        const auto same = IsSameFile(signature, directory, report);
        if (error || !same || *same || Within(directory, signature))
        {
            return std::unexpected(Fail(report, "reserved_signature_path", request.SignatureHeader));
        }
    }
    std::vector<std::filesystem::path> inputs{
        request.RequestFile, request.Compiler,   request.Database,  request.ObjectManifest,
        request.Library,     request.SchemaFile, request.LinkGraph,
    };
    const auto units = LoadCompileUnits(request, report);
    if (!units)
    {
        return std::unexpected(units.error());
    }
    for (const auto& unit : *units)
    {
        inputs.push_back(unit.Source);
        inputs.push_back(unit.Object);
        inputs.insert(inputs.end(), unit.ResponseFiles.begin(), unit.ResponseFiles.end());
        const auto receiptPath = CompileReceiptPath(request, unit);
        if (Exists(receiptPath))
        {
            const auto receipt = ReadJson(receiptPath, report);
            if (!receipt)
            {
                return std::unexpected(receipt.error());
            }
            const auto* snapshot = Find(*receipt, "snapshot");
            const auto* files = snapshot == nullptr ? nullptr : Find(*snapshot, "files");
            if (files != nullptr && files->is_array())
            {
                for (const auto& file : *files)
                {
                    const auto path = StringField(file, "path");
                    if (!path.empty())
                    {
                        inputs.push_back(Path(path));
                    }
                }
            }
        }
    }
    if (const auto checked = CheckCollectionInputPaths(request, inputs, report); !checked)
    {
        return checked;
    }
    if (Exists(request.SignatureHeader))
    {
        const auto generation = ReadFile(request.SignatureHeader, "validate_signature_generation", report);
        if (!generation || !generation->starts_with("// Pangu compile generation: "))
        {
            return std::unexpected(Fail(report, "signature_aliases_input", request.SignatureHeader));
        }
    }
    for (const auto& output : outputs)
    {
        if (const auto safe = SafeOutput(request, output, report); !safe)
        {
            return safe;
        }
        std::error_code outputError;
        const bool directory = std::filesystem::is_directory(output, outputError);
        if (outputError == std::errc::no_such_file_or_directory)
        {
            outputError.clear();
        }
        if (directory || outputError)
        {
            return std::unexpected(Fail(report, "output_is_directory", output, outputError));
        }
        for (const auto& other : outputs)
        {
            if (&output != &other && Within(output.lexically_normal(), other.lexically_normal(), true))
            {
                return std::unexpected(Fail(report, "output_ancestor", output));
            }
        }
    }
    return {};
}
Status MakeDirectory(const std::filesystem::path& path, FailureReport& report)
{
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return error ? Status{std::unexpected(Fail(report, "create_state_directory", path, error))} : Status{};
}
Status Rename(const std::filesystem::path& from, const std::filesystem::path& to, FailureReport& report)
{
    std::error_code error;
    std::filesystem::rename(from, to, error);
    return error ? Status{std::unexpected(Fail(report, "publish_state", to, error))} : Status{};
}
class Transaction
{
public:
    Transaction(const CollectionRequest& request, FailureReport& report, const CollectionIo& io = {})
        : Request(request)
        , Report(report)
        , Io(io)
        , Directory(request.OutputDirectory / ".collection-lock")
    {
    }
    ~Transaction()
    {
        if (Locked)
        {
            Report.CleanupIncomplete |= !Close();
        }
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = delete;
    Transaction& operator=(Transaction&&) = delete;
    Status Begin(bool wait = false)
    {
        if (const auto safe = CheckOutputs(Request, Report); !safe)
        {
            return safe;
        }
        if (const auto safe = SafeOutput(Request, Directory, Report); !safe)
        {
            return safe;
        }
        if (const auto made = MakeDirectory(Request.OutputDirectory, Report); !made)
        {
            return made;
        }
        std::error_code error;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (true)
        {
            Locked = std::filesystem::create_directory(Directory, error);
            // Some filesystems report an already-created directory as
            // file_exists during competing short compiler transitions.
            // Preserve real I/O errors and never treat a non-directory as a lock.
            if (!Locked && wait && error == std::errc::file_exists)
            {
                std::error_code kindError;
                if (std::filesystem::is_directory(Directory, kindError) && !kindError)
                {
                    error.clear();
                }
            }
            if (Locked || error || !wait || std::chrono::steady_clock::now() >= deadline)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!Locked)
        {
            // A competing transaction cannot silently leave a winner's complete record.
            if (!error && !wait)
            {
                Report.CleanupIncomplete |= !WriteFile(Directory / "conflict", "concurrent writer", Report);
            }
            return std::unexpected(Fail(Report, "collection_busy", Directory, error));
        }
        return {};
    }
    Status Revoke()
    {
        // A retained tombstone prevents an unreadable/unremovable old complete from
        // being accepted. Never follow a generated link to remove or write inputs.
        if (const auto safe = SafeOutput(Request, Request.OutputDirectory / ".invalid", Report); !safe)
        {
            return safe;
        }
        if (const auto marked = Io.Write(Request.OutputDirectory / ".invalid", "incomplete\n", Report); !marked)
        {
            Report.CleanupIncomplete |= !RemoveGeneratedFile(Request.OutputDirectory / "complete.json", Report);
            return marked;
        }
        const auto removed = Io.Remove(Request.OutputDirectory / "complete.json", Report);
        Report.CleanupIncomplete |= !removed;
        return removed;
    }
    Status Write(std::string_view name, std::string_view contents)
    {
        const auto path = Directory / name;
        Temporaries.push_back(path);
        return Io.Write(path, contents, Report);
    }
    Status Publish(std::string_view name, const std::filesystem::path& destination)
    {
        if (const auto safe = SafeOutput(Request, destination, Report); !safe)
        {
            return safe;
        }
        if (Exists(Directory / "conflict"))
        {
            return std::unexpected(Fail(Report, "collection_conflict", Directory));
        }
        if (const auto removed = Io.Remove(destination, Report); !removed)
        {
            return removed;
        }
        return Io.Publish != nullptr ? Io.Publish(Directory / name, destination, Report)
                                     : Rename(Directory / name, destination, Report);
    }
    Status Complete()
    {
        if (Exists(Directory / "conflict"))
        {
            return std::unexpected(Fail(Report, "collection_conflict", Directory));
        }
        if (const auto removed = Io.Remove(Request.OutputDirectory / ".invalid", Report); !removed)
        {
            return removed;
        }
        return Publish("complete.json", Request.OutputDirectory / "complete.json");
    }
    Status Close()
    {
        std::optional<Error> failure;
        for (const auto& path : Temporaries)
        {
            const auto removed = Io.Remove(path, Report);
            if (!removed)
            {
                failure = removed.error();
            }
        }
        if (Exists(Directory / "conflict"))
        {
            failure = Fail(Report, "collection_conflict", Directory);
            (void)Io.Remove(Directory / "conflict", Report);
        }
        if (const auto removed = Io.Remove(Directory, Report); !removed)
        {
            failure = removed.error();
        }
        Locked = false;
        if (failure)
        {
            Report.CleanupIncomplete = true;
            Report.CleanupIncomplete |= !WriteFile(Request.OutputDirectory / ".invalid", "cleanup failed\n", Report);
            Report.CleanupIncomplete |= !RemoveGeneratedFile(Request.OutputDirectory / "complete.json", Report);
            return std::unexpected(*failure);
        }
        return {};
    }

private:
    const CollectionRequest& Request;
    FailureReport& Report;
    CollectionIo Io;
    std::filesystem::path Directory;
    std::vector<std::filesystem::path> Temporaries;
    bool Locked = false;
};

nlohmann::json SnapshotJson(const InputSnapshot& snapshot)
{
    return {
        {"files", snapshot.Files},
        {"context", snapshot.Context},
        {"environment", snapshot.Environment},
        {"compile_digest", snapshot.CompileDigest},
        {"dependency_digest", snapshot.DependencyDigest},
        {"digest", snapshot.Digest},
    };
}
Result<InputSnapshot> DecodeSnapshot(const nlohmann::json& value, FailureReport& report)
{
    for (const auto* key : {"files", "context", "environment", "compile_digest", "dependency_digest", "digest"})
    {
        if (!value.is_object() || !value.contains(key))
        {
            return std::unexpected(Fail(report, "invalid_snapshot"));
        }
    }
    if (!value.at("files").is_array() || !value.at("context").is_object() || !value.at("environment").is_object() ||
        !value.at("compile_digest").is_string() || !value.at("dependency_digest").is_string() ||
        !value.at("digest").is_string())
    {
        return std::unexpected(Fail(report, "invalid_snapshot"));
    }
    return InputSnapshot{
        .Files = value.at("files"),
        .Context = value.at("context"),
        .Environment = value.at("environment"),
        .CompileDigest = value.at("compile_digest").get<std::string>(),
        .DependencyDigest = value.at("dependency_digest").get<std::string>(),
        .Digest = value.at("digest").get<std::string>(),
    };
}
void Digest(InputSnapshot& snapshot)
{
    auto own = nlohmann::json::array();
    auto dependencies = nlohmann::json::array();
    auto validation = nlohmann::json::array();
    for (const auto& file : snapshot.Files)
    {
        const auto role = file.at("role").get<std::string>();
        if (role == "dependency")
        {
            dependencies.push_back(file);
        }
        else if (role == "validation" || role == "collection")
        {
            validation.push_back(file);
        }
        else
        {
            own.push_back(file);
        }
    }
    snapshot.CompileDigest = Sha256Hex(
        nlohmann::json{{"context", snapshot.Context}, {"environment", snapshot.Environment}, {"files", own}}.dump());
    snapshot.DependencyDigest = Sha256Hex(dependencies.dump());
    snapshot.Digest =
        Sha256Hex(nlohmann::json{snapshot.CompileDigest, snapshot.DependencyDigest, validation, kStateFormat}.dump());
}
Status Environment(const CollectionRequest& request, FailureReport& report)
{
    for (const auto& [name, expected] : request.EnvironmentBaseline)
    {
        // Read-only process environment; compilation inherits these same values.
        std::optional<std::string> actual;
#ifdef _WIN32
        const auto variable = Path(name).native();
        ::SetLastError(ERROR_SUCCESS);
        const auto size = ::GetEnvironmentVariableW(variable.c_str(), nullptr, 0);
        if (size == 0)
        {
            const auto error = ::GetLastError();
            if (error == ERROR_SUCCESS)
            {
                actual = "";
            }
            else if (error != ERROR_ENVVAR_NOT_FOUND)
            {
                return std::unexpected(Fail(report, "read_environment", name));
            }
        }
        else
        {
            std::vector<wchar_t> buffer(size);
            const auto read = ::GetEnvironmentVariableW(variable.c_str(), buffer.data(), size);
            if (read >= size || (read == 0 && size != 1) || read > std::numeric_limits<int>::max())
            {
                return std::unexpected(Fail(report, "read_environment", name));
            }
            if (read == 0)
            {
                actual = "";
            }
            else
            {
                const auto length = static_cast<int>(read);
                const auto count = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(), length, nullptr,
                                                         0, nullptr, nullptr);
                if (count == 0)
                {
                    return std::unexpected(Fail(report, "read_environment_encoding", name));
                }
                std::string text(static_cast<std::size_t>(count), '\0');
                if (::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(), length, text.data(), count,
                                          nullptr, nullptr) != count)
                {
                    return std::unexpected(Fail(report, "read_environment_encoding", name));
                }
                actual = std::move(text);
            }
        }
#else
        const auto* value = std::getenv(name.c_str());
        if (value != nullptr)
        {
            actual = value;
        }
#endif
        if (actual != expected)
        {
            return std::unexpected(Fail(report, "environment_requires_reconfigure", name));
        }
    }
    return {};
}
Status AddFile(InputSnapshot& snapshot, const std::filesystem::path& path, std::string_view role, FailureReport& report)
{
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error)
    {
        return std::unexpected(Fail(report, "snapshot_file", path, error));
    }
    const auto hash = HashFile(canonical, report);
    if (!hash)
    {
        return std::unexpected(hash.error());
    }
    const nlohmann::json entry{
        {"path", PathText(path)},
        {"identity", PathText(canonical)},
        {"role", role},
        {"sha256", *hash},
    };
    if (std::ranges::find(snapshot.Files, entry) == snapshot.Files.end())
    {
        snapshot.Files.push_back(entry);
    }
    return {};
}
nlohmann::json UnitJson(const CompileUnit& unit)
{
    return {
        {"compiler", PathText(unit.Compiler)},
        {"directory", PathText(unit.Directory)},
        {"source", PathText(unit.Source)},
        {"object", PathText(unit.Object)},
        {"arguments", unit.Arguments},
        {"original_arguments", unit.OriginalArguments},
        {"driver_name", unit.DriverName},
        {"driver_mode", unit.DriverMode},
        {"response_style", static_cast<unsigned>(unit.ResponseStyle)},
    };
}
Result<CompileObservation> ObservationFromSnapshot(const InputSnapshot& snapshot, FailureReport& report)
{
    CompileObservation observation;
    const auto* facts = Find(snapshot.Context, "facts");
    if (facts == nullptr)
    {
        return std::unexpected(Fail(report, "snapshot_facts"));
    }
    observation.Facts = *facts;
    for (const auto& file : snapshot.Files)
    {
        const auto* path = Find(file, "path");
        const auto* role = Find(file, "role");
        if (path == nullptr || !path->is_string() || role == nullptr || !role->is_string())
        {
            return std::unexpected(Fail(report, "snapshot_file_record"));
        }
        if (*role == "header")
        {
            observation.Headers.push_back(Path(path->get<std::string>()));
        }
        if (*role == "observed")
        {
            observation.ObservedInputs.push_back(Path(path->get<std::string>()));
        }
    }
    return observation;
}
Result<InputSnapshot> Refresh(const CollectionRequest& request, const InputSnapshot& previous, FailureReport& report)
{
    const auto units = LoadCompileUnits(request, report);
    if (!units)
    {
        return std::unexpected(units.error());
    }
    const auto observation = ObservationFromSnapshot(previous, report);
    if (!observation)
    {
        return std::unexpected(observation.error());
    }
    auto refreshed = SnapshotInputs(request, *units, *observation, report);
    if (!refreshed)
    {
        return refreshed;
    }
    for (const auto& file : previous.Files)
    {
        if (StringField(file, "role") == "collection")
        {
            if (const auto added = AddFile(*refreshed, Path(StringField(file, "path")), "collection", report); !added)
            {
                return std::unexpected(added.error());
            }
        }
    }
    Digest(*refreshed);
    return refreshed;
}
Status Generation(const CollectionRequest& request, std::string_view context, FailureReport& report)
{
    const auto text = std::string("// Pangu compile generation: ") + std::string(context) + "\n";
    if (Exists(request.SignatureHeader))
    {
        const auto old = ReadFile(request.SignatureHeader, "read_generation", report);
        if (!old)
        {
            return std::unexpected(old.error());
        }
        if (*old == text)
        {
            return {};
        }
    }
    if (const auto made = MakeDirectory(request.SignatureHeader.parent_path(), report); !made)
    {
        return made;
    }
    return WriteFile(request.SignatureHeader, text, report);
}
Status VerifyResponseFiles(const nlohmann::json& files, FailureReport& report)
{
    if (!files.is_array())
    {
        return std::unexpected(Fail(report, "receipt_response_files"));
    }
    for (const auto& file : files)
    {
        const auto* path = Find(file, "path");
        const auto* identity = Find(file, "identity");
        const auto* hash = Find(file, "sha256");
        if (path == nullptr || !path->is_string() || identity == nullptr || !identity->is_string() || hash == nullptr ||
            !hash->is_string())
        {
            return std::unexpected(Fail(report, "receipt_response_files"));
        }
        std::error_code error;
        const auto actual = std::filesystem::canonical(Path(path->get<std::string>()), error);
        const auto digest = HashFile(Path(path->get<std::string>()), report);
        if (error || !digest || PathText(actual) != identity->get<std::string>() || *digest != hash->get<std::string>())
        {
            return std::unexpected(Fail(report, "receipt_response_changed"));
        }
    }
    return {};
}
Result<nlohmann::json> Receipts(const CollectionRequest& request, const InputSnapshot& snapshot, FailureReport& report)
{
    const auto units = LoadCompileUnits(request, report);
    if (!units)
    {
        return std::unexpected(units.error());
    }
    auto records = nlohmann::json::array();
    for (const auto& unit : *units)
    {
        const auto path = CompileReceiptPath(request, unit);
        const auto record = ReadJson(path, report);
        if (!record)
        {
            return std::unexpected(Fail(report, "missing_compile_proof", path));
        }
        const auto hash = HashFile(unit.Object, report);
        if (!hash)
        {
            return std::unexpected(hash.error());
        }
        if (StringField(*record, "format") != kStateFormat ||
            StringField(*record, "compile_digest") != snapshot.CompileDigest ||
            StringField(*record, "object") != PathText(unit.Object) || StringField(*record, "object_sha256") != *hash ||
            !record->contains("invocation") || !record->contains("response_files"))
        {
            return std::unexpected(Fail(report, "untrusted_object_rebuild_required", unit.Object));
        }
        if (const auto checked = VerifyResponseFiles(record->at("response_files"), report); !checked)
        {
            return std::unexpected(checked.error());
        }
        const auto receiptHash = HashFile(path, report);
        if (!receiptHash)
        {
            return std::unexpected(receiptHash.error());
        }
        records.push_back({
            {"path", PathText(path)},
            {"sha256", *receiptHash},
            {"object", PathText(unit.Object)},
            {"object_sha256", *hash},
        });
    }
    return records;
}

std::optional<std::size_t> ArchiveNumber(std::string_view text)
{
    const auto last = text.find_last_not_of(' ');
    if (last == std::string_view::npos)
    {
        return std::nullopt;
    }
    text = text.substr(0, last + 1);
    std::size_t value = 0;
    const auto* end = std::to_address(text.end());
    const auto parsed = std::from_chars(std::to_address(text.begin()), end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end ? std::optional{value} : std::nullopt;
}

bool ArchiveIndex(std::string_view name)
{
    return name == "/" || name == "/SYM64/" || name == "__.SYMDEF" || name == "__.SYMDEF SORTED" ||
           name == "__.SYMDEF_64" || name == "__.SYMDEF_64 SORTED";
}

// Ordinary GNU/COFF archives and BSD/Darwin extended names share the same
// framing. Match a multiset of complete object bytes, never member names: two
// distinct compile directories may legitimately contribute the same basename.
Result<std::string> VerifyArchive(const CollectionRequest& request, const nlohmann::json& receipts,
                                  FailureReport& report)
{
    const auto archive = ReadFile(request.Library, "read_archive_members", report);
    if (!archive)
    {
        return std::unexpected(archive.error());
    }
    const std::string_view bytes(*archive);
    if (!bytes.starts_with("!<arch>\n"))
    {
        // Thin archives and unknown formats require a separate verified contract.
        return std::unexpected(Fail(report, "unsupported_archive_format", request.Library));
    }
    std::vector<std::string> objects;
    for (const auto& receipt : receipts)
    {
        const auto object = ReadFile(Path(StringField(receipt, "object")), "read_archive_object", report);
        if (!object)
        {
            return std::unexpected(object.error());
        }
        // Bind this read to the verified receipt, including concurrent mutations.
        if (Sha256Hex(*object) != StringField(receipt, "object_sha256"))
        {
            return std::unexpected(Fail(report, "archive_object_changed", request.Library));
        }
        objects.push_back(*object);
    }
    std::string_view longNames;
    std::size_t offset = 8;
    while (offset < bytes.size())
    {
        constexpr std::size_t kHeaderSize = 60;
        if (bytes.size() - offset < kHeaderSize)
        {
            return std::unexpected(Fail(report, "invalid_archive_header", request.Library));
        }
        const auto header = bytes.substr(offset, kHeaderSize);
        const auto size = ArchiveNumber(header.substr(48, 10));
        if (header.substr(58) != "`\n" || !size || *size > bytes.size() - offset - kHeaderSize)
        {
            return std::unexpected(Fail(report, "invalid_archive_member_size", request.Library));
        }
        auto name = header.substr(0, 16);
        name = name.substr(0, name.find_last_not_of(' ') + 1);
        auto contents = bytes.substr(offset + kHeaderSize, *size);
        offset += kHeaderSize + *size;
        if ((offset & 1U) != 0)
        {
            if (offset == bytes.size() || bytes.substr(offset, 1) != "\n")
            {
                return std::unexpected(Fail(report, "invalid_archive_padding", request.Library));
            }
            ++offset;
        }
        if (name == "//")
        {
            if (!longNames.empty())
            {
                return std::unexpected(Fail(report, "duplicate_archive_names", request.Library));
            }
            longNames = contents;
            continue;
        }
        const bool extendedName = name.starts_with("#1/");
        if (extendedName)
        {
            const auto length = ArchiveNumber(name.substr(3));
            if (!length || *length == 0 || *length > contents.size())
            {
                return std::unexpected(Fail(report, "invalid_archive_extended_name", request.Library));
            }
            name = contents.substr(0, *length);
            contents.remove_prefix(*length);
            const auto terminator = name.find('\0');
            if (terminator != std::string_view::npos)
            {
                if (name.find_first_not_of('\0', terminator) != std::string_view::npos)
                {
                    return std::unexpected(Fail(report, "invalid_archive_extended_name", request.Library));
                }
                name = name.substr(0, terminator);
            }
        }
        else if (name.starts_with('/') && !ArchiveIndex(name))
        {
            const auto index = ArchiveNumber(name.substr(1));
            if (!index || *index >= longNames.size() ||
                (*index != 0 && longNames.substr(*index - 1, 1) != "\n" &&
                 longNames.substr(*index - 1, 1) != std::string_view("\0", 1)))
            {
                return std::unexpected(Fail(report, "invalid_archive_long_name", request.Library));
            }
            name = longNames.substr(*index);
            const auto end = name.find_first_of(std::string_view("\n\0", 2));
            if (end == std::string_view::npos)
            {
                return std::unexpected(Fail(report, "invalid_archive_long_name", request.Library));
            }
            name = name.substr(0, end);
        }
        if (name.ends_with('/') && name != "/" && name != "/SYM64/")
        {
            name.remove_suffix(1);
        }
        if (ArchiveIndex(name))
        {
            continue;
        }
        if (name.empty())
        {
            return std::unexpected(Fail(report, "invalid_archive_member_name", request.Library));
        }
        const auto matched = std::ranges::find_if(objects,
                                                  [&](const std::string& object)
                                                  {
                                                      if (contents == object)
                                                      {
                                                          return true;
                                                      }
                                                      // LLVM's Darwin writer includes newline padding in the member's
                                                      // recorded size to align objects to eight bytes. BSD extended
                                                      // names delimit the start; only exact prescribed framing padding
                                                      // may follow the complete current object bytes.
                                                      const auto padding = (8 - (object.size() % 8)) % 8;
                                                      return extendedName && offset % 8 == 0 &&
                                                             contents.size() == object.size() + padding &&
                                                             contents.starts_with(object) &&
                                                             contents.substr(object.size()).find_first_not_of('\n') ==
                                                                 std::string_view::npos;
                                                  });
        if (matched == objects.end())
        {
            return std::unexpected(Fail(report, "archive_member_not_current_object", request.Library));
        }
        objects.erase(matched);
    }
    return objects.empty()
               ? Result<std::string>{Sha256Hex(*archive)}
               : Result<std::string>{std::unexpected(Fail(report, "archive_missing_object", request.Library))};
}
} // namespace

std::filesystem::path CompileReceiptPath(const CollectionRequest& request, const CompileUnit& unit)
{
    return request.OutputDirectory / "receipts" / (Sha256Hex(PathText(unit.Object)) + ".json");
}

Result<InputSnapshot> SnapshotInputs(const CollectionRequest& request, std::span<const CompileUnit> units,
                                     const CompileObservation& observation, FailureReport& report)
{
    if (const auto checked = Environment(request, report); !checked)
    {
        return std::unexpected(checked.error());
    }
    InputSnapshot snapshot;
    snapshot.Environment = nlohmann::json::object();
    for (const auto& [name, value] : request.EnvironmentBaseline)
    {
        snapshot.Environment.emplace(name, value ? nlohmann::json(*value) : nlohmann::json(nullptr));
    }
    auto commands = nlohmann::json::array();
    for (const auto& unit : units)
    {
        commands.push_back(UnitJson(unit));
        for (const auto& path : {unit.Source, unit.Compiler})
        {
            if (const auto added = AddFile(snapshot, path, "compile", report); !added)
            {
                return std::unexpected(added.error());
            }
        }
        for (const auto& path : unit.ResponseFiles)
        {
            if (const auto added = AddFile(snapshot, path, "response", report); !added)
            {
                return std::unexpected(added.error());
            }
        }
    }
    snapshot.Context = {
        {"commands", commands},
        {"facts", observation.Facts},
        {"platform_context", request.PlatformContext},
        {"host", static_cast<unsigned>(request.HostPlatform)},
        {"target", static_cast<unsigned>(request.TargetPlatform)},
        {"target_name", request.TargetName},
        {"configuration", request.Configuration},
    };
    for (const auto& [paths, role] :
         {std::pair{&observation.Headers, "header"}, std::pair{&observation.ObservedInputs, "observed"}})
    {
        for (const auto& path : *paths)
        {
            const auto signature = IsSameFile(path, request.SignatureHeader, report);
            if (!signature)
            {
                return std::unexpected(signature.error());
            }
            if (*signature)
            {
                continue;
            }
            if (Within(request.OutputDirectory, path))
            {
                return std::unexpected(Fail(report, "snapshot_self_reference", path));
            }
            for (const auto& unit : units)
            {
                const auto same = IsSameFile(path, unit.Object, report);
                if (!same || *same)
                {
                    return std::unexpected(Fail(report, "snapshot_self_reference", path));
                }
            }
            const auto same = IsSameFile(path, request.Library, report);
            if (!same || *same)
            {
                return std::unexpected(Fail(report, "snapshot_self_reference", path));
            }
            if (const auto added = AddFile(snapshot, path, role, report); !added)
            {
                return std::unexpected(added.error());
            }
        }
    }
    if (const auto added = AddFile(snapshot, request.LinkGraph, "dependency", report); !added)
    {
        return std::unexpected(added.error());
    }
    // Dependency completions are NOT TU compile inputs: a downstream archive
    // changing must invalidate the dependency digest without falsely claiming
    // that this Target's own objects were recompiled.
    //
    // Cold compile / low-level state fixtures may not yet have a valid graph;
    // the authoritative Collect() gate rejects such graphs before publication.
    FailureReport graphReport;
    const auto graph = ReadDependencyGraph(request, graphReport);
    if (graph)
    {
        const auto plan = PlanDependencies(*graph, request.TargetName, graphReport);
        if (plan)
        {
            for (const auto& name : plan->Targets)
            {
                const auto& target = graph->Targets.at(name);
                if (name == request.TargetName ||
                    (target.Kind != DependencyKind::kStatic && target.Kind != DependencyKind::kModule) ||
                    target.RequestFile.empty() || !Exists(target.RequestFile))
                {
                    continue;
                }
                if (const auto added = AddFile(snapshot, target.RequestFile, "dependency", report); !added)
                {
                    return std::unexpected(added.error());
                }
                const auto dependency = ReadCollectionRequest(target.RequestFile, graphReport);
                if (!dependency)
                {
                    continue; // Collect() will reject invalid request identities.
                }
                bool sameGroup = false;
                for (const auto& cycle : plan->TechnicalCycles)
                {
                    sameGroup |= std::ranges::find(cycle, request.TargetName) != cycle.end() &&
                                 std::ranges::find(cycle, name) != cycle.end();
                }
                if (sameGroup)
                {
                    if (Exists(dependency->Library))
                    {
                        if (const auto added = AddFile(snapshot, dependency->Library, "dependency", report); !added)
                        {
                            return std::unexpected(added.error());
                        }
                    }
                    continue; // The last-published seal binds peer completions without cyclic hashes.
                }
                for (const auto& path : {
                         dependency->Library,
                         dependency->OutputDirectory / "complete.json",
                         dependency->OutputDirectory / "group-complete.json",
                         dependency->OutputDirectory / "profile.json",
                         dependency->OutputDirectory / "canonical.json",
                         dependency->OutputDirectory / "evidence.json",
                         dependency->OutputDirectory / "fingerprint.sha256",
                     })
                {
                    if (Exists(path))
                    {
                        if (const auto added = AddFile(snapshot, path, "dependency", report); !added)
                        {
                            return std::unexpected(added.error());
                        }
                    }
                }
            }
        }
    }
    if (const auto added = AddFile(snapshot, request.SchemaFile, "validation", report); !added)
    {
        return std::unexpected(added.error());
    }
    const auto tool = ToolPath(report);
    if (!tool)
    {
        return std::unexpected(tool.error());
    }
    if (const auto added = AddFile(snapshot, *tool, "validation", report); !added)
    {
        return std::unexpected(added.error());
    }
    Digest(snapshot);
    return snapshot;
}

namespace
{
Result<GuardDisposition> GuardOne(const CollectionRequest& request, FailureReport& report, Transaction& transaction)
{
    const auto completePath = request.OutputDirectory / "complete.json";
    if (!Exists(completePath))
    {
        if (const auto env = Environment(request, report); !env)
        {
            return std::unexpected(env.error());
        }
        const auto units = LoadCompileUnits(request, report);
        if (!units)
        {
            return std::unexpected(units.error());
        }
        bool canRecollect = Exists(request.Library);
        for (const auto& unit : *units)
        {
            canRecollect &= Exists(unit.Object);
            const auto receiptPath = CompileReceiptPath(request, unit);
            if (!Exists(receiptPath))
            {
                if (Exists(unit.Object))
                {
                    return std::unexpected(Fail(report, "missing_compile_proof", unit.Object));
                }
                canRecollect = false;
                continue;
            }
            const auto receipt = ReadJson(receiptPath, report);
            if (!receipt || !receipt->contains("snapshot"))
            {
                return std::unexpected(Fail(report, "invalid_compile_proof", receiptPath));
            }
            const auto previous = DecodeSnapshot(receipt->at("snapshot"), report);
            if (!previous)
            {
                return std::unexpected(previous.error());
            }
            const auto refreshed = Refresh(request, *previous, report);
            if (!refreshed)
            {
                return std::unexpected(refreshed.error());
            }
            if (refreshed->CompileDigest != previous->CompileDigest)
            {
                canRecollect = false;
                if (const auto generated = Generation(request, refreshed->CompileDigest, report); !generated)
                {
                    return std::unexpected(generated.error());
                }
            }
        }
        // A stable initial generation suffices until known compile inputs change.
        if (!Exists(request.SignatureHeader))
        {
            if (const auto generated = Generation(request, "initial", report); !generated)
            {
                return std::unexpected(generated.error());
            }
        }
        // Missing completion alone does not make an unchanged archive dirty in
        // Ninja. Recollect instead of returning success without POST_BUILD;
        // Collect still verifies receipts, objects, members and dependencies.
        return canRecollect ? GuardDisposition::kRecollectRequired : GuardDisposition::kBuildRequired;
    }
    const auto complete = ReadJson(completePath, report);
    // Revoke on any inability to prove the current generation, even malformed data.
    const auto reject = [&](std::string_view stage) -> Result<GuardDisposition>
    {
        report.CleanupIncomplete |= !transaction.Revoke();
        return std::unexpected(Fail(report, stage, completePath));
    };
    if (!complete || StringField(*complete, "library") != PathText(request.Library) ||
        Exists(request.OutputDirectory / ".invalid") || StringField(*complete, "format") != kStateFormat ||
        !complete->contains("snapshot"))
    {
        return reject("invalid_completion_rebuild_required");
    }
    const auto old = DecodeSnapshot(complete->at("snapshot"), report);
    if (!old)
    {
        return reject("invalid_completion_snapshot");
    }
    const auto current = Refresh(request, *old, report);
    if (!current)
    {
        return reject("inputs_require_reconfigure");
    }
    if (current->CompileDigest != old->CompileDigest)
    {
        if (const auto revoked = transaction.Revoke(); !revoked)
        {
            return std::unexpected(revoked.error());
        }
        if (const auto generated = Generation(request, current->CompileDigest, report); !generated)
        {
            return std::unexpected(generated.error());
        }
        return GuardDisposition::kBuildRequired;
    }
    const auto receipts = Receipts(request, *current, report);
    if (!receipts || !complete->contains("receipts") || *receipts != complete->at("receipts"))
    {
        return reject("compile_proof_changed");
    }
    for (const auto& [path, key] : {
             std::pair{request.Library, "library_sha256"},
             std::pair{request.OutputDirectory / "canonical.json", "canonical_sha256"},
             std::pair{request.OutputDirectory / "profile.json", "profile_sha256"},
             std::pair{request.OutputDirectory / "evidence.json", "evidence_sha256"},
             std::pair{request.OutputDirectory / "fingerprint.sha256", "fingerprint_file_sha256"},
         })
    {
        const auto hash = HashFile(path, report);
        if (!hash || StringField(*complete, key) != *hash)
        {
            return reject("artifact_content_changed");
        }
    }
    if (current->Digest != old->Digest)
    {
        if (const auto revoked = transaction.Revoke(); !revoked)
        {
            return std::unexpected(revoked.error());
        }
        // Compiler receipts and own artifacts were verified above. A
        // dependency-only drift needs recollection, not object recompilation.
        return GuardDisposition::kRecollectRequired;
    }
    return GuardDisposition::kCurrent;
}

Status PrepareCollection(const CollectionRequest& request, const CollectedProfile& profile,
                         const InputSnapshot& inputSnapshot, FailureReport& report, Transaction& transaction)
{
    auto snapshot = inputSnapshot;
    for (const auto& path : profile.ObservedInputs)
    {
        if (const auto added = AddFile(snapshot, path, "collection", report); !added)
        {
            return added;
        }
    }
    Digest(snapshot);
    const auto current = Refresh(request, snapshot, report);
    if (!current || current->Digest != snapshot.Digest)
    {
        return std::unexpected(Fail(report, "collection_inputs_changed"));
    }
    const auto receipts = Receipts(request, snapshot, report);
    if (!receipts)
    {
        return std::unexpected(receipts.error());
    }
    const auto library = VerifyArchive(request, *receipts, report);
    if (!library)
    {
        return std::unexpected(library.error());
    }
    const auto archivePath = request.OutputDirectory / "archive-binding.json";
    if (const auto safe = SafeOutput(request, archivePath, report); !safe)
    {
        return safe;
    }
    const auto profileText = CanonicalJson(profile.Profile);
    if (const auto written = transaction.Write("profile.json", profileText); !written)
    {
        return written;
    }
    if (const auto registered = transaction.Write("canonical.json", ""); !registered)
    {
        return registered;
    }
    if (const auto registered = transaction.Write("fingerprint.sha256", ""); !registered)
    {
        return registered;
    }
    // Frozen encoder validates the pinned Schema and v1 Profile; it cannot sign completion.
    const auto temporary = request.OutputDirectory / ".collection-lock";
    if (EncodeProfileFiles(request.SchemaFile, temporary / "profile.json", temporary / "canonical.json",
                           temporary / "fingerprint.sha256", report) != 0)
    {
        return std::unexpected(Fail(report, "collection_profile_invalid"));
    }
    // Route all transaction output writes through the checked I/O boundary.
    const auto canonical = ReadFile(temporary / "canonical.json", "canonical", report);
    const auto fingerprint = ReadFile(temporary / "fingerprint.sha256", "fingerprint", report);
    if (!canonical || !fingerprint)
    {
        return std::unexpected(Fail(report, "collection_encoding_read"));
    }
    if (const auto written = transaction.Write("canonical.json", *canonical); !written)
    {
        return written;
    }
    if (const auto written = transaction.Write("fingerprint.sha256", *fingerprint); !written)
    {
        return written;
    }
    const nlohmann::json evidence{
        {"platform", profile.Evidence},
        {"snapshot", SnapshotJson(snapshot)},
        {"receipts", *receipts},
    };
    const auto evidenceText = evidence.dump();
    if (const auto written = transaction.Write("evidence.json", evidenceText); !written)
    {
        return written;
    }
    const nlohmann::json complete{
        {"format", kStateFormat},
        {"library", PathText(request.Library)},
        {"library_sha256", *library},
        {"canonical_sha256", Sha256Hex(*canonical)},
        {"profile_sha256", Sha256Hex(profileText)},
        {"evidence_sha256", Sha256Hex(evidenceText)},
        {"fingerprint", *fingerprint},
        {"fingerprint_file_sha256", Sha256Hex(*fingerprint)},
        {"snapshot", SnapshotJson(snapshot)},
        {"receipts", *receipts},
        {"tool_identity", snapshot.Files},
    };
    if (const auto written = transaction.Write("complete.json", complete.dump()); !written)
    {
        return written;
    }
    const auto finalInputs = Refresh(request, snapshot, report);
    const auto finalReceipts = Receipts(request, snapshot, report);
    const auto finalLibrary = HashFile(request.Library, report);
    if (!finalInputs || finalInputs->Digest != snapshot.Digest || !finalReceipts || *finalReceipts != *receipts ||
        !finalLibrary || *finalLibrary != *library)
    {
        return std::unexpected(Fail(report, "publish_inputs_changed"));
    }
    const nlohmann::json binding{{"library_sha256", *library}, {"receipts", *receipts}};
    if (const auto written = transaction.Write("archive-binding.json", binding.dump()); !written)
    {
        return written;
    }
    return {};
}

Status PublishPrepared(const CollectionRequest& request, Transaction& transaction)
{
    for (const auto* name :
         {"profile.json", "canonical.json", "fingerprint.sha256", "evidence.json", "archive-binding.json"})
    {
        if (const auto published = transaction.Publish(name, request.OutputDirectory / name); !published)
        {
            return published;
        }
    }
    return {};
}

Result<std::vector<CollectionRequest>> StateGroup(const CollectionRequest& request, FailureReport& report)
{
    // State primitives also serve cold compiler receipts before graph export;
    // Collect remains the strict graph gate. Existing snapshots bind graph bytes.
    FailureReport graphReport;
    const auto graph = ReadDependencyGraph(request, graphReport);
    if (!graph)
    {
        return std::vector<CollectionRequest>{request};
    }
    const auto plan = PlanDependencies(*graph, request.TargetName, report);
    if (!plan)
    {
        return std::unexpected(plan.error());
    }
    return ResolveCollectionGroup(*graph, *plan, request, report);
}

class LockedGroup
{
public:
    LockedGroup(std::span<const CollectionRequest> requests, FailureReport& report, const CollectionIo& io = {})
        : Requests(requests)
        , Report(report)
        , Io(io)
    {
    }
    ~LockedGroup()
    {
        if (Dirty)
        {
            Report.CleanupIncomplete |= !Revoke();
        }
    }
    LockedGroup(const LockedGroup&) = delete;
    LockedGroup& operator=(const LockedGroup&) = delete;
    LockedGroup(LockedGroup&&) = delete;
    LockedGroup& operator=(LockedGroup&&) = delete;
    Status Begin(bool wait = false)
    {
        // ResolveCollectionGroup sorts by target name. Compiler transitions
        // may wait briefly; all operations acquire the same member lock order.
        for (const auto& request : Requests)
        {
            auto transaction = std::make_unique<Transaction>(request, Report, Io);
            if (const auto begun = transaction->Begin(wait); !begun)
            {
                return begun;
            }
            Transactions.push_back(std::move(transaction));
        }
        return {};
    }
    Status Revoke()
    {
        Dirty = true;
        std::optional<Error> failure;
        for (const auto& transaction : Transactions)
        {
            if (const auto revoked = transaction->Revoke(); !revoked)
            {
                failure = revoked.error();
            }
        }
        if (Requests.size() > 1)
        {
            if (const auto removed = Io.Remove(Requests.front().OutputDirectory / "group-complete.json", Report);
                !removed)
            {
                failure = removed.error();
            }
        }
        return failure ? Status{std::unexpected(*failure)} : Status{};
    }
    Status Close()
    {
        std::optional<Error> failure;
        for (const auto& transaction : Transactions)
        {
            if (const auto closed = transaction->Close(); !closed)
            {
                failure = closed.error();
            }
        }
        if (failure)
        {
            Dirty = true;
            return std::unexpected(*failure);
        }
        Dirty = false;
        return {};
    }
    Transaction& At(std::size_t index) { return *Transactions.at(index); }

private:
    std::span<const CollectionRequest> Requests;
    FailureReport& Report;
    CollectionIo Io;
    std::vector<std::unique_ptr<Transaction>> Transactions;
    bool Dirty = false;
};

Result<nlohmann::json> GroupSeal(std::span<const CollectionRequest> requests, FailureReport& report)
{
    auto members = nlohmann::json::array();
    for (const auto& request : requests)
    {
        const auto path = request.OutputDirectory / "complete.json";
        const auto hash = HashFile(path, report);
        if (!hash)
        {
            return std::unexpected(hash.error());
        }
        members.push_back({{"target", request.TargetName}, {"completion", PathText(path)}, {"sha256", *hash}});
    }
    return nlohmann::json{{"format", "pangu-collection-group-1"}, {"members", members}};
}

void RevokeUnresolved(const CollectionRequest& request, FailureReport& report)
{
    Transaction transaction(request, report);
    if (transaction.Begin())
    {
        report.CleanupIncomplete |= !transaction.Revoke();
    }
}
} // namespace

Status InvalidateCollection(const CollectionRequest& request, FailureReport& report)
{
    const auto requests = StateGroup(request, report);
    if (!requests)
    {
        RevokeUnresolved(request, report);
        return std::unexpected(requests.error());
    }
    LockedGroup group(*requests, report);
    if (const auto begun = group.Begin(true); !begun)
    {
        return begun;
    }
    if (const auto revoked = group.Revoke(); !revoked)
    {
        return revoked;
    }
    for (const auto& member : *requests)
    {
        for (const auto* name : kOutputs)
        {
            if (const auto removed = RemoveGeneratedFile(member.OutputDirectory / name, report); !removed)
            {
                report.CleanupIncomplete = true;
                return removed;
            }
        }
    }
    return group.Close();
}

Result<GuardDisposition> GuardCollection(const CollectionRequest& request, FailureReport& report)
{
    const auto requests = StateGroup(request, report);
    if (!requests)
    {
        RevokeUnresolved(request, report);
        return std::unexpected(requests.error());
    }
    LockedGroup group(*requests, report);
    if (const auto begun = group.Begin(); !begun)
    {
        return std::unexpected(begun.error());
    }
    auto disposition = GuardDisposition::kCurrent;
    for (std::size_t index = 0; index < requests->size(); ++index)
    {
        const auto state = GuardOne(requests->at(index), report, group.At(index));
        if (!state)
        {
            report.CleanupIncomplete |= !group.Revoke();
            return std::unexpected(state.error());
        }
        if (*state == GuardDisposition::kBuildRequired ||
            (*state == GuardDisposition::kRecollectRequired && disposition == GuardDisposition::kCurrent))
        {
            disposition = *state;
        }
    }
    if (requests->size() > 1 && disposition == GuardDisposition::kCurrent)
    {
        const auto sealPath = requests->front().OutputDirectory / "group-complete.json";
        if (!Exists(sealPath))
        {
            disposition = GuardDisposition::kBuildRequired;
        }
        else
        {
            const auto expected = GroupSeal(*requests, report);
            const auto actual = ReadJson(sealPath, report);
            if (!expected || !actual || *expected != *actual)
            {
                report.CleanupIncomplete |= !group.Revoke();
                return std::unexpected(Fail(report, "group_completion_changed", sealPath));
            }
        }
    }
    if (disposition != GuardDisposition::kCurrent)
    {
        if (const auto revoked = group.Revoke(); !revoked)
        {
            return std::unexpected(revoked.error());
        }
    }
    if (const auto closed = group.Close(); !closed)
    {
        return std::unexpected(closed.error());
    }
    return disposition;
}

struct CollectionTransaction::State
{
    std::vector<CollectionRequest> Requests;
    FailureReport& Report;
    LockedGroup Group;
    bool Used = false;
    State(std::span<const CollectionRequest> requests, FailureReport& report, const CollectionIo& io)
        : Requests(requests.begin(), requests.end())
        , Report(report)
        , Group(Requests, report, io)
    {
    }
};
CollectionTransaction::CollectionTransaction(std::unique_ptr<State> state)
    : Data(std::move(state))
{
}
CollectionTransaction::~CollectionTransaction() = default;

Result<std::unique_ptr<CollectionTransaction>> BeginCollectionTransaction(std::span<const CollectionRequest> requests,
                                                                          FailureReport& report, const CollectionIo& io)
{
    if (requests.empty())
    {
        return std::unexpected(Fail(report, "group_input_count"));
    }
    const auto expected = StateGroup(requests.front(), report);
    if (!expected || !std::ranges::equal(*expected, requests))
    {
        return std::unexpected(Fail(report, "group_membership"));
    }
    auto state = std::make_unique<CollectionTransaction::State>(requests, report, io);
    if (const auto begun = state->Group.Begin(); !begun)
    {
        return std::unexpected(begun.error());
    }
    if (const auto revoked = state->Group.Revoke(); !revoked)
    {
        return std::unexpected(revoked.error());
    }
    return std::make_unique<CollectionTransaction>(std::move(state));
}

Status CollectionTransaction::Publish(std::span<const CollectedProfile> profiles,
                                      std::span<const InputSnapshot> snapshots)
{
    auto& report = Data->Report;
    if (std::exchange(Data->Used, true))
    {
        return std::unexpected(Fail(report, "group_transaction_already_used"));
    }
    auto& group = Data->Group;
    const auto& requests = Data->Requests;
    if (requests.size() != profiles.size() || requests.size() != snapshots.size())
    {
        return std::unexpected(Fail(report, "group_input_count"));
    }
    for (std::size_t index = 0; index < requests.size(); ++index)
    {
        const auto prepared = PrepareCollection(requests.at(index), profiles.subspan(index).front(),
                                                snapshots.subspan(index).front(), report, group.At(index));
        if (!prepared)
        {
            return prepared;
        }
    }
    if (requests.size() > 1)
    {
        const auto graph = ReadDependencyGraph(requests.front(), report);
        if (!graph)
        {
            return std::unexpected(graph.error());
        }
        DependencyPlan comparison;
        std::map<std::string, nlohmann::json> values;
        for (std::size_t index = 0; index < requests.size(); ++index)
        {
            comparison.Targets.push_back(requests.at(index).TargetName);
            values.emplace(requests.at(index).TargetName, profiles.subspan(index).front().Profile);
        }
        if (const auto compatible = CompareDependencyProfiles(*graph, comparison, values, report); !compatible)
        {
            return compatible;
        }
    }
    for (std::size_t index = 0; index < requests.size(); ++index)
    {
        if (const auto published = PublishPrepared(requests.at(index), group.At(index)); !published)
        {
            return published;
        }
    }
    for (std::size_t index = 0; index < requests.size(); ++index)
    {
        if (const auto completed = group.At(index).Complete(); !completed)
        {
            return completed;
        }
    }
    for (std::size_t index = 0; index < requests.size(); ++index)
    {
        // Include late backend observations, every receipt and archive again.
        // Partial per-member completions are unusable until the group seal.
        const auto current = GuardOne(requests.at(index), report, group.At(index));
        if (!current || *current != GuardDisposition::kCurrent)
        {
            return std::unexpected(Fail(report, "group_inputs_changed"));
        }
    }
    if (requests.size() > 1)
    {
        const auto seal = GroupSeal(requests, report);
        if (!seal)
        {
            return std::unexpected(seal.error());
        }
        if (const auto written = group.At(0).Write("group-complete.json", seal->dump()); !written)
        {
            return written;
        }
        if (const auto published =
                group.At(0).Publish("group-complete.json", requests.front().OutputDirectory / "group-complete.json");
            !published)
        {
            return published;
        }
    }
    return group.Close();
}

Status PublishCollectionGroup(std::span<const CollectionRequest> requests, std::span<const CollectedProfile> profiles,
                              std::span<const InputSnapshot> snapshots, FailureReport& report, const CollectionIo& io)
{
    auto transaction = BeginCollectionTransaction(requests, report, io);
    if (!transaction)
    {
        return std::unexpected(transaction.error());
    }
    return (*transaction)->Publish(profiles, snapshots);
}

Status PublishCollection(const CollectionRequest& request, const CollectedProfile& profile,
                         const InputSnapshot& snapshot, FailureReport& report, const CollectionIo& io)
{
    return PublishCollectionGroup(std::span(&request, 1), std::span(&profile, 1), std::span(&snapshot, 1), report, io);
}

int RunObservedCompilerInvocation(const CollectionRequest& request, const CompileUnit& unit,
                                  const CompileObservation& observation, FailureReport& report,
                                  std::stop_token cancellation)
{
    const auto matched = MatchCompilerInvocation(request, unit.OriginalArguments, unit.Directory, report);
    if (!matched || matched->Arguments != unit.Arguments || matched->ResponseFiles != unit.ResponseFiles)
    {
        return 2;
    }
    const auto units = LoadCompileUnits(request, report);
    if (!units)
    {
        return 2;
    }
    const auto before = SnapshotInputs(request, *units, observation, report);
    if (!before)
    {
        return 3;
    }
    InputSnapshot responseSnapshot;
    for (const auto& path : unit.ResponseFiles)
    {
        if (!AddFile(responseSnapshot, path, "response", report))
        {
            return 3;
        }
    }
    const auto receiptPath = CompileReceiptPath(request, unit);
    // Only the brief state transitions take the target lock; ordinary parallel
    // object compilation remains possible. Per-object ownership spans execution.
    const auto objectLock = request.OutputDirectory / (Sha256Hex(PathText(unit.Object)) + ".compiling");
    if (!SafeOutput(request, objectLock, report) || !MakeDirectory(request.OutputDirectory, report))
    {
        return 3;
    }
    std::error_code error;
    if (!std::filesystem::create_directory(objectLock, error))
    {
        (void)Fail(report, "object_compile_busy", objectLock, error);
        return 3;
    }
    const auto cleanup = [&]
    {
        const auto removed = RemoveGeneratedFile(objectLock, report);
        report.CleanupIncomplete |= !removed;
        return removed.has_value();
    };
    {
        const auto requests = StateGroup(request, report);
        if (!requests)
        {
            RevokeUnresolved(request, report);
            (void)cleanup();
            return 3;
        }
        LockedGroup group(*requests, report);
        if (!group.Begin(true) || !group.Revoke() || !SafeOutput(request, receiptPath, report) ||
            !RemoveGeneratedFile(receiptPath, report) || !group.Close())
        {
            (void)cleanup();
            return 3;
        }
    }
    // Exit zero alone is not production evidence: a fresh output must have been
    // created by this invocation. Preserve all inputs if the object aliases one.
    if (!SafeOutput(request, unit.Object, report))
    {
        (void)cleanup();
        return 3;
    }
    for (const auto& file : before->Files)
    {
        const auto same = IsSameFile(unit.Object, Path(StringField(file, "path")), report);
        if (!same || *same)
        {
            (void)Fail(report, "object_aliases_input", unit.Object);
            (void)cleanup();
            return 3;
        }
    }
    if (!RemoveGeneratedFile(unit.Object, report))
    {
        (void)cleanup();
        return 3;
    }
    ProcessRequest process;
    process.Program = Path(unit.OriginalArguments.front());
    process.Arguments.assign(unit.OriginalArguments.begin() + 1, unit.OriginalArguments.end());
    process.Directory = unit.Directory;
    process.OutputMode = ProcessOutputMode::kForward;
    process.Timeout = std::chrono::milliseconds::zero();
    const InvocationSignals signals;
    process.InterruptFlag = &gInterruption;
    process.Cancellation = std::move(cancellation);
    const auto result = RunProcess(process, report);
    if (!result || result->ExitCode != 0)
    {
        (void)cleanup();
        if (result)
        {
            return result->ExitCode;
        }
        return gInterruption != 0 ? 128 + gInterruption : 3;
    }
    const auto after = Refresh(request, *before, report);
    // Technical STATIC SCC members can be compiled/archived concurrently.
    // The current TU's own input and validation digests must remain stable;
    // dependency archives are rechecked by Collect() after compilation.
    const bool validationUnchanged =
        after &&
        std::ranges::all_of(
            before->Files, [&after](const nlohmann::json& file)
            { return file.at("role") != "validation" || std::ranges::find(after->Files, file) != after->Files.end(); });
    if (!after || before->CompileDigest != after->CompileDigest || !validationUnchanged ||
        !VerifyResponseFiles(responseSnapshot.Files, report))
    {
        (void)Fail(report, "compile_inputs_changed", unit.Object);
        (void)cleanup();
        return 3;
    }
    const auto objectHash = HashFile(unit.Object, report);
    if (!objectHash)
    {
        (void)cleanup();
        return 3;
    }
    Transaction transaction(request, report);
    if (!transaction.Begin(true) || !MakeDirectory(receiptPath.parent_path(), report))
    {
        (void)cleanup();
        return 3;
    }
    const nlohmann::json receipt{
        {"format", kStateFormat},
        {"compile_digest", before->CompileDigest},
        {"object", PathText(unit.Object)},
        {"object_sha256", *objectHash},
        {"generation", std::chrono::system_clock::now().time_since_epoch().count()},
        {"invocation", UnitJson(unit)},
        {"response_files", responseSnapshot.Files},
        {"snapshot", SnapshotJson(*before)},
    };
    const bool saved = transaction.Write("receipt.json", receipt.dump()).has_value() &&
                       transaction.Publish("receipt.json", receiptPath).has_value() && transaction.Close().has_value();
    const bool cleaned = cleanup();
    if (!saved || !cleaned)
    {
        report.CleanupIncomplete |= !RemoveGeneratedFile(receiptPath, report);
        return 3;
    }
    return 0;
}
} // namespace pg::abi
