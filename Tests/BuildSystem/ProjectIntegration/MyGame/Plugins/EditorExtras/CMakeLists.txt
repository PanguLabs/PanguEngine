cmake_minimum_required(VERSION 4.0)
file(APPEND "${CMAKE_BINARY_DIR}/PluginVisits.txt" "mygame.editor-extras\n")
add_library(MyGameEditorExtrasHeaders INTERFACE)
target_include_directories(MyGameEditorExtrasHeaders INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Public")
target_link_libraries(MyGameEditorExtrasHeaders INTERFACE Pangu::EngineHost)
pangu_module_library_type(moduleType)
add_library(
    MyGameEditorExtras
    ${moduleType}
    Private/EditorExtras.cpp
)
pangu_configure_module(MyGameEditorExtras EXPORT_MACRO PG_EDITOR_EXTRAS_API)
target_link_libraries(
    MyGameEditorExtras
    PUBLIC
        MyGameEditorExtrasHeaders
        FixtureEnginePhysics
)
pangu_export_module_query(
    TARGET MyGameEditorExtras
    ID pg::fixture::kEditorExtrasId
    ID_HEADER EditorExtrasIdentity.h
    QUERY pg::fixture::QueryEditorExtras
    HEADER EditorExtrasModule.h
)
