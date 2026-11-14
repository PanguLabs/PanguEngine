#ifndef NO_META
#include <Actor.gen.h>
static_assert(pg::meta::TypeTraits<fixture::Actor>::HasTypedAccess);
#endif
int main() { return 0; }
