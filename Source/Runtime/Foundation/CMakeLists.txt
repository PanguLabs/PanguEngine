add_library(PanguFoundation STATIC
    Private/Error.cpp
)

add_library(Pangu::Foundation ALIAS PanguFoundation)
pangu_configure_module(PanguFoundation)
# Build tool bootstrap reuses this Foundation implementation without Target ABI hooks.
if(COMMAND pangu_register_abi_module)
    pangu_register_abi_module(PanguFoundation)
endif()

target_include_directories(PanguFoundation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Public"
)
