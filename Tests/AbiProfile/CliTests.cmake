# End-to-end ABI Profile tool tests using the frozen versioned vectors.
foreach(_required IN ITEMS PANGU_ABI_PROFILE_TOOL PANGU_TEST_SCHEMA PANGU_TEST_VECTORS PANGU_TEST_OUTPUT)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "Missing CTest input ${_required}.")
    endif()
endforeach()

file(READ "${PANGU_TEST_VECTORS}" _vectors)
string(JSON _baseline GET "${_vectors}" baseline profile)
string(JSON _canonicalExpected GET "${_vectors}" baseline canonical)
string(JSON _digestExpected GET "${_vectors}" baseline sha256)

file(MAKE_DIRECTORY "${PANGU_TEST_OUTPUT}")
set(_profile "${PANGU_TEST_OUTPUT}/AbiProfileCli-valid.json")
set(_badProfile "${PANGU_TEST_OUTPUT}/AbiProfileCli-invalid.json")
set(_canonical "${PANGU_TEST_OUTPUT}/AbiProfileCli-canonical.json")
set(_digest "${PANGU_TEST_OUTPUT}/AbiProfileCli-sha256.txt")
file(WRITE "${_profile}" "${_baseline}")

execute_process(
    COMMAND "${PANGU_ABI_PROFILE_TOOL}" "${PANGU_TEST_SCHEMA}" "${_profile}" "${_canonical}" "${_digest}"
    RESULT_VARIABLE _goodExit
    OUTPUT_VARIABLE _goodOut
    ERROR_VARIABLE _goodErr
)
if(NOT _goodExit EQUAL 0)
    message(FATAL_ERROR "Valid frozen vector rejected: ${_goodErr} Report: ${_goodOut}")
endif()

file(READ "${_canonical}" _actualCanonical)
file(READ "${_digest}" _actualDigest)
if(NOT _actualCanonical STREQUAL _canonicalExpected OR NOT _actualDigest STREQUAL _digestExpected)
    message(FATAL_ERROR "CLI disagrees with frozen canonical/digest vectors.")
endif()

set(_duplicate "{\"schema\":1,\"schema\":1}")
set(_malformed "{ \"schema\": 1 ")
string(REPLACE "\"schema\" : 1" "\"schema\" : 1.0" _floatSchema "${_baseline}")
if(_floatSchema STREQUAL _baseline)
    message(FATAL_ERROR "CLI test did not mutate Schema integer token.")
endif()
# A truncated, duplicate-key and missing-property Profile MUST fail closed.
set(_firstInvalid TRUE)
foreach(_invalid IN ITEMS _duplicate _malformed _floatSchema)
    file(WRITE "${_badProfile}" "${${_invalid}}")
    if(_firstInvalid)
        # Keep previous good outputs: rejecting a newer invalid Profile must clear stale digests.
        set(_firstInvalid FALSE)
    else()
        file(REMOVE "${_canonical}" "${_digest}")
    endif()
    execute_process(
        COMMAND "${PANGU_ABI_PROFILE_TOOL}" "${PANGU_TEST_SCHEMA}" "${_badProfile}" "${_canonical}" "${_digest}"
        RESULT_VARIABLE _badExit
        ERROR_VARIABLE _badErr
    )
    if(_badExit EQUAL 0)
        message(FATAL_ERROR "Invalid Profile was accepted: ${_invalid}")
    endif()
    if(EXISTS "${_canonical}" OR EXISTS "${_digest}")
        message(FATAL_ERROR "Invalid Profile produced output files: ${_invalid}")
    endif()
endforeach()

# A substituted, weaker Schema must not be accepted as the frozen v1 contract.
file(READ "${PANGU_TEST_SCHEMA}" _originalSchema)
string(REPLACE "PanguEngine ABI Profile v1" "Forged ABI Profile v1" _forgedSchema "${_originalSchema}")
if(_forgedSchema STREQUAL _originalSchema)
    message(FATAL_ERROR "Schema corruption fixture failed to modify Schema contents.")
endif()
set(_forgedSchemaPath "${PANGU_TEST_OUTPUT}/AbiProfileCli-forged-schema.json")
file(WRITE "${_forgedSchemaPath}" "${_forgedSchema}")
file(WRITE "${_profile}" "${_baseline}")
file(REMOVE "${_canonical}" "${_digest}")
execute_process(
    COMMAND "${PANGU_ABI_PROFILE_TOOL}" "${_forgedSchemaPath}" "${_profile}" "${_canonical}" "${_digest}"
    RESULT_VARIABLE _forgedExit
    ERROR_VARIABLE _forgedErr
)
if(_forgedExit EQUAL 0 OR EXISTS "${_canonical}" OR EXISTS "${_digest}")
    message(FATAL_ERROR "Forged v1 Schema accepted or generated outputs.")
endif()

message(STATUS "ABI CLI matched frozen digest and rejected invalid/stale/forged Profiles.")

# Nonexistent output aliases must be rejected before either file is created.
set(_aliasRelative "AbiProfileCli-alias.json")
set(_aliasAbsolute "${PANGU_TEST_OUTPUT}/${_aliasRelative}")
file(REMOVE "${_aliasAbsolute}")
execute_process(
    COMMAND "${PANGU_ABI_PROFILE_TOOL}" "${PANGU_TEST_SCHEMA}" "${_profile}"
        "${_aliasRelative}" "${_aliasAbsolute}"
    WORKING_DIRECTORY "${PANGU_TEST_OUTPUT}"
    RESULT_VARIABLE _aliasExit
    OUTPUT_VARIABLE _aliasReport
    ERROR_VARIABLE _aliasError
)
if(_aliasExit EQUAL 0 OR EXISTS "${_aliasAbsolute}")
    message(FATAL_ERROR "Relative/absolute aliases were accepted as distinct outputs.")
endif()

# Compare actual stored bytes: some CIFS mounts give separate inode/EXISTS results
# for case variants even though both spellings refer to the same content.
set(_caseCanonical "${PANGU_TEST_OUTPUT}/AbiProfileCli-Case.json")
set(_caseDigest "${PANGU_TEST_OUTPUT}/abiprofilecli-case.json")
file(REMOVE "${_caseCanonical}" "${_caseDigest}")
file(WRITE "${_caseCanonical}" "first-case-probe")
file(WRITE "${_caseDigest}" "second-case-probe")
file(READ "${_caseCanonical}" _caseProbeCanonical)
file(READ "${_caseDigest}" _caseProbeDigest)
set(_caseInsensitive FALSE)
if(NOT _caseProbeCanonical STREQUAL "first-case-probe" OR
   NOT _caseProbeDigest STREQUAL "second-case-probe")
    set(_caseInsensitive TRUE)
endif()
file(REMOVE "${_caseCanonical}" "${_caseDigest}")
execute_process(
    COMMAND "${PANGU_ABI_PROFILE_TOOL}" "${PANGU_TEST_SCHEMA}" "${_profile}"
        "${_caseCanonical}" "${_caseDigest}"
    RESULT_VARIABLE _caseExit
    OUTPUT_VARIABLE _caseReport
    ERROR_VARIABLE _caseError
)
if(_caseInsensitive)
    if(_caseExit EQUAL 0 OR EXISTS "${_caseCanonical}" OR EXISTS "${_caseDigest}")
        message(FATAL_ERROR "Case-insensitive output aliases were accepted or not rolled back.")
    endif()
else()
    if(NOT _caseExit EQUAL 0)
        message(FATAL_ERROR "Distinct case-sensitive outputs were rejected: ${_caseReport}")
    endif()
    file(READ "${_caseCanonical}" _caseCanonicalContents)
    file(READ "${_caseDigest}" _caseDigestContents)
    if(NOT _caseCanonicalContents STREQUAL _canonicalExpected OR NOT _caseDigestContents STREQUAL _digestExpected)
        message(FATAL_ERROR "Case-sensitive outputs do not match the frozen vectors.")
    endif()
endif()

# Failure to remove one output must not skip the other output's cleanup.
set(_blockedCanonical "${PANGU_TEST_OUTPUT}/AbiProfileCli-blocked")
file(MAKE_DIRECTORY "${_blockedCanonical}")
file(WRITE "${_blockedCanonical}/keep.txt" "prevent directory removal")
file(WRITE "${_digest}" "stale digest")
execute_process(
    COMMAND "${PANGU_ABI_PROFILE_TOOL}" "${PANGU_TEST_SCHEMA}" "${_badProfile}"
        "${_blockedCanonical}" "${_digest}"
    RESULT_VARIABLE _cleanupExit
    OUTPUT_VARIABLE _cleanupReport
    ERROR_VARIABLE _cleanupError
)
if(_cleanupExit EQUAL 0 OR EXISTS "${_digest}")
    message(FATAL_ERROR "A failed canonical cleanup skipped stale digest removal.")
endif()
string(JSON _cleanupIncomplete GET "${_cleanupReport}" cleanup_incomplete)
string(JSON _cleanupStage GET "${_cleanupReport}" errors 0 stage)
if(NOT _cleanupIncomplete OR NOT _cleanupStage STREQUAL "remove_output")
    message(FATAL_ERROR "Incomplete cleanup was not retained in the report.")
endif()

# The report survives stderr redirection and identifies the failure stage/path.
execute_process(
    COMMAND "${PANGU_ABI_PROFILE_TOOL}" "${PANGU_TEST_SCHEMA}" "${_profile}.missing"
        "${_canonical}" "${_digest}"
    RESULT_VARIABLE _missingExit
    OUTPUT_VARIABLE _missingReport
    ERROR_QUIET
)
if(_missingExit EQUAL 0)
    message(FATAL_ERROR "Missing input was accepted.")
endif()
string(JSON _reportStage ERROR_VARIABLE _reportError GET "${_missingReport}" errors 0 stage)
string(JSON _reportPath ERROR_VARIABLE _pathError GET "${_missingReport}" errors 0 path)
string(JSON _nativeCode ERROR_VARIABLE _nativeError GET "${_missingReport}" errors 0 native code)
if(NOT _reportError STREQUAL "NOTFOUND" OR NOT _pathError STREQUAL "NOTFOUND"
        OR NOT _nativeError STREQUAL "NOTFOUND" OR _nativeCode EQUAL 0
        OR NOT _reportStage STREQUAL "read_profile" OR NOT _reportPath STREQUAL "${_profile}.missing")
    message(FATAL_ERROR "Failure report lost the input path/stage: ${_missingReport}")
endif()
