# Windows uses upstream Clang 23 and MSVC STL/CRT.
# Windows x86/x64 are supported; Windows ARM is outside ABI Profile v1.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM|arm|aarch)")
    message(FATAL_ERROR "PanguEngine ABI Profile v1 does not support Windows ARM.")
endif()
