cmake_minimum_required(VERSION 4.0)

pangu_module_library_type(moduleType)
add_library(PanguLog ${moduleType} Private/LogModule.cpp Private/SpdlogBackend.cpp Private/LogFormat.cpp
    Private/LogMemory.cpp Private/LogQueue.cpp Private/WriterControl.cpp)
add_library(Pangu::Log ALIAS PanguLog)
pangu_configure_module(PanguLog EXPORT_MACRO PG_LOG_API)
set_property(TARGET PanguLog PROPERTY CXX_SCAN_FOR_MODULES OFF)
target_include_directories(PanguLog PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Public")
target_link_libraries(PanguLog PUBLIC Pangu::Foundation ThirdParty::Fmt PRIVATE ThirdParty::Spdlog)
pangu_export_module_query(TARGET PanguLog ID pg::log::kLogModuleId ID_HEADER LogModule.h
    QUERY pg::log::QueryLogModule HEADER LogModule.h)

target_sources(PanguLog PRIVATE Private/NativeLogIo.cpp Private/LogFileStore.cpp)
target_sources(PanguLog PRIVATE Private/LogSystem.cpp Private/LogDrain.cpp Private/NativeLogThread.cpp)
target_include_directories(PanguLog PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Private")
if(WIN32)
    target_sources(PanguLog PRIVATE Private/Platform/Windows/NativeLogThread.cpp)
    target_sources(PanguLog PRIVATE Private/Platform/Windows/NativeLogIo.cpp Private/Platform/Windows/LogFileStore.cpp)
else()
    target_sources(PanguLog PRIVATE Private/Platform/Posix/NativeLogThread.cpp)
    target_sources(PanguLog PRIVATE Private/Platform/Posix/NativeLogIo.cpp Private/Platform/Posix/LogFileStore.cpp)
endif()
