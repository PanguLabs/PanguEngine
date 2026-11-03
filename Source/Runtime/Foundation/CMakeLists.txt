cmake_minimum_required(VERSION 4.0)

add_library(PanguFoundation STATIC
    Private/Error.cpp
)

add_library(Pangu::Foundation ALIAS PanguFoundation)
pangu_configure_module(PanguFoundation)
# 保持改造前的扫描策略：只对本目标关闭，不做全局关闭；A/B 验收要求扫描设置一致。
set_property(TARGET PanguFoundation PROPERTY CXX_SCAN_FOR_MODULES OFF)

target_include_directories(PanguFoundation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/Public"
)
