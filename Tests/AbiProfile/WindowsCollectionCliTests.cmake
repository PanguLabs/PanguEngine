cmake_minimum_required(VERSION 4.0)

# Negative CLI regression using the same schema-2 request and native C++ Guard
# as production. This script is test-data orchestration, not a collector.
foreach(name IN ITEMS PANGU_ABI_PROFILE_TOOL PANGU_ABI_REQUEST PANGU_TEST_OUTPUT)
    if(NOT DEFINED ${name} OR "${${name}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${name}")
    endif()
endforeach()
file(MAKE_DIRECTORY "${PANGU_TEST_OUTPUT}")
file(READ "${PANGU_ABI_REQUEST}" valid)
string(JSON schemaFile GET "${valid}" schema_file)
file(SHA256 "${schemaFile}" schemaBefore)
file(SHA256 "${PANGU_ABI_REQUEST}" requestBefore)

function(reject name body)
    set(requestPath "${PANGU_TEST_OUTPUT}/${name}.json")
    file(WRITE "${requestPath}" "${body}")
    execute_process(
        COMMAND "${PANGU_ABI_PROFILE_TOOL}" guard --request "${requestPath}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE diagnostic
        ERROR_VARIABLE errors)
    if(result EQUAL 0)
        message(FATAL_ERROR "CLI accepted invalid request ${name}: ${diagnostic}")
    endif()
    string(JSON operation ERROR_VARIABLE parseError GET "${diagnostic}" operation)
    string(JSON count ERROR_VARIABLE countError LENGTH "${diagnostic}" errors)
    if(parseError OR countError OR NOT operation STREQUAL "abi-profile" OR count LESS 1)
        message(FATAL_ERROR "Missing structured request error for ${name}: ${diagnostic} ${errors}")
    endif()
endfunction()

reject(malformed "{malformed")
reject(duplicate "{\"schema\":2,\"schema\":2}")
string(JSON invalid SET "${valid}" schema 99)
reject(wrong-schema "${invalid}")

string(REPLACE "\\" "/" schemaPath "${schemaFile}")
string(JSON invalid SET "${valid}" output_directory "\"\${schemaPath}\"")
reject(output-alias "${invalid}")

string(REPLACE "\\" "/" inputPath "${PANGU_ABI_REQUEST}")
string(JSON invalid SET "${valid}" signature_header "\"\${inputPath}\"")
reject(signature-alias "${invalid}")

file(SHA256 "${schemaFile}" schemaAfter)
file(SHA256 "${PANGU_ABI_REQUEST}" requestAfter)
if(NOT schemaBefore STREQUAL schemaAfter OR NOT requestBefore STREQUAL requestAfter)
    message(FATAL_ERROR "Invalid CLI request modified protected input bytes")
endif()
