include("${CMAKE_CURRENT_LIST_DIR}/Helpers.cmake")
Run(PASS "" "${CMAKE_COMMAND}"
    -S "${CMAKE_CURRENT_LIST_DIR}/RegressionFixture" -B "${PANGU_TEST_ROOT}/Monolithic" -G Ninja
    ${compilerArgs} -DCMAKE_BUILD_TYPE=Release "-DPANGU_SOURCE_ROOT=${PANGU_SOURCE_ROOT}"
    "-DPANGU_META_HOST_BUILD_ROOT=${PANGU_META_HOST_BUILD_ROOT}"
    -DPANGU_TEST_MODULE_MATRIX_CHILD=ON -DBUILD_TESTING=ON -DPANGU_BUILD_ENGINE_TESTS=ON)
Run(PASS "" "${CMAKE_COMMAND}" --build "${PANGU_TEST_ROOT}/Monolithic"
    --parallel "${PANGU_TEST_BUILD_JOBS}" --target PanguModuleUnitTests PanguModuleConcurrencyTests
    PanguScopeBuildMockTests PanguModuleIntegrationTests PanguModuleUnloadTests PanguScopeDestructorFixture PanguStaticCatalogTests
    PanguEngineHostTests PanguEngineHostDestructorFixture PanguEngineHostConcurrencyTests)
Run(PASS "100% tests passed" "${CMAKE_CTEST_COMMAND}" --test-dir "${PANGU_TEST_ROOT}/Monolithic"
    -L "^area[.](module|engine-host)$" -L "^run.precommit$" --output-on-failure --no-tests=error)
