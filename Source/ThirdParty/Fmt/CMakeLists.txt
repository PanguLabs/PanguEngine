cmake_minimum_required(VERSION 4.0)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Pangu fmt adapter requires Clang")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Repo/src/format.cc")
    message(FATAL_ERROR "fmt source missing; run git submodule update --init --recursive")
endif()
# 直接声明编译型 Target，不执行上游 examples/tests/install 配置。
add_library(PanguThirdPartyFmt STATIC Repo/src/format.cc Repo/src/os.cc)
add_library(ThirdParty::Fmt ALIAS PanguThirdPartyFmt)
add_library(fmt::fmt ALIAS PanguThirdPartyFmt)
target_compile_features(PanguThirdPartyFmt PUBLIC cxx_std_23)
target_compile_definitions(PanguThirdPartyFmt PUBLIC FMT_EXCEPTIONS=0)
target_compile_options(PanguThirdPartyFmt PRIVATE -fno-exceptions -frtti)
target_include_directories(PanguThirdPartyFmt SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Repo/include")
set_target_properties(PanguThirdPartyFmt PROPERTIES
    POSITION_INDEPENDENT_CODE ON DEBUG_POSTFIX "" CXX_SCAN_FOR_MODULES OFF)
