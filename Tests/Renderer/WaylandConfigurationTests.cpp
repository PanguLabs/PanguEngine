#include "WaylandConfiguration.h"

#include <gtest/gtest.h>

#include <optional>

namespace pg::app::detail
{
namespace
{
TEST(WaylandConfigurationTest, SuspendAndResumeApplyOnlyAtSurfaceConfigure)
{
    WaylandConfiguration window;
    EXPECT_EQ(window.DrawableSize().Width, 0U);
    window.Configure(640, 480, false);
    window.Acknowledge();
    window.Configure(0, 0, true);
    EXPECT_EQ(window.DrawableSize().Width, 640U);
    window.Acknowledge();
    EXPECT_EQ(window.DrawableSize().Width, 0U);
    EXPECT_EQ(window.DrawableSize().Height, 0U);

    // 挂起期间仍可协商下一次恢复尺寸；未建议的轴沿用上次尺寸。
    window.Configure(800, 0, std::nullopt);
    window.Acknowledge();
    EXPECT_EQ(window.DrawableSize().Width, 0U);
    EXPECT_EQ(window.ContentSize().Width, 800U);
    window.Configure(0, 0, false);
    EXPECT_EQ(window.DrawableSize().Width, 0U);
    window.Acknowledge();
    EXPECT_EQ(window.DrawableSize().Width, 800U);
    EXPECT_EQ(window.DrawableSize().Height, 480U);
}
} // namespace
} // namespace pg::app::detail
