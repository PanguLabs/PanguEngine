include_guard(GLOBAL)

# Pangu-owned code policy only; compiler/stdlib/CRT selection belongs to Toolchain/Presets.
add_library(PanguBuildOptions INTERFACE)

target_compile_features(PanguBuildOptions INTERFACE cxx_std_23)
target_compile_options(PanguBuildOptions INTERFACE
    -fno-exceptions
    -Wall
    -Wextra
    -Wpedantic
)
