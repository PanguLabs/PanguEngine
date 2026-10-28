include_guard(GLOBAL)

# Build and publish an immutable native ABI tool for the current host.
# Never forward target compiler, sysroot, toolchain or build configuration.
function(pangu_prepare_build_tools outExecutable)
    if(NOT CMAKE_HOST_SYSTEM_NAME MATCHES "^(Windows|Linux|Darwin)$")
        message(FATAL_ERROR "Unsupported Host platform: ${CMAKE_HOST_SYSTEM_NAME}")
    endif()
    if(DEFINED PANGU_BUILD_TOOLS_SOURCE_ROOT)
        set(_source "${PANGU_BUILD_TOOLS_SOURCE_ROOT}")
    else()
        set(_source "${PROJECT_SOURCE_DIR}")
    endif()
    if(NOT EXISTS "${_source}/BuildTools/AbiProfile/Bootstrap/CMakeLists.txt")
        message(FATAL_ERROR "Build tool source root is not PanguEngine: ${_source}")
    endif()
    get_filename_component(_source "${_source}" ABSOLUTE)

    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(_host "macOS")
    else()
        set(_host "${CMAKE_HOST_SYSTEM_NAME}")
    endif()
    if(DEFINED PANGU_BUILD_TOOLS_TEST_ROOT)
        set(_root "${PANGU_BUILD_TOOLS_TEST_ROOT}")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(_root "${_source}/Build/BuildTools/Windows")
    else()
        set(_root "$ENV{HOME}/PanguEngineBuild/BuildTools/${_host}")
    endif()
    if("${_root}" STREQUAL "" OR "${_root}" STREQUAL "/PanguEngineBuild/BuildTools/${_host}")
        message(FATAL_ERROR "Build tool cache requires a local HOME or explicit test root.")
    endif()
    get_filename_component(_root "${_root}" ABSOLUTE)

    if(NOT "$ENV{LLVM_ROOT}" STREQUAL "")
        if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
            set(_compiler "$ENV{LLVM_ROOT}/bin/clang++.exe")
        else()
            set(_compiler "$ENV{LLVM_ROOT}/bin/clang++")
        endif()
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        find_program(_compiler NAMES clang++ NO_CACHE REQUIRED)
    else()
        message(FATAL_ERROR "Native Host Clang 23 requires LLVM_ROOT on Windows and macOS.")
    endif()
    if(NOT EXISTS "${_compiler}")
        message(FATAL_ERROR "Native Host Clang is missing: ${_compiler}")
    endif()
    # Preserve the clang++ driver spelling; resolving to clang can change
    # default driver mode even when the executable bytes are identical.
    get_filename_component(_compilerReal "${_compiler}" REALPATH)

    # One configure uses one input snapshot per preparation context. GLOBAL
    # properties are process-local, unlike Cache entries. Script-mode callers
    # may deliberately change inputs between calls, so they always revalidate.
    if(NOT DEFINED CMAKE_SCRIPT_MODE_FILE)
        string(SHA256 _context "${_host}|${_source}|${_root}|${_compiler}|${_compilerReal}")
        get_property(_prepared GLOBAL PROPERTY "PANGU_BUILD_TOOLS_${_context}")
        if(_prepared)
            get_property(_dependencies GLOBAL PROPERTY "PANGU_BUILD_TOOLS_${_context}_INPUTS")
            # Every calling directory must retain its regeneration inputs.
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_dependencies}")
            set(${outExecutable} "${_prepared}" PARENT_SCOPE)
            return()
        endif()
    endif()
    execute_process(COMMAND "${_compiler}" --version OUTPUT_VARIABLE _version
        RESULT_VARIABLE _versionResult ERROR_VARIABLE _versionError)
    if(NOT _versionResult EQUAL 0 OR NOT _version MATCHES "clang version 23\\.")
        message(FATAL_ERROR "ABI build tool requires native Clang 23: ${_compiler}: ${_versionError}")
    endif()

    file(GLOB_RECURSE _inputs LIST_DIRECTORIES FALSE
        "${_source}/BuildTools/AbiProfile/Private/*.cpp"
        "${_source}/BuildTools/AbiProfile/Private/*.h"
        "${_source}/BuildTools/AbiProfile/Probe/*.cpp"
        "${_source}/BuildTools/AbiProfile/Probe/*.h"
        "${_source}/Source/Runtime/Foundation/Private/*.cpp"
        "${_source}/Source/Runtime/Foundation/Public/*.h"
        "${_source}/Source/ThirdParty/NlohmannJson/Repo/single_include/nlohmann/*.hpp")
    list(APPEND _inputs
        "${_source}/BuildTools/AbiProfile/Bootstrap/CMakeLists.txt"
        "${_source}/BuildTools/AbiProfile/CMakeLists.txt"
        "${_source}/Source/Runtime/Foundation/CMakeLists.txt"
        "${_source}/Source/ThirdParty/NlohmannJson/CMakeLists.txt"
        "${_source}/cmake/Platform/Common/BuildOptions.cmake"
        "${_source}/cmake/Platform/Common/Module.cmake"
        "${CMAKE_CURRENT_FUNCTION_LIST_FILE}")
    set(_hostToolchain "")
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        set(_hostToolchain "${_source}/cmake/Platform/Linux/Toolchain.cmake")
    elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
        set(_hostToolchain "${_source}/cmake/Platform/macOS/Toolchain.cmake")
    endif()
    if(_hostToolchain)
        list(APPEND _inputs "${_hostToolchain}")
    endif()
    list(SORT _inputs)
    set(_identity "PanguAbiProfileBootstrap/v1|${_host}|Release|${_compiler}|${_compilerReal}|${_version}|Ninja")
    file(SHA256 "${_compilerReal}" _compilerSha)
    string(APPEND _identity "|${_compilerSha}")
    foreach(_input IN LISTS _inputs)
        if(NOT EXISTS "${_input}")
            message(FATAL_ERROR "Build tool input missing: ${_input}")
        endif()
        file(SHA256 "${_input}" _sha)
        string(APPEND _identity "|${_input}=${_sha}")
    endforeach()
    string(SHA256 _key "${_identity}")

    # A native configure must be invalidated on source/header/tool change.
    if(NOT DEFINED CMAKE_SCRIPT_MODE_FILE)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_inputs}" "${_compilerReal}")
    endif()
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
        set(_suffix ".exe")
    else()
        set(_suffix "")
    endif()
    set(_published "${_root}/published/${_key}")
    set(_tool "${_published}/PanguAbiProfileTool${_suffix}")
    file(MAKE_DIRECTORY "${_root}")
    file(LOCK "${_root}/bootstrap.lock" GUARD FUNCTION TIMEOUT 600 RESULT_VARIABLE _lockResult)
    if(NOT _lockResult STREQUAL "0")
        message(FATAL_ERROR "Build tool bootstrap lock failed: ${_lockResult}")
    endif()
    if(EXISTS "${_published}")
        if(NOT EXISTS "${_published}/ready.sha256" OR NOT EXISTS "${_tool}")
            message(FATAL_ERROR "Incomplete Build tool publication: ${_published}")
        endif()
        file(READ "${_published}/ready.sha256" _expectedSha)
        file(SHA256 "${_tool}" _actualSha)
        if(NOT _expectedSha STREQUAL _actualSha)
            message(FATAL_ERROR "Published Build tool integrity failure: ${_published}")
        endif()
        if(NOT DEFINED CMAKE_SCRIPT_MODE_FILE)
            set_property(GLOBAL PROPERTY "PANGU_BUILD_TOOLS_${_context}" "${_tool}")
            set_property(GLOBAL PROPERTY "PANGU_BUILD_TOOLS_${_context}_INPUTS" "${_inputs};${_compilerReal}")
        endif()
        set(${outExecutable} "${_tool}" PARENT_SCOPE)
        return()
    endif()

    set(_build "${_root}/build")
    # Explicitly clear a toolchain cached by a prior failed cross-target attempt.
    set(_toolchainArg "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=")
    if(_hostToolchain)
        set(_toolchainArg "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${_hostToolchain}")
    endif()
    # A cross-target configure may export SDKROOT/CXXFLAGS/CRT variables.
    # Start a clean native configure rather than inheriting those target flags.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "--unset=SDKROOT" "--unset=CC" "--unset=CXX"
        "--unset=CFLAGS" "--unset=CXXFLAGS" "--unset=LDFLAGS"
        "--unset=CMAKE_TOOLCHAIN_FILE"
        "--unset=MACOSX_DEPLOYMENT_TARGET"
        # This mutable bootstrap tree may contain system detection from a failed
        # target configure. Published versions are separate and stay untouched.
        "${CMAKE_COMMAND}" --fresh -S "${_source}/BuildTools/AbiProfile/Bootstrap"
        -B "${_build}" -G Ninja ${_toolchainArg}
        "-DPANGU_BUILD_TOOLS_SOURCE_ROOT=${_source}"
        "-DCMAKE_CXX_COMPILER=${_compiler}" "-DCMAKE_BUILD_TYPE=Release"
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
        RESULT_VARIABLE _configureResult OUTPUT_VARIABLE _configureOutput ERROR_VARIABLE _configureError)
    if(NOT _configureResult EQUAL 0)
        message(FATAL_ERROR "ABI build tool configure failed: ${_configureResult}\n${_configureOutput}\n${_configureError}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "--unset=SDKROOT" "--unset=CC" "--unset=CXX"
        "--unset=CFLAGS" "--unset=CXXFLAGS" "--unset=LDFLAGS"
        "--unset=CMAKE_TOOLCHAIN_FILE"
        "--unset=MACOSX_DEPLOYMENT_TARGET"
        "${CMAKE_COMMAND}" --build "${_build}"
        --target PanguAbiProfileTool --parallel 8
        RESULT_VARIABLE _buildResult OUTPUT_VARIABLE _buildOutput ERROR_VARIABLE _buildError)
    if(NOT _buildResult EQUAL 0)
        message(FATAL_ERROR "ABI build tool build failed: ${_buildResult}\n${_buildOutput}\n${_buildError}")
    endif()
    set(_built "${_build}/Bin/PanguAbiProfileTool${_suffix}")
    if(NOT EXISTS "${_built}")
        message(FATAL_ERROR "ABI build tool build produced no executable: ${_built}")
    endif()
    file(SHA256 "${_built}" _builtSha)
    set(_staging "${_root}/staging/${_key}")
    if(EXISTS "${_staging}")
        message(FATAL_ERROR "Incomplete Build tool staging directory: ${_staging}")
    endif()
    file(MAKE_DIRECTORY "${_staging}")
    file(COPY_FILE "${_built}" "${_staging}/PanguAbiProfileTool${_suffix}")
    file(WRITE "${_staging}/ready.sha256" "${_builtSha}")
    file(MAKE_DIRECTORY "${_root}/published")
    file(RENAME "${_staging}" "${_published}" RESULT _publishResult)
    if(NOT _publishResult STREQUAL "0")
        message(FATAL_ERROR "Build tool immutable publication failed: ${_publishResult}")
    endif()
    if(NOT DEFINED CMAKE_SCRIPT_MODE_FILE)
        set_property(GLOBAL PROPERTY "PANGU_BUILD_TOOLS_${_context}" "${_tool}")
        set_property(GLOBAL PROPERTY "PANGU_BUILD_TOOLS_${_context}_INPUTS" "${_inputs};${_compilerReal}")
    endif()
    set(${outExecutable} "${_tool}" PARENT_SCOPE)
endfunction()
