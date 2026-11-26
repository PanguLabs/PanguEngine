#pragma once

#include <RendererTypes.h> // IWYU pragma: export
#include <Result.h>        // IWYU pragma: export

#include <array>
#include <cstdint>

namespace pg::engine
{
class EngineHost;
}

namespace pg::app
{
using renderer::FrameResult;
using renderer::LinearColor;
using renderer::TriangleVertex;
// 仅由选择 INDEXED_MESH 的消费目标定义，供设备打开及重开时借用。
extern const renderer::IndexedQuad kIndexedQuad;

enum class LoopControl : std::uint8_t
{
    kContinue,
    kStop,
};

namespace detail
{
class GraphicsApplication;
}

// 只在 OnFrame 回调期间借用，操作由窗口线程串行执行。
class GraphicsContext final
{
public:
    GraphicsContext(const GraphicsContext&) = delete;
    GraphicsContext& operator=(const GraphicsContext&) = delete;
    GraphicsContext(GraphicsContext&&) = delete;
    GraphicsContext& operator=(GraphicsContext&&) = delete;
    ~GraphicsContext() = default;

    // kPresentRequested 表示 Forge 的呈现调用已返回。
    [[nodiscard]] Result<FrameResult> Present(LinearColor color) noexcept;
    [[nodiscard]] Result<FrameResult> PresentIndexedMesh(LinearColor clearColor) noexcept;
    // 顶点使用 X 向右、Y 向上的裁剪空间及线性 RGBA，调用返回后可立即修改。
    [[nodiscard]] Result<FrameResult> PresentTriangle(LinearColor clearColor,
                                                      const std::array<TriangleVertex, 3>& vertices) noexcept;
    // 清理现有会话后显式重开。成功后下一次 Present 重新挂接当前 Surface。
    [[nodiscard]] Status ReopenGraphics() noexcept;

private:
    friend class detail::GraphicsApplication;
    explicit GraphicsContext(detail::GraphicsApplication& app) noexcept;
    detail::GraphicsApplication& App;
};

[[nodiscard]] Result<LoopControl> OnFrame(GraphicsContext& graphics) noexcept;

// Host 已启动。返回前释放工具持有的借用与在途工作，由入口完成 Host 关闭。
[[nodiscard]] Status RunTool(engine::EngineHost& host) noexcept;
} // namespace pg::app
