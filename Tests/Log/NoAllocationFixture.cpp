#include "FileStoreAllocationCases.h"
#include "FormatAllocationCases.h"
#include "SystemAllocationCases.h"
#include "WriterAllocationCases.h"
#include <LogFileStore.h>
#include <LogRecord.h>
#include <LogReports.h>
#include <LogTypes.h>
#include <NativeLogIo.h>
#include <SpdlogBackend.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <malloc.h> // IWYU pragma: keep
#include <yvals.h>
#endif

namespace
{
// 监督进程的分配探针也覆盖真实消费者；开关/计数使用原子值。
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> gArmed{};
std::atomic<bool> gNewOnly{};
std::atomic<unsigned int> gRequests{};
unsigned int gConsoleWrites{};
#ifdef __linux__
bool gFailAllocation{};
unsigned int gRejectedAllocations{};
unsigned int gFailAllocationNumber{};
unsigned int gAllocationNumber{};
#endif
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// 分配替换必须调用原生分配入口，不能再经由会递归进入 operator new 的容器。
// NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
void* Allocate(std::size_t size)
{
    if (gArmed)
    {
        ++gRequests;
        // 无异常模式的意外 throwing new 必须由监督进程观察为失败。
        std::_Exit(91);
    }
    if (auto* memory = std::malloc(size == 0 ? 1 : size))
    {
        return memory;
    }
    std::_Exit(92);
}
pg::log::detail::OutputOutcome WriteConsole(void* /*context*/, std::string_view text) noexcept
{
    ++gConsoleWrites;
    return {.BytesWritten = text.size(), .Complete = true};
}
pg::log::detail::OutputOutcome WriteFile(void* /*context*/, std::string_view /*text*/) noexcept
{
    return {.Failure = pg::log::MakeLogError(pg::log::LogError::kIoFailed)};
}
pg::log::detail::OutputOutcome Flush(void* /*context*/) noexcept { return {.Complete = true}; }
} // namespace

// 仅此独立测试进程替换全局 new，生产库不携带故障开关。
void* operator new(std::size_t size) { return Allocate(size); }
void* operator new[](std::size_t size) { return Allocate(size); }
// 本进程的 operator new 以 malloc 分配，配对 delete 必须回到 free。
// NOLINTNEXTLINE(clang-analyzer-unix.MismatchedDeallocator)
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void* operator new(std::size_t size, const std::nothrow_t& /*tag*/) noexcept
{
    if (gArmed)
    {
        ++gRequests;
        return nullptr;
    }
    return std::malloc(size == 0 ? 1 : size);
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept { return ::operator new(size, tag); }
void operator delete(void* memory, const std::nothrow_t& /*tag*/) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t& /*tag*/) noexcept { std::free(memory); }

void* operator new(std::size_t size, std::align_val_t alignment)
{
    if (gArmed)
    {
        ++gRequests;
        std::_Exit(91);
    }
#ifdef _WIN32
    // NOLINTNEXTLINE(misc-include-cleaner) MSVC 的 malloc.h 声明该 CRT 入口。
    auto* memory = _aligned_malloc(size, static_cast<std::size_t>(alignment));
#else
    void* memory{};
    if (posix_memalign(&memory, static_cast<std::size_t>(alignment), size) != 0)
    {
        memory = nullptr;
    }
#endif
    if (memory == nullptr)
    {
        std::_Exit(92);
    }
    return memory;
}
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void* memory, std::align_val_t /*alignment*/) noexcept
{
#ifdef _WIN32
    // NOLINTNEXTLINE(misc-include-cleaner) MSVC 的 malloc.h 声明该 CRT 入口。
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}
void operator delete[](void* memory, std::align_val_t alignment) noexcept { ::operator delete(memory, alignment); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept
{
    if (gArmed)
    {
        ++gRequests;
        return nullptr;
    }
    return ::operator new(size, alignment);
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
    return ::operator new(size, alignment, tag);
}
void operator delete(void* memory, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept
{
    ::operator delete(memory, alignment);
}
void operator delete[](void* memory, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept
{
    ::operator delete(memory, alignment);
}

#ifdef __linux__
extern "C" void* __real_malloc(std::size_t size);
extern "C" void* __real_calloc(std::size_t count, std::size_t size);
extern "C" void* __real_realloc(void* memory, std::size_t size);
extern "C" void* __real_aligned_alloc(std::size_t alignment, std::size_t size);
extern "C" int __real_posix_memalign(void** memory, std::size_t alignment, std::size_t size);
// --wrap 捕获可执行文件和静态库的直接引用；不声称覆盖 libc/libc++ 动态库内部调用。
extern "C" void* __wrap_malloc(std::size_t size)
{
    if (gFailAllocation && (gFailAllocationNumber == 0 || ++gAllocationNumber == gFailAllocationNumber))
    {
        ++gRejectedAllocations;
        return nullptr;
    }
    if (gArmed && !gNewOnly)
    {
        ++gRequests;
        std::_Exit(91);
    }
    return __real_malloc(size);
}
extern "C" void* __wrap_calloc(std::size_t count, std::size_t size)
{
    if (gArmed && !gNewOnly)
    {
        ++gRequests;
        std::_Exit(91);
    }
    return __real_calloc(count, size);
}
extern "C" void* __wrap_realloc(void* memory, std::size_t size)
{
    if (gArmed && !gNewOnly)
    {
        ++gRequests;
        std::_Exit(91);
    }
    return __real_realloc(memory, size);
}
extern "C" void* __wrap_aligned_alloc(std::size_t alignment, std::size_t size)
{
    if (gArmed && !gNewOnly)
    {
        ++gRequests;
        std::_Exit(91);
    }
    return __real_aligned_alloc(alignment, size);
}
extern "C" int __wrap_posix_memalign(void** memory, std::size_t alignment, std::size_t size)
{
    if (gArmed && !gNewOnly)
    {
        ++gRequests;
        std::_Exit(91);
    }
    return __real_posix_memalign(memory, alignment, size);
}
#endif
// NOLINTEND(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)

// NOLINTNEXTLINE(misc-const-correctness) main 保持标准签名，内部不修改 argv。
int main(int argumentCount, char** arguments)
{
    // 初始化允许显式 Try/malloc 资源，但所有隐式 throwing new 必须仍受监督。
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (argumentCount > 1 && std::string_view{arguments[1]} == "initialize")
    {
        gNewOnly = true;
        gArmed = true;
        const bool initialized = RunSystemInitializationAllocationCase();
        gArmed = false;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,modernize-use-std-print)
        std::printf("InitializationAllocationRequests=%u\n", gRequests.load());
#ifdef _WIN32
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,modernize-use-std-print)
        std::printf("IteratorDebugLevel=%d\n", _ITERATOR_DEBUG_LEVEL);
#endif
        return initialized && gRequests == 0 ? 0 : 1;
    }
#ifdef __linux__
    gFailAllocation = true;
    const bool oomHandled = RunWriterOutOfMemoryCases();
    gFailAllocation = false;
    if (!oomHandled || gRejectedAllocations != 2)
    {
        return 1;
    }
    for (unsigned int failure = 1; failure <= 2; ++failure)
    {
        gFailAllocation = true;
        gAllocationNumber = 0;
        gFailAllocationNumber = failure;
        const bool recovered = RunSystemOutOfMemoryCase();
        gFailAllocation = false;
        if (!recovered)
        {
            return 1;
        }
    }
    if (gRejectedAllocations != 4)
    {
        return 1;
    }
#endif
    if (!RunDebugSystemOutOfMemoryCases() || !PrepareWriterAllocationCases() || !PrepareFileStoreAllocationCases() ||
        !PrepareSystemAllocationCases())
    {
        return 1;
    }
    const std::string ownedText(4096, 'a');
    bool success{};
    gArmed = true;
    // 独立负例证明探针确实能拒绝分配，防止优化或链接选择导致空测。
    if (argumentCount > 1)
    {
        // 已检查 argc，argv 是 main 的 C ABI 借用数组。
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const std::string_view mode{arguments[1]};
        if (mode == "throwing-new")
        {
            auto* (*volatile allocate)(std::size_t) = ::operator new;
            ::operator delete(allocate(1));
        }
        else if (mode == "aligned-new")
        {
            auto* (*volatile allocate)(std::size_t, std::align_val_t) = ::operator new;
            ::operator delete(allocate(64, std::align_val_t{64}), std::align_val_t{64});
        }
        else if (mode == "nothrow-new")
        {
            auto* (*volatile allocate)(std::size_t, const std::nothrow_t&) noexcept = ::operator new;
            auto* memory = allocate(1, std::nothrow);
            const bool rejected = memory == nullptr && gRequests == 1;
            ::operator delete(memory);
            return rejected ? 91 : 1;
        }
#ifdef __linux__
        else if (mode == "malloc")
        {
            auto* (*volatile allocate)(std::size_t) = std::malloc;
            static_cast<void>(allocate(1));
        }
#endif
        return 1;
    }
    {
        const pg::log::detail::OutputPort emptyPort;
        pg::log::detail::SpdlogBackend empty{emptyPort, emptyPort};
        static_cast<void>(empty.Emit(pg::log::Level::kInfo, "empty {}"));
        static_cast<void>(empty.Flush());
        pg::log::detail::SpdlogBackend backend{{.Write = WriteConsole, .Flush = Flush},
                                               {.Write = WriteFile, .Flush = Flush}};
        const auto outcomes = backend.Emit(pg::log::Level::kInfo, "raw {} {bad}");
        success = std::get<0>(outcomes).Complete && std::get<1>(outcomes).Failure.has_value();
        static_cast<void>(backend.Flush());
    }
    success = success && RunFormatAllocationCases(ownedText);
    success = RunWriterAllocationCases() && success;
    success = RunFileStoreAllocationCases() && success;
    success = RunSystemAllocationCases() && success;
    gArmed = false;
    // 关卡结束后输出原生计数，不引入额外格式库路径或平台 C++ print 支持前提。
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,modernize-use-std-print)
    std::printf("AllocationRequests=%u ConsoleWrites=%u RecordBytes=%zu LineBytes=%zu FileStoreBytes=%zu "
                "NativeIoBytes=%zu InitReportBytes=%zu FileSnapshotBytes=%zu\n",
                gRequests.load(), gConsoleWrites, sizeof(pg::log::LogRecord), sizeof(pg::log::EncodedLogLine),
                sizeof(pg::log::detail::FileStore), sizeof(pg::log::detail::NativeLogIo),
                sizeof(pg::log::LogInitReport), sizeof(pg::log::FileStoreSnapshot));
    return success && gRequests == 0 && gConsoleWrites == 1 ? 0 : 1;
}
