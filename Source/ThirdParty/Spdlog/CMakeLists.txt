cmake_minimum_required(VERSION 4.0)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Pangu spdlog adapter requires Clang")
endif()
if(NOT TARGET fmt::fmt OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Repo/src/spdlog.cpp")
    message(FATAL_ERROR "spdlog source or local fmt::fmt missing; initialize submodules and configure fmt first")
endif()
find_package(Threads REQUIRED)
# 沿用上游编译单元，不执行 tests/examples/install；Pangu 后端不创建异步池。
add_library(PanguThirdPartySpdlog STATIC Repo/src/spdlog.cpp Repo/src/stdout_sinks.cpp
    Repo/src/color_sinks.cpp Repo/src/file_sinks.cpp Repo/src/async.cpp Repo/src/cfg.cpp)
add_library(ThirdParty::Spdlog ALIAS PanguThirdPartySpdlog)
target_compile_features(PanguThirdPartySpdlog PUBLIC cxx_std_23)
target_compile_definitions(PanguThirdPartySpdlog PUBLIC SPDLOG_COMPILED_LIB SPDLOG_FMT_EXTERNAL
    SPDLOG_NO_EXCEPTIONS SPDLOG_NO_THREAD_ID SPDLOG_DISABLE_DEFAULT_LOGGER)
target_compile_options(PanguThirdPartySpdlog PRIVATE -fno-exceptions -frtti)
target_include_directories(PanguThirdPartySpdlog SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Repo/include")
target_link_libraries(PanguThirdPartySpdlog PUBLIC ThirdParty::Fmt Threads::Threads)
set_target_properties(PanguThirdPartySpdlog PROPERTIES
    POSITION_INDEPENDENT_CODE ON DEBUG_POSTFIX "" CXX_SCAN_FOR_MODULES OFF)
