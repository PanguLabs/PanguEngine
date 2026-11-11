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
pangu_register_test(NAME MetaCli.ConsumerPipeline COMMAND "${CMAKE_COMMAND}"
    "-DPG_TOOL=$<TARGET_FILE:PanguMetaTool>" "-DPG_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DPG_BINARY=${CMAKE_CURRENT_BINARY_DIR}/Consumer" "-DPG_ENGINE=${PANGU_ENGINE_ROOT}"
    "-DPG_COMPILER=${CMAKE_CXX_COMPILER}" "-DPG_RESOURCE=${PANGU_META_RESOURCE_DIR}"
    "-DPG_TOOLCHAIN=${CMAKE_TOOLCHAIN_FILE}" -P "${CMAKE_CURRENT_SOURCE_DIR}/CliTests.cmake"
    BOUNDARY integration AREA build TIER precommit BUILD_TARGETS PanguMetaTool REQUIRES subprocess TIMEOUT 240)
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
add_executable(PanguMetaContextTests ContextTests.cpp OwnershipTests.cpp)
pangu_meta_target(PanguMetaContextTests)
target_include_directories(PanguMetaContextTests SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_link_libraries(PanguMetaContextTests PRIVATE PanguMetaFrontend PanguMetaModel ThirdParty::GoogleTestMain ThirdParty::NlohmannJson clangTooling)
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
add_executable(PanguMetaSdkTests SdkLinkTests.cpp)
pangu_meta_target(PanguMetaSdkTests)
target_link_libraries(PanguMetaSdkTests PRIVATE PanguMetaFrontend ThirdParty::GoogleTestMain)
pangu_register_test(NAME MetaSdk.AcceptsCompatibleSdk
    COMMAND "$<TARGET_FILE:PanguMetaSdkTests>" --gtest_filter=MetaSdk.AcceptsCompatibleSdk
    BOUNDARY integration AREA build TIER precommit BUILD_TARGETS PanguMetaSdkTests)
foreach(case IN ITEMS RejectsNoRtti RejectsWrongStdlib)
    pangu_register_test(NAME "MetaSdk.${case}" COMMAND "${CMAKE_COMMAND}"
        "-DPG_SOURCE=${PROJECT_SOURCE_DIR}"
        "-DPG_BINARY=${CMAKE_CURRENT_BINARY_DIR}/${case}"
        "-DPG_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DPG_LLVM_DIR=${PANGU_META_LLVM_DIR}"
        "-DPG_CLANG_DIR=${PANGU_META_CLANG_DIR}"
        "-DPG_RESOURCE_DIR=${PANGU_META_RESOURCE_DIR}"
        "-DPG_CASE=${case}" -DPG_WRAP_NO_RTTI=ON
        -P "${CMAKE_CURRENT_SOURCE_DIR}/SdkTests.cmake"
        BOUNDARY integration AREA build TIER precommit REQUIRES subprocess TIMEOUT 180)
endforeach()
pangu_register_test(NAME MetaSdk.CliCheck COMMAND "$<TARGET_FILE:PanguMetaTool>" --check-sdk
    BOUNDARY system AREA build TIER precommit BUILD_TARGETS PanguMetaTool)
pangu_register_test(NAME MetaSdk.CliUsage COMMAND "${CMAKE_COMMAND}"
    "-DPG_TOOL=$<TARGET_FILE:PanguMetaTool>" "-DPG_BINARY=${CMAKE_CURRENT_BINARY_DIR}/CliUsage" -DPG_CLI_USAGE=ON
    -P "${CMAKE_CURRENT_SOURCE_DIR}/SdkTests.cmake"
    BOUNDARY system AREA build TIER precommit BUILD_TARGETS PanguMetaTool REQUIRES subprocess)
pangu_register_test(NAME MetaSdk.PreparePreservesExistingDirectory COMMAND "${CMAKE_COMMAND}"
    "-DPG_SOURCE=${PROJECT_SOURCE_DIR}" "-DPG_BINARY=${CMAKE_CURRENT_BINARY_DIR}/PrepareGuards"
    "-DPG_COMPILER=${CMAKE_CXX_COMPILER}" -DPG_PREPARE_GUARD=ON
    -P "${CMAKE_CURRENT_SOURCE_DIR}/SdkTests.cmake"
    BOUNDARY system AREA build TIER precommit REQUIRES subprocess)
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
