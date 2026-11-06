#include <Error.h>
#include <ModuleDeclaration.h>
#include <ModuleError.h>
#include <ModuleIdentity.h>
#include <Result.h>
#include <ScopeAccess.h>
#include <ScopeBuildContext.h>
#include <ScopeIdentity.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

namespace
{
class MockScopeBuildSink : public pg::module::detail::ScopeBuildSink
{
public:
    MOCK_METHOD(pg::module::ModuleId, Owner, (), (const, noexcept, override));
    MOCK_METHOD(pg::module::ScopeHandle, Target, (), (const, noexcept, override));
    MOCK_METHOD(pg::Error, Reject, (pg::Error), (noexcept, override));
    MOCK_METHOD(pg::Result<std::shared_ptr<pg::module::detail::ScopeObjectControl>>, Stage,
                (std::unique_ptr<pg::module::detail::ErasedScopeObject>), (noexcept, override));
    MOCK_METHOD(pg::Result<pg::module::detail::ScopeDependencyObject>, Resolve, (pg::module::DeclarationId),
                (noexcept, override));
};

void RequestStop(int& /*object*/) noexcept {}

TEST(ScopeBuildMock, InvalidHooksRejectBeforeStage)
{
    ::testing::StrictMock<MockScopeBuildSink> sink;
    EXPECT_CALL(sink, Stage).Times(0);
    EXPECT_CALL(sink, Reject(pg::module::MakeModuleError(pg::module::ModuleError::kInvalidDeclaration)))
        .WillOnce(::testing::ReturnArg<0>());
    pg::module::ScopeBuildContext context(sink);
    const auto result = context.Own(std::make_unique<int>(7), pg::module::ScopeHooks<int>{.RequestStop = RequestStop});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), pg::module::MakeModuleError(pg::module::ModuleError::kInvalidDeclaration));
}
} // namespace
