#include "ThirdParty.h"

#include <Error.h>
#include <FixtureModules.h>
#include <ModuleIdentityParse.h>

int main()
{
    const auto error = pg::MakeError(pg::ErrorDomainId::Constant<1>(), 23);
    const auto id = pg::module::detail::ParseModuleId("a127f219664c4413b670e820625088e1");
    const auto* first = pg::module::fixture::QueryFirst();
    const auto* second = pg::module::fixture::QuerySecond();
    return error.Code() == 23 && ThirdPartyValue() == 23 && id && *id == first->Descriptor.Id &&
                   second->Descriptor.Id == pg::module::fixture::kSecondId
               ? 0
               : 1;
}
