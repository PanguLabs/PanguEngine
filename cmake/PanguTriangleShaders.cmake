include_guard(GLOBAL)

function(pangu_add_triangle_shaders)
    cmake_parse_arguments(PARSE_ARGV 0 arg "MESH_TRANSFORM_SHADERS" "TARGET;BUILD_ROOT;DEPLOY_DIRECTORY" "")
    if(NOT arg_TARGET OR NOT arg_BUILD_ROOT OR arg_UNPARSED_ARGUMENTS OR arg_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "pangu_add_triangle_shaders requires TARGET <name> BUILD_ROOT <directory>")
    endif()

    cmake_path(GET CMAKE_CURRENT_FUNCTION_LIST_DIR PARENT_PATH engineSource)
    set(shaderSource "${engineSource}/Shaders/FSL")
    set(forgeCommon "${engineSource}/Source/ThirdParty/TheForge/Repo/Common_3")
    set(fslDirectory "${forgeCommon}/Tools/ForgeShadingLanguage")
    if(NOT EXISTS "${fslDirectory}/fsl.py")
        message(FATAL_ERROR "Forge FSL is missing. Initialize the pinned TheForge submodule to build ${arg_TARGET}.")
    endif()

    set(shaderNames
        Graph.vert
        Graph.frag
    )
    if(WIN32)
        set(shaderPlatform DIRECT3D12)
        set(compilerName dxc.exe)
        set(compilerEnvironment FSL_COMPILER_DXC)
        set(compilerHint "${forgeCommon}/Graphics/ThirdParty/OpenSource/DirectXShaderCompiler/bin/x64")
        set(derivativeExtension dxil)
        list(APPEND shaderNames default.rootsig)
    elseif(ANDROID OR LINUX)
        if(ANDROID)
            set(shaderPlatform ANDROID_VULKAN)
        else()
            set(shaderPlatform VULKAN)
        endif()
        # FSL 按 Python 所在宿主决定 glslang 的文件名和环境变量。
        if(CMAKE_HOST_LINUX)
            set(compilerName glslangValidator)
            set(compilerEnvironment FSL_COMPILER_LINUX_VK)
            set(compilerHint "${forgeCommon}/Graphics/ThirdParty/OpenSource/VulkanSDK/bin/Linux")
        else()
            set(compilerName glslangValidator.exe)
            set(compilerEnvironment FSL_COMPILER_VK)
            if(CMAKE_HOST_WIN32)
                set(compilerHint "${forgeCommon}/Graphics/ThirdParty/OpenSource/VulkanSDK/bin/Win32")
            endif()
        endif()
        set(derivativeExtension spv)
    elseif(APPLE)
        if(IOS)
            set(shaderPlatform IOS)
            set(metalSdk iphoneos)
        else()
            set(shaderPlatform MACOS)
            set(metalSdk macosx)
        endif()
        set(shaderNames
            Graph.vert.metal
            Graph.frag.metal
        )
        set(derivativeExtension air)
    else()
        message(FATAL_ERROR "Forge triangle shaders have no target platform for ${CMAKE_SYSTEM_NAME}")
    endif()

    set(shaderList "${shaderSource}/shaders.list")
    set(extraInputs)
    if(arg_MESH_TRANSFORM_SHADERS)
        set(shaderList "${shaderSource}/mesh-transform.list")
        list(
            APPEND extraInputs
            "${shaderSource}/MeshTransform.vert.fsl"
            "${shaderSource}/MeshTransform.srt.h"
        )
        if(APPLE)
            list(APPEND shaderNames MeshTransform.vert.metal)
        else()
            list(APPEND shaderNames MeshTransform.vert)
        endif()
    endif()
    if(APPLE)
        find_program(xcrun NAMES xcrun NO_CMAKE_FIND_ROOT_PATH NO_CACHE)
        if(xcrun)
            execute_process(
                COMMAND
                    "${xcrun}" --sdk "${metalSdk}" --find metal
                RESULT_VARIABLE metalResult
                OUTPUT_VARIABLE compilerExecutable
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
        endif()
        if(NOT xcrun OR NOT metalResult STREQUAL "0" OR NOT EXISTS "${compilerExecutable}")
            message(FATAL_ERROR "Forge FSL ${shaderPlatform} requires the host compiler: xcrun --sdk ${metalSdk} metal")
        endif()
        set(compilerDependencies
            "${xcrun}"
            "${compilerExecutable}"
        )
        cmake_path(GET xcrun PARENT_PATH xcrunDirectory)
        set(fslEnvironment
            --modify
            "PATH=path_list_prepend:${xcrunDirectory}"
        )
    else()
        set(PANGU_FSL_COMPILER_DIRECTORY "" CACHE PATH "Host compiler directory for Forge FSL DXC or glslang")
        if(PANGU_FSL_COMPILER_DIRECTORY)
            set(compilerHint "${PANGU_FSL_COMPILER_DIRECTORY}")
        endif()
        find_program(
            compilerExecutable
            NAMES
                "${compilerName}"
            PATHS
                "${compilerHint}"
            NO_DEFAULT_PATH
            NO_CMAKE_FIND_ROOT_PATH
            NO_CACHE
        )
        if(NOT compilerExecutable AND NOT PANGU_FSL_COMPILER_DIRECTORY)
            find_program(compilerExecutable NAMES "${compilerName}" NO_CMAKE_FIND_ROOT_PATH NO_CACHE)
        endif()
        if(NOT compilerExecutable)
            message(
                FATAL_ERROR
                "Forge FSL ${shaderPlatform} cannot find the host compiler ${compilerName}. "
                "Set PANGU_FSL_COMPILER_DIRECTORY to the directory containing it."
            )
        endif()
        cmake_path(GET compilerExecutable PARENT_PATH compilerDirectory)
        set(fslEnvironment "${compilerEnvironment}=${compilerDirectory}")
        if(compilerEnvironment STREQUAL "FSL_COMPILER_LINUX_VK")
            # FSL 只有在 FSL_COMPILER_VK 已设置时才保留显式 Linux 路径。
            list(APPEND fslEnvironment "FSL_COMPILER_VK=${compilerDirectory}")
        endif()
        set(compilerDependencies "${compilerExecutable}")
        if(shaderPlatform STREQUAL "DIRECT3D12")
            foreach(library IN ITEMS dxcompiler.dll dxil.dll)
                if(EXISTS "${compilerDirectory}/${library}")
                    list(APPEND compilerDependencies "${compilerDirectory}/${library}")
                endif()
            endforeach()
        endif()
    endif()

    # 正式构建仅支持单配置 Ninja，FSL 自行在生成目录内区分目标平台。
    set(workDirectory "${arg_BUILD_ROOT}/Generated/TriangleShaders/${arg_TARGET}/${CMAKE_BUILD_TYPE}")
    set(preprocessorLink)
    if(CMAKE_HOST_WIN32)
        set(preprocessor "${forgeCommon}/Utilities/ThirdParty/OpenSource/mcpp/bin/mcpp.exe")
        set(preprocessorName mcpp.exe)
    else()
        set(preprocessorName cc)
        find_program(preprocessor NAMES "${preprocessorName}" NO_CMAKE_FIND_ROOT_PATH NO_CACHE)
        if(preprocessor)
            cmake_path(GET preprocessor PARENT_PATH preprocessorDirectory)
        elseif(NOT CMAKE_CROSSCOMPILING)
            # FSL 固定调用 cc，纯 Clang 宿主可在隔离的 BuildRoot 中提供此名称。
            set(preprocessor "${CMAKE_C_COMPILER}")
            set(preprocessorDirectory "${workDirectory}/HostTools")
            set(preprocessorLink "${preprocessorDirectory}/${preprocessorName}")
            add_custom_command(
                OUTPUT
                    "${preprocessorLink}"
                COMMAND
                    "${CMAKE_COMMAND}" -E make_directory "${preprocessorDirectory}"
                COMMAND
                    "${CMAKE_COMMAND}" -E create_symlink "${preprocessor}" "${preprocessorLink}"
                DEPENDS
                    "${preprocessor}"
                VERBATIM
            )
        endif()
        list(
            APPEND fslEnvironment
            --modify
            "PATH=path_list_prepend:${preprocessorDirectory}"
        )
    endif()
    if(NOT EXISTS "${preprocessor}")
        message(FATAL_ERROR "Forge FSL ${shaderPlatform} cannot find the host C preprocessor ${preprocessorName}")
    endif()
    find_package(Python3 3 COMPONENTS Interpreter QUIET)
    if(NOT Python3_Interpreter_FOUND)
        message(FATAL_ERROR "Forge FSL ${shaderPlatform} requires a host Python 3 interpreter")
    endif()

    set(binaryDirectory "${workDirectory}/CompiledShaders")
    set(platformBinaries "${binaryDirectory}/${shaderPlatform}")
    set(platformSources "${workDirectory}/Source/${shaderPlatform}")
    set(outputs)
    set(generatedSources)
    set(looseDerivatives)
    foreach(shaderName IN LISTS shaderNames)
        list(APPEND outputs "${platformBinaries}/${shaderName}")
        list(APPEND generatedSources "${platformSources}/${shaderName}")
        list(APPEND looseDerivatives "${platformBinaries}/${shaderName}_0.${derivativeExtension}")
    endforeach()
    # 固定版工具的 Python 模块、生成器头和根签名头共同决定字节码。
    file(
        GLOB fslInputs
        CONFIGURE_DEPENDS
        "${fslDirectory}/*.py"
        "${fslDirectory}/generators/*.py"
        "${fslDirectory}/includes/*.h"
        "${forgeCommon}/Graphics/FSL/*.h"
    )
    add_custom_command(
        OUTPUT
            ${outputs}
        BYPRODUCTS
            ${generatedSources}
        COMMAND
            "${CMAKE_COMMAND}" -E env ${fslEnvironment} "${Python3_EXECUTABLE}" -B "${fslDirectory}/fsl.py" --compile
            --mp 0 --language "${shaderPlatform}" --destination "${workDirectory}/Source" --binaryDestination
            "${binaryDirectory}" "${shaderList}"
        # Forge 已将载荷写入容器，部署目录只保留加载器所需文件。
        COMMAND
            "${CMAKE_COMMAND}" -E rm -f ${looseDerivatives}
        DEPENDS
            "${shaderSource}/shaders.list"
            "${shaderList}"
            ${extraInputs}
            "${shaderSource}/Graph.vert.fsl"
            "${shaderSource}/Graph.frag.fsl"
            ${fslInputs}
            ${compilerDependencies}
            "${preprocessor}"
            ${preprocessorLink}
            "${Python3_EXECUTABLE}"
        COMMENT "Compiling Forge triangle shaders for ${shaderPlatform}"
        VERBATIM
    )
    add_custom_target("${arg_TARGET}" DEPENDS ${outputs})
    if(arg_DEPLOY_DIRECTORY)
        set(deployDirectory "${arg_DEPLOY_DIRECTORY}/${shaderPlatform}")
        cmake_path(ABSOLUTE_PATH deployDirectory BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)
        set(deployKey "${deployDirectory}")
        if(CMAKE_HOST_WIN32)
            string(TOLOWER "${deployKey}" deployKey)
        endif()
        foreach(shaderName IN LISTS shaderNames)
            string(SHA256 fileKey "${deployKey}/${shaderName}")
            set(producer "PanguShaderDeployment_${fileKey}")
            if(NOT TARGET "${producer}")
                set(deployed "${deployDirectory}/${shaderName}")
                set(compiled "${platformBinaries}/${shaderName}")
                add_custom_command(
                    OUTPUT
                        "${deployed}"
                    COMMAND
                        "${CMAKE_COMMAND}" -E make_directory "${deployDirectory}"
                    COMMAND
                        "${CMAKE_COMMAND}" -E copy "${compiled}" "${deployed}"
                    DEPENDS
                        "${compiled}"
                    VERBATIM
                )
                add_custom_target("${producer}" DEPENDS "${deployed}")
            endif()
            add_dependencies("${arg_TARGET}" "${producer}")
        endforeach()
    endif()
endfunction()
