#include "FixtureModules.h"
#include "HostCheck.h"
#include <Error.h>
#ifdef PANGU_FIXTURE_MODULAR
#include "ModuleCandidates.h"
#endif

namespace
{
// 复用真实 EngineHost 启动、Context 服务访问和显式关闭，参与移动端最终链接。
[[nodiscard]] int CheckMobileModules()
{
    const auto error = pg::MakeError(pg::ErrorDomainId::Constant<0x741>(), 7);
    if (error.Code() != 7)
    {
        return 10;
    }
#ifdef PANGU_FIXTURE_MODULAR
    return pg::fixture::VerifyCandidates(pg::fixture::ConsumerCandidates(), pg::fixture::kGameplayId, true);
#else
    return pg::fixture::VerifyCatalog(pg::fixture::GameCatalog(), pg::fixture::kGameplayId);
#endif
}
} // namespace

#ifdef __ANDROID__
// Android 最终宿主是普通原生库，无 ModuleId，也不导出 Module Query。
extern "C" __attribute__((visibility("default"))) int PanguMobileHostCheck() { return CheckMobileModules(); }
#else
int main() { return CheckMobileModules(); }
#endif
