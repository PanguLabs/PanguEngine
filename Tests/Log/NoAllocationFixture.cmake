cmake_minimum_required(VERSION 4.0)

function(pangu_add_log_allocation_fixture sourceRoot binaryRoot)
    # 根工程已校验 Clang；隔离配置子图复用同一正式夹具，避免 Debug 分配门脱离长期入口。
    add_executable(PanguLogNoAllocationFixture "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/NoAllocationFixture.cpp")
    target_sources(PanguLogNoAllocationFixture PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/FormatTests.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/FileStoreAllocationCases.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/WriterAllocationCases.cpp"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/SystemAllocationCases.cpp"
        "${sourceRoot}/Source/Runtime/Log/Private/NativeLogIo.cpp"
        "${sourceRoot}/Source/Runtime/Log/Private/LogFileStore.cpp"
        "${sourceRoot}/Source/Runtime/Log/Private/LogFormat.cpp"
        "${sourceRoot}/Source/Runtime/Log/Private/LogMemory.cpp"
        "${sourceRoot}/Source/Runtime/Log/Private/LogQueue.cpp"
        "${sourceRoot}/Source/Runtime/Log/Private/WriterControl.cpp")
    target_sources(PanguLogNoAllocationFixture PRIVATE
        "${sourceRoot}/Source/Runtime/Log/Private/LogSystem.cpp"
        "${sourceRoot}/Source/Runtime/Log/Private/LogDrain.cpp"
        "${sourceRoot}/Source/Runtime/Log/Private/NativeLogThread.cpp")
    target_compile_definitions(PanguLogNoAllocationFixture PRIVATE PG_LOG_FORMAT_ALLOCATION_TEST PanguLog_EXPORTS)
    target_compile_definitions(PanguLogNoAllocationFixture PRIVATE PG_LOG_ALLOCATION_ROOT="${binaryRoot}/FileStoreAllocation")
    if(WIN32)
        target_sources(PanguLogNoAllocationFixture PRIVATE
            "${sourceRoot}/Source/Runtime/Log/Private/Platform/Windows/NativeLogThread.cpp"
            "${sourceRoot}/Source/Runtime/Log/Private/Platform/Windows/NativeLogIo.cpp"
            "${sourceRoot}/Source/Runtime/Log/Private/Platform/Windows/LogFileStore.cpp")
    else()
        target_sources(PanguLogNoAllocationFixture PRIVATE
            "${sourceRoot}/Source/Runtime/Log/Private/Platform/Posix/NativeLogThread.cpp"
            "${sourceRoot}/Source/Runtime/Log/Private/Platform/Posix/NativeLogIo.cpp"
            "${sourceRoot}/Source/Runtime/Log/Private/Platform/Posix/LogFileStore.cpp")
    endif()
    target_include_directories(PanguLogNoAllocationFixture PRIVATE "$<TARGET_PROPERTY:PanguLog,INTERFACE_INCLUDE_DIRECTORIES>")
    target_sources(PanguLogNoAllocationFixture PRIVATE "${sourceRoot}/Source/Runtime/Log/Private/SpdlogBackend.cpp")
    target_include_directories(PanguLogNoAllocationFixture PRIVATE "${sourceRoot}/Source/Runtime/Log/Private"
        "${sourceRoot}/Source/Runtime/Log/Public")
    target_link_libraries(PanguLogNoAllocationFixture PRIVATE PanguBuildOptions Pangu::Foundation ThirdParty::Spdlog)
    set_target_properties(PanguLogNoAllocationFixture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${binaryRoot}/Bin")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(PanguLogNoAllocationFixture PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc
            -Wl,--wrap=realloc -Wl,--wrap=aligned_alloc -Wl,--wrap=posix_memalign)
    endif()
endfunction()
