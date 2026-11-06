cmake_minimum_required(VERSION 4.0)

if(NOT EXISTS "${PANGU_TOOL_EXE}" OR NOT DEFINED PANGU_TEST_ROOT)
    message(FATAL_ERROR "PluginCli requires PANGU_TOOL_EXE and PANGU_TEST_ROOT")
endif()
set(engine "${PANGU_TEST_ROOT}/Engine")
set(project "${PANGU_TEST_ROOT}/Project")
file(MAKE_DIRECTORY "${engine}/Plugins/Alpha" "${project}/Plugins/Beta")
file(WRITE "${engine}/Plugins/Alpha/CMakeLists.txt" "# Alpha\n")
file(WRITE "${engine}/Plugins/Alpha/Plugin.toml" [=[[plugin]
id = 'z.alpha'
name = '中文 "quoted" \ backslash'
version = '1.2.3+abc.7'
]=])
file(WRITE "${project}/Plugins/Beta/CMakeLists.txt" "# Beta\n")
file(WRITE "${project}/Plugins/Beta/Plugin.toml" [=[[plugin]
id = 'a.beta'
]=])

execute_process(
    COMMAND "${PANGU_TOOL_EXE}" discover --engine-root "${engine}" --project-root "${project}"
    RESULT_VARIABLE status OUTPUT_VARIABLE json ERROR_VARIABLE stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT status EQUAL 0 OR NOT stderr STREQUAL "")
    message(FATAL_ERROR "PluginCli returned ${status}: ${stderr}; stdout=${json}")
endif()
string(JSON type ERROR_VARIABLE err TYPE "${json}" plugins)
if(err OR NOT type STREQUAL "ARRAY")
    message(FATAL_ERROR "PluginCli output is not a JSON plugins array: ${json}")
endif()
string(JSON length LENGTH "${json}" plugins)
if(NOT length EQUAL 2)
    message(FATAL_ERROR "Expected 2 plugins, got ${length}: ${json}")
endif()
string(JSON id0 GET "${json}" plugins 0 id)
string(JSON id1 GET "${json}" plugins 1 id)
string(JSON origin0 GET "${json}" plugins 0 origin)
string(JSON origin1 GET "${json}" plugins 1 origin)
if(NOT origin0 STREQUAL "project" OR NOT origin1 STREQUAL "engine")
    message(FATAL_ERROR "Plugin origin must follow the discovery root: ${json}")
endif()
string(JSON name1 GET "${json}" plugins 1 name)
string(JSON version0 GET "${json}" plugins 0 version)
string(JSON version1 GET "${json}" plugins 1 version)
string(JSON source1 GET "${json}" plugins 1 source_dir)
string(JSON manifest1 GET "${json}" plugins 1 manifest_path)
if(NOT id0 STREQUAL "a.beta" OR NOT id1 STREQUAL "z.alpha" OR
   NOT name1 STREQUAL "中文 \"quoted\" \\ backslash" OR
   NOT version0 STREQUAL "" OR NOT version1 STREQUAL "1.2.3+abc.7" OR
   NOT EXISTS "${source1}" OR NOT EXISTS "${manifest1}")
    message(FATAL_ERROR "JSON encoding/fields/order invalid: ${json}")
endif()

foreach(index RANGE 0 1)
    string(JSON inputType TYPE "${json}" plugins ${index} configure_inputs)
    string(JSON inputCount LENGTH "${json}" plugins ${index} configure_inputs)
    if(NOT inputType STREQUAL "ARRAY" OR inputCount LESS 2)
        message(FATAL_ERROR "Plugin JSON is missing required Configure inputs: ${json}")
    endif()
    math(EXPR lastInput "${inputCount} - 1")
    foreach(inputIndex RANGE 0 ${lastInput})
        string(JSON input GET "${json}" plugins ${index} configure_inputs ${inputIndex})
        if(NOT IS_ABSOLUTE "${input}" OR NOT EXISTS "${input}")
            message(FATAL_ERROR "Configure input did not round-trip as a filesystem path: ${input}")
        endif()
    endforeach()
endforeach()

file(MAKE_DIRECTORY "${PANGU_TEST_ROOT}/EmptyEngine")
execute_process(
    COMMAND "${PANGU_TOOL_EXE}" discover --engine-root "${PANGU_TEST_ROOT}/EmptyEngine"
    RESULT_VARIABLE status OUTPUT_VARIABLE json ERROR_VARIABLE stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT status EQUAL 0 OR NOT json STREQUAL "{\"plugins\":[]}" OR NOT stderr STREQUAL "")
    message(FATAL_ERROR "Empty plugin roots must yield exact JSON: status=${status}, out=${json}, err=${stderr}")
endif()

# 原生路径与 JSON UTF-8 必须支持非 ANSI 字符，不能只验证元数据字符串。
foreach(rootName IN ITEMS "UnicodeRoot" "引擎😀")
    set(unicodeRoot "${PANGU_TEST_ROOT}/${rootName}")
    set(unicodePlugin "${unicodeRoot}/Plugins/插件😀")
    file(MAKE_DIRECTORY "${unicodePlugin}")
    file(WRITE "${unicodePlugin}/Plugin.toml" "[plugin]\nid = 'unicode.plugin'\n")
    file(WRITE "${unicodePlugin}/CMakeLists.txt" "# disabled Unicode plugin\n")
    execute_process(COMMAND "${PANGU_TOOL_EXE}" discover --engine-root "${unicodeRoot}"
        RESULT_VARIABLE status OUTPUT_VARIABLE json ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0 OR NOT stderr STREQUAL "")
        message(FATAL_ERROR "Unicode paths must be discovered: ${status}; ${stderr}")
    endif()
    string(JSON unicodeSource GET "${json}" plugins 0 source_dir)
    string(JSON unicodeManifest GET "${json}" plugins 0 manifest_path)
    file(REAL_PATH "${unicodePlugin}" expectedSource)
    if(NOT unicodeSource STREQUAL expectedSource OR NOT EXISTS "${unicodeManifest}")
        message(FATAL_ERROR "Unicode JSON path did not round-trip: ${json}")
    endif()
    file(WRITE "${unicodePlugin}/Plugin.toml" "[plugin]\nid = 'INVALID'\n")
    execute_process(COMMAND "${PANGU_TOOL_EXE}" discover --engine-root "${unicodeRoot}"
        RESULT_VARIABLE status OUTPUT_VARIABLE json ERROR_VARIABLE stderr)
    string(FIND "${stderr}" "插件😀" unicodeDiagnostic)
    if(status EQUAL 0 OR NOT json STREQUAL "" OR unicodeDiagnostic LESS 0)
        message(FATAL_ERROR "Unicode diagnostic lost its path: ${status}; ${stderr}")
    endif()
endforeach()

file(WRITE "${project}/Plugins/Beta/Plugin.toml" [=[[plugin]
id = 'z.alpha'
]=])
execute_process(
    COMMAND "${PANGU_TOOL_EXE}" discover --engine-root "${engine}" --project-root "${project}"
    RESULT_VARIABLE status OUTPUT_VARIABLE json ERROR_VARIABLE stderr
)
if(status EQUAL 0 OR NOT json STREQUAL "" OR NOT stderr MATCHES "[Dd]uplicate")
    message(FATAL_ERROR "Duplicate IDs must fail on stderr (even if disabled): status=${status}, out=${json}, err=${stderr}")
endif()

execute_process(COMMAND "${PANGU_TOOL_EXE}" nope RESULT_VARIABLE status
                OUTPUT_VARIABLE json ERROR_VARIABLE stderr)
if(status EQUAL 0 OR NOT json STREQUAL "" OR stderr STREQUAL "")
    message(FATAL_ERROR "Unknown CLI subcommands must fail on stderr")
endif()
