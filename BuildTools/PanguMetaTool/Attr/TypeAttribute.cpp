#include "Attr/TypeAttribute.h"
#include "Attr/ClangAttributeAdapter.h"
#include <clang/Basic/ParsedAttrInfo.h>
namespace pg::meta::tool
{
void RegisterTypeAttribute()
{
    static const clang::ParsedAttrInfoRegistry::Add<ClangAttributeAdapter<TypeAttribute>> kRegistration("pangu-type",
                                                                                                        "Pangu Type");
}
} // namespace pg::meta::tool
