#pragma once
#include "Dependency.h"
namespace fixture
{
struct [[pga::Type]] Actor
{
    [[pga::Field]] int Value{kDefault};
};
} // namespace fixture
