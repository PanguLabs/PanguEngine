#include "NativeLogThread.h"
#include <Check.h>

#include <chrono>
#include <thread>

namespace pg::log::detail
{
NativeLogThread::~NativeLogThread() { PG_CHECK(!Started, "日志原生线程未完成回收"); }
void PauseLogWorker() noexcept { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
} // namespace pg::log::detail
