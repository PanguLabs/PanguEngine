#pragma once
#if defined(PG_TEST_FRONTEND)
#if __clang_major__ >= 1
struct [[pga::Type]] Conditional
{
};
#endif
#elif defined(PG_TEST_PLATFORM)
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
struct [[pga::Type]] Conditional
{
};
#endif
#endif
