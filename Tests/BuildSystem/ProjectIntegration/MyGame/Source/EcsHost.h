#pragma once

#ifdef __ANDROID__
extern "C" [[gnu::visibility("default")]] int PanguEcsHostCheck() noexcept;
#else
extern "C" int PanguEcsHostCheck() noexcept;
#endif
