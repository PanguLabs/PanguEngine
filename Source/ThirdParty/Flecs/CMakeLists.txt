cmake_minimum_required(VERSION 4.0)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Pangu flecs adapter requires the target platform's Clang C compiler")
endif()
if(ANDROID)
    file(REAL_PATH "${CMAKE_ANDROID_NDK}" compilerRoot)
else()
    file(REAL_PATH "$ENV{LLVM_ROOT}" compilerRoot)
    if(CMAKE_C_COMPILER_VERSION VERSION_LESS 23.0 OR NOT CMAKE_C_COMPILER_VERSION VERSION_LESS 24.0)
        message(FATAL_ERROR "Pangu flecs adapter requires Clang 23.x except on Android")
    endif()
endif()
file(REAL_PATH "${CMAKE_C_COMPILER}" compilerPath)
cmake_path(
    IS_PREFIX
    compilerRoot
    "${compilerPath}"
    NORMALIZE
    compilerInToolchain
)
if(NOT compilerInToolchain)
    message(FATAL_ERROR "The flecs C compiler must come from the selected target toolchain: ${compilerRoot}")
endif()

set(flecsSource "${CMAKE_CURRENT_SOURCE_DIR}/Repo")
if(NOT EXISTS "${flecsSource}/src/world.c" OR NOT EXISTS "${flecsSource}/include/flecs.h")
    message(FATAL_ERROR "flecs source missing; run git submodule update --init --recursive")
endif()
file(GLOB_RECURSE flecsSources "${flecsSource}/src/*.c")
if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

function(_pangu_add_flecs_target target type)
    add_library(
        ${target}
        ${type}
        ${flecsSources}
    )
    target_include_directories(${target} SYSTEM PUBLIC "${flecsSource}/include")
    target_compile_definitions(
        ${target}
        PUBLIC
            FLECS_CUSTOM_BUILD
            FLECS_CPP
            FLECS_MULTI_WORLD
            FLECS_PIPELINE
            FLECS_LOG
            FLECS_CPP_NO_AUTO_REGISTRATION
            ${ARGN}
    )
    set_target_properties(
        ${target}
        PROPERTIES
            C_STANDARD
                99
            C_STANDARD_REQUIRED
                ON
            C_EXTENSIONS
                OFF
            POSITION_INDEPENDENT_CODE
                ON
            C_VISIBILITY_PRESET
                hidden
            DEBUG_POSTFIX
                ""
    )

    if(type STREQUAL "STATIC")
        target_compile_definitions(${target} PUBLIC flecs_STATIC)
    else()
        set_target_properties(
            ${target}
            PROPERTIES
                DEFINE_SYMBOL
                    flecs_EXPORTS
        )
    endif()
    if(WIN32)
        target_link_libraries(
            ${target}
            PRIVATE
                wsock32
                ws2_32
                dbghelp
        )
    else()
        target_link_libraries(
            ${target}
            PRIVATE
                Threads::Threads
                ${CMAKE_DL_LIBS}
                m
        )
    endif()
endfunction()

pangu_module_library_type(flecsType)
_pangu_add_flecs_target(PanguThirdPartyFlecs ${flecsType} FLECS_KEEP_ASSERT)
add_library(ThirdParty::Flecs ALIAS PanguThirdPartyFlecs)

if(flecsAssertBenchmarks)
    # 在同一目录创建对照库，复用 Threads 等平台依赖的可见范围。
    _pangu_add_flecs_target(PanguThirdPartyFlecsAssertOff STATIC)
endif()
