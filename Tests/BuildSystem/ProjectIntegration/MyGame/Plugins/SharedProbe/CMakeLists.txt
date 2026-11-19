cmake_minimum_required(VERSION 4.0)
add_library(MyGameSharedProbeHeaders INTERFACE)
target_include_directories(MyGameSharedProbeHeaders INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Public")
target_link_libraries(MyGameSharedProbeHeaders INTERFACE Pangu::Module)
pangu_module_library_type(moduleType)
add_library(
    MyGameSharedProbe
    ${moduleType}
    Private/SharedProbe.cpp
)
pangu_configure_module(MyGameSharedProbe EXPORT_MACRO PG_SHARED_PROBE_API)
target_link_libraries(MyGameSharedProbe PUBLIC MyGameSharedProbeHeaders)
pangu_export_module_query(
    TARGET MyGameSharedProbe
    ID pg::fixture::kSharedProbeId
    ID_HEADER SharedProbeIdentity.h
    QUERY pg::fixture::QuerySharedProbe
    HEADER SharedProbe.h
)
