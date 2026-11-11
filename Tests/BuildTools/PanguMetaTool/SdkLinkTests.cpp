#include "Driver/SdkInfo.h"
#include <gtest/gtest.h>

TEST(MetaSdk, AcceptsCompatibleSdk)
{
    const auto info = pg::meta::tool::CheckSdk();
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->HasRtti);
    EXPECT_TRUE(info->FrontendVersion.starts_with("23."));
    EXPECT_FALSE(info->ResourceDir.empty());
#ifdef _WIN32
    EXPECT_EQ(info->HostStdlib, "msvc");
#else
    EXPECT_EQ(info->HostStdlib, "libc++");
#endif
}
