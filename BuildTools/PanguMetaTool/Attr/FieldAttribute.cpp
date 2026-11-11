#include "Attr/FieldAttribute.h"
#include "Attr/ClangAttributeAdapter.h"
#include <clang/Basic/ParsedAttrInfo.h>
namespace pg::meta::tool
{
void RegisterFieldAttribute()
{
    static const clang::ParsedAttrInfoRegistry::Add<ClangAttributeAdapter<FieldAttribute>> kRegistration("pangu-field",
                                                                                                         "Pangu Field");
}
} // namespace pg::meta::tool
