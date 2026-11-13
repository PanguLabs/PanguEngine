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

option(PANGU_ENABLE_ASSERTS "Enable Pangu assertions independently of NDEBUG" ON)
set(PANGU_LOG_COMPILED_MIN_LEVEL "0" CACHE STRING "Pangu compiled log minimum level (0..4)")
set_property(CACHE PANGU_LOG_COMPILED_MIN_LEVEL PROPERTY STRINGS 0 1 2 3 4)
if(NOT PANGU_LOG_COMPILED_MIN_LEVEL MATCHES "^[0-4]$")
    message(FATAL_ERROR "PANGU_LOG_COMPILED_MIN_LEVEL must be an integer from 0 to 4")
endif()
