cmake_minimum_required(VERSION 4.0)

if(APPLE AND NOT IOS)
    _pangu_enable_apple_graphics_languages()
endif()
pangu_require_graphics_renderer()

include("${PROJECT_SOURCE_DIR}/cmake/PanguTestDependencies.cmake")
pangu_require_googletest(RTTI ON)

add_executable(PanguRendererTests SessionTests.cpp)
if(APPLE AND NOT IOS)
    target_sources(PanguRendererTests PRIVATE MacWindowPixelTests.cpp)
    target_link_libraries(PanguRendererTests PRIVATE "-framework CoreGraphics")
endif()
set(rendererTestTargets PanguRendererTests)
if(WIN32)
    add_executable(PanguRendererHostTests HostTests.cpp)
    list(APPEND rendererTestTargets PanguRendererHostTests)
    target_sources(
        PanguRendererTests
        PRIVATE
            GraphicsApplicationTests.cpp
            "${PROJECT_SOURCE_DIR}/Source/Runtime/Application/Private/GraphicsApplication.cpp"
    )
    target_include_directories(
        PanguRendererTests
        PRIVATE
            "${PROJECT_SOURCE_DIR}/Source/Runtime/Application/Private"
            "${PROJECT_SOURCE_DIR}/Source/Runtime/Application/Public"
    )
    target_link_libraries(PanguRendererTests PRIVATE Pangu::EngineHost)
    target_link_libraries(
        PanguRendererHostTests
        PRIVATE
            Pangu::EngineHost
            user32
    )
endif()
set_target_properties(
    ${rendererTestTargets}
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            "${CMAKE_CURRENT_BINARY_DIR}/Bin"
)
foreach(testTarget IN LISTS rendererTestTargets)
    target_include_directories("${testTarget}" PRIVATE "${PROJECT_SOURCE_DIR}/Source/Runtime/Renderer/Private")
    target_link_libraries(
        "${testTarget}"
        PRIVATE
            PanguBuildOptions
            Pangu::Renderer
            ThirdParty::GoogleTestMain
    )
    if(WIN32)
        target_compile_definitions(
            "${testTarget}"
            PRIVATE
                NOMINMAX
                WIN32_LEAN_AND_MEAN
        )
        _pangu_stage_win32_graphics_runtime("${testTarget}")
    elseif(APPLE AND NOT IOS)
        _pangu_set_graphics_runtime_files("${testTarget}" PANGU_METAL_RUNTIME_FILES)
    endif()
    pangu_stage_host_runtime(TARGET "${testTarget}")
endforeach()
pangu_register_gtest(
    TARGET PanguRendererTests
    AREA renderer
    BOUNDARY unit
    TIER fast
    TYPES
        contract
        regression
)

if(WIN32)
    pangu_register_gtest(
        TARGET PanguRendererHostTests
        AREA renderer
        BOUNDARY integration
        TIER full
        TYPES
            contract
            regression
        REQUIRES
            gpu
            window
    )
endif()

if(LINUX AND PANGU_LINK_MODE STREQUAL "Modular")
    pangu_register_test(
        NAME Renderer.VulkanAdapterSymbols
        COMMAND
            "${CMAKE_COMMAND}"
            "-DllvmNm:FILEPATH=${CMAKE_NM}"
            "-DrendererModule:FILEPATH=$<TARGET_FILE:PanguRenderer>"
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/Acceptance/CheckVulkanSymbols.cmake"
        BOUNDARY integration
        AREA renderer
        TIER fast
        BUILD_TARGETS
            PanguRenderer
        TYPES
            contract
            regression
        REQUIRES
            subprocess
    )
endif()

if(APPLE AND NOT IOS AND PANGU_LINK_MODE STREQUAL "Modular")
    pangu_register_test(
        NAME Renderer.MetalAdapterSymbols
        COMMAND
            "${CMAKE_COMMAND}"
            "-DrendererModule:FILEPATH=$<TARGET_FILE:PanguRenderer>"
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/Acceptance/CheckMetalSymbols.cmake"
        BOUNDARY integration
        AREA renderer
        TIER fast
        BUILD_TARGETS
            PanguRenderer
        TYPES
            contract
            regression
        REQUIRES
            subprocess
    )
endif()

if(WIN32 OR LINUX OR (APPLE AND NOT IOS))
    set(observerArgs)
    set(desktopFrameRequirements
        subprocess
        nested-build
    )
    if(WIN32 OR APPLE)
        list(
            APPEND desktopFrameRequirements
            gpu
            window
        )
    endif()
    if(APPLE AND NOT IOS)
        add_executable(PanguMacWindowSampler Acceptance/SampleMacWindow.mm)
        set_target_properties(
            PanguMacWindowSampler
            PROPERTIES
                OUTPUT_NAME
                    PanguWindowSampler
                OBJCXX_STANDARD
                    23
                OBJCXX_STANDARD_REQUIRED
                    ON
        )
        target_compile_options(PanguMacWindowSampler PRIVATE -fobjc-arc)
        target_link_libraries(
            PanguMacWindowSampler
            PRIVATE
                PanguBuildOptions
                "-framework Foundation"
                "-framework CoreGraphics"
                "-framework ScreenCaptureKit"
        )
        pangu_test_build_dependencies(TIER full TARGETS PanguMacWindowSampler)
        set(observerArgs "-DPANGU_MAC_WINDOW_SAMPLER:FILEPATH=$<TARGET_FILE:PanguMacWindowSampler>")
    endif()
    pangu_register_test(
        NAME Renderer.DesktopFrame
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPANGU_ENGINE_SOURCE_DIR:PATH=${PROJECT_SOURCE_DIR}"
            "-DPANGU_TEST_ROOT:PATH=${CMAKE_BINARY_DIR}/Tests/Work/Renderer.DesktopFrame"
            "-DPANGU_TEST_BUILD_JOBS:STRING=${PANGU_TEST_BUILD_JOBS}"
            "-DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE:BOOL=${PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE}"
            "-DPANGU_LINK_MODE:STRING=${PANGU_LINK_MODE}"
            "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
            "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
            "-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}"
            ${observerArgs}
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/Acceptance/RunDesktop.cmake"
        BOUNDARY system
        AREA renderer
        TIER full
        TYPES
            contract
            regression
        REQUIRES
            ${desktopFrameRequirements}
        PROCESSORS "${PANGU_TEST_BUILD_JOBS}"
        TIMEOUT 900
    )
    set_tests_properties(
        Renderer.DesktopFrame
        PROPERTIES
            RESOURCE_LOCK
                RendererDesktop
    )
    if(APPLE AND NOT IOS)
        set_tests_properties(
            Renderer.DesktopFrame
            PROPERTIES
                SKIP_REGULAR_EXPRESSION
                    "Metal unavailable; Open failure and exit verified"
        )
    endif()

    pangu_register_test(
        NAME Renderer.Headless
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPANGU_ENGINE_SOURCE_DIR:PATH=${PROJECT_SOURCE_DIR}"
            "-DPANGU_TEST_ROOT:PATH=${CMAKE_BINARY_DIR}/Tests/Work/Renderer.Headless"
            "-DPANGU_TEST_BUILD_JOBS:STRING=${PANGU_TEST_BUILD_JOBS}"
            "-DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE:BOOL=${PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE}"
            "-DPANGU_LINK_MODE:STRING=${PANGU_LINK_MODE}"
            "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
            "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
            "-DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}"
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/Acceptance/Headless.cmake"
        BOUNDARY system
        AREA renderer
        TIER full
        TYPES
            contract
            regression
        REQUIRES
            subprocess
            nested-build
        PROCESSORS "${PANGU_TEST_BUILD_JOBS}"
        TIMEOUT 900
    )
endif()
