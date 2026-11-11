#include <Identity.h>
#include <ModuleMetaProvider.h>
#include <gtest/gtest.h>
TEST(PanguMetaRegistration, MetadataDefinitionUsesSharedIdentity)
{
    const auto& definition = *fixture::Query();
    ASSERT_NE(definition.Metadata, nullptr);
    const auto& provider = *definition.Metadata;
    EXPECT_EQ(provider.Owner, definition.Descriptor.Id);
    EXPECT_TRUE(provider.Owner.IsValid());
    EXPECT_FALSE(provider.Entries.empty());
}
