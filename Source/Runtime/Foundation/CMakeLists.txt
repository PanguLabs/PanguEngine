add_library(PanguFoundation STATIC
    Private/Error.cpp
)

add_library(Pangu::Foundation ALIAS PanguFoundation)
pangu_configure_module(PanguFoundation)

target_include_directories(PanguFoundation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Public"
)
