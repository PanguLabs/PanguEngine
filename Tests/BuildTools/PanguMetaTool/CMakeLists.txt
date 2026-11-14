cmake_minimum_required(VERSION 4.0)

pangu_require_googletest(RTTI ON)
add_executable(PanguMetaCodeGenTests CodeGenTests.cpp)
pangu_meta_target(PanguMetaCodeGenTests)
target_link_libraries(PanguMetaCodeGenTests PRIVATE PanguMetaCodeGen ThirdParty::GoogleTestMain)
foreach(case IN ITEMS TypedAndDescriptionModes EscapingIsInjective RejectsMappingAndAssembly
        AssemblyPreservesExplicitOrderAndGlobalNames RejectsInvalidAssemblyNames QueryIncludesFollowCanonicalOrder
        LongModuleEntryRespectsColumnLimit)
    pangu_register_test(NAME MetaCodeGen.${case}
        COMMAND "$<TARGET_FILE:PanguMetaCodeGenTests>" --gtest_filter=MetaCodeGen.${case}
        BOUNDARY unit AREA build TIER precommit BUILD_TARGETS PanguMetaCodeGenTests)
endforeach()
set(consumerToolchain "")
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(consumerToolchain "${PANGU_ENGINE_SOURCE_DIR}/cmake/Platform/Linux/Toolchain.cmake")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(consumerToolchain "${PANGU_ENGINE_SOURCE_DIR}/cmake/Platform/macOS/Toolchain.cmake")
endif()
pangu_register_test(NAME MetaCli.ConsumerPipeline COMMAND "${CMAKE_COMMAND}" -E env CMAKE_BUILD_PARALLEL_LEVEL=2 "${CMAKE_COMMAND}"
    "-DPG_TOOL=$<TARGET_FILE:PanguMetaTool>" "-DPG_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DPG_BINARY=${CMAKE_CURRENT_BINARY_DIR}/Consumer" "-DPG_ENGINE=${PANGU_ENGINE_ROOT}"
    "-DPG_COMPILER=${PANGU_META_HOST_COMPILER}" "-DPG_RESOURCE=${PANGU_META_RESOURCE_DIR}"
    "-DPG_TOOLCHAIN=${consumerToolchain}" -P "${CMAKE_CURRENT_SOURCE_DIR}/CliTests.cmake"
    BOUNDARY integration AREA build TIER precommit BUILD_TARGETS PanguMetaTool REQUIRES subprocess PROCESSORS 2 TIMEOUT 240)
add_executable(PanguMetaPublishTests PublishTests.cpp)
pangu_meta_target(PanguMetaPublishTests)
target_link_libraries(PanguMetaPublishTests PRIVATE PanguMetaOutput ThirdParty::GoogleTestMain)
set(publishCases
    SameContentKeepsTimestamp PartialCommitCannotReuseOldStamp ForeignFilePreserved
    InvalidIndexAndPathsRejectedBeforeMutation MissingOutputAndStaleStagingRetry
    CleanupFailureRetainsOwnership IndexFailureAndChangedRetryPreserveAllOwnership
    RootSetAndAliasValidation LinksCannotEscapeOrDeleteForeignFiles UnicodeAndProspectiveAliases
    NativeDeleteFailureCanRetry CandidatePreparationFailurePreservesOldSuccess
    DepfileEscapesSpecialCharacters NativeReplaceFile EveryReplaceBoundaryCanRetry)
if(UNIX)
    list(APPEND publishCases CandidateWriteFailurePreservesOldSuccess)
endif()
foreach(case IN LISTS publishCases)
    pangu_register_test(NAME MetaPublish.${case}
        COMMAND "$<TARGET_FILE:PanguMetaPublishTests>" --gtest_filter=MetaPublish.${case}
        BOUNDARY integration AREA build TIER precommit BUILD_TARGETS PanguMetaPublishTests)
endforeach()
add_library(PanguMetaContextParser STATIC ContextParser.cpp)
pangu_meta_target(PanguMetaContextParser)
target_include_directories(PanguMetaContextParser SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_compile_options(PanguMetaContextParser PRIVATE ${panguMetaClangRttiFlag})
target_link_libraries(PanguMetaContextParser PRIVATE ${panguMetaClangLibraries})
add_executable(PanguMetaContextTests ContextTests.cpp OwnershipTests.cpp)
pangu_meta_target(PanguMetaContextTests)
target_include_directories(PanguMetaContextTests SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_link_libraries(PanguMetaContextTests PRIVATE PanguMetaContextParser PanguMetaFrontend PanguMetaModel ThirdParty::GoogleTestMain ThirdParty::NlohmannJson)
target_compile_definitions(PanguMetaContextTests PRIVATE JSON_NOEXCEPTION=1
    "PG_TEST_FIXTURES=\"${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/Contexts\""
    "PG_TEST_RESOURCE=\"${PANGU_META_RESOURCE_DIR}\""
    "PG_TEST_COMPILER=\"${PANGU_META_HOST_COMPILER}\"")
foreach(case IN ITEMS SelectsExactObject ResponseFilesPreserveArguments WarningSuppressionCannotHidePgaTypo RejectsUnsupportedDriverAndBinaryInputs TargetMacrosMatchInvocation DedicatedSourceAndConfigurationGuards RawHeaderWithWarningsAsErrors CMakeCodeViewDebugContext)
    pangu_register_test(NAME MetaContext.${case}
        COMMAND "$<TARGET_FILE:PanguMetaContextTests>" --gtest_filter=MetaContext.${case}
        BOUNDARY integration AREA build TIER precommit BUILD_TARGETS PanguMetaContextTests)
endforeach()
foreach(case IN ITEMS OutputMappingIsInjective FixedSlotsAndConsumption ExistingAliases ParentFileConflict UnicodeNamesAndCleanup ScanQueryAndReservedSlots DirectoryAliasesAndEscape)
    pangu_register_test(NAME MetaOwnership.${case}
        COMMAND "$<TARGET_FILE:PanguMetaContextTests>" --gtest_filter=MetaOwnership.${case}
        BOUNDARY unit AREA build TIER precommit BUILD_TARGETS PanguMetaContextTests)
endforeach()
foreach(kind IN ITEMS Request Model)
    add_executable(PanguMeta${kind}Tests ${kind}Tests.cpp)
    pangu_meta_target(PanguMeta${kind}Tests)
    target_link_libraries(PanguMeta${kind}Tests PRIVATE PanguMetaModel ThirdParty::GoogleTestMain)
endforeach()
foreach(case IN ITEMS SchemaAndPaths OwnershipAliasesAndLifetime AssemblyModesAndCatalogOrder ExistingOutputAliases PureOutputMapping MissingOutputsAreProspective)
    pangu_register_test(NAME MetaRequest.${case}
        COMMAND "$<TARGET_FILE:PanguMetaRequestTests>" --gtest_filter=MetaRequest.${case}
        BOUNDARY unit AREA build TIER precommit BUILD_TARGETS PanguMetaRequestTests)
endforeach()
foreach(case IN ITEMS OptionalIdentityAndMerge OrderIsDeterministic OwnsDataAfterInputDestruction CanonicalFileAliases)
    pangu_register_test(NAME MetaIR.${case}
        COMMAND "$<TARGET_FILE:PanguMetaModelTests>" --gtest_filter=MetaIR.${case}
        BOUNDARY unit AREA build TIER precommit BUILD_TARGETS PanguMetaModelTests)
endforeach()
add_executable(PanguMetaClangTests ClangLinkTests.cpp)
pangu_meta_target(PanguMetaClangTests)
target_compile_definitions(PanguMetaClangTests PRIVATE "PG_TEST_CLANG_HAS_RTTI=$<BOOL:${LLVM_ENABLE_RTTI}>")
target_link_libraries(PanguMetaClangTests PRIVATE PanguMetaFrontend ThirdParty::GoogleTestMain)
pangu_register_test(NAME MetaClang.AcceptsCompatibleClang
    COMMAND "$<TARGET_FILE:PanguMetaClangTests>" --gtest_filter=MetaClang.AcceptsCompatibleClang
    BOUNDARY integration AREA build TIER precommit BUILD_TARGETS PanguMetaClangTests)
foreach(case IN ITEMS Automatic RejectsMissingRoot RejectsMissingDevelopmentPackage
        RejectsMissingResource RejectsWrongStdlib RejectsMissingSources RejectsWrongRevision RejectsLegacyIdentity ValidationFailureRetries)
    set(processors 1)
    if(case STREQUAL "Automatic" OR case STREQUAL "ValidationFailureRetries")
        set(processors 2)
    endif()
    pangu_register_test(NAME "MetaClang.${case}" COMMAND "${CMAKE_COMMAND}"
        "-DPG_SOURCE=${PROJECT_SOURCE_DIR}" "-DPG_BINARY=${CMAKE_BINARY_DIR}/Discovery/${case}"
        "-DPG_LLVM_ROOT=${llvmRoot}" "-DPG_ENGINE=${PANGU_ENGINE_SOURCE_DIR}" "-DPG_CASE=${case}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/DiscoveryTests.cmake"
        BOUNDARY integration AREA build TIER precommit REQUIRES subprocess PROCESSORS "${processors}" TIMEOUT 300)
endforeach()
add_executable(PanguMetaDependencySmoke Fixtures/Dependencies/Smoke.c)
target_link_libraries(PanguMetaDependencySmoke PRIVATE ThirdParty::Zlib ThirdParty::Zstd)
pangu_register_test(NAME MetaClang.DependencyRoundTrip COMMAND "$<TARGET_FILE:PanguMetaDependencySmoke>"
    BOUNDARY integration AREA build TIER precommit BUILD_TARGETS PanguMetaDependencySmoke)
pangu_register_test(NAME MetaClang.CliUsage COMMAND "${CMAKE_COMMAND}"
    "-DPG_TOOL=$<TARGET_FILE:PanguMetaTool>" "-DPG_BINARY=${CMAKE_CURRENT_BINARY_DIR}/CliUsage" -DPG_CLI_USAGE=ON
    -P "${CMAKE_CURRENT_SOURCE_DIR}/CliUsageTests.cmake"
    BOUNDARY system AREA build TIER precommit BUILD_TARGETS PanguMetaTool REQUIRES subprocess)
add_executable(PanguMetaAttrTests AttrTests.cpp)
pangu_meta_target(PanguMetaAttrTests)
target_link_libraries(PanguMetaAttrTests PRIVATE PanguMetaFrontend PanguMetaModel ThirdParty::GoogleTestMain ThirdParty::NlohmannJson)
target_compile_definitions(PanguMetaAttrTests PRIVATE JSON_NOEXCEPTION=1
    "PG_TEST_ENGINE=\"${PANGU_ENGINE_ROOT}\""
    "PG_TEST_ATTR_FIXTURES=\"${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/Attr\""
    "PG_TEST_RESOURCE=\"${PANGU_META_RESOURCE_DIR}\""
    "PG_TEST_COMPILER=\"${PANGU_META_HOST_COMPILER}\"")
foreach(case IN ITEMS TypeWithoutId OptionsDefaultsAndAliases OwnershipAndConditionGuards ModelSurvivesAstContext ConstantsAndSpellingFailures UnsupportedDeclarations DependenciesAndOtherModules FrontendConditionsAndPragmas CrossTuMergeAndPrivateHeader IdentityDomainsByOwner ConditionalIncludeAndMacroOrigins ResolvedFactoryDependencies ConstantStringStorage)
    pangu_register_test(NAME MetaAttr.${case}
        COMMAND "$<TARGET_FILE:PanguMetaAttrTests>" --gtest_filter=MetaAttr.${case}
        BOUNDARY integration AREA build TIER precommit BUILD_TARGETS PanguMetaAttrTests)
endforeach()
