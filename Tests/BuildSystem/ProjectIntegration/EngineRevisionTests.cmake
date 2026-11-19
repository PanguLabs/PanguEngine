cmake_minimum_required(VERSION 4.0)
if(NOT DEFINED PANGU_SOURCE_ROOT OR NOT DEFINED PANGU_TEST_ROOT)
    message(FATAL_ERROR "EngineRevisionTests requires PANGU_SOURCE_ROOT and PANGU_TEST_ROOT")
endif()
set(checker "${PANGU_SOURCE_ROOT}/cmake/PanguEngineRevision.cmake")
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef runId)
set(PANGU_TEST_ROOT "${PANGU_TEST_ROOT}/${runId}")
set(mini "${PANGU_TEST_ROOT}/RevisionGit")
set(sub "${PANGU_TEST_ROOT}/RevisionSub")
file(
    MAKE_DIRECTORY
        "${mini}"
        "${sub}"
)
file(WRITE "${mini}/CMakeLists.txt" "cmake_minimum_required(VERSION 4.0)\n")
file(WRITE "${sub}/sub.txt" "sub content\n")

function(_pangu_run_engine_revision_git repo)
    execute_process(
        COMMAND
            git -C "${repo}" ${ARGN}
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Git failed in ${repo}: git ${ARGN}\n${err}\n${out}")
    endif()
    set(gitOut "${out}" PARENT_SCOPE)
endfunction()
foreach(repo IN ITEMS "${mini}" "${sub}")
    _pangu_run_engine_revision_git(
        "${repo}"
        init
        -q
    )
    _pangu_run_engine_revision_git(
        "${repo}"
        config
        user.name
        "Fixture User"
    )
    _pangu_run_engine_revision_git(
        "${repo}"
        config
        user.email
        "fixture@example.invalid"
    )
endforeach()
_pangu_run_engine_revision_git(
    "${sub}"
    add
    sub.txt
)
_pangu_run_engine_revision_git(
    "${sub}"
    commit
    -qm
    "one"
)
_pangu_run_engine_revision_git(
    "${mini}"
    add
    CMakeLists.txt
)
_pangu_run_engine_revision_git(
    "${mini}"
    commit
    -qm
    "one"
)
_pangu_run_engine_revision_git(
    "${mini}"
    rev-parse
    HEAD
)
set(commit1 "${gitOut}")
file(APPEND "${mini}/CMakeLists.txt" "# two\n")
_pangu_run_engine_revision_git(
    "${mini}"
    add
    CMakeLists.txt
)
_pangu_run_engine_revision_git(
    "${mini}"
    commit
    -qm
    "two"
)
_pangu_run_engine_revision_git(
    "${mini}"
    rev-parse
    HEAD
)
set(commit2 "${gitOut}")

set(caseScript "${PANGU_TEST_ROOT}/RevisionCase.cmake")
file(
    WRITE "${caseScript}"
    [=[
cmake_minimum_required(VERSION 4.0)
include("${PANGU_REVISION_MODULE}")
pangu_validate_engine_revision(
    SOURCE_DIR "${PANGU_REVISION_SOURCE}"
    EXPECTED_COMMIT "${PANGU_REVISION_EXPECTED}"
    ALLOW_LOCAL_OVERRIDE "${PANGU_REVISION_OVERRIDE}")
]=]
)
function(_pangu_expect_engine_lock_check expectedOk expectedText sha override)
    set(checkedSource "${mini}")
    if(ARGC GREATER 4)
        set(checkedSource "${ARGV4}")
    endif()
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" "-DPANGU_REVISION_MODULE=${checker}" "-DPANGU_REVISION_SOURCE=${checkedSource}"
            "-DPANGU_REVISION_EXPECTED=${sha}" "-DPANGU_REVISION_OVERRIDE=${override}" -P "${caseScript}"
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    set(diagnostics "${stdout}\n${stderr}")
    if(expectedOk)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "Git revision was unexpectedly rejected: ${rc}\n${diagnostics}")
        endif()
    elseif(rc EQUAL 0)
        message(FATAL_ERROR "Git revision was unexpectedly accepted: ${expectedText}")
    endif()
    if(NOT expectedText STREQUAL "")
        string(
            FIND "${diagnostics}"
            "${expectedText}"
            found
        )
        if(found LESS 0)
            message(FATAL_ERROR "Missing diagnostic '${expectedText}':\n${diagnostics}")
        endif()
    endif()
endfunction()

# 内容未变但stat缓存过期时，版本锁校验仍必须只读，不能刷新共享索引。
set(hadOptionalLocks OFF)
if(DEFINED ENV{GIT_OPTIONAL_LOCKS})
    set(hadOptionalLocks ON)
    set(savedOptionalLocks "$ENV{GIT_OPTIONAL_LOCKS}")
endif()
set(ENV{GIT_OPTIONAL_LOCKS} 1)
file(SHA256 "${mini}/.git/index" indexBefore)
execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E sleep 1
)
file(TOUCH "${mini}/CMakeLists.txt")
_pangu_expect_engine_lock_check(TRUE "" "${commit2}" OFF)
file(SHA256 "${mini}/.git/index" indexAfter)
if(hadOptionalLocks)
    set(ENV{GIT_OPTIONAL_LOCKS} "${savedOptionalLocks}")
else()
    unset(ENV{GIT_OPTIONAL_LOCKS})
endif()
if(NOT indexBefore STREQUAL indexAfter)
    message(FATAL_ERROR "Revision validation must not refresh source Git index")
endif()

_pangu_expect_engine_lock_check(TRUE "" "${commit2}" OFF)
file(MAKE_DIRECTORY "${mini}/UntrackedEngine")
file(WRITE "${mini}/UntrackedEngine/CMakeLists.txt" "# untracked nested Engine copy\n")
_pangu_expect_engine_lock_check(FALSE "worktree root" "${commit2}" OFF "${mini}/UntrackedEngine")
_pangu_run_engine_revision_git(
    "${mini}"
    worktree
    add
    --detach
    -q
    "${PANGU_TEST_ROOT}/LinkedEngine"
    "${commit2}"
)
_pangu_expect_engine_lock_check(TRUE "" "${commit2}" OFF "${PANGU_TEST_ROOT}/LinkedEngine")
string(TOUPPER "${commit2}" upperCommit)
_pangu_expect_engine_lock_check(TRUE "" "${upperCommit}" OFF)
file(WRITE "${mini}/untracked.txt" "local build output\n")
_pangu_expect_engine_lock_check(TRUE "" "${commit2}" OFF)
_pangu_expect_engine_lock_check(FALSE "expected" "${commit1}" OFF)
_pangu_expect_engine_lock_check(FALSE "40" "invalid" OFF)
file(APPEND "${mini}/CMakeLists.txt" "# dirty\n")
_pangu_expect_engine_lock_check(FALSE "dirty" "${commit2}" OFF)
_pangu_run_engine_revision_git(
    "${mini}"
    add
    CMakeLists.txt
)
_pangu_expect_engine_lock_check(FALSE "dirty" "${commit2}" OFF)
_pangu_run_engine_revision_git(
    "${mini}"
    restore
    --staged
    CMakeLists.txt
)
_pangu_expect_engine_lock_check(TRUE "${commit1}" "${commit1}" ON)
_pangu_expect_engine_lock_check(TRUE "${commit2}" "${commit1}" ON)
_pangu_run_engine_revision_git(
    "${mini}"
    restore
    CMakeLists.txt
)
_pangu_run_engine_revision_git(
    "${mini}"
    -c
    protocol.file.allow=always
    submodule
    add
    -q
    "${sub}"
    ExternalSub
)
_pangu_run_engine_revision_git(
    "${mini}"
    commit
    -am
    "add submodule"
    -q
)
_pangu_run_engine_revision_git(
    "${mini}"
    rev-parse
    HEAD
)
set(commitWithSubmodule "${gitOut}")
_pangu_expect_engine_lock_check(TRUE "" "${commitWithSubmodule}" OFF)
file(APPEND "${sub}/sub.txt" "changed\n")
_pangu_run_engine_revision_git(
    "${sub}"
    add
    sub.txt
)
_pangu_run_engine_revision_git(
    "${sub}"
    commit
    -qm
    "two"
)
_pangu_run_engine_revision_git(
    "${sub}"
    rev-parse
    HEAD
)
set(nextSubCommit "${gitOut}")
_pangu_run_engine_revision_git(
    "${mini}/ExternalSub"
    fetch
    -q
    "${sub}"
    "${nextSubCommit}"
)
_pangu_run_engine_revision_git(
    "${mini}/ExternalSub"
    checkout
    -q
    "${nextSubCommit}"
)
_pangu_expect_engine_lock_check(FALSE "submodule" "${commitWithSubmodule}" OFF)
_pangu_run_engine_revision_git(
    "${mini}/ExternalSub"
    checkout
    -q
    HEAD~1
)
_pangu_expect_engine_lock_check(TRUE "" "${commitWithSubmodule}" OFF)
_pangu_run_engine_revision_git(
    "${mini}"
    -c
    protocol.file.allow=always
    submodule
    deinit
    -f
    ExternalSub
)
_pangu_expect_engine_lock_check(FALSE "submodule" "${commitWithSubmodule}" OFF)
_pangu_expect_engine_lock_check(TRUE "dirty" "${commitWithSubmodule}" ON)
message(STATUS "Engine revision tests completed")
