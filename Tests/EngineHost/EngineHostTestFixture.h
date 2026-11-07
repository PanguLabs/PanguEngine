#pragma once

#include "HostServiceModule.h"
#include <EngineHost.h>
#include <HostStartupConfig.h>
#include <HostTestState.h>
#include <array>
#ifdef PG_TEST_MODULAR
#include "HostServiceFixturePath.h"
#endif

namespace pg::engine::test
{
inline const auto kStartupError = pg::MakeError(pg::ErrorDomainId::Constant<0x741>(), 101);
inline const auto kStopError = pg::MakeError(pg::ErrorDomainId::Constant<0x741>(), 102);
inline auto HostCandidates()
{
#ifdef PG_TEST_MODULAR
    return std::array{
        module::ModuleLoadCandidate{
            module::SharedModuleCandidate{.ExpectedId = kModuleId, .Path = kHostServiceFixturePath}},
    };
#else
    return std::array{module::ModuleLoadCandidate{module::StaticModuleCandidate{QueryHostServiceFixture}}};
#endif
}
} // namespace pg::engine::test
