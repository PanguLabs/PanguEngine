#pragma once
#include <MetaOptions.h>
namespace sample
{
using Options = pg::meta::FieldOptions;
constexpr Options kAlias{.Key = "别名\"\n", .ReadOnly = true};
constexpr Options MakeOptions() { return {.Key = "factory"}; }
struct [[pga::Type("00000000000000000000000000000001")]] Settings
{
    [[pga::Field]] int Default;
    [[pga::Field(Options{})]] int Aggregate;
    [[pga::Field(kAlias)]] int Alias;
    [[pga::Field(MakeOptions())]] int Factory;
};
} // namespace sample
