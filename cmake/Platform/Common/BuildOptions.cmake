include_guard(GLOBAL)

# The only build-policy target: no additional compiled Module library.
add_library(PanguBuildOptions INTERFACE)
add_library(Pangu::BuildOptions ALIAS PanguBuildOptions)

target_compile_features(PanguBuildOptions INTERFACE cxx_std_23)
target_compile_options(PanguBuildOptions INTERFACE
    -fno-exceptions
    -Wall
    -Wextra
    -Wpedantic
)

if(NOT WIN32 AND NOT ANDROID)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_options(PanguBuildOptions INTERFACE -stdlib=libc++)
    endif()
    target_link_options(PanguBuildOptions INTERFACE -stdlib=libc++)
endif()
