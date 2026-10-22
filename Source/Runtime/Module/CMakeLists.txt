add_library(PanguModule STATIC
    Private/ModuleIdentity.cpp
)

add_library(Pangu::Module ALIAS PanguModule)
pangu_configure_module(PanguModule)

target_include_directories(PanguModule PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Public"
)
