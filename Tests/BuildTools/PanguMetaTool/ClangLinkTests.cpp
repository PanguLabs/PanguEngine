#include "Driver/ClangInfo.h"
#include <gtest/gtest.h>

TEST(MetaClang, AcceptsCompatibleClang)
{
    const auto info = pg::meta::tool::CheckClang();
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->HasRtti, PG_TEST_CLANG_HAS_RTTI != 0);
    EXPECT_TRUE(info->FrontendVersion.starts_with("23."));
    EXPECT_FALSE(info->ResourceDir.empty());
#ifdef _WIN32
    EXPECT_EQ(info->HostStdlib, "msvc");
#elifdef __linux__
    EXPECT_EQ(info->HostStdlib, "libstdc++");
#else
    EXPECT_EQ(info->HostStdlib, "libc++");
#endif
}
