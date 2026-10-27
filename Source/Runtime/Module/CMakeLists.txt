add_library(PanguModule STATIC
    Private/ModuleIdentity.cpp
)

add_library(Pangu::Module ALIAS PanguModule)
pangu_configure_module(PanguModule)
# Build tool bootstrap reuses this Foundation implementation without Target ABI hooks.
if(COMMAND pangu_register_abi_module)
    pangu_register_abi_module(PanguModule)
endif()

target_include_directories(PanguModule PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Public"
)
