#pragma once
namespace sample
{
struct [[pga::Type]] Actor
{
    [[pga::Field]] int Health;
    [[pga::Field]] const int Limit = 7;

protected:
    [[pga::Field]] int Protected;

private:
    [[pga::Field]] int Hidden;
};
} // namespace sample
