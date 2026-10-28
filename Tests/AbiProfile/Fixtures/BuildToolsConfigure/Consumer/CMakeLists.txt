cmake_minimum_required(VERSION 4.0)

pangu_prepare_build_tools(_tool)
get_property(_inputs DIRECTORY PROPERTY CMAKE_CONFIGURE_DEPENDS)
get_filename_component(_compiler "${CMAKE_CXX_COMPILER}" REALPATH)
foreach(_required IN ITEMS
        "${PANGU_BUILD_TOOLS_SOURCE_ROOT}/BuildTools/AbiProfile/Private/Main.cpp"
        "${PANGU_BUILD_TOOLS_SOURCE_ROOT}/BuildTools/AbiProfile/Probe/Libcxx.cpp"
        "${_compiler}")
    if(NOT _required IN_LIST _inputs)
        message(FATAL_ERROR "Repeated preparation lost a configure dependency: ${_required}")
    endif()
endforeach()
set(_consumerTool "${_tool}" PARENT_SCOPE)
